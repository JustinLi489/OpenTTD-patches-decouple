/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file pbs.h PBS support routines. */

#ifndef PBS_H
#define PBS_H

#include "tile_type.h"
#include "direction_type.h"
#include "track_type.h"
#include "vehicle_type.h"
#include "r3r_perf.h"
#include "r3r_resv_probes.h"
#include "3rdparty/cpp-ring-buffer/ring_buffer.hpp"

/* R3R (KI-207, 2026-09-25): reservation-origin tracing.
 *
 * A "ghost reservation" is a reservation bit no train will ever drive over, left
 * behind by a chain edit (couple / decouple / segment flip). To find out *who*
 * installs one, every write to a watched tile's reservation field is logged
 * together with the "phase" the game was in when it happened -- the phase is set by
 * the RAII guard below at the reservation entry points, and nesting restores the
 * previous value automatically.
 *
 * The writes themselves are reported from the six map-accessor setters that own the
 * reservation bits (see r3r_resv_probes.h), so no call site can be forgotten. The
 * watch list defaults to the tiles reported as ghosts and can be extended without a
 * rebuild by dropping "r3r_resv_watch.txt" (one "x y" pair per line, '#' starts a
 * comment) next to R3R_debug.log in the game working directory. */
#if R3R_PROBES
extern const char *r3r_resv_phase;
/** Acting train, as an ID rather than a pointer: the log is reached from deep
 *  inside the map setters, where a raw pointer could outlive the train it names. */
extern VehicleID r3r_resv_actor;
bool R3RIsWatchedResvTile(TileIndex tile);
#endif

/**
 * Sets the reservation-origin context (phase name and/or the train doing the
 * reserving) for the enclosing scope and restores the previous values on exit, so
 * nesting works: an inner guard can refine just the actor while the outer one
 * keeps naming the phase.
 *
 * Passing \c nullptr for the phase (or VehicleID::Invalid() for the actor) leaves
 * that field alone -- that is how the generic pathfinder entry point adds "who"
 * without destroying the phase of the caller. A no-op object in a build without the
 * probes, which is why it is a real type and not a macro: call sites must compile
 * both ways.
 */
struct R3RResvPhaseGuard {
#if R3R_PROBES
	const char *prev_phase;
	VehicleID prev_actor;
	explicit R3RResvPhaseGuard(const char *phase, VehicleID actor = VehicleID::Invalid())
		: prev_phase(r3r_resv_phase), prev_actor(r3r_resv_actor)
	{
		if (phase != nullptr) r3r_resv_phase = phase;
		if (actor != VehicleID::Invalid()) r3r_resv_actor = actor;
	}
	~R3RResvPhaseGuard() { r3r_resv_phase = prev_phase; r3r_resv_actor = prev_actor; }
#else
	explicit R3RResvPhaseGuard(const char *, VehicleID = VehicleID::Invalid()) {}
#endif
};

/**
 * VehicleID of \a v for R3RResvPhaseGuard, tolerating a null pointer (that leaves the
 * current actor alone). A template so that pbs.h does not need the full Vehicle type.
 */
template <typename T>
inline VehicleID R3RResvActorID(const T *v) { return v != nullptr ? v->index : VehicleID::Invalid(); }

TrackBits GetReservedTrackbits(TileIndex t);

void SetRailStationPlatformReservation(TileIndex start, DiagDirection dir, bool b);
bool IsRailStationPlatformFree(const Train *v, TileIndex start, DiagDirection dir);

bool TryReserveRailTrack(TileIndex tile, Track t, bool trigger_stations = true);
bool TryReserveRailTrackdir(const Train *v, TileIndex tile, Trackdir td, bool trigger_stations = true);
void UnreserveRailTrack(TileIndex tile, Track t);
void UnreserveRailTrackdir(TileIndex tile, Trackdir td);

/** This struct contains information about the end of a reserved path. */
struct PBSTileInfo {
	TileIndex tile;      ///< Tile the path ends, INVALID_TILE if no valid path was found.
	Trackdir  trackdir;  ///< The reserved trackdir on the tile.
	bool      okay;      ///< True if tile is a safe waiting position, false otherwise.

	/**
	 * Create an empty PBSTileInfo.
	 */
	PBSTileInfo() : tile(INVALID_TILE), trackdir(INVALID_TRACKDIR), okay(false) {}

	/**
	 * Create a PBSTileInfo with given tile, track direction and safe waiting position information.
	 * @param _t The tile where the path ends.
	 * @param _td The reserved track dir on the tile.
	 * @param _okay Whether the tile is a safe waiting point or not.
	 */
	PBSTileInfo(TileIndex _t, Trackdir _td, bool _okay) : tile(_t), trackdir(_td), okay(_okay) {}
};

enum TrainReservationLookAheadItemType : uint8_t {
	TRLIT_STATION                = 0,     ///< Station/waypoint
	TRLIT_REVERSE                = 1,     ///< Reverse behind signal
	TRLIT_TRACK_SPEED            = 2,     ///< Track or bridge speed limit
	TRLIT_SPEED_RESTRICTION      = 3,     ///< Speed restriction
	TRLIT_SIGNAL                 = 4,     ///< Signal
	TRLIT_CURVE_SPEED            = 5,     ///< Curve speed limit
	TRLIT_SPEED_ADAPTATION       = 6,     ///< Train speed adaptation ahead
};

enum TrainReservationSignalLookAheadItemFlags {
	TRSLAI_NO_ASPECT_INC   = 0,           ///< This signal does not increase the signal aspect (e.g. banner repeater)
	TRSLAI_NEXT_ONLY       = 1,           ///< This signal only permits lookahead up to the next physical signal, even if that has TRSLAI_NO_ASPECT_INC (e.g. shunt)
	TRSLAI_COMBINED        = 2,           ///< This signal is a combined normal/shunt signal, special handling
	TRSLAI_COMBINED_SHUNT  = 3,           ///< This signal is a combined normal/shunt signal, in shunt mode
};

struct TrainReservationLookAheadItem {
	int32_t start;
	int32_t end;
	int16_t z_pos;
	/* gap: 2 bytes */
	uint32_t data_id;
	uint16_t data_aux;
	TrainReservationLookAheadItemType type;
	/* gap: 1 byte */
};

struct TrainReservationLookAheadCurve {
	int32_t position;
	DirDiff dir_diff;
};

enum class TrainReservationLookAheadFlag : uint8_t {
	TunnelBridgeExitFree            = 0,  ///< Reservation ends at signalled tunnel/bridge entrance and the corresponding exit is free, but may not be reserved
	DepotEnd                        = 1,  ///< Reservation ends at a depot
	ApplyAdvisory                   = 2,  ///< Apply advisory speed limit on next iteration
	Chunnel                         = 3,  ///< Reservation ends at a signalled chunnel entrance
	TunnelBridgeCombinedDefer       = 4,  ///< Deferred combined normal/shunt tunnel/bridge exit
};
using TrainReservationLookAheadFlags = EnumBitSet<TrainReservationLookAheadFlag, uint16_t>;

struct TrainReservationLookAhead {
	TileIndex reservation_end_tile;       ///< Tile the reservation ends.
	Trackdir  reservation_end_trackdir;   ///< The reserved trackdir on the end tile.
	uint8_t zpos_refresh_remaining = 0;   ///< Remaining position updates before next refresh of cached_zpos
	int32_t current_position;             ///< Current position of the train on the reservation
	int32_t reservation_end_position;     ///< Position of the end of the reservation
	int32_t lookahead_end_position;       ///< Position of the end of the reservation within the lookahead distance
	int32_t next_extend_position;         ///< Next position to try extending the reservation at the sighting distance of the next mid-reservation signal
	int16_t reservation_end_z;            ///< The z coordinate of the reservation end
	int16_t tunnel_bridge_reserved_tiles; ///< How many tiles a reservation into the tunnel/bridge currently extends into the wormhole
	TrainReservationLookAheadFlags flags; ///< Flags
	uint16_t speed_restriction;
	jgr::ring_buffer<TrainReservationLookAheadItem> items;
	jgr::ring_buffer<TrainReservationLookAheadCurve> curves;
	int32_t cached_zpos = 0;              ///< Cached z position as used in TrainDecelerationStats

	int32_t RealEndPosition() const
	{
		return this->reservation_end_position - (this->tunnel_bridge_reserved_tiles * TILE_SIZE);
	}

	void AddStation(int tiles, StationID id, int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end, end + (((int32_t)TILE_SIZE) * tiles), z_pos, id.base(), 0, TRLIT_STATION });
	}

	void AddReverse(int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end, end, z_pos, 0, 0, TRLIT_REVERSE });
	}

	void AddTrackSpeedLimit(uint16_t speed, int offset, int duration, int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset + duration, z_pos, speed, 0, TRLIT_TRACK_SPEED });
	}

	void AddSpeedRestriction(uint16_t speed, int offset, int duration, int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset + duration, z_pos, speed, 0, TRLIT_SPEED_RESTRICTION });
		this->speed_restriction = speed;
	}

	void AddSignal(uint16_t target_speed, int offset, int16_t z_pos, uint16_t flags)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset, z_pos, target_speed, flags, TRLIT_SIGNAL });
	}

	void AddCurveSpeedLimit(uint16_t target_speed, int offset, int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset, z_pos, target_speed, 0, TRLIT_CURVE_SPEED });
	}

	void AddSpeedAdaptation(TileIndex signal_tile, uint16_t signal_track, int offset, int16_t z_pos)
	{
		int32_t end = this->RealEndPosition();
		this->items.push_back({ end + offset, end + offset, z_pos, signal_tile.base(), signal_track, TRLIT_SPEED_ADAPTATION });
	}

	void SetNextExtendPosition();

	void SetNextExtendPositionIfUnset()
	{
		if (this->next_extend_position <= this->current_position) this->SetNextExtendPosition();
	}
};

/** Flags for FollowTrainReservation */
enum class FollowTrainReservationFlag : uint8_t {
	IgnoreLookahead, ///< No use of cached lookahead
	OkayUnused,      ///< 'okay' return value is not used
};
using FollowTrainReservationFlags = EnumBitSet<FollowTrainReservationFlag, uint8_t>;

bool ValidateLookAhead(const Train *v);
PBSTileInfo FollowTrainReservation(const Train *v, Vehicle **train_on_res = nullptr, FollowTrainReservationFlags flags = {});
void ApplyAvailableFreeTunnelBridgeTiles(TrainReservationLookAhead *lookahead, int free_tiles, TileIndex tile, TileIndex end);
int AdvanceTrainReservationLookaheadEnd(const Train *v, int lookahead_end_position);
void SetTrainReservationLookaheadEnd(Train *v);
void FillTrainReservationLookAhead(Train *v);
bool TrainReservationPassesThroughTile(const Train *v, TileIndex search_tile);
bool IsSafeWaitingPosition(const Train *v, TileIndex tile, Trackdir trackdir, bool include_line_end, bool forbid_90deg = false);

struct TraceRestrictProgram;

struct PBSWaitingPositionRestrictedSignalState {
	const TraceRestrictProgram *prog = nullptr;
	TileIndex tile = INVALID_TILE;
	Trackdir  trackdir = INVALID_TRACKDIR;
	bool defer_test_if_slot_conditional = false;
	bool deferred_test = false;

	inline void TraceRestrictExecuteResEndSlot(const Train *v)
	{
		if (this->prog != nullptr) this->TraceRestrictExecuteResEndSlotIntl(v);
	}

private:
	void TraceRestrictExecuteResEndSlotIntl(const Train *v);
};

bool IsWaitingPositionFree(const Train *v, TileIndex tile, Trackdir trackdir, bool forbid_90deg = false, PBSWaitingPositionRestrictedSignalState *restricted_signal_state = nullptr);
bool IsWaitingPositionFreeTraceRestrictExecute(const TraceRestrictProgram *prog, const Train *v, TileIndex tile, Trackdir trackdir);

Train *GetTrainForReservation(TileIndex tile, Track track);

/**
 * R3R (KI-207c): owner of a reservation stored as a whole-tile bit -- a rail
 * station platform or a waypoint (see pbs.cpp). Same answer as
 * GetTrainForReservation(), which cannot be used for those tiles because it
 * starts from a plain rail track. Returns nullptr when the reservation is stray.
 */
Train *GetTrainForWholeTileReservation(TileIndex tile);

/**
 * R3R (KI-210): same two questions as above, but the answer is the *nearest*
 * train the reserved path runs into instead of the one standing at its far end.
 *
 * The far end is only trustworthy while a path belongs to a single booking; a
 * live booking joined to a stale one (parked consist + locomotive on the same
 * run of reserved bits) makes the far-end answer point at the parked consist and
 * a purge based on it frees the locomotive's own reservation (see pbs.cpp).
 * The nearest train is the one the queried bit really belongs to, so the chain
 * purge uses these.
 */
Train *GetR3RReservationOwnerNearby(TileIndex tile, Track track);
Train *GetR3RWholeTileReservationOwnerNearby(TileIndex tile);

/**
 * R3R (KI-211, fenced in KI-211c/211d): collect every tile the given train's
 * *own* reservation passes through (see pbs.cpp).
 *
 * The walk starts at every vehicle of the chain whose tile it shares with no
 * other train, leaves in *both* directions, and stops at the first tile that has
 * a train on it. Four consequences, all of them needed by the chain-edit purge
 * that uses this:
 *
 * - Direction. A merged chain can hold a dead route booked by a rear segment in
 *   that segment's own facing direction, i.e. *behind* the merged chain's moving
 *   front. Walking only forward from the front would never see it, so the purge
 *   could never free it (KI-211 missed exactly those bits).
 * - Containment. A reservation can only ever be joined to another one *at a
 *   train*, never through it, so stopping at the first train keeps the collected
 *   set inside this chain's own booking. Without that fence, a consist parked
 *   with its nose against a neighbour's fresh route collects the neighbour's
 *   bits as its own and the purge frees a live route (KI-211c, log: the waiter on
 *   33,9 swept 34,9 .. 38,10 of the locomotive standing next to it).
 * - Shared tiles. The fence is blind to a neighbour that stands on the *starting*
 *   tile of the walk, which is not only possible but the normal state right after
 *   a decouple (the locomotive stands on the tile it just left). No walk is made
 *   from such a vehicle: the bits leaving a two-train tile cannot be attributed
 *   (KI-211d, field log: veh 24 on 33,9 with the consist's head 6 and 7, its nose
 *   on 34,9 -- the same sweep survived both earlier fixes).
 * - Ownership. The collected set is still only half the answer: a stale bit of
 *   the neighbour may equally be reachable from here, so callers pair this with
 *   the nearest-train lookups above before freeing anything.
 *
 * The trackdir is reported with the tile on purpose: on a crossing (two tracks
 * sharing a tile) another train's route may legitimately use the *other* track
 * of a tile this chain also passes through, so a caller that matches by tile
 * alone would still free the neighbour's bit. Callers match (tile, track).
 *
 * @param v The train whose reservation is walked.
 * @param handler Called once per tile/trackdir on the path.
 * @param ctx Opaque pointer passed through to the handler.
 */
typedef void (*R3RResvTileHandler)(TileIndex tile, Trackdir trackdir, void *ctx);
void R3RCollectReservedPathTiles(const Train *v, R3RResvTileHandler handler, void *ctx);

/**
 * R3R (KI-207): release every plain-rail reservation bit -- and, since KI-207c,
 * every station/waypoint whole-tile reservation -- that no train can be
 * attributed to (see pbs.cpp). Returns the number of released reservations.
 */
uint R3RPurgeStrayReservations();
CommandCost CheckTrainReservationPreventsTrackModification(TileIndex tile, Track track);
CommandCost CheckTrainReservationPreventsTrackModification(const Train *v);
CommandCost CheckTrainInTunnelBridgePreventsTrackModification(TileIndex start, TileIndex end);

/**
 * Check whether some of tracks is reserved on a tile.
 *
 * @param tile the tile
 * @param tracks the tracks to test
 * @return true if at least on of tracks is reserved
 */
inline bool HasReservedTracks(TileIndex tile, TrackBits tracks)
{
	return (GetReservedTrackbits(tile) & tracks) != TRACK_BIT_NONE;
}

#endif /* PBS_H */
