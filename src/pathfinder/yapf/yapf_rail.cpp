/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file yapf_rail.cpp The rail pathfinding. */

#include "../../stdafx.h"

#include "yapf.hpp"
#include "yapf_cache.h"
#include "yapf_node_rail.hpp"
#include "yapf_costrail.hpp"
#include "yapf_destrail.hpp"
#include "../../viewport_func.h"
#include "../../newgrf_station.h"
#include "../../tracerestrict.h"
#include "../../debug.h"
#include "../../misc/dbg_helpers.h"

#include "../../safeguards.h"

#if defined(UNIX) && defined(__GLIBC__)
#include <unistd.h>
#endif

template <typename Tpf> void DumpState(Tpf &pf1, Tpf &pf2)
{
	DumpTarget dmp1, dmp2;
	pf1.DumpBase(dmp1);
	pf2.DumpBase(dmp2);

#if defined(UNIX) && defined(__GLIBC__)
	static unsigned int num = 0;
	int pid = getpid();
	std::string fn1;
	std::string fn2;
	std::optional<FileHandle> f1;
	std::optional<FileHandle> f2;
	for(;;) {
		fn1 = fmt::format("yapf-{}-{}-1.txt", pid, num);
		f1 = FileHandle::Open(fn1, "wx");
		if (!f1.has_value() && errno == EEXIST) {
			num++;
			continue;
		}
		fn2 = fmt::format("yapf-{}-{}-2.txt", pid, num);
		f2 = FileHandle::Open(fn2, "w");
		num++;
		break;
	}
	Debug(desync, 0, "Dumping YAPF state to {} and {}", fn1, fn2);
#else
	auto f1 = FileHandle::Open("yapf1.txt", "wt");
	auto f2 = FileHandle::Open("yapf2.txt", "wt");
#endif
	assert(f1.has_value());
	assert(f2.has_value());
	fwrite(dmp1.output_buffer.data(), 1, dmp1.output_buffer.size(), *f1);
	fwrite(dmp2.output_buffer.data(), 1, dmp2.output_buffer.size(), *f2);
}

template <class Node>
struct CYapfReserveTrackSafePositionNodes {
	inline static std::vector<Node *> safe_position_nodes;
};

template <class Types>
class CYapfReserveTrack {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

private:
	TileIndex res_dest_tile;   ///< The reservation target tile
	Trackdir res_dest_td;      ///< The reservation target trackdir
	Node *res_dest_node;       ///< The reservation target node
	TileIndex res_fail_tile;   ///< The tile where the reservation failed
	Trackdir res_fail_td;      ///< The trackdir where the reservation failed
	TileIndex origin_tile;     ///< Tile our reservation will originate from

	bool FindSafePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns())) {
			this->res_dest_tile = tile;
			this->res_dest_td = td;
			return false;   // Stop iterating segment
		}
		return true;
	}


	/**
	 * R3R: couple-specific safe-position detector (ported from the pxp-decouple
	 * patch). Tracks the waiting consist's own reservation so the coupling
	 * locomotive drives right up to it.
	 */
	bool FindSafeCouplePositionProc(TileIndex tile, Trackdir td)
	{
		if (IsRailDepotTile(tile)) {
		FILE *dbg = fopen("R3R_debug.log", "a");
		if (dbg != nullptr) {
			fprintf(dbg, "FSCP tile=%d,%d fail=depot\n", (int)TileX(tile), (int)TileY(tile));
			fclose(dbg);
		}
		return false;
	}
		TrackdirBits tdb = TrackdirToTrackdirBits(td);
		TrackBits tracks = TrackdirBitsToTrackBits(tdb);
		if (HasReservedTracks(tile, tracks)) {
			Train *best = nullptr;
			Train *second_best = nullptr;
			auto check_train_on_tile = [&](TileIndex t) {
				for (Train *tr : VehiclesOnTile<VehicleType::Train>(t)) {
					if (tr->vehstatus.Test(VehState::Crashed)) continue;
					if (tr->track == TRACK_BIT_WORMHOLE || HasBit((TrackBits)tr->track, TrackdirToTrack(td))) {
						Train *head = tr->First();
						if (best != nullptr && head->index != best->index) second_best = head;
						if (best == nullptr || head->index < best->index) best = head;
					}
				}
			};
			if (IsRailStationTile(tile)) {
				TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
				for (TileIndex st_tile = tile + diff; IsCompatibleTrainStationTile(st_tile, tile); st_tile += diff) {
					check_train_on_tile(st_tile);
				}
			}
			check_train_on_tile(tile);
			if (best != nullptr) {
				if (!best->current_order.IsType(OT_WAIT_COUPLE)) {
					FILE *dbg = fopen("R3R_debug.log", "a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=notWC best=%d ord=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)best->current_order.GetType());
						fclose(dbg);
					}
					return false;
				}
				if (second_best != nullptr) {
					FILE *dbg = fopen("R3R_debug.log", "a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=2nd best=%d 2nd=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)second_best->index.base());
						fclose(dbg);
					}
					return false;
				}
				Vehicle *other_train = nullptr;
				FollowTrainReservation(best, &other_train);
				if (other_train != nullptr && other_train != best) {
					FILE *dbg = fopen("R3R_debug.log", "a");
					if (dbg != nullptr) {
						fprintf(dbg, "FSCP tile=%d,%d fail=other best=%d other=%d\n", (int)TileX(tile), (int)TileY(tile), (int)best->index.base(), (int)other_train->index.base());
						fclose(dbg);
					}
					return false;
				}
			}
		} else if (GetReservedTrackbits(tile) != TRACK_BIT_NONE) {
			if (!TryReserveRailTrack(tile, TrackdirToTrack(td))) {
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "FSCP tile=%d,%d fail=tryReserve td=%d\n", (int)TileX(tile), (int)TileY(tile), (int)td);
					fclose(dbg);
				}
				return false;
			}
			UnreserveRailTrack(tile, TrackdirToTrack(td));
		}
		return true;
	}	/**
	 * Reserve a railway platform. Tile contains the failed tile on abort.
	 * @param tile The start tile.
	 * @param dir The direction to reserve further tiles in.
	 * @return \c true iff reservation succeeded.
	 */
	bool ReserveRailStationPlatform(TileIndex &tile, DiagDirection dir)
	{
		TileIndex     start = tile;
		TileIndexDiff diff = TileOffsByDiagDir(dir);

		do {
			if (HasStationReservation(tile)) return false;
			SetRailStationReservation(tile, true);
			MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			tile = TileAdd(tile, diff);
		} while (IsCompatibleTrainStationTile(tile, start) && tile != this->origin_tile);

		auto *st = Station::GetByTile(start);
		TriggerStationRandomisation(st, start, StationRandomTrigger::PathReservation);
		TriggerStationAnimation(st, start, StationAnimationTrigger::PathReservation);

		return true;
	}

	/**
	 * Reserve a single track/platform.
	 * @param tile The start tile.
	 * @param td The track direction that is to be reserved.
	 * @return \c true iff reservation succeeded.
	 */
	bool ReserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			if (!ReserveRailStationPlatform(tile, TrackdirToExitdir(ReverseTrackdir(td)))) {
				/* Platform could not be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
			}
		} else {
			if (!TryReserveRailTrackdir(Yapf().GetVehicle(), tile, td)) {
				/* Tile couldn't be reserved, undo. */
				this->res_fail_tile = tile;
				this->res_fail_td = td;
				return false;
			}

			if (IsRailWaypointTile(tile)) {
				auto *st = BaseStation::GetByTile(tile);
				TriggerStationRandomisation(st, tile, StationRandomTrigger::PathReservation);
				TriggerStationAnimation(st, tile, StationAnimationTrigger::PathReservation);
			}
		}

		return tile != this->res_dest_tile || td != this->res_dest_td;
	}

	/**
	 * Unreserve a single track/platform. Stops when the previous failure is reached.
	 * @param tile The start tile.
	 * @param td The track direction that is to be unreserved.
	 * @return \c true iff the unreservation succeeded.
	 */
	bool UnreserveSingleTrack(TileIndex tile, Trackdir td)
	{
		if (IsRailStationTile(tile)) {
			TileIndex     start = tile;
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(td)));
			while ((tile != this->res_fail_tile || td != this->res_fail_td) && IsCompatibleTrainStationTile(tile, start)) {
				SetRailStationReservation(tile, false);
				tile = TileAdd(tile, diff);
			}
		} else if (tile != this->res_fail_tile || td != this->res_fail_td) {
			UnreserveRailTrackdir(tile, td);
		}
		return (tile != this->res_dest_tile || td != this->res_dest_td) && (tile != this->res_fail_tile || td != this->res_fail_td);
	}

public:
	/**
	 * Set the target to where the reservation should be extended.
	 * @param node The destination node.
	 * @param tile The destination tile.
	 * @param td The destination track direction.
	 */
	inline void SetReservationTarget(Node *node, TileIndex tile, Trackdir td)
	{
		this->res_dest_node = node;
		this->res_dest_tile = tile;
		this->res_dest_td = td;
	}

	/**
	 * Check the node for a possible reservation target.
	 * @param node The node to check.
	 * Return true if found.
	 */
	inline bool FindSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafePositionProc)) {
			this->res_dest_node = node;
			return true;
		}
		return false;
	}


	/**
	 * R3R: couple-specific node check (ported from pxp-decouple). Uses
	 * FindSafeCouplePositionProc so the coupling locomotive tracks the waiting
	 * consist's reservation instead of stopping at an ordinary safe position.
	 */
	inline bool CheckSafePositionOnNode(Node *node)
	{
		dbg_assert(node->parent != nullptr);

		if (!node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::FindSafeCouplePositionProc)) {
			return false;
		}
		return true;
	}	/** Find a reservation target on the path ending at this node. */
	void FindSafePositionOnSafePositionNodes()
	{
		const auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
		for (auto it = safe_position_nodes.rbegin(); it != safe_position_nodes.rend(); ++it) {
			if (this->FindSafePositionOnNode(*it)) return;
		}
	}

	/**
	 * Try to reserve the path till the reservation target.
	 * @param target End location of the reservation.
	 * @param origin Start location of the reservation.
	 * @return \c true iff the path could be reserved.
	 */
	bool TryReservePath(PBSTileInfo *target, TileIndex origin, bool unsafe_pos = false)
	{
		this->res_fail_tile = INVALID_TILE;
		this->origin_tile = origin;

		if (target != nullptr) {
			target->tile = this->res_dest_tile;
			target->trackdir = this->res_dest_td;
			target->okay = false;
		}

		/* Don't bother if the target is reserved. */
		PBSWaitingPositionRestrictedSignalState restricted_signal_state;
		restricted_signal_state.defer_test_if_slot_conditional = true;
		if (!unsafe_pos && !IsWaitingPositionFree(Yapf().GetVehicle(), this->res_dest_tile, this->res_dest_td, false, &restricted_signal_state)) return false;

		/* The temporary slot state only needs to be pushed to the stack (i.e. activated) on first use */
		static TraceRestrictSlotTemporaryState temporary_slot_state;
		assert(temporary_slot_state.IsEmpty() && !temporary_slot_state.IsActive());

		struct IntermediaryTraceRestrictSignalInfo {
			const TraceRestrictProgram *prog;
			TileIndex tile;
			Trackdir trackdir;
			bool front_side;
		};
		/* Nodes are iterated in reverse order (from the target), but tiles within the node are iterated in forward order (towards the target).
		 * intermediary_restricted_signals is in reverse order, (the first signal to evaluate at the end).
		 */
		static std::vector<IntermediaryTraceRestrictSignalInfo> intermediary_restricted_signals;
		intermediary_restricted_signals.clear();

		for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
			const size_t intermediary_restricted_signals_current_size = intermediary_restricted_signals.size();
			node->template IterateTiles<CYapfReserveTrack>(Yapf().GetVehicle(), Yapf(), [&](TileIndex tile, Trackdir td) -> bool {
				/* Cheapest tests first */
				if (IsTileType(tile, TileType::Railway) && HasSignals(tile) && IsRestrictedSignal(tile) && HasSignalOnTrack(tile, TrackdirToTrack(td))) {
					const bool front_side = HasSignalOnTrackdir(tile, td);

					TraceRestrictProgramActionsUsedFlags au_flags = TRPAUF_SLOT_ACQUIRE;
					if (front_side) {
						/* Passing through a signal from the front side */
						au_flags |= TRPAUF_WAIT_AT_PBS;
					}

					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(td));
					if (prog != nullptr && prog->actions_used_flags & au_flags) {
						/* Insert at intermediary_restricted_signals_current_size, such that if there are multiple signals for this node, they end up in reverse order */
						intermediary_restricted_signals.insert(intermediary_restricted_signals.begin() + intermediary_restricted_signals_current_size, { prog, tile, td, front_side });
					}
				}

				return this->ReserveSingleTrack(tile, td);
			});
			if (!unsafe_pos && this->res_fail_tile != INVALID_TILE) {
				/* Reservation failed, undo. */
				Node *fail_node = this->res_dest_node;
				TileIndex stop_tile = this->res_fail_tile;
				do {
					/* If this is the node that failed, stop at the failed tile. */
					this->res_fail_tile = fail_node == node ? stop_tile : INVALID_TILE;
					fail_node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
				} while (fail_node != node && (fail_node = fail_node->parent) != nullptr);

				if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
				return false;
			}
		}

		auto undo_reservation = [&]() {
			for (Node *node = this->res_dest_node; node->parent != nullptr; node = node->parent) {
				node->IterateTiles(Yapf().GetVehicle(), Yapf(), *this, &CYapfReserveTrack<Types>::UnreserveSingleTrack);
			}
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(Yapf().GetVehicle()->index);
		};

		/* Iterate in reverse order */
		for (auto iter = intermediary_restricted_signals.rbegin(); iter != intermediary_restricted_signals.rend(); ++iter) {
			extern TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode);

			TraceRestrictProgramInput input(iter->tile, iter->trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
			if (iter->prog->actions_used_flags & TRPAUF_SLOT_ACQUIRE) {
				input.permitted_slot_operations = TRPISP_ACQUIRE_TEMP_STATE;

				if (!temporary_slot_state.IsActive()) {
					/* The temporary slot state needs to be be pushed because permission to use it is granted by TRPISP_ACQUIRE_TEMP_STATE */
					temporary_slot_state.PushToChangeStack();
				}
			}

			TraceRestrictProgramResult out;
			iter->prog->Execute(Yapf().GetVehicle(), input, out);
			if (iter->front_side && out.flags & TRPRF_WAIT_AT_PBS) {
				/* Wait at PBS is set, take this as waiting at the start signal */
				undo_reservation();
				return false;
			}
		}

		if (restricted_signal_state.deferred_test) {
			/* The IsWaitingPositionFree restricted signal test was deferred due to possible slot changes during reservation, test it now */
			if (!IsWaitingPositionFreeTraceRestrictExecute(restricted_signal_state.prog, Yapf().GetVehicle(), restricted_signal_state.tile, restricted_signal_state.trackdir)) {
				/* Target is reserved, undo reservation */
				undo_reservation();
				return false;
			}
		}

		/* This must be done before calling TraceRestrictExecuteResEndSlot */
		TraceRestrictSlotTemporaryState::ClearChangeStackApplyAllTemporaryChanges(Yapf().GetVehicle());

		restricted_signal_state.TraceRestrictExecuteResEndSlot(Yapf().GetVehicle());

		if (target != nullptr) target->okay = true;

		if (Yapf().CanUseGlobalCache(*this->res_dest_node)) {
			YapfNotifyTrackLayoutChange(INVALID_TILE, INVALID_TRACK);
		}

		return true;
	}

	static void stDesyncCheck(Tpf &pf1, Tpf &pf2, const char *name, bool check_res)
	{
		Node *n1 = pf1.GetBestNode();
		Node *n2 = pf2.GetBestNode();
		uint depth = 0;
		for (;;) {
			if ((n1 != nullptr) != (n2 != nullptr)) {
				Debug(desync, 0, "{}: node nonnull state at {} = [{}, {}]", name, depth, (n1 != nullptr), (n2 != nullptr));
				DumpState(pf1, pf2);
				return;
			}
			if (n1 == nullptr) break;

			if (n1->GetTile() != n2->GetTile()) {
				Debug(desync, 0, "{} tile mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTile(), n2->GetTile());
				DumpState(pf1, pf2);
				return;
			}
			if (n1->GetTrackdir() != n2->GetTrackdir()) {
				Debug(desync, 0, "{} trackdir mismatch at {} = [0x{:X}, 0x{:X}]", name, depth, n1->GetTrackdir(), n2->GetTrackdir());
				DumpState(pf1, pf2);
				return;
			}
			n1 = n1->parent;
			n2 = n2->parent;
			depth++;
		}

		if (check_res && (pf1.res_dest_tile != pf2.res_dest_tile || pf1.res_dest_td != pf2.res_dest_td)) {
			Debug(desync, 0, "{} reservation target mismatch = [(0x{:X}, {}), (0x{:X}, {})]", name, pf1.res_dest_tile, pf1.res_dest_td, pf2.res_dest_tile, pf2.res_dest_td);
			DumpState(pf1, pf2);
			return;
		}
	}
};

template <class Types>
class CYapfFollowAnyDepotRailT {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower follower(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node && length >= v->gcache.cached_total_length) {
				if (follower.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, follower, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static FindDepotData stFindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		Tpf pf1;
		/*
		 * With caching enabled it simply cannot get a reliable result when you
		 * have limited the distance a train may travel. This means that the
		 * cached result does not match uncached result in all cases and that
		 * causes desyncs. So disable caching when finding for a depot that is
		 * nearby. This only happens with automatic servicing of vehicles,
		 * so it will only impact performance when you do not manually set
		 * depot orders and you do not disable automatic servicing.
		 */
		if (max_penalty != 0) pf1.DisableCache(true);
		FindDepotData result1 = pf1.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			FindDepotData result2 = pf2.FindNearestDepotTwoWay(v, t1, td1, t2, td2, max_penalty, reverse_penalty);
			if (result1.tile != result2.tile || (result1.reverse != result2.reverse)) {
				Debug(desync, 0, "CACHE ERROR: FindNearestDepotTwoWay() = [{}, {}]",
						result1.tile != INVALID_TILE ? "T" : "F",
						result2.tile != INVALID_TILE ? "T" : "F");
				DumpState(pf1, pf2);
			}
		}

		return result1;
	}

	inline FindDepotData FindNearestDepotTwoWay(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int max_penalty, int reverse_penalty)
	{
		/* set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);
		Yapf().SetMaxCost(max_penalty);

		/* find the best path */
		if (!Yapf().FindPath(v)) return FindDepotData();

		/* Some path found. */
		Node *n = Yapf().GetBestNode();

		/* walk through the path back to the origin */
		Node *node = n;
		while (node->parent != nullptr) {
			node = node->parent;
		}

		/* if the origin node is our front vehicle tile/Trackdir then we didn't reverse
		 * but we can also look at the cost (== 0 -> not reversed, == reverse_penalty -> reversed) */
		return FindDepotData(n->GetLastTile(), n->cost, node->cost != 0);
	}
};

/**
 * Follow class for the "couple pathfinder": standard rail following used to
 * search for the nearest waiting consist (see CYapfDestinationTrainRailT).
 */
template <class Types>
class CYapfFollowCoupleRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf;                     ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node;        ///< this will be our node type
	typedef typename Node::Key Key;                      ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		/* R3R: a depot tile is a dead end - never extend a path through it. */
		{
			const bool is_depot = IsRailDepotTile(old_node.GetLastTile());
			if (is_depot || (TileX(old_node.GetLastTile()) == 38 && TileY(old_node.GetLastTile()) == 27)) {
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "DEPOTCHK t=%d,%d isDepot=%d isRailway=%d parent=%s\n", (int)TileX(old_node.GetLastTile()), (int)TileY(old_node.GetLastTile()), (int)is_depot, (int)IsTileType(old_node.GetLastTile(), TileType::Railway), old_node.parent != nullptr ? "y" : "n");
					fclose(dbg);
				}
			}
		}
		if (IsRailDepotTile(old_node.GetLastTile()) && old_node.parent != nullptr) return;
		TrackFollower F(Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes());
		if (Yapf().GetVehicle()->current_order.IsType(OT_GOTO_COUPLE)) {
			TrackFollower F2(Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes());
			if (F2.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-FOLLOW from=%d,%d td=%d to=%d,%d tdbits=0x%x\n", (int)TileX(old_node.GetLastTile()), (int)TileY(old_node.GetLastTile()), (int)old_node.GetLastTrackdir(), (int)TileX(F2.new_tile), (int)TileY(F2.new_tile), (unsigned)F2.new_td_bits);
					fclose(dbg);
				}
			}
		}
		if (F.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, F);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stFindNearestCoupleTrain(const Train *v, bool dont_reserve)
	{
		/* Create pathfinder instance */
		Tpf pf1;
		pf1.DisableCache(true);
		return pf1.FindNearestCoupleTrain(v, dont_reserve);
	}

	Trackdir FindNearestCoupleTrain(const Train *v, bool dont_reserve)
	{
		PBSTileInfo origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-ORIGIN veh=%d origin=%d,%d td=%d vehDir=%d vehTd=%d dontReserve=%d\n",
						(int)v->index.base(), (int)TileX(origin.tile), (int)TileY(origin.tile),
						(int)origin.trackdir, (int)v->direction, (int)v->GetVehicleTrackdir(), (int)dont_reserve);
				fclose(dbg);
			}
		}
		/* Set origin and destination. */
		Yapf().SetOrigin(origin.tile, origin.trackdir);
		Yapf().SetDestination(v);

		bool path_found = Yapf().FindPath(v);
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-PATHFOUND veh=%d found=%d\n", (int)v->index.base(), (int)path_found);
				fclose(dbg);
			}
		}
		if (!path_found) return INVALID_TRACKDIR;

		/* Found a destination, set as reservation target. */
		Node *pNode = Yapf().GetBestNode();
		/* R3R: route to the consist head car itself instead of the platform dest tile -
		 * the platform dest may sit on the far side of the consist, making the path
		 * cut straight through it. */
		TileIndex consist_head_tile = INVALID_TILE;
		if (pNode != nullptr && IsRailStationTile(pNode->GetLastTile())) {
			Train *ct = GetTrainForReservation(pNode->GetLastTile(), TrackdirToTrack(pNode->GetLastTrackdir()));
			if (ct == nullptr) {
				for (Train *tr : VehiclesOnTile<VehicleType::Train>(pNode->GetLastTile())) {
					if (tr->IsFrontEngine()) { ct = tr; break; }
				}
			}
			if (ct != nullptr) {
				Train *h = ct->First();
				Train *t = ct->Last();
				if (h != nullptr && t != nullptr) {
					/* R3R: couple to whichever end of the consist the loco approaches.
					 * Targeting the head car unconditionally routes a loco coming from the
					 * consist's tail end straight through the consist (no path found,
					 * reservation cutting through the consist). */
					if (DistanceManhattan(v->tile, t->tile) < DistanceManhattan(v->tile, h->tile)) {
						consist_head_tile = t->tile;
					} else {
						consist_head_tile = h->tile;
					}
				} else if (h != nullptr) {
					consist_head_tile = h->tile;
				}
			}
			{
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-HEAD best=%d,%d head=%d,%d\n",
							(int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()),
							(int)TileX(consist_head_tile), (int)TileY(consist_head_tile));
					fclose(dbg);
				}
			}
		}
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-BEST veh=%d tile=%d,%d td=%d cost=%d\n",
						(int)v->index.base(), (int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()),
						(int)pNode->GetLastTrackdir(), (int)pNode->cost);
				fclose(dbg);
			}
		}
		/* R3R: reserve up to the consist's head car (the coupling point) instead of
		 * the far end of the platform - reserving the far end routes the loco through
		 * the consist and extends the reservation past the platform. */
		const TileIndex res_target_tile = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pNode->GetLastTile();
		this->SetReservationTarget(pNode, res_target_tile, pNode->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *pPrev = nullptr;
		TileIndex rp_last_tile = INVALID_TILE;
		while (pNode->parent != nullptr) {
			pPrev = pNode;
			pNode = pNode->parent;

			{
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-BACK pPrev=%d,%d td=%d parent=%d,%d\n", (int)TileX(pPrev->GetLastTile()), (int)TileY(pPrev->GetLastTile()), (int)pPrev->GetLastTrackdir(), (int)TileX(pNode->GetLastTile()), (int)TileY(pNode->GetLastTile()));
					fclose(dbg);
				}
			}
			if (!this->CheckSafePositionOnNode(pPrev)) {
				{
					FILE *dbg = fopen("R3R_debug.log", "a");
					if (dbg != nullptr) {
						fprintf(dbg, "CPL-SAFE-FAIL veh=%d tile=%d,%d\n",
								(int)v->index.base(), (int)TileX(pPrev->GetLastTile()), (int)TileY(pPrev->GetLastTile()));
						fclose(dbg);
					}
				}
				return INVALID_TRACKDIR;
			}
		}

		next_trackdir = pPrev->GetTrackdir();
	if (pNode->parent == nullptr && pPrev != nullptr) {
		/* R3R: single-segment couple path. The target node's GetTrackdir() is
		 * the consist platform direction (points away from the loco), so the
		 * departure trackdir would send the loco the wrong way. Trace the rail
		 * network from the start tile to the consist to get the true departure
		 * direction, and reserve the tiles along that path (they are not part
		 * of any node segment, so TryReservePath does not cover them). */
		TrackFollower ft(v, Yapf().GetCompatibleRailTypes());
		TileIndex st = pNode->GetLastTile();
		TileIndex tg = (consist_head_tile != INVALID_TILE) ? consist_head_tile : pPrev->GetLastTile();
		Trackdir best_td = INVALID_TRACKDIR;
		std::function<bool(TileIndex, Trackdir, int)> reach = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
			if (depth <= 15) {
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "REACH cur=%d,%d td=%d depth=%d trk=%d bits=0x%x\n", (int)TileX(cur), (int)TileY(cur), (int)cur_td, depth, (int)TrackdirToTrack(cur_td), (unsigned)TrackdirBitsToTrackBits(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0)));
					fclose(dbg);
				}
			}
			if (depth > 512) return false;
			if (cur == tg) return true;
			/* R3R: never route the couple loco through a depot. */
			if (IsRailDepotTile(cur)) return false;
			TrackFollower f2(v, Yapf().GetCompatibleRailTypes());
			if (!f2.Follow(cur, cur_td)) return false;
			/* R3R: TrackFollower leaps a whole platform in one step, so the
			 * consist head (tg) lying on that platform stretch would be skipped.
			 * If tg is on the platform segment between cur and f2.new_tile, treat
			 * it as reached. */
			if (IsRailStationTile(f2.new_tile)) {
				const Axis axis = GetRailStationAxis(f2.new_tile);
				const TileIndexDiff delta = TileOffsByAxis(axis);
				TileIndex tt = cur;
				for (int i = 0; i < 32; ++i) {
					tt += delta;
					if (tt == f2.new_tile) break;
					if (tt == tg) return true;
				}
				tt = cur;
				for (int i = 0; i < 32; ++i) {
					tt -= delta;
					if (tt == f2.new_tile) break;
					if (tt == tg) return true;
				}
			}
			TrackdirBits tdb2 = f2.new_td_bits;
			if (tdb2 == TRACKDIR_BIT_NONE) return false;
			if (KillFirstBit(tdb2) == TRACKDIR_BIT_NONE) {
				return reach(f2.new_tile, FindFirstTrackdir(tdb2), depth + 1);
			}
			for (TrackdirBits tb = tdb2; tb != TRACKDIR_BIT_NONE; tb = KillFirstBit(tb)) {
				if (reach(f2.new_tile, (Trackdir)FindFirstBit(tb), depth + 1)) return true;
			}
			return false;
		};
		TrackdirBits tdb;
		tdb = GetTileTrackdirBits(st, ::TransportType::TRANSPORT_RAIL, 0);
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-TRACE st=%d,%d tdb=0x%x\n", (int)TileX(st), (int)TileY(st), (unsigned)tdb);
				fclose(dbg);
			}
		}

		for (; tdb != TRACKDIR_BIT_NONE; tdb = KillFirstBit(tdb)) {
			Trackdir td = (Trackdir)FindFirstBit(tdb);
			if (reach(st, td, 0)) { best_td = td; break; }
		}
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CPL-TRACE-RESULT st=%d,%d best=%d\n", (int)TileX(st), (int)TileY(st), (int)best_td);
				fclose(dbg);
			}
		}
		if (best_td != INVALID_TRACKDIR) {
			next_trackdir = best_td;
			if (!dont_reserve) {
				std::function<bool(TileIndex, Trackdir, int)> rp = [&](TileIndex cur, Trackdir cur_td, int depth) -> bool {
					if (depth <= 15) {
						FILE *dbg = fopen("R3R_debug.log", "a");
						if (dbg != nullptr) {
							fprintf(dbg, "RP cur=%d,%d td=%d depth=%d trk=%d bits=0x%x\n", (int)TileX(cur), (int)TileY(cur), (int)cur_td, depth, (int)TrackdirToTrack(cur_td), (unsigned)TrackdirBitsToTrackBits(GetTileTrackdirBits(cur, ::TransportType::TRANSPORT_RAIL, 0)));
							fclose(dbg);
						}
					}
					if (depth > 512) return false;
					if (cur == tg) return true;
					if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) rp_last_tile = cur;
					if (IsRailDepotTile(cur)) return false; /* R3R: do not reserve through a depot. */
					TrackFollower f3(v, Yapf().GetCompatibleRailTypes());
					if (!f3.Follow(cur, cur_td)) return false;
					if (IsRailStationTile(f3.new_tile)) {
						const Axis axis = GetRailStationAxis(f3.new_tile);
						const TileIndexDiff delta = TileOffsByAxis(axis);
						const Track plat_track = AxisToTrack(axis);
						TileIndex tt = cur;
						for (int i = 0; i < 32; ++i) {
							tt += delta;
							if (tt == f3.new_tile) break;
							if (tt == tg) {
								if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
									TryReserveRailTrack(cur, TrackdirToTrack(cur_td));
								}
								/* R3R: also reserve the platform tiles between the loco and
								 * the consist - the loco must drive over them to reach the
								 * coupling point. Normally the consist holds a
								 * whole-platform reservation, in which case these tiles are
								 * already reserved (TryReserveRailTrack fails harmlessly);
								 * when it does not, this keeps the loco's own path
								 * contiguous so it can still reach the consist. */
								for (TileIndex p = cur; p != tt;) {
									p += delta;
									if (p == tt || p == f3.new_tile) break;
									TryReserveRailTrack(p, plat_track);
								}
								return true;
							}
						}
						tt = cur;
						for (int i = 0; i < 32; ++i) {
							tt -= delta;
							if (tt == f3.new_tile) break;
							if (tt == tg) {
								if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
									TryReserveRailTrack(cur, TrackdirToTrack(cur_td));
								}
								for (TileIndex p = cur; p != tt;) {
									p -= delta;
									if (p == tt || p == f3.new_tile) break;
									TryReserveRailTrack(p, plat_track);
								}
								return true;
							}
						}
					}
					TrackdirBits tdb3 = f3.new_td_bits;
					if (tdb3 == TRACKDIR_BIT_NONE) return false;
					if (KillFirstBit(tdb3) == TRACKDIR_BIT_NONE) {
						if (rp(f3.new_tile, FindFirstTrackdir(tdb3), depth + 1)) {
							if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
								TryReserveRailTrack(cur, TrackdirToTrack(cur_td));
							}
							/* R3R: also reserve the entry track of the next tile - the loco needs the
							 * track it drives INTO, not just the one it leaves. Only reserve if the tile
							 * actually has that track (avoids the pbs.cpp:144 assertion on platforms or
							 * tiles where cur_td points along a different axis from the next tile). */
							if (!IsRailStationTile(f3.new_tile) && !IsRailDepotTile(f3.new_tile)) {
								Track entry_track = TrackdirToTrack(ReverseTrackdir(cur_td));
								TrackBits tile_tracks = TrackdirBitsToTrackBits(GetTileTrackdirBits(f3.new_tile, ::TransportType::TRANSPORT_RAIL, 0));
								if (tile_tracks & TrackToTrackBits(entry_track)) {
									TryReserveRailTrack(f3.new_tile, entry_track);
								}
							}
							return true;
						}
						return false;
					}
					for (TrackdirBits tb = tdb3; tb != TRACKDIR_BIT_NONE; tb = KillFirstBit(tb)) {
						if (rp(f3.new_tile, (Trackdir)FindFirstBit(tb), depth + 1)) {
							if (!IsRailStationTile(cur) && !IsRailDepotTile(cur)) {
								TryReserveRailTrack(cur, TrackdirToTrack(cur_td));
							}
							/* R3R: also reserve the entry track of the next tile, but only if
							 * the tile actually has that track (avoids the pbs.cpp:144 assertion
							 * on platforms/tiles where cur_td points along a different axis). */
							if (!IsRailStationTile(f3.new_tile) && !IsRailDepotTile(f3.new_tile)) {
								Track entry_track = TrackdirToTrack(ReverseTrackdir(cur_td));
								TrackBits tile_tracks = TrackdirBitsToTrackBits(GetTileTrackdirBits(f3.new_tile, ::TransportType::TRANSPORT_RAIL, 0));
								if (tile_tracks & TrackToTrackBits(entry_track)) {
									TryReserveRailTrack(f3.new_tile, entry_track);
								}
							}
							return true;
						}
					}
					return false;
				};
				rp(st, best_td, 0);
				if (rp_last_tile != INVALID_TILE) {
					this->SetReservationTarget(pNode, rp_last_tile, pNode->GetLastTrackdir());
				}
			}
		} else {
			/* R3R: no reachable departure direction (the only path went through the depot) - wait. */
			next_trackdir = INVALID_TRACKDIR;
		}
	}
		if (!dont_reserve) {
			bool reserved = this->TryReservePath(nullptr, pNode->GetLastTile(), true);
			{
				FILE *dbg = fopen("R3R_debug.log", "a");
				if (dbg != nullptr) {
					fprintf(dbg, "CPL-RESERVE veh=%d reserved=%d next=%d\n",
							(int)v->index.base(), (int)reserved, (int)next_trackdir);
					fprintf(dbg, "CHKRES 38,37=0x%x 38,36=0x%x 38,35=0x%x 38,34=0x%x 39,34=0x%x 39,33=0x%x 39,32=0x%x 39,31=0x%x 39,30=0x%x\n",
							(unsigned)GetReservedTrackbits(TileXY(38, 37)),
							(unsigned)GetReservedTrackbits(TileXY(38, 36)),
							(unsigned)GetReservedTrackbits(TileXY(38, 35)),
							(unsigned)GetReservedTrackbits(TileXY(38, 34)),
							(unsigned)GetReservedTrackbits(TileXY(39, 34)),
							(unsigned)GetReservedTrackbits(TileXY(39, 33)),
							(unsigned)GetReservedTrackbits(TileXY(39, 32)),
							(unsigned)GetReservedTrackbits(TileXY(39, 31)),
							(unsigned)GetReservedTrackbits(TileXY(39, 30)));
					fclose(dbg);
				}
			}
			return reserved ? next_trackdir : INVALID_TRACKDIR;
		}

		return next_trackdir;
	}
};

template <class Types>
class CYapfFollowAnySafeTileRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		TrackFollower follower{Yapf().GetVehicle(), Yapf().GetCompatibleRailTypes()};
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir()) && follower.MaskReservedTracks()) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static bool stFindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype)
	{
		/* Create pathfinder instance */
		Tpf pf1;
		bool result1;
		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.FindNearestSafeTile(v, t1, td, override_railtype, false);
		} else {
			bool found_path_1, found_path_2;
			pf1.FindNearestSafeTile(v, t1, td, override_railtype, true, &found_path_1);
			Tpf pf2;
			pf2.DisableCache(true);
			result1 = pf2.FindNearestSafeTile(v, t1, td, override_railtype, false, &found_path_2);
			if (found_path_1 != found_path_2) {
				Debug(desync, 0, "CACHE ERROR: FindSafeTile() = [{}, {}]", found_path_1 ? "T" : "F", found_path_2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (found_path_2) {
				CYapfFollowAnySafeTileRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: FindSafeTile()", true);
			}
		}

		return result1;
	}

	bool FindNearestSafeTile(const Train *v, TileIndex t1, Trackdir td, bool override_railtype, bool dont_reserve, bool *found_path = nullptr)
	{
		/* Set origin and destination. */
		Yapf().SetOrigin(t1, td);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v, override_railtype);

		bool bFound = Yapf().FindPath(v);
		if (found_path) *found_path = bFound;
		if (!bFound) return false;

		/* Found a destination, set as reservation target. */
		Node *node = Yapf().GetBestNode();
		this->SetReservationTarget(node, node->GetLastTile(), node->GetLastTrackdir());

		/* Walk through the path back to the origin. */
		auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
		safe_position_nodes.clear();
		Node *prev = nullptr;
		while (node->parent != nullptr) {
			prev = node;
			node = node->parent;

			safe_position_nodes.push_back(prev);
		}
		this->FindSafePositionOnSafePositionNodes();

		return dont_reserve || this->TryReservePath(nullptr, node->GetLastTile());
	}
};

template <class Types>
class CYapfFollowRailT : public CYapfReserveTrack<Types> {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::TrackFollower TrackFollower;
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	/** @copydoc CYapfBaseT::Yapf */
	inline Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	/** @copydoc CYapfBaseT::PfFollowNodeFunc */
	inline void PfFollowNode(Node &old_node)
	{
		const Train *v = Yapf().GetVehicle();
		TrackFollower follower(v);
		if (old_node.flags_u.flags_s.reverse_pending && old_node.segment->end_segment_reason.Any({EndSegmentReason::SafeTile, EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
			Node *rev_node = &old_node;
			uint length = 0;
			while (rev_node != nullptr && !rev_node->segment->end_segment_reason.Test(EndSegmentReason::Reverse)) {
				length += rev_node->GetNodeLength(v, Yapf(), *this);
				rev_node = rev_node->parent;
			}
			if (rev_node != nullptr && length >= v->gcache.cached_total_length) {
				if (follower.Follow(rev_node->GetLastTile(), ReverseTrackdir(rev_node->GetLastTrackdir()))) {
					Yapf().AddMultipleNodes(&old_node, follower, [&](Node &n) {
						n.flags_u.flags_s.reverse_pending = false;
						n.flags_u.flags_s.teleport = true;
					});
				}
				return;
			} else if (old_node.segment->end_segment_reason.Any({EndSegmentReason::Depot, EndSegmentReason::DeadEnd})) {
				return;
			}
		}
		if (follower.Follow(old_node.GetLastTile(), old_node.GetLastTrackdir())) {
			Yapf().AddMultipleNodes(&old_node, follower);
		}
	}

	/** @copydoc CYapfBaseT::TransportTypeCharFunc */
	inline char TransportTypeChar() const
	{
		return 't';
	}

	static Trackdir stChooseRailTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		/* create pathfinder instance */
		Tpf pf1;
		Trackdir result1;

		if (GetDebugLevel(DebugLevelID::yapfdesync) < 1 && GetDebugLevel(DebugLevelID::desync) < 2) {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
		} else {
			result1 = pf1.ChooseRailTrack(v, tile, enterdir, tracks, path_found, false, nullptr, nullptr);
			Tpf pf2;
			pf2.DisableCache(true);
			Trackdir result2 = pf2.ChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);
			if (result1 != result2) {
				Debug(desync, 0, "CACHE ERROR: ChooseRailTrack() = [{}, {}]", result1, result2);
				DumpState(pf1, pf2);
			} else if (result1 != INVALID_TRACKDIR) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: ChooseRailTrack()", true);
			}
		}

		return result1;
	}

	inline Trackdir ChooseRailTrack(const Train *v, TileIndex, DiagDirection, TrackBits, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
	{
		if (target != nullptr) target->tile = INVALID_TILE;
		if (dest != nullptr) *dest = INVALID_TILE;

		/* set origin and destination nodes */
		PBSTileInfo origin;
		if (v->current_order.IsType(OT_GOTO_COUPLE)) {
			/* R3R: when coupling, start the path from the loco itself.
			 * FollowTrainReservation would walk the consist's platform reservation up
			 * to its far end, and the path would then extend past the platform. */
			origin = PBSTileInfo(v->tile, v->GetVehicleTrackdir(), false);
		} else {
			origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
		}
		Yapf().SetOrigin(origin.tile, origin.trackdir, INVALID_TILE, INVALID_TRACKDIR, 1);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(true);
		Yapf().SetDestination(v);

		/* find the best path */
		path_found = Yapf().FindPath(v);
		{
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CRT veh=%d order=%d dir=%d origin=%d,%d td=%d found=%d\n",
						(int)v->index.base(), (int)v->current_order.GetType(), (int)v->direction,
						(int)TileX(origin.tile), (int)TileY(origin.tile), (int)origin.trackdir, (int)path_found);
				fclose(dbg);
			}
		}

		/* if path not found - return INVALID_TRACKDIR */
		Trackdir next_trackdir = INVALID_TRACKDIR;
		Node *node = Yapf().GetBestNode();
		if (node != nullptr) {
			/* reserve till end of path */
			this->SetReservationTarget(node, node->GetLastTile(), node->GetLastTrackdir());

			/* path was found or at least suggested
			 * walk through the path back to the origin */
			auto &safe_position_nodes = CYapfReserveTrackSafePositionNodes<Node>::safe_position_nodes;
			safe_position_nodes.clear();
			Node *prev = nullptr;
			while (node->parent != nullptr) {
				prev = node;
				node = node->parent;

				safe_position_nodes.push_back(prev);
			}
			this->FindSafePositionOnSafePositionNodes();

			/* If the best PF node has no parent, then there is no (valid) best next trackdir to return.
			 * This occurs when the PF is called while the train is already at its destination. */
			if (prev == nullptr) return INVALID_TRACKDIR;

			/* return trackdir from the best origin node (one of start nodes) */
			Node &best_next_node = *prev;
			next_trackdir = best_next_node.GetTrackdir();

			if (reserve_track && path_found) {
				if (dest != nullptr) *dest = Yapf().GetBestNode()->GetLastTile();
				this->TryReservePath(target, node->GetLastTile());
			}
		}

		/* Treat the path as found if stopped on the first two way signal(s). */
		path_found |= Yapf().stopped_on_first_two_way_signal;
		return next_trackdir;
	}

	static bool stCheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty, TraceRestrictProgramInputFlags tracerestrict_flags)
	{
		Tpf pf1;
		bool result1 = pf1.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty, tracerestrict_flags);

		if (GetDebugLevel(DebugLevelID::yapfdesync) > 0 || GetDebugLevel(DebugLevelID::desync) >= 2) {
			Tpf pf2;
			pf2.DisableCache(true);
			bool result2 = pf2.CheckReverseTrain(v, t1, td1, t2, td2, reverse_penalty, tracerestrict_flags);
			if (result1 != result2) {
				Debug(desync, 2, "CACHE ERROR: CheckReverseTrain() = [{}, {}]", result1 ? "T" : "F", result2 ? "T" : "F");
				DumpState(pf1, pf2);
			} else if (result1) {
				CYapfFollowRailT::stDesyncCheck(pf1, pf2, "CACHE ERROR: CheckReverseTrain()", false);
			}
		}

		return result1;
	}

	inline bool CheckReverseTrain(const Train *v, TileIndex t1, Trackdir td1, TileIndex t2, Trackdir td2, int reverse_penalty, TraceRestrictProgramInputFlags tracerestrict_flags)
	{
		/* create pathfinder instance
		 * set origin and destination nodes */
		Yapf().SetOrigin(t1, td1, t2, td2, reverse_penalty);
		Yapf().SetTreatFirstRedTwoWaySignalAsEOL(false);
		Yapf().SetTraceRestrictProgramInputFlags(tracerestrict_flags);
		Yapf().SetDestination(v);

		/* find the best path */
		if (!Yapf().FindPath(v)) return false;

		/* path was found
		 * walk through the path back to the origin */
		Node *node = Yapf().GetBestNode();
		while (node->parent != nullptr) {
			node = node->parent;
		}

		/* check if it was reversed origin */
		bool reversed = (node->cost != 0);
		return reversed;
	}
};

template <class Tpf_, class Ttrack_follower, template <class Types> class TdestinationT, template <class Types> class TfollowT>
struct CYapfRail_TypesT {
	typedef CYapfRail_TypesT<Tpf_, Ttrack_follower, TdestinationT, TfollowT>  Types;

	typedef Tpf_                                Tpf;
	typedef Ttrack_follower                     TrackFollower;
	typedef CRailNodeList                       NodeList;
	typedef Train                               VehicleType;
	typedef CYapfBaseT<Types>                   PfBase;
	typedef TfollowT<Types>                     PfFollow;
	typedef CYapfOriginTileTwoWayT<Types>       PfOrigin;
	typedef TdestinationT<Types>                PfDestination;
	typedef CYapfSegmentCostCacheGlobalT<Types> PfCache;
	typedef CYapfCostRailT<Types>               PfCost;
};

template <typename Types>
struct CYapfRailBase : CYapfT<Types> {
	typedef typename Types::NodeList::Item Node;

	/**
	 * In some cases an intermediate node branch should be pruned.
	 * The most prominent case is when a red EOL signal is encountered, but
	 * there was a segment change (e.g. a rail type change) before that. If
	 * the branch would not be pruned, the rail type change location would
	 * remain the best intermediate node, and thus the vehicle would still
	 * go towards the red EOL signal.
	 * @param n The node to start pruning at.
	 */
	void PruneIntermediateNodeBranch(Node *n)
	{
		bool intermediate_on_branch = false;
		while (n != nullptr && !n->segment->end_segment_reason.Test(EndSegmentReason::ChoiceFollows)) {
			if (n == this->best_intermediate_node) intermediate_on_branch = true;
			n = n->parent;
		}
		if (intermediate_on_branch) this->best_intermediate_node = n;
	}
};

struct CYapfRail         : CYapfRailBase<CYapfRail_TypesT<CYapfRail        , CFollowTrackRail    , CYapfDestinationTileOrStationRailT, CYapfFollowRailT>> {};
struct CYapfRailNo90     : CYapfRailBase<CYapfRail_TypesT<CYapfRailNo90    , CFollowTrackRailNo90, CYapfDestinationTileOrStationRailT, CYapfFollowRailT>> {};

struct CYapfAnyDepotRail     : CYapfRailBase<CYapfRail_TypesT<CYapfAnyDepotRail,     CFollowTrackRail    , CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT>> {};
struct CYapfAnyDepotRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfAnyDepotRailNo90, CFollowTrackRailNo90, CYapfDestinationAnyDepotRailT     , CYapfFollowAnyDepotRailT>> {};

struct CYapfCoupleRail     : CYapfRailBase<CYapfRail_TypesT<CYapfCoupleRail,     CFollowTrackRail    , CYapfDestinationTrainRailT, CYapfFollowCoupleRailT>> {};
struct CYapfCoupleRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfCoupleRailNo90, CFollowTrackRailNo90, CYapfDestinationTrainRailT, CYapfFollowCoupleRailT>> {};

struct CYapfAnySafeTileRail     : CYapfRailBase<CYapfRail_TypesT<CYapfAnySafeTileRail    , CFollowTrackFreeRail    , CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT>> {};
struct CYapfAnySafeTileRailNo90 : CYapfRailBase<CYapfRail_TypesT<CYapfAnySafeTileRailNo90, CFollowTrackFreeRailNo90, CYapfDestinationAnySafeTileRailT , CYapfFollowAnySafeTileRailT>> {};


Track YapfTrainChooseTrack(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool reserve_track, PBSTileInfo *target, TileIndex *dest)
{
	Trackdir td_ret = _settings_game.pf.forbid_90_deg
		? CYapfRailNo90::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest)
		: CYapfRail::stChooseRailTrack(v, tile, enterdir, tracks, path_found, reserve_track, target, dest);

	return (td_ret != INVALID_TRACKDIR) ? TrackdirToTrack(td_ret) : FindFirstTrack(tracks);
}

static bool YapfTrainCheckReverse(const Train *v, TileIndex tile, Trackdir td, TileIndex tile_rev, Trackdir td_rev, int reverse_penalty)
{
	TraceRestrictProgramInputFlags input_flags{};
	bool swapped = false;
	if (reverse_penalty < 0) {
		reverse_penalty = -reverse_penalty;
		swapped = true;
		input_flags.Set(TraceRestrictProgramInputFlag::InvertDrivingDirection);

		std::swap(tile, tile_rev);
		std::swap(td, td_rev);
	} else if (reverse_penalty == 0) {
		/* slightly hackish: If the pathfinders finds a path, the cost of the first node is tested to distinguish between forward- and reverse-path. */
		reverse_penalty = 1;
	}

	bool reverse = _settings_game.pf.forbid_90_deg
		? CYapfRailNo90::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty, input_flags)
		: CYapfRail::stCheckReverseTrain(v, tile, td, tile_rev, td_rev, reverse_penalty, input_flags);

	return reverse != swapped;
}

constexpr int DRIVING_BACKWARDS_PENALTY = 100 * YAPF_TILE_LENGTH;

bool YapfTrainCheckReverse(const Train *v)
{
	const Train *moving_front = v->GetMovingFront();
	const Train *moving_back = v->GetMovingBack();

	/* get trackdirs of both ends */
	Trackdir td = moving_front->GetVehicleTrackdir();
	Trackdir td_rev = ReverseTrackdir(moving_back->GetVehicleTrackdir());

	/* tiles where front and back are */
	TileIndex tile = moving_front->tile;
	TileIndex tile_rev = moving_back->tile;

	/* R3R: after a nose-to-nose coupling the consist can sit spatially folded -
	 * its "back" vehicle is physically in FRONT of the locomotive (several
	 * vehicles share one tile). The reverse heuristic compares the forward path
	 * from the front origin with the backward path from the back origin; a
	 * folded chain misleads it into recommending a flip onto a heading that
	 * cannot reach the destination (REVERSEDIR then CRT found=0, train stranded).
	 * Detect the fold: the back wagon lies ahead of the front vehicle along the
	 * front vehicle's facing direction -> never suggest reversing in that state. */
	if (moving_front->track != TRACK_BIT_DEPOT && moving_back->track != TRACK_BIT_DEPOT) {
		const TileIndexDiffC delta = TileIndexDiffCByDir(moving_front->direction);
		const int rel_x = TileX(tile_rev) - TileX(tile);
		const int rel_y = TileY(tile_rev) - TileY(tile);
		if (rel_x * delta.x + rel_y * delta.y > 0) {
			FILE *dbg = fopen("R3R_debug.log", "a");
			if (dbg != nullptr) {
				fprintf(dbg, "CRT-FOLD veh=%d tile=%d,%d dir=%d backTile=%d,%d rel=%d,%d dot=%d\n",
						(int)v->index.base(), (int)TileX(tile), (int)TileY(tile), (int)moving_front->direction,
						(int)TileX(tile_rev), (int)TileY(tile_rev), rel_x, rel_y,
						rel_x * delta.x + rel_y * delta.y);
				fclose(dbg);
			}
			return false;
		}
	}
	int reverse_penalty = 0;

	/* Consider whether the train might back up at reduced speed. */
	if (_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None && !v->Last()->CanLeadTrain()
			&& moving_front->track != TRACK_BIT_DEPOT && moving_back->track != TRACK_BIT_DEPOT) {
		if (!v->vehicle_flags.Test(VehicleFlag::DrivingBackwards)) {
			/* We're currently driving forwards at full speed, and would rather not reverse if possible. */
			reverse_penalty += DRIVING_BACKWARDS_PENALTY;
		} else {
			/* We're currently driving backwards slowly, prefer reversing. */
			reverse_penalty -= DRIVING_BACKWARDS_PENALTY;
		}
	}

	if (moving_front->track & TRACK_BIT_WORMHOLE) {
		/* front in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile);

		/* Current position of the train in the wormhole */
		TileIndex cur_tile = TileVirtXY(moving_front->x_pos, moving_front->y_pos);

		/* Add distance to drive in the wormhole as penalty for the forward path, i.e. bonus for the reverse path
		 * Note: Negative penalties are ok for the start tile. */
		if (TrackdirToExitdir(td) == dir_into_wormhole) {
			reverse_penalty += DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty -= DistanceManhattan(cur_tile, tile) * YAPF_TILE_LENGTH;
		}
	}

	if (moving_back->track & TRACK_BIT_WORMHOLE) {
		/* back in tunnel / on bridge */
		DiagDirection dir_into_wormhole = GetTunnelBridgeDirection(tile_rev);

		/* Current position of the last wagon in the wormhole */
		TileIndex cur_tile = TileVirtXY(moving_back->x_pos, moving_back->y_pos);

		/* Add distance to drive in the wormhole as penalty for the revere path. */
		if (TrackdirToExitdir(td_rev) == dir_into_wormhole) {
			reverse_penalty -= DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		} else {
			reverse_penalty += DistanceManhattan(cur_tile, tile_rev) * YAPF_TILE_LENGTH;
		}
	}

	return YapfTrainCheckReverse(v, tile, td, tile_rev, td_rev, reverse_penalty);
}

bool YapfTrainCheckDepotReverse(const Train *v, TileIndex forward_depot, TileIndex reverse_depot)
{
	int reverse_penalty = 1;
	if (_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None && !v->Last()->CanLeadTrain()) {
		/* Apply penalties to prefer driving out in forward direction. */
		if (v->vehicle_flags.Test(VehicleFlag::DrivingBackwards)) {
			/* Prefer reverse depot. */
			reverse_penalty = -DRIVING_BACKWARDS_PENALTY;
		} else {
			/* Prefer forward depot. */
			reverse_penalty = DRIVING_BACKWARDS_PENALTY;
		}
	}

	return YapfTrainCheckReverse(v, forward_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(forward_depot)),
			reverse_depot, DiagDirToDiagTrackdir(GetRailDepotDirection(reverse_depot)), reverse_penalty);
}

FindDepotData YapfTrainFindNearestDepot(const Train *v, int max_penalty)
{
	const Train *moving_back = v->GetMovingBack();

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
	TileIndex last_tile = moving_back->tile;
	Trackdir td_rev = ReverseTrackdir(moving_back->GetVehicleTrackdir());

	return _settings_game.pf.forbid_90_deg
		? CYapfAnyDepotRailNo90::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY)
		: CYapfAnyDepotRail::stFindNearestDepotTwoWay(v, origin.tile, origin.trackdir, last_tile, td_rev, max_penalty, YAPF_INFINITE_PENALTY);
}

bool YapfTrainFindNearestSafeTile(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	return _settings_game.pf.forbid_90_deg
		? CYapfAnySafeTileRailNo90::stFindNearestSafeTile(v, tile, td, override_railtype)
		: CYapfAnySafeTileRail::stFindNearestSafeTile(v, tile, td, override_railtype);
}

/**
 * Find the track to take when approaching a waiting consist to couple with.
 * @param v The train to find a track for.
 * @param dont_reserve Whether to skip making a reservation.
 * @return The track to take, or #INVALID_TRACK if no path was found.
 */
Track YapfTrainCoupleTrack(const Train *v, bool dont_reserve)
{
	{
		FILE *dbg = fopen("R3R_debug.log", "a");
		if (dbg != nullptr) {
			fprintf(dbg, "CPL-ENTRY veh=%d tile=%d,%d orderType=%d dontReserve=%d\n",
					(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
					(int)v->current_order.GetType(), (int)dont_reserve);
			fclose(dbg);
		}
	}
	Trackdir ret = _settings_game.pf.forbid_90_deg
		? CYapfCoupleRailNo90::stFindNearestCoupleTrain(v, dont_reserve)
		: CYapfCoupleRail::stFindNearestCoupleTrain(v, dont_reserve);

	return (ret != INVALID_TRACKDIR) ? TrackdirToTrack(ret) : INVALID_TRACK;
}

/** if any track changes, this counter is incremented - that will invalidate segment cost cache */
int CSegmentCostCacheBase::s_rail_change_counter = 0;

void YapfCheckRailSignalPenalties()
{
	bool negative = false;
	int p0 = _settings_game.pf.yapf.rail_look_ahead_signal_p0;
	int p1 = _settings_game.pf.yapf.rail_look_ahead_signal_p1;
	int p2 = _settings_game.pf.yapf.rail_look_ahead_signal_p2;
	for (int i = 0; i < (int) _settings_game.pf.yapf.rail_look_ahead_max_signals; i++) {
		if (p0 + i * (p1 + i * p2) < 0) negative = true;
	}
	if (negative) {
		Debug(misc, 0, "Settings: pf.yapf.rail_look_ahead_signal_p0, pf.yapf.rail_look_ahead_signal_p1, pf.yapf.rail_look_ahead_signal_p2 and pf.yapf.rail_look_ahead_max_signal "
				"are set to incorrect values (i.e. resulting in negative penalties), negative penalties will be truncated");
	}
}
