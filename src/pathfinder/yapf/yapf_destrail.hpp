/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file yapf_destrail.hpp Determining the destination for rail vehicles. */

#ifndef YAPF_DESTRAIL_HPP
#define YAPF_DESTRAIL_HPP

#include "../../train.h"
#include "../../vehicle_func.h"
#include "../pathfinder_func.h"
#include "../pathfinder_type.h"
#include "../../r3r_perf.h"

class CYapfDestinationRailBase {
protected:
	RailTypes compatible_railtypes;

public:
	void SetDestination(const Train *v, bool override_rail_type = false)
	{
		this->compatible_railtypes = v->compatible_railtypes;
		if (override_rail_type) this->compatible_railtypes.Set(GetAllCompatibleRailTypes(v->railtypes));
	}

	bool IsCompatibleRailType(RailType rt)
	{
		return this->compatible_railtypes.Test(rt);
	}

	RailTypes GetCompatibleRailTypes() const
	{
		return this->compatible_railtypes;
	}
};

template <class Types>
class CYapfDestinationAnyDepotRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, [[maybe_unused]] Trackdir td)
	{
		return IsRailDepotTile(tile);
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		n.estimate = n.cost;
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

template <class Types>
class CYapfDestinationAnySafeTileRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables
	typedef typename Types::TrackFollower TrackFollower; ///< TrackFollower. Need to typedef for gcc 2.95

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		return IsSafeWaitingPosition(Yapf().GetVehicle(), tile, td, true, !TrackFollower::Allow90degTurns()) &&
				IsWaitingPositionFree(Yapf().GetVehicle(), tile, td, !TrackFollower::Allow90degTurns());
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		n.estimate = n.cost;
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

template <class Types>
class CYapfDestinationTileOrStationRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf; ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key; ///< key to hash tables

protected:
	TileIndex dest_tile;
	TrackdirBits dest_trackdirs;
	StationID dest_station_id;
	bool any_depot;

	/** @copydoc CYapfBaseT::Yapf */
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

public:
	void SetDestination(const Train *v)
	{
		this->any_depot = false;
		switch (v->current_order.GetType()) {
			case OT_GOTO_WAYPOINT:
				if (!Waypoint::Get(v->current_order.GetDestination().ToStationID())->IsSingleTile()) {
					/* In case of 'complex' waypoints we need to do a look
					 * ahead. This look ahead messes a bit about, which
					 * means that it 'corrupts' the cache. To prevent this
					 * we disable caching when we're looking for a complex
					 * waypoint. */
					Yapf().DisableCache(true);
				}
				[[fallthrough]];

			case OT_GOTO_STATION:
				this->dest_tile = CalcClosestStationTile(v->current_order.GetDestination().ToStationID(), v->GetMovingFront()->tile, v->current_order.IsType(OT_GOTO_STATION) ? StationType::Rail : StationType::RailWaypoint);
				this->dest_station_id = v->current_order.GetDestination().ToStationID();
				this->dest_trackdirs = INVALID_TRACKDIR_BIT;
				break;

			case OT_GOTO_DEPOT:
				if (v->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) {
					this->any_depot = true;
				}
				[[fallthrough]];

			default:
				this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
				this->dest_station_id = StationID::Invalid();
				this->dest_trackdirs = GetTileTrackdirBits(this->dest_tile, TRANSPORT_RAIL, 0);
				break;
		}
		this->CYapfDestinationRailBase::SetDestination(v);
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		if (this->dest_station_id != StationID::Invalid()) {
			return HasStationTileRail(tile)
				&& (GetStationIndex(tile) == this->dest_station_id)
				&& (GetRailStationTrack(tile) == TrackdirToTrack(td));
		}

		if (this->any_depot) {
			return IsRailDepotTile(tile);
		}

		return (tile == this->dest_tile) && HasTrackdir(this->dest_trackdirs, td);
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		if (this->PfDetectDestination(n)) {
			n.estimate = n.cost;
			return true;
		}

		n.estimate = n.cost + OctileDistanceCost(n.GetLastTile(), n.GetLastTrackdir(), this->dest_tile);
		assert(n.estimate >= n.parent->estimate);
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		auto calculate_distance_cost = [&](TileIndex t, int d_adjust) -> int {
			int x1 = 2 * TileX(t);
			int y1 = 2 * TileY(t);
			int x2 = 2 * TileX(this->dest_tile);
			int y2 = 2 * TileY(this->dest_tile);
			int dx = abs(x1 - x2) + d_adjust;
			int dy = abs(y1 - y2);
			int dmin = std::min(dx, dy) + d_adjust; // up to 2x track exit dir tile offsets in opposite directions
			int dxy = abs(dx - dy) + d_adjust; // "
			return dmin * YAPF_TILE_CORNER_LENGTH + (dxy - 1) * (YAPF_TILE_LENGTH / 2);
		};
		return std::max<int>(0, calculate_distance_cost(prev_tile, 8) - calculate_distance_cost(cur_tile, 0));
	}
};

/**
 * Destination: a waiting consist (R3R consist-group free-wagon chain).
 * The pathfinder searches for the nearest platform/depot tile whose reserved
 * train is a consist waiting to be coupled onto.
 */
template <class Types>
class CYapfDestinationTrainRailT : public CYapfDestinationRailBase {
public:
	typedef typename Types::Tpf Tpf;              ///< the pathfinder class (derived from THIS class)
	typedef typename Types::NodeList::Item Node; ///< this will be our node type
	typedef typename Node::Key Key;               ///< key to hash tables

protected:
	Tpf &Yapf()
	{
		return *static_cast<Tpf *>(this);
	}

	TileIndex dest_tile; ///< heuristic destination (the order's station/depot).

protected:
	Order dest_order; ///< Copy of the locomotive's GOTO_COUPLE order (pxp-decouple reference).

public:
	void SetDestination(const Train *v)
	{
		this->dest_order.AssignOrder(v->current_order);
		this->CYapfDestinationRailBase::SetDestination(v);
		this->dest_tile = (v->dest_tile == INVALID_TILE) ? TileIndex{} : v->dest_tile;
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationFunc */
	inline bool PfDetectDestination(Node &n)
	{
		return this->PfDetectDestination(n.GetLastTile(), n.GetLastTrackdir());
	}

	/** Check the load-state requirement from the GOTO_COUPLE order. */
	bool CheckOrderLoad(const Train *t) const
	{
		switch (dest_order.GetCoupleLoad()) {
			case ODC_ANY: return true;
			case ODC_IS_EMPTY: return t->cargo.StoredCount() == 0;
			case ODC_IS_FULL: return t->cargo.StoredCount() == t->cargo_cap;
			default: NOT_REACHED();
		}
	}

	/** Check the cargo-type requirement from the GOTO_COUPLE order. */
	bool CheckOrderCargoType(const Train *t) const
	{
		if (!dest_order.HasCoupleCargoType()) return true;
		CargoType cargo_type = dest_order.GetCoupleCargoType();
		for (const Train *v = t; v != nullptr; v = v->Next()) {
			if (v->cargo_type == cargo_type && v->cargo_cap > 0) return true;
		}
		return false;
	}

	/** Check the wagon-count requirement from the GOTO_COUPLE order. */
	bool CheckNumberOfWagons(const Train *t) const
	{
		if (dest_order.GetNumCouple() == 0) return true;
		return dest_order.GetNumCouple() == CountVehiclesInChain(t);
	}

	/** Check the trace-restrict-slot requirement from the GOTO_COUPLE order.
	 * The couple-slot selector is a purely optional user restriction. Nothing
	 * in the game sets it on GOTO_COUPLE orders (there is no SetCoupleSlot
	 * caller and no order-window UI for it yet), so an unset order reads back
	 * slot id 0 (the default xdata value), NOT the Invalid() sentinel 0xFFFF.
	 * Slot 0 typically does not exist in the pool, so GetIfValid() returns
	 * null and every candidate would be rejected. Treat an unset/0 slot and any
	 * slot id that no longer resolves as "no restriction". A slot that exists
	 * but holds no occupants cannot discriminate either — the decouple flow
	 * never registers the waiting WAIT_COUPLE consist into the coupler's slot
	 * automatically (occupancy only appears if the map's trace-restrict setup
	 * adds it), so an empty slot must accept the sole waiting consist instead
	 * of making every station pick-up impossible. The slot only rejects a
	 * candidate when it genuinely holds some other consist. */
	bool CheckOrderSlot(const Train *t) const
	{
		TraceRestrictSlotID slot = dest_order.GetCoupleSlot();
		if (slot == TraceRestrictSlotID::Invalid() || slot == TraceRestrictSlotID(0)) return true;
		const TraceRestrictSlot *s = TraceRestrictSlot::GetIfValid(slot);
		if (s == nullptr) return true;
		if (s->IsOccupant(t->index)) return true;
		if (s->occupants.empty()) return true;
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "COUPLE-SLOT-REJ u=%d slotRaw=%u nOcc=%zu\n",
					(int)t->index.base(), (unsigned)slot.base(), s->occupants.size());
				fclose(dbg);
			}
		}
		return false;
	}

	/** @copydoc CYapfBaseT::PfDetectDestinationTileFunc */
	inline bool PfDetectDestination(TileIndex tile, Trackdir td)
	{
		/* Only platform or depot tiles qualify. */
		if (!IsRailStationTile(tile) && !IsRailDepotTile(tile)) return false;

		TrackdirBits tdb = TrackdirToTrackdirBits(td);
		bool has_res = HasReservedTracks(tile, TrackdirBitsToTrackBits(tdb));
		if (IsRailStationTile(tile)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "PFD tile=%d,%d td=%d trackbits=0x%x hasRes=%d\n",
					(int)TileX(tile), (int)TileY(tile), (int)td,
					(unsigned)TrackdirBitsToTrackBits(tdb), (int)has_res);
				fclose(dbg);
			}
		}
		if (!has_res && !IsRailStationTile(tile)) return false;

		Train *t = nullptr;
		if (has_res) {
			t = GetTrainForReservation(tile, TrackdirToTrack(td));
			/* R3R: the coupling loco's own reservation may cover the consist's
			 * platform tile, making GetTrainForReservation return the loco itself
			 * and mask the consist (couple pathfinder found=0 right next to it).
			 * The consist is never GOTO_COUPLE - ignore such reservations. */
			if (t != nullptr && t->IsPrimaryVehicle() && t->current_order.IsType(OT_GOTO_COUPLE)) t = nullptr;
		}
		if (t == nullptr && IsRailStationTile(tile)) {
			/* R3R: the whole-platform reservation may leave reserved tiles that
			 * have no consist on them (the consist is elsewhere on the platform).
			 * Scan the rest of the platform for a consist before giving up. */
			const Track track = TrackdirToTrack(td);
			const TileIndexDiff delta = TileOffsByAxis(GetRailStationAxis(tile));
			for (int dir = 0; dir < 2 && t == nullptr; ++dir) {
				TileIndex st = tile + (dir == 0 ? delta : -delta);
				while (IsCompatibleTrainStationTile(st, tile)) {
					if (HasReservedTracks(st, TrackToTrackBits(track))) {
						t = GetTrainForReservation(st, track);
						if (t != nullptr && t->IsPrimaryVehicle() && t->current_order.IsType(OT_GOTO_COUPLE)) t = nullptr;
						if (t == nullptr) {
							for (Train *tr : VehiclesOnTile<VehicleType::Train>(st)) {
								Train *first = tr->First();
								if (tr->IsFrontEngine() && (R3RIsCarOnlyFormation(first) ||
										(first->IsPrimaryVehicle() && first->current_order.IsType(OT_WAIT_COUPLE)))) {
									t = first; break;
								}
							}
						}
						if (t != nullptr) break;
					}
					st += (dir == 0 ? delta : -delta);
				}
			}
		}
		if (t == nullptr && IsRailStationTile(tile)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				const TileIndexDiff delta = TileOffsByAxis(GetRailStationAxis(tile));
				TileIndex st0 = tile;
				while (IsCompatibleTrainStationTile(st0, tile)) st0 -= delta;
				for (TileIndex st = st0 + delta; IsCompatibleTrainStationTile(st, tile); st += delta) {
					for (Train *tr : VehiclesOnTile<VehicleType::Train>(st)) {
						fprintf(dbg, "PFD-SCAN tile=%d,%d veh=%d front=%d co=%d ord=%d\n",
							(int)TileX(st), (int)TileY(st), (int)tr->index.base(),
							(int)tr->IsFrontEngine(), (int)R3RIsCarOnlyFormation(tr->First()),
							(int)tr->current_order.GetType());
					}
				}
				fclose(dbg);
			}
		}
		if (t == nullptr) {
			if (IsRailStationTile(tile)) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "PFD tile=%d,%d hasResButNoTrain\n",
						(int)TileX(tile), (int)TileY(tile));
					fclose(dbg);
				}
			}
			return false;
		}
		t = t->First();
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				bool co = R3RIsCarOnlyFormation(t);
				bool wc = t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE);
				fprintf(dbg, "PFD tile=%d,%d t=%d co=%d wc=%d ordType=%d fit=%d load=%d cargo=%d wag=%d slot=%d\n",
					(int)TileX(tile), (int)TileY(tile), (int)t->index.base(),
					(int)co, (int)wc, (int)t->current_order.GetType(),
					(int)TrainFitStation(t),
					(int)CheckOrderLoad(t), (int)CheckOrderCargoType(t),
					(int)CheckNumberOfWagons(t), (int)CheckOrderSlot(t));
				fclose(dbg);
			}
		}

		/* R3R (pxp-decouple): the waiting formation must fit into its station. */
		if (!TrainFitStation(t)) return false;

		/* Target 1: a car-only formation (zero-power train front) waiting to be
		 * coupled. */
		if (R3RIsCarOnlyFormation(t)) return true;

		/* Target 2 (pxp-decouple reference): any primary vehicle whose current
		 * order declares WAIT_COUPLE is a candidate, provided it satisfies all
		 * requirements of the locomotive's GOTO_COUPLE order (load / cargo type /
		 * wagon count / trace restrict slot). */
		if (t->IsPrimaryVehicle() && t->current_order.IsType(OT_WAIT_COUPLE)) {
			return CheckOrderLoad(t) && CheckOrderCargoType(t) &&
					CheckNumberOfWagons(t) && CheckOrderSlot(t);
		}

		return false;
	}

	/** @copydoc CYapfBaseT::PfCalcEstimateFunc */
	inline bool PfCalcEstimate(Node &n)
	{
		if (this->PfDetectDestination(n)) {
			n.estimate = n.cost;
			return true;
		}
		n.estimate = n.cost + OctileDistanceCost(n.GetLastTile(), n.GetLastTrackdir(), this->dest_tile);
		assert(n.estimate >= n.parent->estimate);
		return true;
	}

	inline int TeleportCost(TileIndex cur_tile, TileIndex prev_tile)
	{
		return 0;
	}
};

#endif /* YAPF_DESTRAIL_HPP */
