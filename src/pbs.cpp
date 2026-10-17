/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pbs.cpp PBS support routines. */

#include "stdafx.h"
#include "debug.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "newgrf_station.h"
#include "pathfinder/follow_track.hpp"
#include "tracerestrict.h"
#include "newgrf_newsignals.h"
#include "train_speed_adaptation.h"
#include "bridge_signal_map.h"
#include "r3r_perf.h"
#include "train.h"
#include "sl/saveload.h"

#include "table/strings.h"

#include <vector>
#include <unordered_map>

#include "safeguards.h"

/*
 * R3R (KI-207, 2026-09-25): reservation-origin tracing for ghost reservations.
 *
 * Both directions of every reservation change to a *watched* tile are logged with
 * the phase the game was in at that moment (see R3RResvPhaseGuard in pbs.h).
 *
 * The writes are reported by the six map-accessor setters that own the reservation
 * bits (see r3r_resv_probes.h), not by the call sites: that makes the trace complete
 * by construction, including paths nobody thought of (load-time clearing, depot
 * sell, track removal, tile conversion, ...).
 *
 * The watch list is loaded once, lazily:
 *   - the tiles the player reported as ghosts are compiled in as defaults, so a
 *     plain rebuild is already a usable trap;
 *   - "r3r_resv_watch.txt" in the game working directory (next to R3R_debug.log)
 *     adds more tiles without a rebuild: one "x y" per line, '#' comments out.
 *
 * R3R (KI-359, 2026-10-11): as of this revision the phase/actor context and the
 * hook itself are built unconditionally, because the *ledger* below is a real fix
 * and not a probe -- it is fed from the same hook. Only the watched-tile logging
 * stays behind R3R_PROBES.
 */
const char *r3r_resv_phase = "misc";

/* R3R (KI-207): the train that is doing the reserving right now, if the caller knows
 * it (see R3RResvPhaseGuard). Logged together with the phase so a watched tile names
 * both the code path *and* the vehicle that booked it. Held as an ID, not a pointer:
 * this is reached from inside the map setters, where a raw pointer could name a train
 * that has already been deleted. */
VehicleID r3r_resv_actor = VehicleID::Invalid();

#if R3R_PROBES
static const std::vector<TileIndex> &R3RWatchedResvTiles()
{
	static const std::vector<TileIndex> tiles = []() {
		std::vector<TileIndex> v;
		/* Reported by the player (KI-205 "60,16" / KI-207 "57,49"). */
		v.push_back(TileXY(60, 16));
		v.push_back(TileXY(57, 49));
		FILE *f = fopen("r3r_resv_watch.txt", "r");
		if (f != nullptr) {
			char line[128];
			while (fgets(line, sizeof(line), f) != nullptr) {
				for (char *p = line; *p != '\0'; p++) {
					if (*p == '#' || *p == '\r' || *p == '\n') { *p = '\0'; break; }
				}
				int x = -1, y = -1;
				if (sscanf(line, "%d %d", &x, &y) == 2 && x >= 0 && y >= 0 && x < (int)Map::SizeX() && y < (int)Map::SizeY()) {
					v.push_back(TileXY(x, y));
				}
			}
			fclose(f);
		}
		return v;
	}();
	return tiles;
}

bool R3RIsWatchedResvTile(TileIndex tile)
{
	for (TileIndex t : R3RWatchedResvTiles()) {
		if (t == tile) return true;
	}
	return false;
}
#endif /* R3R_PROBES */

/*
 * R3R (KI-359, 2026-10-11): the reservation ledger -- "which train laid down which
 * bit".
 *
 * Why the walk is not enough: GetTrainForReservation() answers "which train stands
 * at the far end of this reserved path". While a locomotive couples to a waiting
 * consist the far end is *always* the consist, so the bits the coupling path
 * installs get attributed to the train being coupled to rather than to the one
 * reserving -- the field log has SET-RAIL actor=23 on the very tile the audit
 * reported as owner=veh22. The ledger instead books each bit under the chain head
 * of whoever was acting at the instant the bit was written; that is a caller-side
 * fact, and it is the same unit of ownership the rest of R3R uses.
 *
 * Two maps, keyed by a dense bit key (tile * 8 + track slot, slot 7 for whole-tile
 * bits): bit -> owner, and owner -> its bits, so releasing an owner is a single
 * lookup. Both are in-memory only and therefore empty after a load; the audit
 * falls back to the walk for bits that were never booked.
 *
 * A bit re-booked by somebody else simply moves to the new owner (the old row is
 * dropped first). For a multi-track tile only the first track is booked -- see
 * the "CHG-*" note at R3RResvWatchLog -- which under-books rather than over-books:
 * a missing row can only mean "released late", never "released somebody else's".
 *
 * R3R (KI-362/363/364 + REG-243-K, 2026-10-14): the ledger is an ownership table,
 * not a one-way log book, and as such it has to obey two laws:
 *
 *  1. *Release law*: a release may only free the bits the caller is entitled to by
 *     **purpose**. A bit that is still needed for another purpose must only have
 *     that purpose taken off it. Before this, R3RResvLedgerForget() freed every
 *     bit of the owner, including the tiles the train physically stands on that
 *     ReserveTrackUnderConsist() keeps under it; those were immediately re-booked
 *     by the next ReserveTrackUnderConsist(), once per tick, for every tile of the
 *     consist (KI-362). And R3RReleaseReservationSafe() only looked at the map
 *     ("is a bit set here?") without ever asking the ledger whose it is, so a
 *     drifted ledger could free somebody else's bit (REG-243-K).
 *  2. *Mirror law*: every row must match a bit that really is reserved on the map,
 *     and a bit that is still reserved and still ours must not be abandoned. The
 *     old release path dropped the row in both cases, so a platform bit it refused
 *     to free (a train stands there) was left with no owner at all -- which is
 *     exactly the ownerless stray the reap path then had to sweep up (KI-363/364).
 *     R3RResvLedgerResync() re-checks the ledger against the map so that a writer
 *     which bypasses the setters (MakeStation() rebuilding the tile,
 *     SetRailStationPlatformReservation() clearing a whole platform) cannot cause
 *     permanent drift.
 *
 * "Purpose" is read from the phase the caller named with R3RResvPhaseGuard(); see
 * R3RResvLedgerPurpose(). The reason string handed to R3RResvLedgerForget() picks
 * the mask of the release; see R3RResvLedgerForgetMask().
 */
struct R3RResvLedgerRec {
	TileIndex tile;
	Track track;
	uint8_t purposes;  ///< bitmask of R3RResvPurpose, i.e. why the bit is held
};

/** Why a bit is held; see the two laws in the ledger comment above. */
enum R3RResvPurpose : uint8_t {
	R3R_RESV_PURPOSE_PATH    = 0x01,  ///< a path reservation (lookahead / coupling path)
	R3R_RESV_PURPOSE_CONSIST = 0x02,  ///< the tiles the train physically stands on
	R3R_RESV_PURPOSE_ALL     = 0x03,  ///< nothing holds the bit once the train is gone
};

static std::unordered_map<uint32_t, std::vector<R3RResvLedgerRec>> _r3r_resv_by_owner;
static std::unordered_map<uint32_t, uint32_t> _r3r_resv_owner_of_bit;

/** R3R (第 245 轮, KI-369): the bit key's slot for whole-tile (station / waypoint)
 *  reservations. train_cmd.cpp spells the same value out in its own audit key, but
 *  it cannot be shared across translation units, so inside pbs.cpp it lives here --
 *  the ledger and the R3RL savegame then decode the key from one definition. */
static constexpr uint8_t R3R_RESV_SLOT_WHOLE_TILE = 7;

static inline uint32_t R3RResvBitKey(TileIndex tile, Track track)
{
	const uint32_t slot = (track == INVALID_TRACK) ? R3R_RESV_SLOT_WHOLE_TILE : (uint32_t)track;
	return (tile.base() << 3) | slot;
}

/** The acting train's chain head, i.e. the ledger's unit of ownership, or
 *  R3R_RESV_NO_OWNER when no actor is set (a load-time or engine-internal write
 *  that must not be booked: it has no R3R owner to release it later). */
static uint32_t R3RResvLedgerActorHead()
{
	const Vehicle *v = Vehicle::GetIfValid(r3r_resv_actor);
	if (v == nullptr) return R3R_RESV_NO_OWNER;
	const Vehicle *h = v->First();
	return (h != nullptr ? h->index.base() : v->index.base());
}

/** Drop one row from an owner's list (the bit key is dropped separately). */
static void R3RResvLedgerDropRow(uint32_t owner, TileIndex tile, Track track)
{
	const auto own_it = _r3r_resv_by_owner.find(owner);
	if (own_it == _r3r_resv_by_owner.end()) return;
	auto &rows = own_it->second;
	for (auto it = rows.begin(); it != rows.end(); ++it) {
		if (it->tile == tile && it->track == track) { rows.erase(it); break; }
	}
	if (rows.empty()) _r3r_resv_by_owner.erase(own_it);
}

/** strcmp() == 0 without pulling in a header for it. */
static inline bool R3RResvPhaseIs(const char *a, const char *b)
{
	if (a == nullptr || b == nullptr) return false;
	while (*a != '\0' && *a == *b) { ++a; ++b; }
	return *a == *b;
}

/**
 * The purpose of the bit that is being written *right now*, taken from the phase
 * the caller named with R3RResvPhaseGuard().
 *
 * The three phases below are the ones ReserveTrackUnderConsist(),
 * ClearReservationUnderConsist() and the platform waiter (train_cmd.cpp
 * "waiter-platform") run under, i.e. they own the tiles the train stands on.
 * Everything else -- "choose-track", "controller", "yapf-couple", "release-ghost",
 * "reap", "reap-stn", "gc-stn", and a nested guard that inherits a phase -- books a
 * path bit.
 *
 * A new phase must be registered here if it books *consist* bits. Misclassification
 * is safe in one direction only and that is the direction it fails in: a consist
 * bit mistaken for a path bit gets freed by a "free-path" release, which is the old
 * KI-362 behaviour, whereas a path bit mistaken for a consist bit would simply be
 * freed a little later by the train leaving its tile. Nothing can be released that
 * the *owner* does not hold, because R3RReleaseReservationLedgerChecked() and
 * R3RResvLedgerForget() both check ownership first.
 */
static uint8_t R3RResvLedgerPurpose()
{
	static const char *const consist_phases[] = { "consist", "clear-consist", "waiter-platform" };
	for (const char *phase : consist_phases) {
		if (R3RResvPhaseIs(r3r_resv_phase, phase)) return R3R_RESV_PURPOSE_CONSIST;
	}
	return R3R_RESV_PURPOSE_PATH;
}

/**
 * Which purposes a release with the given reason may take off the bits of an owner
 * (Release law). The reason string is the one passed to R3RResvLedgerForget() by
 * its callers in train_cmd.cpp / vehicle.cpp:
 *
 *  - "free-path": FreeTrainTrackReservation() is rebuilding the path, so it only
 *    owns the path bits. Freeing the consist bits here is KI-362: the tiles under
 *    the train were freed and immediately re-booked by ReserveTrackUnderConsist(),
 *    once per tick, for every tile of the consist.
 *  - the chain-edit commit points "couple" / "flip" / "depot-edit" (KI-363 part C):
 *    the train is *still there* -- the edit changes which vehicle is the chain head,
 *    so the rows booked under the old head are the dead ones. Same KI-362 argument
 *    applies to the tiles under the consist, hence PATH-only as well.
 *  - anything else ("decouple", "destroy"): the train is gone or is being split,
 *    so both kinds really do end. An unknown reason keeps the old, conservative
 *    behaviour (release everything the owner holds) rather than leaking bits.
 */
static uint8_t R3RResvLedgerForgetMask(const char *why)
{
	if (R3RResvPhaseIs(why, "free-path") || R3RResvPhaseIs(why, "couple") ||
			R3RResvPhaseIs(why, "flip") || R3RResvPhaseIs(why, "depot-edit")) {
		return (uint8_t)R3R_RESV_PURPOSE_PATH;
	}
	return (uint8_t)R3R_RESV_PURPOSE_ALL;
}

/** Drop the reverse entry of one bit as well, i.e. forget it completely. */
static void R3RResvLedgerDropBit(TileIndex tile, Track track)
{
	const uint32_t bit_key = R3RResvBitKey(tile, track);
	const auto bit_it = _r3r_resv_owner_of_bit.find(bit_key);
	if (bit_it == _r3r_resv_owner_of_bit.end()) return;
	R3RResvLedgerDropRow(bit_it->second, tile, track);
	_r3r_resv_owner_of_bit.erase(bit_it);
}

static void R3RResvLedgerResync();

/** Number of ledger writes between two reconciliations against the map (Mirror
 *  law). Bounded, so the sweep cannot become a per-tick cost, and rare enough
 *  (thousands of *changes*, not ticks) that it is invisible in the profile. */
static const uint32_t R3R_LEDGER_RESYNC_NOTES = 8192;
static uint32_t _r3r_resv_ledger_notes = 0;

void R3RResvLedgerNote(const char *kind, TileIndex tile, Track track)
{
	/* Mirror law: every so often, check the whole ledger against the map, in case a
	 * writer bypassed the setters (MakeStation() rebuilding a tile,
	 * SetRailStationPlatformReservation() clearing a platform) or a row was left
	 * behind by a release that did not settle. */
	if (++_r3r_resv_ledger_notes >= R3R_LEDGER_RESYNC_NOTES) {
		_r3r_resv_ledger_notes = 0;
		R3RResvLedgerResync();
	}

	const uint32_t bit_key = R3RResvBitKey(tile, track);

	/* "CLEAR-*" means the bit went away; every other kind we receive ("SET-*",
	 * "CHG-*") adds or re-writes it. Matching on the prefix rather than passing a
	 * flag keeps the six map setters untouched: they already spell it out in kind. */
	if (kind[0] == 'C' && kind[1] == 'L') {
		const auto bit_it = _r3r_resv_owner_of_bit.find(bit_key);
		if (bit_it == _r3r_resv_owner_of_bit.end()) return;
		R3RResvLedgerDropRow(bit_it->second, tile, track);
		_r3r_resv_owner_of_bit.erase(bit_it);
		return;
	}

	const uint32_t owner = R3RResvLedgerActorHead();
	if (owner == R3R_RESV_NO_OWNER) return;
	const uint8_t purpose = R3RResvLedgerPurpose();

	const auto bit_it = _r3r_resv_owner_of_bit.find(bit_key);
	if (bit_it != _r3r_resv_owner_of_bit.end()) {
		if (bit_it->second == owner) {
			/* Same owner, possibly a new purpose for the same bit: a train standing
			 * on a platform books that whole-tile bit as CONSIST and can book the
			 * entry track of the same tile as PATH. OR the purpose in instead of
			 * returning -- returning is what made the ledger forget that the bit was
			 * also a consist bit, and it would then be freed by a "free-path"
			 * release (KI-362). */
			const auto own_it = _r3r_resv_by_owner.find(owner);
			if (own_it != _r3r_resv_by_owner.end()) {
				for (R3RResvLedgerRec &row : own_it->second) {
					if (row.tile == tile && row.track == track) {
						row.purposes |= purpose;
						return;
					}
				}
			}
			/* A reverse entry without a forward row cannot happen; repair it rather
			 * than losing the bit. */
			_r3r_resv_by_owner[owner].push_back(R3RResvLedgerRec{tile, track, purpose});
			return;
		}
		R3RResvLedgerDropRow(bit_it->second, tile, track);
		bit_it->second = owner;
	} else {
		_r3r_resv_owner_of_bit.emplace(bit_key, owner);
	}
	_r3r_resv_by_owner[owner].push_back(R3RResvLedgerRec{tile, track, purpose});
}

uint32_t R3RResvLedgerOwner(TileIndex tile, Track track)
{
	const auto it = _r3r_resv_owner_of_bit.find(R3RResvBitKey(tile, track));
	return (it == _r3r_resv_owner_of_bit.end()) ? R3R_RESV_NO_OWNER : it->second;
}

/**
 * R3R (KI-361, round 243-cont): the train the ledger books \a tile/\a track under,
 * or nullptr when there is no row (a bit that came in with a savegame, or one
 * written by an engine path that carries no R3R actor).
 *
 * Callers use this to answer the ownership question *before* the walk: the walk
 * names the train standing at the far end of the reserved path, which is the
 * coupling target while a locomotive lays down a coupling path -- i.e. it blames
 * somebody else for the locomotive's own bits. The ledger recorded who wrote them.
 */
static Train *R3RTrainForLedgerBit(TileIndex tile, Track track)
{
	const uint32_t owner = R3RResvLedgerOwner(tile, track);
	if (owner == R3R_RESV_NO_OWNER) return nullptr;
	/* The owner is a chain head that was live when the bit was written; it may have
	 * been sold since (R3RResvLedgerForget then drops the rows, so this is only a
	 * belt-and-braces check). */
	return Train::GetIfValid((size_t)owner);
}

/**
 * Release one booked bit, asking the ledger who owns it first, and report whether
 * the bit is now *settled* (Release law; REG-243-K).
 *
 * @return true when nothing of ours is left on the map: the bit was freed, or it
 *     was already gone (the tile was rebuilt into something else, the platform
 *     reservation was cleared wholesale). false when the bit is still reserved but
 *     this call deliberately did not touch it -- because the ledger attributes it
 *     to a different owner now (REG-243-K), or because a train stands on the tile
 *     and the bit is still doing its job. The caller must then keep the row: that
 *     is the mirror law, and dropping the row there is what used to leave the bit
 *     ownerless for the reap path (KI-363/364).
 */
bool R3RReleaseReservationLedgerChecked(uint32_t key, TileIndex tile, Track track, const char *why)
{
	/* Never free a bit the ledger attributes to somebody else (REG-243-K). A
	 * *missing* row (a bit that came in with a savegame, or one written without an
	 * R3R actor) cannot answer the question, so it keeps the old behaviour -- the
	 * audit's reap path and the older per-consist bookkeeping both rely on that. */
	const uint32_t booker = R3RResvLedgerOwner(tile, track);
	if (booker != R3R_RESV_NO_OWNER && booker != key) {
		/* The ledger keys bookings by the *chain head*, while the older per-consist
		 * table (R3RReconcileConsistReservation, ClearReservationUnderConsist) is
		 * keyed by the vehicle that called in. A difference is therefore only
		 * benign when both name the same chain. */
		uint32_t own_head = R3R_RESV_NO_OWNER;
		if (const Train *k = Train::GetIfValid((size_t)key); k != nullptr && k->First() != nullptr) {
			own_head = k->First()->index.base();
		}
		if (booker != own_head) {
			R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=0x%X why=%s-notowner(veh=%u)\n",
					key, (int)TileX(tile), (int)TileY(tile), (uint)track, why, booker);
			return false;
		}
	}

	if (IsValidTile(tile) && GetTileType(tile) == TileType::Station && HasStationRail(tile)) {
		if (!HasStationReservation(tile)) {
			/* The whole-tile bit is gone: nothing to free, and the row is stale. */
			R3RResvLedgerDropBit(tile, track);
			R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=stn kind=%s why=%s-gone\n",
					key, (int)TileX(tile), (int)TileY(tile),
					IsRailWaypointTile(tile) ? "WAYPOINT" : "STATION", why);
			return true;
		}
		if (GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr ||
				GetTrainForWholeTileReservation(tile) != nullptr) {
			/* Still held by a train that stands there (or by a path whose far end is
			 * somebody): the bit is doing its job, keep it booked. */
			R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=stn kind=%s why=%s-hold\n",
					key, (int)TileX(tile), (int)TileY(tile),
					IsRailWaypointTile(tile) ? "WAYPOINT" : "STATION", why);
			return false;
		}
		const TrackBits rail_bits = GetStationReservationTrackBits(tile);
		if (rail_bits != TRACK_BIT_NONE) {
			R3RResvPhaseGuard phase_guard("gc-stn");
			UnreserveRailTrack(tile, FindFirstTrack(rail_bits));
		}
		R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=stn kind=%s why=%s\n",
				key, (int)TileX(tile), (int)TileY(tile),
				IsRailWaypointTile(tile) ? "WAYPOINT" : "STATION", why);
		return true;
	}

	const bool ours = IsValidTile(tile) && GetTileType(tile) == TileType::Railway &&
			IsPlainRailTile(tile) && HasTrack(tile, track) &&
			HasBit(GetRailReservationTrackBits(tile), track);
	if (ours) {
		UnreserveTrack(tile, track);
		R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=0x%X why=%s\n",
				key, (int)TileX(tile), (int)TileY(tile), (uint)track, why);
		return true;
	}
	/* Not plain rail anymore, track lifted, or the bit is already gone: the bit
	 * cannot exist here, so the row is stale either way. */
	R3RResvLedgerDropBit(tile, track);
	R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=0x%X why=%s-gone\n",
			key, (int)TileX(tile), (int)TileY(tile), (uint)track, why);
	return true;
}

void R3RResvLedgerForget(VehicleID owner, const char *why)
{
	if (owner == VehicleID::Invalid()) {
		/* R3R (KI-363, Mirror law): an ownerless "forget" is the reconcile-now
		 * request R3RReservationAudit() makes once per sweep -- there is nothing to
		 * forget, so align the whole ledger with the map instead. Riding this
		 * already-declared entry point keeps pbs.h (and therefore the incremental
		 * build) untouched. */
		R3RResvLedgerResync();
		return;
	}

	/* Normalise to the chain head, the ledger's unit of ownership: the caller may
	 * hold a mid-chain vehicle (a sold wagon, a decoupled half). */
	uint32_t key = owner.base();
	if (const Vehicle *v = Vehicle::GetIfValid(owner); v != nullptr) {
		if (const Vehicle *h = v->First(); h != nullptr) key = h->index.base();
	}

	const auto own_it = _r3r_resv_by_owner.find(key);
	if (own_it == _r3r_resv_by_owner.end()) return;
	std::vector<R3RResvLedgerRec> rows = std::move(own_it->second);
	_r3r_resv_by_owner.erase(own_it);

	const uint8_t mask = R3RResvLedgerForgetMask(why);
	unsigned released = 0;
	unsigned kept = 0;

	for (const R3RResvLedgerRec &row : rows) {
		/* Take this purpose off the bit; only a bit nothing else needs may go. */
		const uint8_t left = (uint8_t)(row.purposes & ~mask);
		if (left != 0) {
			_r3r_resv_by_owner[key].push_back(R3RResvLedgerRec{row.tile, row.track, left});
			++kept;
			continue;
		}

		/* Note the order: release *first*, drop the reverse entry afterwards.
		 * R3RReleaseReservationLedgerChecked() needs the row still in place to
		 * verify the bit is ours (REG-243-K); erasing first is what made that check
		 * a no-op. */
		if (!R3RReleaseReservationLedgerChecked(key, row.tile, row.track, why)) {
			/* The bit is still doing its job on the map (a train stands on the
			 * platform, the ledger handed it to somebody else): keep it booked, or
			 * it becomes an ownerless stray (KI-364). */
			_r3r_resv_by_owner[key].push_back(row);
			++kept;
			continue;
		}

		const uint32_t bit_key = R3RResvBitKey(row.tile, row.track);
		const auto bit_it = _r3r_resv_owner_of_bit.find(bit_key);
		/* Only drop the reverse entry while it still points at us: a bit that was
		 * re-booked by somebody else in the meantime must keep its new owner. */
		if (bit_it != _r3r_resv_owner_of_bit.end() && bit_it->second == key) {
			_r3r_resv_owner_of_bit.erase(bit_it);
		}
		++released;
	}

	R3RDbgWrite("RESV-LEDGER-FORGET veh=%u why=%s mask=0x%X released=%u kept=%u\n",
			key, why, (unsigned)mask, released, kept);
}

/** Reconcile the whole ledger against the map (Mirror law). Rows whose bit is not
 *  reserved on the map any more are dropped; a row that is still ours and still
 *  reserved is left alone. Only logs when it actually found something. */
static void R3RResvLedgerResync()
{
	unsigned rows = 0;
	unsigned dropped = 0;
	for (auto own_it = _r3r_resv_by_owner.begin(); own_it != _r3r_resv_by_owner.end(); ) {
		auto &list = own_it->second;
		for (auto row_it = list.begin(); row_it != list.end(); ) {
			++rows;
			const R3RResvLedgerRec &row = *row_it;
			bool alive = false;
			if (IsValidTile(row.tile)) {
				const TrackBits bits = GetReservedTrackbits(row.tile);
				alive = (row.track == INVALID_TRACK) ? (bits != TRACK_BIT_NONE)
						: HasBit(bits, row.track);
			}
			if (alive) { ++row_it; continue; }

			const uint32_t bit_key = R3RResvBitKey(row.tile, row.track);
			const auto bit_it = _r3r_resv_owner_of_bit.find(bit_key);
			if (bit_it != _r3r_resv_owner_of_bit.end() && bit_it->second == own_it->first) {
				_r3r_resv_owner_of_bit.erase(bit_it);
			}
			row_it = list.erase(row_it);
			++dropped;
		}
		if (list.empty()) {
			own_it = _r3r_resv_by_owner.erase(own_it);
		} else {
			++own_it;
		}
	}
	if (dropped != 0) {
		R3RDbgWrite("RESV-LEDGER-RESYNC rows=%u dropped=%u why=map-mismatch\n", rows, dropped);
	}
}

/**
 * Log one reservation state change on a watched tile, tagged with the phase and the
 * acting train, and always feed the ledger (KI-359).
 *
 * "actor" is the vehicle the caller was working on (a locomotive booking its path,
 * a consist sitting on its platform, the release sweep, ...). "head" is that
 * vehicle's chain head at that instant -- the ownership question is always asked
 * about the head, so seeing actor != head is itself a finding: a chain edit moved
 * the identity between booking the bit and freeing it.
 *
 * Called from the six map-accessor setters (see r3r_resv_probes.h), i.e. from the
 * hottest path in the game: the ledger update runs unconditionally (a hash lookup
 * or two per *change*, not per tile visited), while the watched-tile logging that
 * follows is compiled out with the probes off and additionally checks the watch
 * list only after the cheap debug switch. Note that the "CHG-*" kinds report just
 * the first track of the new trackbits, so a multi-track tile books one bit; that
 * direction of error is safe (see R3RResvLedgerNote).
 */
void R3RResvWatchLog(const char *kind, TileIndex tile, Track track)
{
	R3RResvLedgerNote(kind, tile, track);

#if R3R_PROBES
	if (!R3RDbgOn()) return;
	if (!R3RIsWatchedResvTile(tile)) return;
	int actor = -1;
	int head = -1;
	if (const Vehicle *v = Vehicle::GetIfValid(r3r_resv_actor); v != nullptr) {
		actor = (int)v->index.base();
		if (const Vehicle *h = v->First(); h != nullptr) head = (int)h->index.base();
	}
	R3RDbgWrite("RESV-WATCH %s tile=%d,%d track=%u phase=%s actor=%d head=%d\n",
			kind, (int)TileX(tile), (int)TileY(tile), (unsigned)track, r3r_resv_phase, actor, head);
#endif
}

/**
 * R3R (KI-203b, moved here from train_cmd.cpp by KI-359): release one booked
 * reservation without tripping over tiles that changed type since it was booked
 * (track lifted, tile rebuilt into a station, depot, tunnel bridge or crossing).
 *
 * UnreserveTrack() is the low level plain-rail helper: it goes through
 * HasTrack() -> GetTrackBits(), which dbg_asserts IsPlainRailTile()
 * (rail_map.h:168). Calling it on a booked tile that has been rebuilt since is
 * what crashed the game -- the bookkeeping has to be defensive here, because it
 * by design looks at tiles the consist *left* long ago.
 *
 * Anything we cannot prove to be a plain rail tile which still carries our bit
 * is left untouched and only dropped from the books.
 *
 * It moved into pbs.cpp with the ledger because the ledger is shared by every
 * reservation-releasing path now, and pbs.cpp is where the other map-level
 * reservation helpers live; the three call sites in train_cmd.cpp did not change.
 *
 * R3R (KI-362/REG-243-K): this is now a thin forwarder. The real work lives in
 * R3RReleaseReservationLedgerChecked(), which also asks the ledger whether the bit
 * really is ours, and which reports whether the bit is settled -- the ledger needs
 * that answer to keep a row it could not release (see the two laws above).
 */
void R3RReleaseReservationSafe(uint32_t key, TileIndex tile, Track track, const char *why)
{
	R3RReleaseReservationLedgerChecked(key, tile, track, why);
}

/*
 * R3R (第 245 轮, KI-371): the ledger goes into the savegame.
 *
 * Why: the ledger is the ownership answer every release path trusts now, so a
 * client that loads a game with an empty ledger answers "no owner" for every bit
 * on the map and falls back to the walk -- which is the wrong answer while a
 * locomotive couples to a waiting consist. Two clients then reap different bits
 * and desync (REG-243-K). Saving the ledger keeps both ends asking the same
 * question from the same table.
 *
 * Only the forward table (_r3r_resv_by_owner) is written; the reverse index
 * (_r3r_resv_owner_of_bit) is a pure derivation of it and is rebuilt on load.
 *
 * The array index carries the bit key (tile << 3 | slot), which is unique, so one
 * row is one bit and tile/track are decoded back from the index; see
 * R3RResvBitKey(). The index stays far below 2^31 even on the largest map.
 *
 * The block is a CH_SPARSE_TABLE with a named table header, so an old savegame
 * simply has no R3RL chunk and the ledger starts empty -- exactly today's
 * behaviour, no SLV bump. The chunk is registered from couple_group_sl.cpp, while
 * these two functions live here, so the ledger stays private to pbs.cpp and no
 * header has to grow an accessor.
 */

/** One saved bit: the owner and why it is held. The tile/track are the index. */
struct R3RResvLedgerSaveRec {
	uint32_t owner;
	uint8_t purposes;
};

static const NamedSaveLoad _r3r_resv_ledger_desc[] = {
	NSL("owner",    SLE_VAR(R3RResvLedgerSaveRec, owner, SLE_UINT32)),
	NSL("purposes", SLE_VAR(R3RResvLedgerSaveRec, purposes, SLE_UINT8)),
};

void Save_R3RL()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_resv_ledger_desc);

	for (const auto &own_it : _r3r_resv_by_owner) {
		for (const R3RResvLedgerRec &row : own_it.second) {
			R3RResvLedgerSaveRec rec{own_it.first, row.purposes};
			SlSetArrayIndex(R3RResvBitKey(row.tile, row.track));
			SlObjectSaveFiltered(&rec, slt);
		}
	}
}

void Load_R3RL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_resv_ledger_desc);

	/* Start from a clean ledger: the savegame is the whole truth, and the reverse
	 * index is rebuilt from the forward table below. */
	_r3r_resv_by_owner.clear();
	_r3r_resv_owner_of_bit.clear();

	int index;
	while ((index = SlIterateArray()) != -1) {
		R3RResvLedgerSaveRec rec{ R3R_RESV_NO_OWNER, 0 };
		SlObjectLoadFiltered(&rec, slt);

		/* An ownerless row cannot be released by anybody; drop it. A row with no
		 * purpose would be dropped by the first release, so normalise it to
		 * "everything is held" -- the same conservative reading
		 * R3RResvLedgerForgetMask() uses for an unknown reason. */
		if (rec.owner == R3R_RESV_NO_OWNER) continue;
		uint8_t purposes = rec.purposes;
		if (purposes == 0) purposes = (uint8_t)R3R_RESV_PURPOSE_ALL;

		const uint32_t bit_key = (uint32_t)index;
		const TileIndex tile((uint32_t)bit_key >> 3);
		const uint8_t slot = (uint8_t)(bit_key & 7u);
		const Track track = (slot == R3R_RESV_SLOT_WHOLE_TILE) ? INVALID_TRACK : (Track)slot;

		_r3r_resv_by_owner[rec.owner].push_back(R3RResvLedgerRec{tile, track, purposes});
		_r3r_resv_owner_of_bit[bit_key] = rec.owner;
	}
}

/**
 * Get the reserved trackbits for any tile, regardless of type.
 * @param t the tile
 * @return the reserved trackbits. TRACK_BIT_NONE on nothing reserved or
 *     a tile without rail.
 */
TrackBits GetReservedTrackbits(TileIndex t)
{
	switch (GetTileType(t)) {
		case TileType::Railway:
			if (IsRailDepot(t)) return GetDepotReservationTrackBits(t);
			if (IsPlainRail(t)) return GetRailReservationTrackBits(t);
			break;

		case TileType::Road:
			if (IsLevelCrossing(t)) return GetCrossingReservationTrackBits(t);
			break;

		case TileType::Station:
			if (HasStationRail(t)) return GetStationReservationTrackBits(t);
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL) return GetTunnelBridgeReservationTrackBits(t);
			break;

		default:
			break;
	}
	return TRACK_BIT_NONE;
}

/**
 * Set the reservation for a complete station platform.
 * @pre IsRailStationTile(start)
 * @param start starting tile of the platform
 * @param dir the direction in which to follow the platform
 * @param b the state the reservation should be set to
 */
void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert_tile(IsRailStationTile(start), start);
	assert_tile(GetRailStationAxis(start) == DiagDirToAxis(dir), start);

	do {
		SetRailStationReservation(tile, b); // R3R (KI-207): traced inside the setter
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
		tile = TileAdd(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
}

/**
 * Check whether a rail station platform is free (only occupied by \a v itself).
 * R3R: used when clearing platform reservations — a platform holding a waiting
 * consist (WAIT_COUPLE) must NOT be unreserved, otherwise the consist loses its
 * reservation and the coupling locomotive pathfinds straight through it.
 * @param v     The train to ignore.
 * @param start Starting tile of the platform.
 * @param dir   Direction in which to follow the platform.
 * @return true if no other train occupies the platform.
 */
bool IsRailStationPlatformFree(const Train *v, TileIndex start, DiagDirection dir)
{
	TileIndex     tile = start;
	TileIndexDiff diff = TileOffsByDiagDir(dir);

	assert_tile(IsRailStationTile(start), start);
	assert_tile(GetRailStationAxis(start) == DiagDirToAxis(dir), start);

	do {
		for (const Vehicle *u : Vehicle::Iterate()) {
			if (u->type != VehicleType::Train || u->vehstatus.Test(VehState::Crashed)) continue;
			const Train *t = Train::From(u);
			if (t->First()->index == v->First()->index) continue;
			if (t->tile == tile) return false;
		}
		tile = TileAdd(tile, diff);
	} while (IsCompatibleTrainStationTile(tile, start));
	return true;
}

/**
 * Try to reserve a specific track on a tile
 * This also sets PBS signals to green if reserving through the facing track direction
 * @param v the train performing the reservation
 * @param tile the tile
 * @param t the track
 * @param trigger_stations whether to call station randomisation trigger
 * @return \c true if reservation was successful, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 */
bool TryReserveRailTrackdir(const Train *v, TileIndex tile, Trackdir td, bool trigger_stations)
{
	const Track track = TrackdirToTrack(td);
	bool success = TryReserveRailTrack(tile, track, trigger_stations);

	/* R3R (KI-361, memo round243 §26): the couple pathfinder books a few tiles
	 * Track-level only (its rp() lambda has no signal-setting capability and the
	 * tiles are "not part of any node segment"), so a later Trackdir-level
	 * reservation of the very same bit returns false here even though the bit is
	 * still *ours*. Two consequences, both observed in the 1112,129 red-signal
	 * scene: (a) the caller drops the tile from its path (never released again ->
	 * orphan reservation), and (b) the PBS signal is never set green, so the train
	 * is held forever behind a tile it reserved itself. Treat "already booked by
	 * our own chain" as a successful reservation: the bit is ours either way, and
	 * the green is what the signal is for. Only plain rail is special-cased; every
	 * other tile type keeps its original semantics.
	 *
	 * R3R (KI-370, 2026-10-16): the tile-type test MUST be IsPlainRailTile() --
	 * IsPlainRail() has "@pre IsTileType(t, TileType::Railway)" and asserts that
	 * very precondition inside GetRailTileType() (rail_map.h:40). Since the
	 * trackdir-level call sites (yapf_rail.cpp rp() since 修法甲) hand us station
	 * waypoints, level crossings, tunnel/bridge tiles and depots as well, an
	 * unguarded IsPlainRail(tile) tripped the assert the moment such a tile was
	 * already reserved by our own chain. */
	if (!success && v != nullptr && IsPlainRailTile(tile)) {
		const Train *const booker = R3RTrainForLedgerBit(tile, track);
		if (booker != nullptr && booker->index == v->First()->index &&
				HasBit(GetRailReservationTrackBits(tile), track)) {
			success = true;
		}
	}

	if (success && HasPbsSignalOnTrackdir(tile, td)) {
		SetSignalStateByTrackdir(tile, td, SignalState::Green);
		MarkSingleSignalDirty(tile, td);
		if (_extra_aspects > 0) {
			SetSignalAspect(tile, TrackdirToTrack(td), 0);
			UpdateAspectDeferredWithVehicleRail(v, tile, td);
		}
	}
	return success;
}

/**
 * Try to reserve a specific track on a tile
 * @param tile the tile
 * @param track the track
 * @param trigger_stations whether to call station randomisation trigger
 * @return \c true if reservation was successful, i.e. the track was
 *     free and didn't cross any other reserved tracks.
 *
 * R3R (KI-207): every branch below only writes through the map-accessor setters,
 * which report the change themselves -- no probe here on purpose, so that the trace
 * cannot go blind on paths that bypass this function (ReadSavegame, tile conversion,
 * track removal, ...). See r3r_resv_probes.h.
 */
bool TryReserveRailTrack(TileIndex tile, Track track, bool trigger_stations)
{
	assert_msg_tile((TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(track)) != 0, tile,
			"{:X}, {:X}, {:X}", TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)), track, TrackToTrackBits(track));

	switch (GetTileType(tile)) {
		case TileType::Railway:
			if (IsPlainRail(tile)) {
				bool changed = TryReserveTrack(tile, track);
				if (changed && _settings_client.gui.show_track_reservation) MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
				return changed;
			}
			if (IsRailDepot(tile)) {
				if (!HasDepotReservation(tile)) {
					SetDepotReservation(tile, true);
					MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE); // some GRFs change their appearance when tile is reserved
					return true;
				}
			}
			break;

		case TileType::Road:
			if (IsLevelCrossing(tile) && !HasCrossingReservation(tile)) {
				if (_settings_game.vehicle.safer_crossings) {
					if (IsCrossingOccupiedByRoadVehicle(tile)) return false;
					if (_settings_game.vehicle.adjacent_crossings) {
						const Axis axis = GetCrossingRoadAxis(tile);
						for (TileIndex t = tile; t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, AxisToDiagDir(GetCrossingRoadAxis(t)))) {
							if (IsCrossingOccupiedByRoadVehicle(t)) return false;
						}
						for (TileIndex t = tile; t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, ReverseDiagDir(AxisToDiagDir(GetCrossingRoadAxis(t))))) {
							if (IsCrossingOccupiedByRoadVehicle(t)) return false;
						}
					}
				}
				SetCrossingReservation(tile, true);
				UpdateLevelCrossing(tile, false);
				if (_settings_client.gui.show_track_reservation) MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
				return true;
			}
			break;

		case TileType::Station:
			if (HasStationRail(tile) && !HasStationReservation(tile)) {
				SetRailStationReservation(tile, true);
				if (trigger_stations) {
					auto *st = BaseStation::GetByTile(tile);
					TriggerStationRandomisation(st, tile, StationRandomTrigger::PathReservation);
					TriggerStationAnimation(st, tile, StationAnimationTrigger::PathReservation);
				}
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE); // some GRFs need redraw after reserving track
				return true;
			}
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile) && !HasTunnelReservation(tile)) {
					SetTunnelReservation(tile, true);
					MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
					return true;
				}
				if (IsBridge(tile) && TryReserveRailBridgeHead(tile, track)) {
					MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
					return true;
				}
			}
			break;

		default:
			break;
	}
	return false;
}

/**
 * Lift the reservation of a specific trackdir on a tile
 * This also sets PBS signals to red if unreserving through the facing track direction
 * @param tile the tile
 * @param t the track
 */
void UnreserveRailTrackdir(TileIndex tile, Trackdir td)
{
	if (HasPbsSignalOnTrackdir(tile, td)) {
		SetSignalStateByTrackdir(tile, td, SignalState::Red);
		MarkSingleSignalDirty(tile, td);
	}
	/* R3R (第 243 轮, REG-243-O): deliberately no guard here. Each caller of this
	 * function names its own phase, so the UNRES-RAIL line tells the two apart. A guard
	 * placed here would overwrite the caller's name and hide that information. */
	UnreserveRailTrack(tile, TrackdirToTrack(td));
}

/**
 * Lift the reservation of a specific track on a tile
 * @param tile the tile
 * @param t the track
 *
 * R3R (KI-207): as in TryReserveRailTrack(), the log comes from the map-accessor
 * setters, so it also covers every path that clears a reservation without coming
 * through here.
 */
void UnreserveRailTrack(TileIndex tile, Track t)
{
	assert_msg_tile(TrackdirBitsToTrackBits(GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0)) & TrackToTrackBits(t), tile, "track: {:X}", t);

	/* R3R (第 243 轮, REG-243-M): tag every release at the *entry* of this function,
	 * where the phase the caller named is still current, so the map-setter's
	 * CLEAR-RAIL line (r3r_resv_probes.h) can be attributed to a code path. Printed
	 * just before the setter fires, so the two lines are adjacent in the log.
	 * Only logged when the tile really carries a reservation, which keeps the volume
	 * at "one line per real release". */
	if (GetReservedTrackbits(tile) != TRACK_BIT_NONE) {
		R3RDbgWrite("UNRES-RAIL tile=%d,%d track=%d phase=%s actor=%u\n",
				(int)TileX(tile), (int)TileY(tile), (int)t,
				r3r_resv_phase, (uint)r3r_resv_actor.base());
	}

	switch (GetTileType(tile)) {
		case TileType::Railway:
			if (IsRailDepot(tile)) {
				SetDepotReservation(tile, false);
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
				break;
			}
			if (IsPlainRail(tile)) {
				UnreserveTrack(tile, t);
				if (_settings_client.gui.show_track_reservation) MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		case TileType::Road:
			if (IsLevelCrossing(tile)) {
				SetCrossingReservation(tile, false);
				UpdateLevelCrossing(tile);
				if (_settings_client.gui.show_track_reservation) MarkTileGroundDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		case TileType::Station:
			if (HasStationRail(tile)) {
				SetRailStationReservation(tile, false);
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		case TileType::TunnelBridge:
			if (GetTunnelBridgeTransportType(tile) == TRANSPORT_RAIL) {
				if (IsTunnel(tile)) {
					SetTunnelReservation(tile, false);
				} else {
					UnreserveRailBridgeHeadTrack(tile, t);
				}
				if (IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile) && IsTrackAcrossTunnelBridge(tile, t)) {
					if (IsTunnelBridgePBS(tile)) {
						SetTunnelBridgeExitSignalState(tile, SignalState::Red);
						if (_extra_aspects > 0) PropagateAspectChange(tile, GetTunnelBridgeExitTrackdir(tile), 0);
					} else {
						UpdateSignalsOnSegment(tile, DiagDirection::Invalid, GetTileOwner(tile));
					}
				}
				MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
			}
			break;

		default:
			break;
	}
}

/** Flags for FollowReservation */
enum FollowReservationFlags {
	FRF_NONE                 = 0,        ///< No flags
	FRF_IGNORE_ONEWAY        = 0x01,     ///< Ignore one way signals in the opposite direction
	FRF_TB_EXIT_FREE         = 0x02,     ///< Exit of starting tunnel/bridge is free
};
DECLARE_ENUM_AS_BIT_SET(FollowReservationFlags)

static void CheckCurveLookAhead(const Train *v, TrainReservationLookAhead *lookahead, int end_position, int z, RailType rt)
{
	/* Coarse filter: remove curves beyond train length */
	while (!lookahead->curves.empty() && lookahead->curves.front().position < end_position - v->gcache.cached_total_length) {
		lookahead->curves.pop_front();
	}

	if (lookahead->curves.empty() || v->Next() == nullptr) return;

	static const int absolute_max_speed = UINT16_MAX;
	int max_speed = absolute_max_speed;

	int curvecount[2] = {0, 0};

	/* first find the curve speed limit */
	int numcurve = 0;
	int sum = 0;
	int pos = 0;
	int lastpos = -1;
	const Train *u = v->Next();
	int veh_offset = v->CalcNextVehicleOffset();
	for (auto iter = lookahead->curves.rbegin(); iter != lookahead->curves.rend(); ++iter) {
		const TrainReservationLookAheadCurve &curve = *iter;
		int delta = end_position - curve.position;
		while (delta >= veh_offset) {
			if (u->Next() != nullptr) {
				veh_offset += u->CalcNextVehicleOffset();
				u = u->Next();
				pos++;
			} else {
				u = nullptr;
				break;
			}
		}
		if (u == nullptr) break;

		if (curve.dir_diff == DirDiff::Left45) curvecount[0]++;
		if (curve.dir_diff == DirDiff::Right45) curvecount[1]++;
		if (curve.dir_diff == DirDiff::Left45 || curve.dir_diff == DirDiff::Right45) {
			if (lastpos != -1) {
				numcurve++;
				sum += pos - lastpos;
				if (pos - lastpos == 1 && max_speed > 88) {
					max_speed = 88;
				}
			}
			lastpos = pos;
		}

		/* if we have a 90 degree turn, fix the speed limit to 60 */
		if (curve.dir_diff == DirDiff::Left90 || curve.dir_diff == DirDiff::Right90) {
			max_speed = 61;
		}
	}

	if (numcurve > 0 && max_speed > 88) {
		if (curvecount[0] == 1 && curvecount[1] == 1) {
			max_speed = absolute_max_speed;
		} else {
			sum /= numcurve;
			max_speed = 232 - (13 - Clamp(sum, 1, 12)) * (13 - Clamp(sum, 1, 12));
		}
	}

	if (max_speed != absolute_max_speed) {
		/* Apply the engine's rail type curve speed advantage, if it slowed by curves */
		const RailTypeInfo *rti = GetRailTypeInfo(rt);
		max_speed += (max_speed / 2) * rti->curve_speed;

		if (v->tcache.cached_tflags & TCF_TILT) {
			/* Apply max_speed bonus of 20% for a tilting train */
			max_speed += max_speed / 5;
		}

		lookahead->AddCurveSpeedLimit(max_speed, 4, z);
	}
}

static int LookaheadTileHeightForChunnel(int length, int offset)
{
	if (offset == 0) return 0;
	if (offset < 3) return -1 * (int)TILE_HEIGHT;
	if (offset < length - 3) return -2 * (int)TILE_HEIGHT;
	if (offset < length) return -1 * (int)TILE_HEIGHT;
	return 0;
}

static uint16_t ApplyTunnelBridgeLookaheadSignalSpeedRestriction(TileIndex tile, Trackdir trackdir, const Train *consist,
		uint16_t initial_speed_restriction, TrainReservationLookAhead *lookahead, int offset, int16_t z)
{
	uint16_t speed_restriction = initial_speed_restriction;

	if (consist != nullptr && IsTunnelBridgeRestrictedSignal(tile)) {
		if (trackdir == INVALID_TRACKDIR) {
			trackdir = GetTunnelBridgeExitTrackdir(tile);
		}
		const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
		if (prog != nullptr && prog->actions_used_flags & TRPAUF_SPEED_RESTRICTION) {
			TraceRestrictProgramResult out;
			TraceRestrictProgramInput input(tile, trackdir, nullptr, nullptr);
			prog->Execute(consist, input, out);
			if (out.flags & TRPRF_SPEED_RESTRICTION_SET) {
				int duration;
				if (TrackdirEntersTunnelBridge(tile, trackdir)) {
					duration = 4 + (IsDiagonalTrackdir(trackdir) ? 16 : 8);
				} else {
					duration = 4;
				}
				lookahead->AddSpeedRestriction(out.speed_restriction, offset, duration, z);
				if (out.speed_restriction != 0 && (speed_restriction == 0 || out.speed_restriction < speed_restriction)) {
					/* lower of the speed restrictions before or after the signal */
					speed_restriction = out.speed_restriction;
				}
			}
		}
	}

	return speed_restriction;
}

static uint16_t GetTrainSpeedLimitForRailtype(const Train *consist, RailType rt, TileIndex tile, Track track)
{
	uint16_t speed = GetRailTypeInfo(rt)->max_speed;
	if (consist->tcache.cached_tflags & TCF_SPD_RAILTYPE) {
		for (const Train *u = consist; u != nullptr; u = u->Next()) {
			if (u->GetEngine()->callbacks_used & SGCU_CB36_SPEED_RAILTYPE) {
				const TileIndex prev_tile = u->tile;
				const TrackBits prev_track = u->track;
				const_cast<Train *>(u)->tile = tile;
				const_cast<Train *>(u)->track = TrackToTrackBits(track);
				uint16_t cb_speed = GetVehicleProperty(u, PROP_TRAIN_SPEED, speed);
				if (cb_speed != 0 && (cb_speed < speed || speed == 0)) speed = cb_speed;
				const_cast<Train *>(u)->tile = prev_tile;
				const_cast<Train *>(u)->track = prev_track;
			}
		}
	}
	return speed;
}

static void AddSignalToLookAhead(TrainReservationLookAhead *lookahead, uint16_t signal_speed, uint16_t signal_flags, TileIndex signal_tile, uint16_t signal_track, int offset, int16_t z_pos)
{
	lookahead->AddSignal(signal_speed, offset, z_pos, signal_flags);
	if (_settings_game.vehicle.train_speed_adaptation) {
		lookahead->AddSpeedAdaptation(signal_tile, signal_track, offset, z_pos);
	}
}

/**
 * Follow a reservation starting from a specific tile to the end.
 * @param o The owner of the track to follow; tracks of other owners are excluded.
 * @param rts The rail types to follow the reservation for; rail types not in the mask are excluded.
 * @param tile The start tile.
 * @param trackdir The start trackdir on the tile.
 * @param ignore_oneway Whether one way signals are to be ignored.
 * @return The end of the path.
 */
static PBSTileInfo FollowReservation(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, FollowReservationFlags flags, const Train *v, TrainReservationLookAhead *lookahead)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!(flags & FRF_TB_EXIT_FREE) && !HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return PBSTileInfo(tile, trackdir, false);

	RailType rt = INVALID_RAILTYPE;
	Direction dir = Direction::Invalid;
	int z = 0;
	auto update_z = [&](TileIndex t, Trackdir td, bool force) {
		if (force || TrackdirToTrack(td) == TRACK_X || TrackdirToTrack(td) == TRACK_Y) {
			if (IsBridgeTile(t) && TrackdirToExitdir(td) == GetTunnelBridgeDirection(t)) {
				z = GetBridgePixelHeight(t);
			} else {
				int x = (TileX(t) * TILE_SIZE) + 8;
				int y = (TileY(t) * TILE_SIZE) + 8;
				if (!IsTunnelTile(tile)) {
					switch (TrackdirToExitdir(td)) {
						case DiagDirection::NE: x -= 8; break;
						case DiagDirection::SE: y += 7; break;
						case DiagDirection::SW: x += 7; break;
						case DiagDirection::NW: y -= 8; break;
						default: NOT_REACHED();
					}
				}
				z = GetSlopePixelZ(x, y, true);
			}
		}
	};

	if (lookahead != nullptr) {
		rt = GetRailTypeByTrack(tile, TrackdirToTrack(trackdir));
		dir = TrackdirToDirection(trackdir);
		update_z(tile, trackdir, true);
	}

	auto check_rail_type = [&](TileIndex t, Trackdir td, int offset) {
		RailType new_rt = GetRailTypeByTrack(t, TrackdirToTrack(td));
		if (new_rt != rt) {
			uint16_t rail_speed = GetTrainSpeedLimitForRailtype(v, new_rt, t, TrackdirToTrack(td));
			if (rail_speed > 0) lookahead->AddTrackSpeedLimit(rail_speed, offset, 4, z);
			if (GetRailTypeInfo(rt)->curve_speed != GetRailTypeInfo(new_rt)->curve_speed) {
				CheckCurveLookAhead(v, lookahead, lookahead->RealEndPosition() + 4 + offset, z, new_rt);
			}
			rt = new_rt;
		}
	};

	auto check_direction = [&](Direction new_dir, int offset, TileIndex tile) {
		if (dir == new_dir) return;
		DirDiff dirdiff = DirDifference(dir, new_dir);
		int32_t end = lookahead->RealEndPosition() + 4;
		lookahead->curves.push_back({ end + offset, dirdiff });
		dir = new_dir;
		CheckCurveLookAhead(v, lookahead, end + offset, z, rt);
	};

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	auto check_tunnel_bridge = [&]() -> bool {
		if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntrance(tile)) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && GetTunnelBridgeExitSignalState(end) == SignalState::Green &&
						((flags & FRF_TB_EXIT_FREE) || TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded())) {
					/* skip far end */
					if (lookahead != nullptr) {
						lookahead->reservation_end_position += (DistanceManhattan(tile, end) - 1) * TILE_SIZE;
					}
					Trackdir end_trackdir = GetTunnelBridgeExitTrackdir(end);
					if (lookahead != nullptr) {
						if ((flags & FRF_TB_EXIT_FREE) && GetTunnelBridgeLength(tile, end) > 1) {
							/* middle part of bridge is in wormhole direction */
							dir = DiagDirToDir(GetTunnelBridgeDirection(tile));
						}
						check_direction(TrackdirToDirection(end_trackdir), 0, end);
						lookahead->reservation_end_position += (IsDiagonalTrackdir(end_trackdir) ? 16 : 8);
						update_z(end, end_trackdir, false);
					}
					tile = end;
					trackdir = end_trackdir;
					return true;
				}
			}
			if ((flags & FRF_IGNORE_ONEWAY) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationExit(tile) &&
					GetTunnelBridgeExitSignalState(tile) == SignalState::Green) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded()) {
					/* skip far end */
					tile = end;
					trackdir = GetTunnelBridgeExitTrackdir(tile);
					return true;
				}
			}
			return false;
		}
		return true;
	};
	while (check_tunnel_bridge() && ft.Follow(tile, trackdir)) {
		flags &= ~FRF_TB_EXIT_FREE;
		TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) {
			if (ft.is_station) {
				/* Check skipped station tiles as well, maybe our reservation ends inside the station. */
				TileIndexDiff diff = TileOffsByDiagDir(ft.exitdir);
				while (ft.tiles_skipped-- > 0) {
					ft.new_tile -= diff;
					if (HasStationReservation(ft.new_tile)) {
						if (lookahead != nullptr) {
							lookahead->AddStation(1 + ft.tiles_skipped, GetStationIndex(ft.new_tile), z);
							lookahead->reservation_end_position += (1 + ft.tiles_skipped) * TILE_SIZE;
						}
						tile = ft.new_tile;
						trackdir = DiagDirToDiagTrackdir(ft.exitdir);
						break;
					}
				}
			}
			break;
		}

		/* Can't have more than one reserved trackdir */
		Trackdir new_trackdir = FindFirstTrackdir(reserved);

		/* One-way signal against us. The reservation can't be ours as it is not
		 * a safe position from our direction and we can never pass the signal. */
		if (!(flags & FRF_IGNORE_ONEWAY) && HasOnewaySignalBlockingTrackdir(ft.new_tile, new_trackdir)) break;

		tile = ft.new_tile;
		trackdir = new_trackdir;

		if (lookahead != nullptr) {
			if (ft.tiles_skipped > 0) {
				DiagDirection skip_dir = ReverseDiagDir(TrackdirToExitdir(ReverseTrackdir(trackdir)));
				check_direction(DiagDirToDir(skip_dir), 0, tile);
			}
			if (ft.is_station) {
				if (ft.tiles_skipped > 0) {
					TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(trackdir));
					TileIndex start = tile - (diff * ft.tiles_skipped);
					for (int i = 0; i < ft.tiles_skipped; i++) {
						check_rail_type(start, trackdir, i * TILE_SIZE);
						start += diff;
					}
				}
				check_rail_type(tile, trackdir, ft.tiles_skipped * TILE_SIZE);
				lookahead->AddStation(1 + ft.tiles_skipped, GetStationIndex(ft.new_tile), z);
			} else {
				check_rail_type(tile, trackdir, 0);
			}
			check_direction(TrackdirToDirection(trackdir), ft.tiles_skipped * TILE_SIZE, tile);
			if (IsTileType(tile, TileType::TunnelBridge) && TrackdirEntersTunnelBridge(tile, trackdir)) {
				uint16_t bridge_speed = 0;
				if (IsBridge(tile)) {
					bridge_speed = GetBridgeSpec(GetBridgeType(tile))->speed;
					lookahead->AddTrackSpeedLimit(bridge_speed, 0, 8, z);
				}
				const int start_offset = (IsDiagonalTrackdir(trackdir) ? 16 : 8);
				const Tunnel *tunnel = IsTunnel(tile) ? Tunnel::GetByTile(tile) : nullptr;
				const TileIndex end = IsTunnel(tile) ? tunnel->GetOtherEnd(tile) : GetOtherBridgeEnd(tile);
				const int length = GetTunnelBridgeLength(tile, end);
				if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
					const int spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
					const int signals = length / spacing;

					uint16_t speed_restriction = ApplyTunnelBridgeLookaheadSignalSpeedRestriction(tile, trackdir, v, lookahead->speed_restriction, lookahead, 0, z);

					uint16_t signal_speed = GetRailTypeInfo(rt)->max_speed;
					if (signal_speed == 0 || (speed_restriction != 0 && speed_restriction < signal_speed)) signal_speed = speed_restriction;
					if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;

					const uint16_t entrance_signal_flags = ((tunnel != nullptr) ? tunnel->GetSignalStyle(tile) : GetBridgeSignalStyle(tile)) << 8;

					/* Entrance signal */
					AddSignalToLookAhead(lookahead, signal_speed, entrance_signal_flags, tile, TrackdirToTrack(trackdir), 0, z);

					update_z(tile, trackdir, false);

					if (length > 1) {
						check_direction(DiagDirToDir(GetTunnelBridgeDirection(tile)), start_offset, tile);
					}

					bool chunnel = (tunnel != nullptr) && tunnel->is_chunnel;

					/* Middle signals */
					int offset = start_offset - TILE_SIZE;
					for (int i = 0; i < signals; i++) {
						offset += TILE_SIZE * spacing;
						const int signal_z = chunnel ? LookaheadTileHeightForChunnel(length, i * spacing) : z;
						AddSignalToLookAhead(lookahead, signal_speed, entrance_signal_flags, tile, 0x100 + i, offset, signal_z);
					}

					/* Exit signal */
					const int end_offset = start_offset + (TILE_SIZE * length);
					const uint8_t exit_signal_style = ((tunnel != nullptr) ? tunnel->GetSignalStyle(end) : GetBridgeSignalStyle(end));
					uint16_t exit_signal_flags = exit_signal_style << 8;

					uint16_t exit_speed_restriction = ApplyTunnelBridgeLookaheadSignalSpeedRestriction(end, INVALID_TRACKDIR, v, lookahead->speed_restriction, lookahead, end_offset, z);
					if (exit_speed_restriction != speed_restriction) {
						speed_restriction = exit_speed_restriction;
						signal_speed = GetRailTypeInfo(rt)->max_speed;
						if (signal_speed == 0 || (speed_restriction != 0 && speed_restriction < signal_speed)) signal_speed = speed_restriction;
						if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;
					}

					if (HasBit(_signal_style_masks.combined_normal_shunt, exit_signal_style)) {
						SetBit(exit_signal_flags, TRSLAI_COMBINED);
						lookahead->flags.Set(TrainReservationLookAheadFlag::TunnelBridgeCombinedDefer);
					}
					AddSignalToLookAhead(lookahead, signal_speed, exit_signal_flags, end, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(end)), end_offset, z);

					lookahead->SetNextExtendPositionIfUnset();
				} else {
					update_z(tile, trackdir, false);
					if (length > 1) {
						check_direction(DiagDirToDir(GetTunnelBridgeDirection(tile)), start_offset, tile);
					}
				}
			}

			if (IsTileType(tile, TileType::Railway) && HasSignalOnTrack(tile, TrackdirToTrack(trackdir))) {
				TraceRestrictProgramActionsUsedFlags au_flags;
				if (HasSignalOnTrackdir(tile, trackdir)) {
					/* Passing through a signal from the front side */
					au_flags = TRPAUF_SPEED_RESTRICTION;
				} else {
					/* Passing through a signal from the rear side */
					au_flags = TRPAUF_SPEED_RESTRICTION | TRPAUF_REVERSE_BEHIND;
				}
				uint16_t speed_restriction = lookahead->speed_restriction;
				if (v != nullptr) {
					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
					if (prog != nullptr && prog->actions_used_flags & au_flags) {
						TraceRestrictProgramResult out;
						TraceRestrictProgramInput input(tile, trackdir, nullptr, nullptr);
						prog->Execute(v, input, out);
						if (out.flags & TRPRF_REVERSE_BEHIND && au_flags & TRPAUF_REVERSE_BEHIND) {
							lookahead->AddReverse(z);
						}
						if (out.flags & TRPRF_SPEED_RESTRICTION_SET) {
							lookahead->AddSpeedRestriction(out.speed_restriction, 0, 0, z);
							if (out.speed_restriction != 0 && (speed_restriction == 0 || out.speed_restriction < speed_restriction)) {
								/* lower of the speed restrictions before or after the signal */
								speed_restriction = out.speed_restriction;
							}
						}
					}
				}
				if (!(au_flags & TRPAUF_REVERSE_BEHIND)) {
					/* Passing through a signal from the front side */
					uint16_t signal_speed = GetRailTypeInfo(rt)->max_speed;
					if (signal_speed == 0 || (speed_restriction != 0 && speed_restriction < signal_speed)) signal_speed = speed_restriction;
					uint8_t signal_style = GetSignalStyle(tile, TrackdirToTrack(trackdir));
					uint16_t signal_flags = signal_style << 8;
					if (HasBit(_signal_style_masks.non_aspect_inc, signal_style)) {
						SetBit(signal_flags, TRSLAI_NO_ASPECT_INC);
					}
					if (HasBit(_signal_style_masks.next_only, signal_style)) {
						SetBit(signal_flags, TRSLAI_NEXT_ONLY);
					}
					if (HasBit(_signal_style_masks.combined_normal_shunt, signal_style)) {
						SetBit(signal_flags, TRSLAI_COMBINED);
						UpdateLookaheadCombinedNormalShuntSignalDeferred(tile, trackdir, lookahead->RealEndPosition());
					}
					AddSignalToLookAhead(lookahead, signal_speed, signal_flags, tile, TrackdirToTrack(trackdir), 0, z);
					lookahead->SetNextExtendPositionIfUnset();
				}
			}

			lookahead->reservation_end_position += (IsDiagonalTrackdir(trackdir) ? 16 : 8) + (ft.tiles_skipped * 16);
			update_z(tile, trackdir, false);
		}

		if (first_loop) {
			/* Update the start tile after we followed the track the first
			 * time. This is necessary because the track follower can skip
			 * tiles (in stations for example) which means that we might
			 * never visit our original starting tile again. */
			start_tile = tile;
			start_trackdir = trackdir;
			first_loop = false;
		} else {
			/* Loop encountered? */
			if (tile == start_tile && trackdir == start_trackdir) break;
		}
		/* Depot tile? Can't continue. */
		if (IsRailDepotTile(tile)) {
			if (lookahead != nullptr) lookahead->flags.Set(TrainReservationLookAheadFlag::DepotEnd);
			break;
		}
		/* Non-pbs signal? Reservation can't continue. */
		if (HasBlockSignalOnTrackdir(tile, trackdir)) break;
	}

	if (lookahead != nullptr) lookahead->reservation_end_z = z;

	return PBSTileInfo(tile, trackdir, false);
}

/** Follow a reservation starting from a specific tile to the end. */
template <typename T>
static void FollowReservationEnumerate(Owner o, RailTypes rts, TileIndex tile, Trackdir trackdir, FollowReservationFlags flags, T handler)
{
	TileIndex start_tile = tile;
	Trackdir  start_trackdir = trackdir;
	bool      first_loop = true;

	/* Start track not reserved? This can happen if two trains
	 * are on the same tile. The reservation on the next tile
	 * is not ours in this case, so exit. */
	if (!(flags & FRF_TB_EXIT_FREE) && !HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(trackdir)))) return;

	if (handler(start_tile, start_trackdir)) return;

	/* Do not disallow 90 deg turns as the setting might have changed between reserving and now. */
	CFollowTrackRail ft(o, rts);
	auto check_tunnel_bridge = [&]() -> bool {
		if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntrance(tile)) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && GetTunnelBridgeExitSignalState(end) == SignalState::Green &&
						((flags & FRF_TB_EXIT_FREE) || TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded())) {
					/* skip far end */
					Trackdir end_trackdir = GetTunnelBridgeExitTrackdir(end);
					tile = end;
					trackdir = end_trackdir;
					if (handler(tile, trackdir)) return false;
					return true;
				}
			}
			if ((flags & FRF_IGNORE_ONEWAY) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationExit(tile) &&
					GetTunnelBridgeExitSignalState(tile) == SignalState::Green) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				if (HasAcrossTunnelBridgeReservation(end) && TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded()) {
					/* skip far end */
					tile = end;
					trackdir = GetTunnelBridgeExitTrackdir(tile);
					if (handler(tile, trackdir)) return false;
					return true;
				}
			}
			return false;
		}
		return true;
	};
	while (check_tunnel_bridge() && ft.Follow(tile, trackdir)) {
		flags &= ~FRF_TB_EXIT_FREE;
		TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));

		if (ft.is_station) {
			/* Check skipped station tiles as well, maybe our reservation ends inside the station. */
			TileIndexDiff diff = TileOffsByDiagDir(ft.exitdir);
			TileIndex t = ft.new_tile - (ft.tiles_skipped * diff);
			while (ft.tiles_skipped-- > 0) {
				if (HasStationReservation(t)) {
					if (handler(t, DiagDirToDiagTrackdir(ft.exitdir))) return;
				} else {
					break;
				}
				t += diff;
			}
		}

		/* No reservation --> path end found */
		if (reserved == TRACKDIR_BIT_NONE) {
			break;
		}

		/* Can't have more than one reserved trackdir */
		Trackdir new_trackdir = FindFirstTrackdir(reserved);

		/* One-way signal against us. The reservation can't be ours as it is not
		 * a safe position from our direction and we can never pass the signal. */
		if (!(flags & FRF_IGNORE_ONEWAY) && HasOnewaySignalBlockingTrackdir(ft.new_tile, new_trackdir)) break;

		tile = ft.new_tile;
		trackdir = new_trackdir;

		if (handler(tile, trackdir)) return;

		if (first_loop) {
			/* Update the start tile after we followed the track the first
			 * time. This is necessary because the track follower can skip
			 * tiles (in stations for example) which means that we might
			 * never visit our original starting tile again. */
			start_tile = tile;
			start_trackdir = trackdir;
			first_loop = false;
		} else {
			/* Loop encountered? */
			if (tile == start_tile && trackdir == start_trackdir) break;
		}
		/* Depot tile? Can't continue. */
		if (IsRailDepotTile(tile)) {
			break;
		}
		/* Non-pbs signal? Reservation can't continue. */
		if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) break;
	}
}

/**
 * Helper struct for finding the best matching vehicle on a specific track.
 */
struct FindTrainOnTrackInfo {
	PBSTileInfo res; ///< Information about the track.
	Train *best;     ///< The currently "best" vehicle we have found.

	/** Init the best location to nullptr always! */
	FindTrainOnTrackInfo() : best(nullptr) {}
};

/**
 * Find the best matching vehicle on a tile.
 * @param[in,out] info State of the finding a vehicle on a reservation.
 * @param tile The tile to search for.
 */
static void CheckTrainsOnTrack(FindTrainOnTrackInfo &info, TileIndex tile)
{
	for (Train *t : VehiclesOnTile<VehicleType::Train>(tile)) {
		if (t->vehstatus.Test(VehState::Crashed)) continue;

		if (t->track & TRACK_BIT_WORMHOLE) {
			/* Do not find trains inside bridges, when the search track is a bridge-bypassing custom bridge head track. */
			if (IsCustomBridgeHeadTile(info.res.tile) && !IsTrackAcrossTunnelBridge(info.res.tile, TrackdirToTrack(info.res.trackdir))) {
				continue;
			}

			/* Do not find trains inside signalled bridge/tunnels.
			 * Trains on the ramp/entrance itself are found though.
			 */
			if (IsTileType(info.res.tile, TileType::TunnelBridge) && IsTunnelBridgeWithSignalSimulation(info.res.tile) && info.res.tile != TileVirtXY(t->x_pos, t->y_pos)) {
				continue;
			}
		}
		if (t->track & TRACK_BIT_WORMHOLE || HasBit((TrackBits)t->track, TrackdirToTrack(info.res.trackdir))) {
			t = t->First();

			/* ALWAYS return the lowest ID (anti-desync!) */
			if (info.best == nullptr || t->index < info.best->index) info.best = t;
		}
	}
}

void TrainReservationLookAhead::SetNextExtendPosition()
{
	int32_t threshold = this->current_position + 24;
	for (const TrainReservationLookAheadItem &item : this->items) {
		if (item.type == TRLIT_SIGNAL && item.start > threshold) {
			this->next_extend_position = item.start - 24;
			return;
		}
	}
	this->next_extend_position = this->current_position;
}

bool ValidateLookAhead(const Train *v)
{
	TileIndex tile = v->lookahead->reservation_end_tile;
	Trackdir trackdir = v->lookahead->reservation_end_trackdir;

	if (v->lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) {
		if (!likely(IsRailTunnelBridgeTile(tile) && TrackdirEntersTunnelBridge(tile, trackdir))) {
			return false;
		}
	}
	if (v->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd) && !IsRailDepotTile(tile)) return false;

	TrackdirBits trackdirbits = GetTileTrackdirBits(tile, TRANSPORT_RAIL, 0);
	if (!HasTrackdir(trackdirbits, trackdir)) return false;

	return true;
}

/**
 * Follow a train reservation to the last tile.
 *
 * @param v the vehicle
 * @param train_on_res Is set to a train we might encounter
 * @returns The last tile of the reservation or the current train tile if no reservation present.
 */
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res, FollowTrainReservationFlags flags)
{
	assert(v->type == VehicleType::Train);

	TileIndex tile;
	Trackdir  trackdir;

	if (!flags.Test(FollowTrainReservationFlag::IgnoreLookahead) && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->lookahead != nullptr) {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
		if (v->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) return PBSTileInfo(tile, trackdir, false);
		if (v->lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) {
			TileIndex exit_tile = GetOtherTunnelBridgeEnd(tile);
			if (IsTunnelBridgeSignalSimulationExit(exit_tile) && GetTunnelBridgeExitSignalState(exit_tile) == SignalState::Green && HasAcrossTunnelBridgeReservation(exit_tile)) {
				tile = exit_tile;
				trackdir = GetTunnelBridgeExitTrackdir(exit_tile);
			}
		}
	} else {
		const Train *moving_front = v->GetMovingFront();

		tile = moving_front->tile;
		trackdir = moving_front->GetVehicleTrackdir();
	}

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) return PBSTileInfo(tile, trackdir, false);

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(v->owner, v->GetIndirectCompatibleRailTypes(), tile, trackdir, FRF_NONE, v, nullptr);
	ftoti.res.okay = flags.Test(FollowTrainReservationFlag::OkayUnused) ? false : IsSafeWaitingPosition(v, ftoti.res.tile, ftoti.res.trackdir, true, _settings_game.pf.forbid_90_deg);
	if (train_on_res != nullptr) {
		CheckTrainsOnTrack(ftoti, ftoti.res.tile);
		if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		if (*train_on_res == nullptr && IsRailStationTile(ftoti.res.tile)) {
			/* The target tile is a rail station. The track follower
			 * has stopped on the last platform tile where we haven't
			 * found a train. Also check all previous platform tiles
			 * for a possible train. */
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
			for (TileIndex st_tile = ftoti.res.tile + diff; *train_on_res == nullptr && IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
				CheckTrainsOnTrack(ftoti, st_tile);
				if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
			}
		}
		if (*train_on_res == nullptr && IsTileType(ftoti.res.tile, TileType::TunnelBridge) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir)) && !IsTunnelBridgeWithSignalSimulation(ftoti.res.tile)) {
			/* The target tile is a bridge/tunnel, also check the other end tile. */
			CheckTrainsOnTrack(ftoti, GetOtherTunnelBridgeEnd(ftoti.res.tile));
			if (ftoti.best != nullptr) *train_on_res = ftoti.best->First();
		}
	}
	return ftoti.res;
}

void ApplyAvailableFreeTunnelBridgeTiles(TrainReservationLookAhead *lookahead, int free_tiles, TileIndex tile, TileIndex end)
{
	lookahead->flags.Set(TrainReservationLookAheadFlag::TunnelBridgeExitFree, free_tiles == INT_MAX);
	if (free_tiles == INT_MAX) {
		/* whole tunnel/bridge is empty */
		if (unlikely(end == INVALID_TILE)) end = GetOtherTunnelBridgeEnd(tile);
		free_tiles = DistanceManhattan(tile, end) - 1;
	} else {
		if (free_tiles > 0) {
			int spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
			free_tiles = (((free_tiles - 1) / spacing) * spacing) - 1;
		} else {
			free_tiles = -1;
		}
	}
	lookahead->reservation_end_position += ((free_tiles - lookahead->tunnel_bridge_reserved_tiles) * TILE_SIZE);
	lookahead->tunnel_bridge_reserved_tiles = free_tiles;
	if (lookahead->flags.Test(TrainReservationLookAheadFlag::Chunnel)) {
		if (unlikely(end == INVALID_TILE)) end = GetOtherTunnelBridgeEnd(tile);
		lookahead->reservation_end_z = LookaheadTileHeightForChunnel(GetTunnelBridgeLength(tile, end), free_tiles + 1);
	}
}

static void FillLookAheadCurveDataFromTrainPosition(Train *moving_front, TrainReservationLookAhead &lookahead)
{
	TileIndex tile = TileVirtXY(moving_front->x_pos, moving_front->y_pos);
	Direction dir = moving_front->direction;
	int32_t current_pos = lookahead.reservation_end_position + 4 - (IsDiagonalDirection(dir) ? 16 : 8);
	for (Train *u = moving_front->GetMovingNext(); u != nullptr; u = u->GetMovingNext()) {
		TileIndex cur_tile = TileVirtXY(u->x_pos, u->y_pos);
		if (cur_tile == tile) continue;
		tile = cur_tile;
		Direction u_dir = u->GetMovingDirection();
		if (u_dir != dir) {
			DirDiff dirdiff = DirDifference(u_dir, dir);
			lookahead.curves.push_front({ current_pos, dirdiff });
			dir = u_dir;
		}
		current_pos -= (IsDiagonalDirection(dir) ? 16 : 8);
	}
}

static int ScanTrainPositionForLookAheadStation(Train *moving_front, TileIndex start_tile)
{
	StationID prev = StationID::Invalid();
	int offset = 0;
	int start_offset_tiles = 0;
	TileIndex cur_tile = start_tile;
	for (const Train *u = moving_front; u != nullptr; u = u->GetMovingNext()) {
		if (u != moving_front) {
			TileIndex u_tile = TileVirtXY(u->x_pos, u->y_pos);
			if (u_tile != cur_tile) {
				offset += (IsDiagonalTrackdir(u->GetVehicleTrackdir()) ? 16 : 8);
				cur_tile = u_tile;
			}
		}
		if (HasStationTileRail(u->tile)) {
			StationID current = GetStationIndex(u->tile);
			if (current != prev) {
				/* Train is in a station, add that to the lookahead */
				TileIndex tile = u->tile;
				Trackdir trackdir = u->GetVehicleTrackdir();

				RailType rt = GetRailTypeByTrack(tile, TrackdirToTrack(trackdir));
				int z = GetTileMaxPixelZ(tile);

				DiagDirection forward_dir = TrackdirToExitdir(trackdir);
				TileIndexDiff diff = TileOffsByDiagDir(forward_dir);
				uint forward_length = BaseStation::GetByTile(tile)->GetPlatformLength(tile, forward_dir);
				uint reverse_length = BaseStation::GetByTile(tile)->GetPlatformLength(tile, ReverseDiagDir(forward_dir));

				TrainReservationLookAhead &lookahead = *(moving_front->First()->lookahead);

				if (u == moving_front) {
					for (uint i = 1; i < forward_length; i++) {
						/* Check for mid platform rail type change */
						TileIndex new_tile = tile + (i * diff);
						RailType new_rt = GetRailTypeByTrack(new_tile, TrackdirToTrack(trackdir));
						if (new_rt != rt) {
							uint16_t rail_speed = GetTrainSpeedLimitForRailtype(moving_front->First(), new_rt, new_tile, TrackdirToTrack(trackdir));
							if (rail_speed > 0) lookahead.AddTrackSpeedLimit(rail_speed, (i - 1) * TILE_SIZE, 4, z);
							rt = new_rt;
						}
					}
					start_offset_tiles = forward_length - 1;
				}

				lookahead.AddStation(forward_length - 1, current, z);
				lookahead.items.back().start -= offset + (reverse_length * TILE_SIZE);
				lookahead.items.back().end -= offset;

				prev = current;
			}
		} else {
			prev = StationID::Invalid();
		}
		if (!u->flags.Test(VehicleRailFlag::BeyondPlatformEnd)) break;
	}
	return start_offset_tiles;
}

static void TryCreateLookAheadForTrainInTunnelBridge(Train *consist, Train *moving_front)
{
	if (IsTunnelBridgeSignalSimulationExitOnly(moving_front->tile)) return;

	const TileIndex tile = moving_front->tile;
	const DiagDirection tb_dir = GetTunnelBridgeDirection(tile);
	if (DirToDiagDirAlongAxis(moving_front->GetMovingDirection(), DiagDirToAxis(tb_dir)) == tb_dir) {
		/* Going in the right direction, allocate a new lookahead. */
		consist->lookahead.reset(new TrainReservationLookAhead());
		TrainReservationLookAhead &lookahead = *(consist->lookahead);
		lookahead.reservation_end_tile = tile;
		lookahead.reservation_end_trackdir = GetTunnelBridgeEntranceTrackdir(tile);
		lookahead.reservation_end_z = moving_front->z_pos;
		lookahead.current_position = 0;
		lookahead.next_extend_position = 0;
		lookahead.tunnel_bridge_reserved_tiles = DistanceManhattan(tile, TileVirtXY(moving_front->x_pos, moving_front->y_pos));
		lookahead.reservation_end_position = GetTileMarginInFrontOfTrain(moving_front);
		lookahead.flags = {};
		lookahead.speed_restriction = consist->speed_restriction;
		lookahead.cached_zpos = consist->CalculateOverallZPos();
		lookahead.zpos_refresh_remaining = consist->GetZPosCacheUpdateInterval();
		if (IsTunnel(tile) && Tunnel::GetByTile(tile)->is_chunnel) lookahead.flags.Set(TrainReservationLookAheadFlag::Chunnel);

		if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
			const uint16_t bridge_speed = IsBridge(tile) ? GetBridgeSpec(GetBridgeType(tile))->speed : 0;
			const TileIndex end = GetOtherTunnelBridgeEnd(tile);
			const int length = GetTunnelBridgeLength(tile, end);
			const int spacing = GetTunnelBridgeSignalSimulationSpacing(tile);
			const int signals = length / spacing;

			const RailType rt = GetRailTypeByTrack(tile, TrackdirToTrack(lookahead.reservation_end_trackdir));
			uint16_t signal_speed = GetRailTypeInfo(rt)->max_speed;
			if (signal_speed == 0 || (consist->speed_restriction != 0 && consist->speed_restriction < signal_speed)) signal_speed = consist->speed_restriction;
			if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;

			int z = IsBridge(tile) ? GetBridgeHeight(tile) : GetTilePixelZ(tile);

			const uint16_t signal_flags = GetTunnelBridgeSignalStyle(tile) << 8;

			/* Middle signals */
			int offset = -(int)TILE_SIZE;
			for (int i = 0; i < signals; i++) {
				offset += TILE_SIZE * spacing;
				const int signal_z = lookahead.flags.Test(TrainReservationLookAheadFlag::Chunnel) ? LookaheadTileHeightForChunnel(length, i * spacing) : z;
				AddSignalToLookAhead(&lookahead, signal_speed, signal_flags, tile, 0x100 + i, offset, signal_z);
			}

			/* Exit signal */
			const int end_offset = TILE_SIZE * length;

			uint16_t exit_speed_restriction = ApplyTunnelBridgeLookaheadSignalSpeedRestriction(end, INVALID_TRACKDIR, consist, consist->speed_restriction, &lookahead, end_offset, z);
			if (exit_speed_restriction != consist->speed_restriction) {
				signal_speed = GetRailTypeInfo(rt)->max_speed;
				if (signal_speed == 0 || (exit_speed_restriction != 0 && exit_speed_restriction < signal_speed)) signal_speed = exit_speed_restriction;
				if (signal_speed == 0 || (bridge_speed != 0 && bridge_speed < signal_speed)) signal_speed = bridge_speed;
			}

			AddSignalToLookAhead(&lookahead, signal_speed, signal_flags, end, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(end)), end_offset, z);

			lookahead.SetNextExtendPositionIfUnset();
		}

		FillLookAheadCurveDataFromTrainPosition(moving_front, lookahead);
		TileIndex end = GetOtherTunnelBridgeEnd(tile);
		int raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(tile, end, lookahead.tunnel_bridge_reserved_tiles + 1);
		ApplyAvailableFreeTunnelBridgeTiles(&lookahead, raw_free_tiles, tile, end);
		ScanTrainPositionForLookAheadStation(moving_front, TileVirtXY(moving_front->x_pos, moving_front->y_pos));
	}
}

int AdvanceTrainReservationLookaheadEnd(const Train *v, int lookahead_end_position)
{
	if (_settings_game.vehicle.realistic_braking_aspect_limited != TRBALM_ON || _extra_aspects == 0) {
		return v->lookahead->reservation_end_position + 1;
	}

	if (lookahead_end_position > v->lookahead->reservation_end_position) return lookahead_end_position;

	int32_t threshold = v->lookahead->current_position + 24;
	uint8_t known_signals_ahead = 1;
	bool allow_skip_no_aspect_inc = false;
	if (v->IsInDepot()) {
		if (_default_signal_style_lookahead_extra_aspects == 0xFF) {
			/* Default signal style (depot) has unlimited lookahead */
			return v->lookahead->reservation_end_position + 1;
		}
		known_signals_ahead = _default_signal_style_lookahead_extra_aspects + 1;
		allow_skip_no_aspect_inc = true;
	}
	for (const TrainReservationLookAheadItem &item : v->lookahead->items) {
		if (item.end >= v->lookahead->reservation_end_position) break;
		if (item.type == TRLIT_SIGNAL) {
			if (HasBit(item.data_aux, TRSLAI_COMBINED_SHUNT)) {
				/* Combined normal/shunt in shunt mode */
				allow_skip_no_aspect_inc = false;
				if (item.start <= threshold) {
					known_signals_ahead = 1;
					continue;
				} else {
					if (item.start > lookahead_end_position) lookahead_end_position = item.start;
					return lookahead_end_position;
				}
			}

			if (item.start <= threshold) {
				/* Signal is within visual range */
				uint8_t style = item.data_aux >> 8;
				uint8_t max_aspect = (style == 0) ? _default_signal_style_lookahead_extra_aspects : _new_signal_styles[style - 1].lookahead_extra_aspects;
				if (max_aspect == 0xFF) {
					/* This signal has unlimited lookahead */
					return v->lookahead->reservation_end_position + 1;
				}
				if (!HasBit(item.data_aux, TRSLAI_NEXT_ONLY)) allow_skip_no_aspect_inc = true;
				max_aspect += ((HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC) && allow_skip_no_aspect_inc) ? 1 : 2);
				if (max_aspect > known_signals_ahead) known_signals_ahead = max_aspect;
			}
			if (!HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC) || !allow_skip_no_aspect_inc) {
				known_signals_ahead--;
				if (known_signals_ahead == 0) {
					if (item.start > lookahead_end_position) lookahead_end_position = item.start;
					return lookahead_end_position;
				}
			}
		}
	}

	/* Didn't need to stop at a signal along the reservation */
	if (v->lookahead->reservation_end_position >= lookahead_end_position) {
		lookahead_end_position = v->lookahead->reservation_end_position;
		if (known_signals_ahead > 1) lookahead_end_position++;
	}
	return lookahead_end_position;
}

void SetTrainReservationLookaheadEnd(Train *v)
{
	v->lookahead->lookahead_end_position = AdvanceTrainReservationLookaheadEnd(v, v->lookahead->lookahead_end_position);
}

void FillTrainReservationLookAhead(Train *v)
{
	Train *moving_front = nullptr;
	if (v->lookahead == nullptr) {
		moving_front = v->GetMovingFront();
		if (moving_front->track & TRACK_BIT_WORMHOLE) {
			TryCreateLookAheadForTrainInTunnelBridge(v, moving_front);
			if (v->lookahead == nullptr) return;
		}
	}

	int32_t old_reservation_end_position = 0;

	TileIndex tile;
	Trackdir  trackdir;
	TrainReservationLookAhead *lookahead = v->lookahead.get();
	if (lookahead == nullptr) {
		v->lookahead.reset(new TrainReservationLookAhead());
		lookahead = v->lookahead.get();
		lookahead->current_position = 0;
		lookahead->next_extend_position = 0;

		/* Special case, if called from TrainController,
		 * v->tile, v->track and v->direction can be updated to the new tile,
		 * but v->x_pos and v->y_pos can still use the coordinates on the old tile,
		 * GetTileMarginInFrontOfTrain could erroneously return -5 if the old and
		 * new directions don't match. */
		lookahead->reservation_end_position = std::max(GetTileMarginInFrontOfTrain(moving_front), -4);

		lookahead->tunnel_bridge_reserved_tiles = 0;
		lookahead->flags = {};
		lookahead->speed_restriction = v->speed_restriction;
		lookahead->cached_zpos = v->CalculateOverallZPos();
		lookahead->zpos_refresh_remaining = v->GetZPosCacheUpdateInterval();
		FillLookAheadCurveDataFromTrainPosition(moving_front, *lookahead);
		tile = moving_front->tile;
		trackdir = moving_front->GetVehicleTrackdir();
		TileIndex virt_tile = TileVirtXY(moving_front->x_pos, moving_front->y_pos);
		if (tile != virt_tile) {
			lookahead->reservation_end_position += (IsDiagonalDirection(moving_front->direction) ? 16 : 8);
		}
		int station_offset_tiles = ScanTrainPositionForLookAheadStation(moving_front, tile);
		if (station_offset_tiles > 0) {
			TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(trackdir));
			tile += station_offset_tiles * diff;
			lookahead->reservation_end_position += station_offset_tiles * TILE_SIZE;
		}
	} else {
		old_reservation_end_position = lookahead->reservation_end_position;
		tile = lookahead->reservation_end_tile;
		trackdir = lookahead->reservation_end_trackdir;
		if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
			TileIndex end = GetOtherTunnelBridgeEnd(tile);
			int raw_free_tiles;
			if (lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) {
				raw_free_tiles = INT_MAX;
			} else {
				raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(tile, end, lookahead->tunnel_bridge_reserved_tiles + 1);
				ApplyAvailableFreeTunnelBridgeTiles(lookahead, raw_free_tiles, tile, end);
			}
			if (!(HasAcrossTunnelBridgeReservation(end) && GetTunnelBridgeExitSignalState(end) == SignalState::Green && raw_free_tiles == INT_MAX)) {
				/* do not attempt to follow through a signalled tunnel/bridge if it is not empty or the far end is not reserved */
				FlushDeferredDetermineCombineNormalShuntMode(v);
				SetTrainReservationLookaheadEnd(v);
				return;
			}
			if (lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeCombinedDefer) && IsTunnelBridgeSignalSimulationExitTile(end)) {
				for (auto iter = lookahead->items.rbegin(); iter != lookahead->items.rend(); ++iter) {
					const TrainReservationLookAheadItem &item = *iter;
					if (item.type == TRLIT_SIGNAL && HasBit(item.data_aux, TRSLAI_COMBINED)) {
						UpdateLookaheadCombinedNormalShuntSignalDeferred(end, GetTunnelBridgeExitTrackdir(end), lookahead->reservation_end_position);
						break;
					}
				}
				lookahead->flags.Reset(TrainReservationLookAheadFlag::TunnelBridgeCombinedDefer);
			}
		}
	}

	if (IsRailDepotTile(tile) && !GetDepotReservationTrackBits(tile)) {
		FlushDeferredDetermineCombineNormalShuntMode(v);
		SetTrainReservationLookaheadEnd(v);
		return;
	}

	FollowReservationFlags flags = FRF_NONE;
	if (lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) flags |= FRF_TB_EXIT_FREE;
	PBSTileInfo res = FollowReservation(v->owner, v->GetIndirectCompatibleRailTypes(), tile, trackdir, flags, v, lookahead);

	if (IsTunnelBridgeWithSignalSimulation(res.tile) && TrackdirEntersTunnelBridge(res.tile, res.trackdir)) {
		lookahead->flags.Set(TrainReservationLookAheadFlag::Chunnel, IsTunnel(res.tile) && Tunnel::GetByTile(res.tile)->is_chunnel);
		if (lookahead->current_position < lookahead->reservation_end_position - ((int)TILE_SIZE * (1 + lookahead->tunnel_bridge_reserved_tiles))) {
			/* Vehicle is not itself in this tunnel/bridge, scan how much is available */
			TileIndex end = INVALID_TILE;
			int free_tiles;
			if (GetTunnelBridgeEntranceSignalState(res.tile) == SignalState::Green) {
				end = GetOtherTunnelBridgeEnd(res.tile);
				free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(res.tile, end, res.tile);
			} else {
				free_tiles = -1;
			}
			ApplyAvailableFreeTunnelBridgeTiles(lookahead, free_tiles, res.tile, end);
		}
	} else {
		lookahead->flags.Reset(TrainReservationLookAheadFlag::TunnelBridgeExitFree);
		lookahead->flags.Reset(TrainReservationLookAheadFlag::Chunnel);
		if (lookahead->tunnel_bridge_reserved_tiles != 0) {
			lookahead->reservation_end_position -= (lookahead->tunnel_bridge_reserved_tiles * (int)TILE_SIZE);
			lookahead->tunnel_bridge_reserved_tiles = 0;
		}
	}

	lookahead->reservation_end_tile = res.tile;
	lookahead->reservation_end_trackdir = res.trackdir;

	FlushDeferredDetermineCombineNormalShuntMode(v);
	SetTrainReservationLookaheadEnd(v);

	if (_settings_game.vehicle.train_speed_adaptation && v->signal_speed_restriction > 0 && lookahead->reservation_end_position > old_reservation_end_position) {
		for (const TrainReservationLookAheadItem &item : lookahead->items) {
			if (item.type == TRLIT_SPEED_ADAPTATION && item.end + 1 >= old_reservation_end_position && item.end + 1 < lookahead->reservation_end_position) {
				uint16_t signal_speed = GetLowestSpeedTrainAdaptationSpeedAtSignal(TileIndex{item.data_id}, item.data_aux);

				if (signal_speed == 0) {
					/* unrestricted signal ahead, remove current speed adaptation */
					v->UpdateTrainSpeedAdaptationLimit(0);
					break;
				}
				if (signal_speed > v->signal_speed_restriction) {
					/* signal ahead with higher speed, increase current speed adaptation */
					v->UpdateTrainSpeedAdaptationLimit(signal_speed);
				}
			}
		}
	}
}

/**
 * R3R: one direction of the "who owns this reserved path" walk, starting at
 * (tile, trackdir) and only walking towards that direction. Extracted verbatim
 * from GetTrainForReservation() below so that GetTrainForWholeTileReservation()
 * can ask the very same question for a tile whose reservation is a whole-tile
 * bit (a station platform or waypoint) instead of a plain rail track.
 *
 * FollowReservation() ignores one-way signals here on purpose: one of the two
 * search directions of a caller is always the "wrong" way round.
 *
 * @return The train owning the path, or nullptr if the path is stray.
 */
#if R3R_PROBES
/**
 * R3R (KI-210, diagnosis): explain one direction of the ownership walk above.
 *
 * The bug it exists for: a reservation that a train booked *this tick* (a live
 * booking) got attributed to the parked consist whose stale route happened to
 * join it, and the waiter purge then released it -- the player sees the preview
 * blink on and off every tick. The walk itself is the engine's own and cannot be
 * instrumented without touching it, but every *answer* can be logged: which tile
 * the walk ended on and by which of the three ways the train was found.
 *
 * @param start     tile the walk started from (== the queried reservation bit).
 * @param start_td  trackdir of that first direction.
 * @param res_tile  tile FollowReservation() stopped on.
 * @param res_td    trackdir it stopped with.
 * @param how       "blocked" / "direct" / "platform" / "tunnel" / "none".
 * @param found     train that was accepted as the owner (nullptr if none).
 * @param found_on  tile that train was found on (only for "platform").
 */
static void R3RResvWalkLog(TileIndex start, Trackdir start_td, TileIndex res_tile, Trackdir res_td,
		const char *how, const Train *found, TileIndex found_on = INVALID_TILE)
{
	if (!R3RDbgOn()) return;
	if (!R3RIsWatchedResvTile(start)) return;

	int owner = -1;
	int head  = -1;
	if (found != nullptr) {
		owner = (int)found->index.base();
		const Train *const f = found->First();
		head = f != nullptr ? (int)f->index.base() : -1;
	}
	R3RDbgWrite("RESV-WALK start=%d,%d td=%u phase=%s how=%s end=%d,%d/td%u found=%d,%d owner=veh%d head=veh%d\n",
			(int)TileX(start), (int)TileY(start), (unsigned)start_td, r3r_resv_phase, how,
			(int)TileX(res_tile), (int)TileY(res_tile), (unsigned)res_td,
			(int)TileX(found_on), (int)TileY(found_on), owner, head);
}
#endif /* R3R_PROBES */

static Train *R3RFollowReservationToOwner(TileIndex tile, Trackdir trackdir, RailTypes rts)
{
	/* If the tile has a one-way block signal in the current trackdir, skip the
	 * search in this direction as the reservation can't come from this side.*/
	if (HasOnewaySignalBlockingTrackdir(tile, ReverseTrackdir(trackdir)) && !HasPbsSignalOnTrackdir(tile, trackdir)) {
#if R3R_PROBES
		R3RResvWalkLog(tile, trackdir, tile, trackdir, "blocked", nullptr);
#endif
		return nullptr;
	}

	FindTrainOnTrackInfo ftoti;
	ftoti.res = FollowReservation(GetTileOwner(tile), rts, tile, trackdir, FRF_IGNORE_ONEWAY, nullptr, nullptr);

	CheckTrainsOnTrack(ftoti, ftoti.res.tile);
	if (ftoti.best != nullptr) {
#if R3R_PROBES
		R3RResvWalkLog(tile, trackdir, ftoti.res.tile, ftoti.res.trackdir, "direct", ftoti.best);
#endif
		return ftoti.best;
	}

	/* Special case for stations: check the whole platform for a vehicle. */
	if (IsRailStationTile(ftoti.res.tile)) {
		TileIndexDiff diff = TileOffsByDiagDir(TrackdirToExitdir(ReverseTrackdir(ftoti.res.trackdir)));
		for (TileIndex st_tile = ftoti.res.tile + diff; IsCompatibleTrainStationTile(st_tile, ftoti.res.tile); st_tile += diff) {
			CheckTrainsOnTrack(ftoti, st_tile);
			if (ftoti.best != nullptr) {
#if R3R_PROBES
				R3RResvWalkLog(tile, trackdir, ftoti.res.tile, ftoti.res.trackdir, "platform", ftoti.best, st_tile);
#endif
				return ftoti.best;
			}
		}
	}

	if (IsTileType(ftoti.res.tile, TileType::TunnelBridge) && IsTrackAcrossTunnelBridge(ftoti.res.tile, TrackdirToTrack(ftoti.res.trackdir))) {
		if (IsTunnelBridgeWithSignalSimulation(ftoti.res.tile)) {
			/* Special case for signalled bridges/tunnels: find best train on bridge/tunnel if exit reserved. */
			if (IsTunnelBridgeSignalSimulationExit(ftoti.res.tile) && !(IsTunnelBridgeEffectivelyPBS(ftoti.res.tile) && GetTunnelBridgeExitSignalState(ftoti.res.tile) == SignalState::Red)) {
				ftoti.best = GetTrainClosestToTunnelBridgeEnd(ftoti.res.tile, GetOtherTunnelBridgeEnd(ftoti.res.tile));
			}
		} else {
			/* Special case for bridges/tunnels: check the other end as well. */
			CheckTrainsOnTrack(ftoti, GetOtherTunnelBridgeEnd(ftoti.res.tile));
		}
		if (ftoti.best != nullptr) {
#if R3R_PROBES
			R3RResvWalkLog(tile, trackdir, ftoti.res.tile, ftoti.res.trackdir, "tunnel", ftoti.best,
					GetOtherTunnelBridgeEnd(ftoti.res.tile));
#endif
			return ftoti.best;
		}
	}

#if R3R_PROBES
	R3RResvWalkLog(tile, trackdir, ftoti.res.tile, ftoti.res.trackdir, "none", nullptr);
#endif
	return nullptr;
}

/**
 * R3R (KI-210): the "first train on the path" flavour of the walk above.
 *
 * FollowReservation() answers the question "which train stands at the *end* of
 * this reserved path" -- and that answer is only as good as the assumption that
 * the whole path belongs to one booking. Which breaks exactly when a *live*
 * booking is appended to a stale one: the parked consist's dead route and the
 * locomotive's fresh route are one continuous run of reserved bits, so walking
 * from a bit of the fresh part runs through the stale part and lands on the
 * parked consist. The purge then frees the locomotive's own bits -- the player
 * watches the preview blink on and off every tick (KI-210; log: 24 books
 * 37,9/37,10/38,10, the walk from 37,9 ends on 27,9 at the parked consist 6,
 * and 6's waiter purge frees all three).
 *
 * This walk stops at the *nearest* train instead: the first tile along the path
 * that has a train standing on it. A booking can only ever be joined to another
 * one at a train, never through it, so the first train met is the one the
 * queried bit really belongs to -- and for the stale part of the route it is the
 * parked consist itself, which is exactly what the purge wants to know. The
 * walk is deliberately conservative: when the nearest train is somebody else,
 * the bit is not attributed to the consist asking and is left alone.
 *
 * @return The first train met along the path, or nullptr if there is none.
 */
static Train *R3RFollowReservationToNearbyTrain(TileIndex tile, Trackdir trackdir, RailTypes rts)
{
	/* If the tile has a one-way block signal in the current trackdir, skip the
	 * search in this direction as the reservation can't come from this side.*/
	if (HasOnewaySignalBlockingTrackdir(tile, ReverseTrackdir(trackdir)) && !HasPbsSignalOnTrackdir(tile, trackdir)) {
		return nullptr;
	}

	Train *hit = nullptr;
	FollowReservationEnumerate(GetTileOwner(tile), rts, tile, trackdir, FRF_IGNORE_ONEWAY,
			[&hit](TileIndex t, Trackdir) -> bool {
				Vehicle *v = GetFirstVehicleOnTile(t, VehicleType::Train);
				if (v == nullptr) return false;
				hit = static_cast<Train *>(v);
				return true; /* Stop here: this is the train the path really runs into. */
			});
	return hit;
}

/**
 * Find the train which has reserved a specific path.
 *
 * @param tile A tile on the path.
 * @param track A reserved track on the tile.
 * @return The vehicle holding the reservation or nullptr if the path is stray.
 */
Train *GetTrainForReservation(TileIndex tile, Track track)
{
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: {:X}", track);

	/* R3R (KI-361, round 243-cont): ask the ledger first. The walk below answers
	 * "which train stands at the far end of this reserved path", and while a
	 * locomotive couples to a waiting consist that far end is always the *target*
	 * -- so the bits the coupling path installs come back attributed to the
	 * waiting consist, i.e. to somebody else. Field log 2026-10-11: SET-RAIL
	 * actor=23 on 1112,128..131 while the walk answered veh22 for all four, which
	 * turned the locomotive's own booking into a red signal in front of itself
	 * (veh23 stood still on 1112,125 for the whole session). The ledger recorded
	 * the writer, which is the question the callers actually ask.
	 *
	 * The walk stays as the fallback for bits without a row: those came in with a
	 * savegame, or an engine-internal path wrote them without an R3R actor. */
	if (Train *owner = R3RTrainForLedgerBit(tile, track)) return owner;

	Trackdir  trackdir = TrackToTrackdir(track);

	RailTypes rts = GetRailTypeInfo(GetTileRailTypeByTrack(tile, track))->indirect_compatible_railtypes;

	/* Follow the path from tile to both ends, one of the end tiles should
	 * have a train on it. We need FollowReservation to ignore one-way signals
	 * here, as one of the two search directions will be the "wrong" way. */
	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		if (Train *t = R3RFollowReservationToOwner(tile, trackdir, rts)) return t;
	}

	return nullptr;
}

/**
 * R3R (KI-207c): owner of a reservation that is stored as a *whole tile* bit --
 * a rail station platform (which includes a waypoint), i.e. exactly the tiles
 * GetTrainForReservation() above cannot be asked about, because its first line
 * asserts the *plain rail* reservation bit (rail_map.h:168).
 *
 * Which is the asymmetry that creates the ghosts the player reported: the setter
 * TryReserveRailTrack() dispatches on HasStationRail(tile), which includes
 * waypoints, while every *release* of a platform goes through
 * SetRailStationPlatformReservation(), whose call sites are all guarded by
 * IsRailStationTile() -- waypoints excluded. A consist parked on a waypoint by
 * the R3R platform waiter therefore reserves that waypoint tile and, if the
 * reservation is ever orphaned, nothing in the engine can ever clear it again.
 *
 * The walk itself is the engine's own (same helper GetTrainForReservation() uses);
 * only the entry differs: instead of one plain rail track, every track the tile
 * reserves is followed in both directions. A train standing on the tile is the
 * owner by definition.
 *
 * @param tile A tile whose reservation is a whole-tile bit (station / waypoint).
 * @return The vehicle holding the reservation, or nullptr if it is stray.
 */
Train *GetTrainForWholeTileReservation(TileIndex tile)
{
	if (!IsValidTile(tile)) return nullptr;

	/* A train standing on the tile owns its reservation by definition. */
	if (Vehicle *v = GetFirstVehicleOnTile(tile, VehicleType::Train)) return static_cast<Train *>(v);

	const TrackBits bits = GetReservedTrackbits(tile);
	if (bits == TRACK_BIT_NONE) return nullptr;

	const RailType rt = GetTileRailType(tile);
	if (rt == INVALID_RAILTYPE) return nullptr;
	const RailTypes rts = GetRailTypeInfo(rt)->indirect_compatible_railtypes;

	for (Track track = TRACK_BEGIN; track < TRACK_END; track++) {
		if (!HasBit(bits, track)) continue;
		Trackdir trackdir = TrackToTrackdir(track);
		for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
			if (Train *t = R3RFollowReservationToOwner(tile, trackdir, rts)) return t;
		}
	}
	return nullptr;
}

/**
 * R3R (KI-210): GetTrainForReservation() with the *nearest* train as the answer
 * instead of the train at the far end of the path -- see
 * R3RFollowReservationToNearbyTrain() for why the difference matters. Used by
 * the chain-edit reservation purge, which must never free a bit that a live
 * booking of another train happens to have joined to its own stale route.
 */
Train *GetR3RReservationOwnerNearby(TileIndex tile, Track track)
{
	assert_msg_tile(HasReservedTracks(tile, TrackToTrackBits(track)), tile, "track: {:X}", track);
	Trackdir trackdir = TrackToTrackdir(track);

	const RailTypes rts = GetRailTypeInfo(GetTileRailTypeByTrack(tile, track))->indirect_compatible_railtypes;

	for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
		if (Train *t = R3RFollowReservationToNearbyTrain(tile, trackdir, rts)) return t;
	}

	return nullptr;
}

/**
 * R3R (KI-350): direction-split sibling of GetR3RReservationOwnerNearby() for the
 * red-signal probe (KI-349) in train_cmd.cpp.
 *
 * GetR3RReservationOwnerNearby() walks the forward direction first and returns as
 * soon as *one* of the two directions meets a train, so a "near=" answer alone
 * cannot tell that the *other* direction also reaches a train. That datum is
 * exactly what separates the two explanations of a red signal whose block bit is
 * held: "the bit sits at the end of my own booking" (the block is held by the
 * train asking, i.e. its own tail) from "the bit is a stale leftover of the
 * neighbour" (held by somebody else). This entry point answers one direction at a
 * time; the walk, its conservatism and its read-only nature are identical to the
 * merged helper above.
 *
 * @param tile A tile whose reservation is stored as a plain rail track bit.
 * @param trackdir The single direction to walk in.
 * @return The first train met along that one direction, or nullptr if there is none.
 */
Train *R3RReservationOwnerInDirection(TileIndex tile, Trackdir trackdir)
{
	if (!IsValidTile(tile) || !IsPlainRailTile(tile)) return nullptr;

	const Track track = TrackdirToTrack(trackdir);
	if (!HasBit(GetReservedTrackbits(tile), track)) return nullptr;

	const RailTypes rts = GetRailTypeInfo(GetTileRailTypeByTrack(tile, track))->indirect_compatible_railtypes;
	return R3RFollowReservationToNearbyTrain(tile, trackdir, rts);
}

/**
 * R3R (KI-210): GetTrainForWholeTileReservation() with the *nearest* train as
 * the answer instead of the train at the far end of the path (station platform /
 * waypoint flavour, for the same caller and the same reason).
 */
Train *GetR3RWholeTileReservationOwnerNearby(TileIndex tile)
{
	if (!IsValidTile(tile)) return nullptr;

	/* A train standing on the tile owns its reservation by definition. */
	if (Vehicle *v = GetFirstVehicleOnTile(tile, VehicleType::Train)) return static_cast<Train *>(v);

	const TrackBits bits = GetReservedTrackbits(tile);
	if (bits == TRACK_BIT_NONE) return nullptr;

	const RailType rt = GetTileRailType(tile);
	if (rt == INVALID_RAILTYPE) return nullptr;
	const RailTypes rts = GetRailTypeInfo(rt)->indirect_compatible_railtypes;

	for (Track track = TRACK_BEGIN; track < TRACK_END; track++) {
		if (!HasBit(bits, track)) continue;
		Trackdir trackdir = TrackToTrackdir(track);
		for (int i = 0; i < 2; ++i, trackdir = ReverseTrackdir(trackdir)) {
			if (Train *t = R3RFollowReservationToNearbyTrain(tile, trackdir, rts)) return t;
		}
	}
	return nullptr;
}

/**
 * R3R (KI-207): release every plain-rail reservation bit that answers to nobody.
 *
 * The criterion is the engine's own: GetTrainForReservation() follows the reserved
 * path from the queried bit to both ends and returns the train standing at one of
 * them, or nullptr when the path is stray. A stray bit is therefore, by the
 * engine's own measure, dead -- nothing is going to consume it -- yet it keeps
 * blocking other trains and keeps painting the reservation preview.
 *
 * Two passes, for the reason documented at R3RReleaseChainReservations(): freeing
 * while scanning cuts the path of everything behind the freed bit, so the rest of
 * the scan would answer "nobody" for bits that in fact still belong to a train.
 * Pass 1 only collects, pass 2 frees.
 *
 * The same guards as that function: never a tile a train is standing on. Plain rail
 * is handled per track bit; a station platform / waypoint is handled as the
 * whole-tile bit that it is (KI-207c -- a depot or crossing reservation is state
 * protecting the vehicle standing there and is left alone). Exposed as the console
 * command 'r3r_resv_purge' so a save with ghosts inherited from an older build can
 * be cleaned on demand; the periodic reservation audit does the same thing on its own
 * once a bit has been stray for R3R_RESV_REAP_TICKS ticks.
 *
 * @return Number of released reservations (track bits / whole tiles).
 */
uint R3RPurgeStrayReservations()
{
	/* R3R (第 243 轮, REG-243-O): name this release path so the UNRES-RAIL lines it
	 * produces read "purge" instead of inheriting whatever phase ran last. "purge" is
	 * not in R3RResvLedgerPurpose()'s consist_phases[]: log only, no behaviour. */
	R3RResvPhaseGuard r3r_phase_purge("purge");

	struct R3RResvBit { TileIndex tile; Track track; bool whole_tile; };
	std::vector<R3RResvBit> doomed;

	for (TileIndex t(0); t < Map::Size(); t++) {
		if (GetFirstVehicleOnTile(t, VehicleType::Train) != nullptr) continue;

		/* KI-207c: a rail station platform or waypoint carries a whole-tile
		 * reservation. Nothing in the engine can release one (every release site is
		 * guarded by IsRailStationTile(), which excludes waypoints), so a stale one
		 * is a permanent ghost on exactly that tile. */
		if (GetTileType(t) == TileType::Station && HasStationRail(t)) {
			if (!HasStationReservation(t)) continue;
			if (GetTrainForWholeTileReservation(t) != nullptr) continue;
			doomed.push_back({t, TRACK_BEGIN, true});
			continue;
		}

		if (GetTileType(t) != TileType::Railway || !IsPlainRailTile(t)) continue;
		const TrackBits bits = GetRailReservationTrackBits(t);
		if (bits == TRACK_BIT_NONE) continue;

		for (Track track = TRACK_BEGIN; track < TRACK_END; track++) {
			if (!HasBit(bits, track)) continue;
			if (HasReservedTracks(t, TrackToTrackBits(track)) && GetTrainForReservation(t, track) != nullptr) continue;
			doomed.push_back({t, track, false});
		}
	}

	uint freed = 0;
	for (const R3RResvBit &rec : doomed) {
		/* Nothing runs between the passes, but validate anyway: the map must never
		 * be touched through a stale record. */
		if (rec.whole_tile) {
			if (GetTileType(rec.tile) != TileType::Station || !HasStationRail(rec.tile)) continue;
			if (!HasStationReservation(rec.tile)) continue;
			const TrackBits rail_bits = GetStationReservationTrackBits(rec.tile);
			if (rail_bits == TRACK_BIT_NONE) continue;

			UnreserveRailTrack(rec.tile, FindFirstTrack(rail_bits));
			freed++;
			R3RDbgWrite("RESV-PURGE tile=%d,%d track=stn kind=%s\n",
					(int)TileX(rec.tile), (int)TileY(rec.tile), IsRailWaypointTile(rec.tile) ? "WAYPOINT" : "STATION");
			continue;
		}

		if (GetTileType(rec.tile) != TileType::Railway || !IsPlainRailTile(rec.tile)) continue;
		if (!HasBit(GetRailReservationTrackBits(rec.tile), rec.track)) continue;

		UnreserveRailTrack(rec.tile, rec.track);
		freed++;
		R3RDbgWrite("RESV-PURGE tile=%d,%d track=%u\n",
				(int)TileX(rec.tile), (int)TileY(rec.tile), (uint)rec.track);
	}
	return freed;
}

CommandCost CheckTrainReservationPreventsTrackModification(TileIndex tile, Track track)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && _settings_game.vehicle.track_edit_ignores_realistic_braking) {
		return CheckTrainReservationPreventsTrackModification(GetTrainForReservation(tile, track));
	}
	return CommandCost();
}
CommandCost CheckTrainReservationPreventsTrackModification(const Train *v)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && !_settings_game.vehicle.track_edit_ignores_realistic_braking &&
			v != nullptr && v->UsingRealisticBraking() && (v->cur_speed > 0 || !v->vehstatus.Any({VehState::Stopped, VehState::Crashed}))) {
		return CommandCost(STR_ERROR_CANNOT_MODIFY_TRACK_TRAIN_APPROACHING);
	}
	return CommandCost();
}

CommandCost CheckTrainInTunnelBridgePreventsTrackModification(TileIndex start, TileIndex end)
{
	if (_settings_game.vehicle.train_braking_model != TBM_REALISTIC || _settings_game.vehicle.track_edit_ignores_realistic_braking) return CommandCost();

	auto handler = [&](const Train *t) -> bool {
		return CheckTrainReservationPreventsTrackModification(t->First()).Failed();
	};
	if (HasVehicleOnTile<VehicleType::Train>(start, handler) || HasVehicleOnTile<VehicleType::Train>(end, handler)) {
		return CommandCost(STR_ERROR_CANNOT_MODIFY_TRACK_TRAIN_APPROACHING);
	}
	return CommandCost();
}

/**
 * This is called to retrieve the previous signal, as required
 * This is not run all the time as it is somewhat expensive and most restrictions will not test for the previous signal
 */
TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode)
{
	if (mode == TRPESAF_RES_END_TILE) return INVALID_TILE;

	TileIndex tile;
	Trackdir  trackdir;

	if (mode == TRPESAF_RES_END && v->lookahead != nullptr) {
		tile = v->lookahead->reservation_end_tile;
		trackdir = v->lookahead->reservation_end_trackdir;
	} else {
		const Train *moving_front = v->GetMovingFront();
		if (IsRailDepotTile(moving_front->tile)) {
			return moving_front->tile;
		}
		if (moving_front->track & TRACK_BIT_WORMHOLE && IsTileType(moving_front->tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExit(moving_front->tile) && IsTunnelBridgeEffectivelyPBS(moving_front->tile)) {
			return moving_front->tile;
		}
		tile = moving_front->tile;
		trackdir = moving_front->GetVehicleTrackdir();
	}

	// scan forwards from vehicle position, for the case that train is waiting at/approaching PBS signal

	CFollowTrackRail ft(v);

	for (;;) {
		if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir)) {
			if (HasPbsSignalOnTrackdir(tile, trackdir)) {
				// found PBS signal
				return tile;
			} else {
				// wrong type of signal
				return INVALID_TILE;
			}
		}

		if (IsTileType(tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile) && TrackdirExitsTunnelBridge(tile, trackdir)) {
			return tile;
		}

		// advance to next tile
		if (!ft.Follow(tile, trackdir)) {
			// ran out of track
			return INVALID_TILE;
		}

		if (KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) {
			// reached a junction tile
			return INVALID_TILE;
		}

		tile = ft.new_tile;
		trackdir = FindFirstTrackdir(ft.new_td_bits);
	}
}

/**
 * Test whether a train's reservation passes through a given tile.
 */
bool TrainReservationPassesThroughTile(const Train *v, TileIndex search_tile)
{
	bool found = false;
	const Train *moving_front = v->GetMovingFront();
	FollowReservationEnumerate(v->owner, v->GetIndirectCompatibleRailTypes(), moving_front->tile, moving_front->GetVehicleTrackdir(), FRF_NONE, [&](TileIndex tile, Trackdir trackdir) -> bool {
		if (tile == search_tile) {
			found = true;
			return true;
		}
		return false;
	});
	return found;
}

/**
 * R3R (KI-211, fenced in KI-211c/211d): the tiles a train's own booking covers --
 * see pbs.h for why the walk is fenced at the first train, leaves in both
 * directions and refuses to start on a shared tile.
 *
 * Three shapes of the same failure drove the three rounds, each one hiding behind
 * the previous fix: a walk that asks the map for an owner crosses onto a
 * neighbour's booking whenever the two touch (KI-210: the locomotive's live route
 * was freed by the parked consist's purge); a walk that starts inside our own
 * booking but does not stop at the neighbour collects the neighbour's bits as
 * ours (KI-211: the parked waiter on 33,9 collected 34,9 .. 38,10 of the
 * locomotive standing next to it, and -- paired with a far-end owner lookup that
 * answered "the waiter" -- freed that live route); and a walk that *does* stop at
 * the neighbour is still blind when the neighbour is standing on the very tile
 * the walk starts from (KI-211d, the field log of the same waiter: veh 24 had
 * just decoupled and stood on 33,9 together with the consist's head 6 and 7, its
 * nose already on 34,9, so the walk left the shared tile -- skipped by design as
 * "our own tile" -- and stepped straight onto 24's booking, where the fence,
 * which only looks at tiles the walk *enters*, had nothing to see).
 *
 * Hence the start-tile guard below: two trains on one tile make the bits leaving
 * it ambiguous (the engine does not record which train booked a bit), so this
 * vehicle contributes no path at all. Conservative on purpose -- a stale bit of
 * ours may stay on the map for a while longer, but a live route of the neighbour
 * is never collected and therefore never freed.
 *
 * The start tile of each walk is not reported: it is the vehicle's own tile, and
 * a caller matching tiles would only have to filter it back out.
 *
 * @param v The train whose reservation is walked.
 * @param handler Called once per tile on the path.
 * @param ctx Opaque pointer passed through to the handler.
 */
void R3RCollectReservedPathTiles(const Train *v, R3RResvTileHandler handler, void *ctx)
{
	const Train *const chain_head = v->First();

	for (const Train *w = v; w != nullptr; w = w->Next()) {
		/* KI-211d: a tile may hold two trains (a just-decoupled locomotive usually
		 * stands on the same tile as the consist it left). The reservation bits
		 * leaving such a tile cannot be attributed to us, so do not walk.
		 * Walk the *tile's* vehicle list here: GetFirstVehicleOnTile() alone only
		 * reports its head, and the head may well be ourselves -- which is how the
		 * nearest-train gate above was fooled in the field log. Train::From() is
		 * only ever applied to a non-null pointer on purpose: it asserts on the
		 * vehicle's type. */
		bool shared_tile = false;
		for (const Vehicle *vt = GetFirstVehicleOnTile(w->tile, VehicleType::Train); vt != nullptr; vt = vt->HashTileNext()) {
			if (Train::From(vt)->First() != chain_head) {
				shared_tile = true;
				break;
			}
		}
		if (shared_tile) continue;

		const Trackdir own_td = w->GetVehicleTrackdir();
		for (int i = 0; i < 2; i++) {
			bool first = true;
			FollowReservationEnumerate(w->owner, w->GetIndirectCompatibleRailTypes(), w->tile,
					i == 0 ? own_td : ReverseTrackdir(own_td), FRF_NONE,
					[&](TileIndex tile, Trackdir trackdir) -> bool {
						if (first) {
							first = false;
							return false;
						}
						/* A booking ends where the next train begins (see above). */
						if (GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr) return true;
						handler(tile, trackdir, ctx);
						return false;
					});
		}
	}
}

/**
 * Determine whether a certain track on a tile is a safe position to end a path.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param include_line_end Should end-of-line tiles be considered safe?
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @return True if it is a safe position
 */
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg)
{
	if (IsRailDepotTile(tile)) return true;

	/* For non-pbs signals, stop on the signal tile. */
	if (HasBlockSignalOnTrackdir(tile, trackdir)) return true;

	if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) {
		return true;
	}

	/* Check next tile. For performance reasons, we check for 90 degree turns ourself. */
	CFollowTrackRail ft(v, v->GetIndirectCompatibleRailTypes());

	/* End of track? */
	if (!ft.Follow(tile, trackdir)) {
		/* Last tile of a terminus station is a safe position. */
		if (include_line_end) return true;
	}

	/* Check for reachable tracks. */
	ft.new_td_bits &= DiagdirReachesTrackdirs(ft.exitdir);
	if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td, forbid_90deg)) ft.new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);
	if (ft.new_td_bits == TRACKDIR_BIT_NONE) return include_line_end;

	if (ft.new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.new_td_bits);
		/* PBS signal on next trackdir? Conditionally safe position. */
		if (HasPbsSignalOnTrackdir(ft.new_tile, td)) {
			const Track track = TrackdirToTrack(td);
			if (GetSignalType(ft.new_tile, track) == SignalType::NoEntry) return include_line_end;
			if (GetSignalAlwaysReserveThrough(ft.new_tile, track)) return false;
			if (GetSignalSpecialPropagationFlag(ft.new_tile, track)) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(ft.new_tile, track);
				if (prog != nullptr && prog->actions_used_flags & TRPAUF_RESERVE_THROUGH) {
					TraceRestrictProgramResult out;
					prog->Execute(v, TraceRestrictProgramInput(ft.new_tile, td, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr), out);
					if (out.flags & TRPRF_RESERVE_THROUGH) {
						return false;
					}
				}
			}
			return true;
		}
		/* One-way PBS signal against us? Safe if end-of-line is allowed. */
		if (IsTileType(ft.new_tile, TileType::Railway) && HasSignalOnTrackdir(ft.new_tile, ReverseTrackdir(td)) &&
				GetSignalType(ft.new_tile, TrackdirToTrack(td)) == SignalType::PathOneWay) {
			return include_line_end;
		}
		if (IsRailTunnelBridgeTile(ft.new_tile) &&
				IsTrackAcrossTunnelBridge(ft.new_tile, TrackdirToTrack(td)) &&
				IsTunnelBridgeSignalSimulationExitOnly(ft.new_tile) && IsTunnelBridgeEffectivelyPBS(ft.new_tile)) {
			return include_line_end;
		}
	}

	return false;
}

void PBSWaitingPositionRestrictedSignalState::TraceRestrictExecuteResEndSlotIntl(const Train *v)
{
	TraceRestrictProgramActionsUsedFlags actions_used_flags = TRPAUF_PBS_RES_END_SLOT;
	const bool tb_entrance_slots = _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntranceTile(this->tile);
	if (tb_entrance_slots) actions_used_flags |= TRPAUF_SLOT_ACQUIRE;

	if (prog->actions_used_flags & actions_used_flags) {
		TraceRestrictProgramResult out;
		TraceRestrictProgramInput input(this->tile, this->trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
		input.permitted_slot_operations = TRPISP_PBS_RES_END_ACQUIRE;
		if (tb_entrance_slots) input.permitted_slot_operations |= TRPISP_ACQUIRE;
		prog->Execute(v, input, out);
	}
}

bool IsWaitingPositionFreeTraceRestrictExecute(const TraceRestrictProgram *prog, const Train *v, TileIndex tile, Trackdir trackdir)
{
	if (prog != nullptr && prog->actions_used_flags & TRPAUF_PBS_RES_END_WAIT) {
		TraceRestrictProgramInput input(tile, trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
		input.permitted_slot_operations = TRPISP_PBS_RES_END_ACQ_DRY;
		TraceRestrictProgramResult out;
		prog->Execute(v, input, out);
		if (out.flags & TRPRF_PBS_RES_END_WAIT) {
			return false;
		}
	}
	return true;
}

/**
 * Check if a safe position is free.
 *
 * @param v the vehicle to test for
 * @param tile The tile
 * @param trackdir The trackdir to test
 * @param forbid_90deg Don't allow trains to make 90 degree turns
 * @param restricted_signal_state Restricted signal state in/out
 * @return True if the position is free
 */
bool IsWaitingPositionFree(const Train *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg, PBSWaitingPositionRestrictedSignalState *restricted_signal_state)
{
	Track     track = TrackdirToTrack(trackdir);
	TrackBits reserved = GetReservedTrackbits(tile);

	/* Tile reserved? Can never be a free waiting position. */
	if (TrackOverlapsTracks(reserved, track)) return false;

	/* Not reserved and depot or not a pbs signal -> free. */
	if (IsRailDepotTile(tile)) return true;

	auto pbs_res_end_wait_test = [v, restricted_signal_state](TileIndex t, Trackdir td, bool tunnel_bridge) -> bool {
		if (tunnel_bridge ? IsTunnelBridgeRestrictedSignal(t) : IsRestrictedSignal(t)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(t, TrackdirToTrack(td));
			if (restricted_signal_state != nullptr && prog != nullptr) {
				restricted_signal_state->prog = prog;
				restricted_signal_state->tile = t;
				restricted_signal_state->trackdir = td;
				if (restricted_signal_state->defer_test_if_slot_conditional && (prog->actions_used_flags & TRPAUF_SLOT_CONDITIONALS) && (prog->actions_used_flags & TRPAUF_PBS_RES_END_WAIT)) {
					restricted_signal_state->deferred_test = true;
					return true;
				}
			}
			return IsWaitingPositionFreeTraceRestrictExecute(prog, v, t, td);
		}
		return true;
	};

	if (IsTileType(tile, TileType::Railway) && HasSignalOnTrackdir(tile, trackdir) && !IsPbsSignal(GetSignalType(tile, track))) {
		return pbs_res_end_wait_test(tile, trackdir, false);
	}

	if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(trackdir))) {
		bool free = pbs_res_end_wait_test(tile, trackdir, true);
		if (free && IsTunnelBridgeSignalSimulationBidirectional(tile)) {
			TileIndex other_end = GetOtherTunnelBridgeEnd(tile);
			if (HasAcrossTunnelBridgeReservation(other_end) && GetTunnelBridgeExitSignalState(other_end) == SignalState::Red) return false;
			const Direction dir = DiagDirToDir(GetTunnelBridgeDirection(other_end));
			for (const Train *u : VehiclesOnTile<VehicleType::Train>(other_end)) {
				DirDiff diff = DirDifference(u->direction, dir);
				if (diff == DirDiff::Same) return false;
				if (diff == DirDiff::Right45 || diff == DirDiff::Left45) {
					if (GetAcrossTunnelBridgeTrackBits(other_end) & u->track) return false;
				}
			}
		}
		return free;
	}

	/* Check the next tile, if it's a PBS signal, it has to be free as well. */
	CFollowTrackRail ft(v, v->GetIndirectCompatibleRailTypes());

	if (!ft.Follow(tile, trackdir)) return true;

	/* Check for reachable tracks. */
	ft.new_td_bits &= DiagdirReachesTrackdirs(ft.exitdir);
	if (Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td, forbid_90deg)) ft.new_td_bits &= ~TrackdirCrossesTrackdirs(trackdir);

	if (HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits))) return false;

	if (ft.new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
		Trackdir td = FindFirstTrackdir(ft.new_td_bits);
		/* PBS signal on next trackdir? */
		if (HasPbsSignalOnTrackdir(ft.new_tile, td)) {
			return pbs_res_end_wait_test(ft.new_tile, td, false);
		}
	}

	return true;
}
