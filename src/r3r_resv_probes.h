/*
 * R3R (KI-207, 2026-09-25): reservation-change probes.
 *
 * A "ghost reservation" is a reservation bit no train will ever drive over -- it
 * is left behind by a chain edit (couple / decouple / segment flip / depot drag).
 * To find the *origin* of one, every write to a reservation field must be logged
 * with the phase the game was in and the train that was acting.
 *
 * Rather than instrumenting the ~20 call sites of the reservation setters (and
 * missing the ones added later), the probes sit inside the six map accessors that
 * own the reservation bits -- the setters below are the ONLY writers of those
 * bits, so the trace is complete by construction:
 *
 *   SetTrackReservation()               plain rail           (rail_map.h)
 *   SetDepotReservation()               rail depot           (rail_map.h)
 *   SetCrossingReservation()            level crossing       (road_map.h)
 *   SetRailStationReservation()         rail station/waypoint (station_map.h)
 *   SetTunnelReservation()              tunnel               (tunnel_map.h)
 *   SetBridgeReservationTrackBits()     bridge head          (bridge_map.h)
 *
 * Each of them reports old-vs-new through R3RResvWatchLog(), which drops the
 * event unless the tile is on the watch list (see pbs.cpp) -- so the log stays
 * free of filler and the probe costs one comparison when nothing is watched.
 *
 * Boundary: a whole-tile rebuild (MakeRailNormal/MakeRailDepot/MakeBridgeRamp/
 * MakeStation/...) overwrites the map bytes directly and does not go through the
 * setters. Such a change is not a "reservation" event but a tile-type change, and
 * the reservation audit (R3RReservationAudit) reports the current type of every
 * watched tile, so it is still visible.
 *
 * With R3R_PROBES == 0 nothing of this exists in the binary; the call sites below
 * are compiled out, and the no-op overload keeps the header self-consistent.
 */

#ifndef R3R_RESV_PROBES_H
#define R3R_RESV_PROBES_H

#include "r3r_perf.h"
#include "tile_type.h"
#include "track_type.h"

#if R3R_PROBES
/**
 * Log one reservation state change on a watched tile, tagged with the phase the
 * game is in and the acting train (see R3RResvPhaseGuard in pbs.h).
 *
 * @param kind  what changed, e.g. "SET-RAIL" / "CLEAR-STN" / "CHG-BRIDGE".
 * @param tile  the tile whose reservation field changed.
 * @param track the track that was added or removed, INVALID_TRACK if the caller
 *              only knows the whole-tile state (bool-valued setters).
 */
void R3RResvWatchLog(const char *kind, TileIndex tile, Track track = INVALID_TRACK);
#else
inline void R3RResvWatchLog(const char *, TileIndex, Track = INVALID_TRACK) {}
#endif

#endif /* R3R_RESV_PROBES_H */
