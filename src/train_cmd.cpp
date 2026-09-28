/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_cmd.cpp Handling of trains. */

#include "stdafx.h"
#include <unordered_map>
#include "r3r_perf.h"
#include "error.h"
#include "articulated_vehicles.h"
#include "command_func.h"
#include "couple_score.h"
/* R3R (KI-73): `R3RSettleLoadingBeforeChainEdit()` does `delete v->cargo_payment`
 * on the fallback path. Without the complete type the compiler silently skips
 * `~CargoPayment()` (MSVC C4150), i.e. neither the pool slot is released nor
 * `front->cargo_payment` cleared, and the money booking in the destructor never
 * runs. Pull the real definition in. */
#include "economy_base.h"
#include "pathfinder/yapf/yapf.hpp"
#include "news_func.h"
#include "company_func.h"
#include "newgrf_sound.h"
#include "newgrf_text.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "ai/ai.hpp"
#include "game/game.hpp"
#include "newgrf_station.h"
#include "effectvehicle_func.h"
#include "network/network.h"
#include "core/random_func.hpp"
#include "company_base.h"
#include "newgrf.h"
#include "infrastructure_func.h"
#include "order_backup.h"
#include "zoom_func.h"
#include "newgrf_debug.h"
#include "framerate_type.h"
#include "tracerestrict.h"
#include "tbtr_template_vehicle_func.h"
#include "autoreplace_func.h"
#include "engine_func.h"
#include "bridge_signal_map.h"
#include "scope_info.h"
#include "scope.h"
#include "core/checksum_func.hpp"
#include "debug_dbg_assert.h"
#include "debug_settings.h"
#include "train_settings.h"
#include "train_speed_adaptation.h"
#include "event_logs.h"
#include "misc_cmd.h"
#include "tile_cmd.h"
#include "couple_group.h"
#include "train_cmd.h"
#include "vehicle_cmd.h"
#include "tbtr_template_vehicle_cmd.h"
#include "3rdparty/cpp-btree/btree_map.h"

#include "table/strings.h"
#include "table/train_cmd.h"
#include "date_func.h"

#include "safeguards.h"

extern btree::btree_multimap<VehicleID, PendingSpeedRestrictionChange> _pending_speed_restriction_change_map;

enum {
	REALISTIC_BRAKING_MIN_SPEED = 5,
};

enum ChooseTrainTrackLookAheadStateFlags {
	CTTLASF_STOP_FOUND       = 0,         ///< Stopping destination found
	CTTLASF_REVERSE_FOUND    = 1,         ///< Reverse destination found
	CTTLASF_NO_RES_VEH_TILE  = 2,         ///< Do not reserve the vehicle tile
};

struct ChooseTrainTrackLookAheadState {
	uint          order_items_start = 0;  ///< Order items start for VehicleOrderSaver
	uint16_t      flags = 0;              ///< Flags
	DestinationID reverse_dest = 0;       ///< Reverse station ID when CTTLASF_REVERSE_FOUND is set
};

/** Flags for ChooseTrainTrack */
enum ChooseTrainTrackFlags {
	CTTF_NONE                   = 0,      ///< No flags
	CTTF_FORCE_RES              = 0x01,   ///< Force a reservation to be made
	CTTF_MARK_STUCK             = 0x02,   ///< The train has to be marked as stuck when needed
	CTTF_NON_LOOKAHEAD          = 0x04,   ///< Any lookahead should not be used, if necessary reset the lookahead state
	CTTF_NO_LOOKAHEAD_VALIDATE  = 0x08,   ///< Don't validate the lookahead state as it has already been done
};
DECLARE_ENUM_AS_BIT_SET(ChooseTrainTrackFlags)

/** Result flags for ChooseTrainTrack */
enum ChooseTrainTrackResultFlags {
	CTTRF_NONE                  = 0,      ///< No flags
	CTTRF_RESERVATION_MADE      = 0x01,   ///< A reservation was made
	CTTRF_REVERSE_AT_SIGNAL     = 0x02,   ///< Reverse at signal
};
DECLARE_ENUM_AS_BIT_SET(ChooseTrainTrackResultFlags)

struct ChooseTrainTrackResult {
	Track track;
	ChooseTrainTrackResultFlags ctt_flags;
};

btree::btree_map<SignalSpeedKey, SignalSpeedValue> _signal_speeds;

static void TryLongReserveChooseTrainTrackFromReservationEnd(Train *v, bool no_reserve_vehicle_tile = false);
static ChooseTrainTrackResult ChooseTrainTrack(Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, ChooseTrainTrackFlags flags, ChooseTrainTrackLookAheadState lookahead_state = {});
static bool TrainApproachingLineEnd(Train *moving_front, bool signal, bool reverse);
static bool TrainCheckIfLineEnds(Train *moving_front, bool reverse = true);
static bool TrainCanLeaveTile(const Train *moving_front);
static inline bool CheckCompatibleRail(const Train *v, TileIndex tile, DiagDirection enterdir, bool check_railtype);
int ReversingDistanceTargetSpeed(const Train *v);
bool TrainController(Train *v, Vehicle *nomove, bool reverse = true); // Also used in vehicle_sl.cpp.
static TileIndex TrainApproachingCrossingTile(const Train *v);
static void CheckIfTrainNeedsService(Train *v);
static void CheckNextTrainTile(Train *moving_front);
extern TileIndex VehiclePosTraceRestrictPreviousSignalCallback(const Train *v, const void *, TraceRestrictPBSEntrySignalAuxField mode);
static void TrainEnterStation(Train *v, StationID station);
static void UnreserveBridgeTunnelTile(TileIndex tile);
static bool CheckTrainStayInWormHolePathReserve(Train *consist, Train *moving_front, TileIndex tile);

static constexpr uint16_t SPEED_ADAPTATION_MIN_SPEED = 50;

/** Return the scaled date ticks by which the speed restriction
 *  at the current position of the train is going to be invalid */
static StateTicks GetSpeedRestrictionTimeout(const Train *t)
{
	if (t->cur_speed < SPEED_ADAPTATION_MIN_SPEED) return _state_ticks;
	const int64_t look_ahead_distance = Clamp(t->cur_speed / 16, 4, 12); // In tiles, varying between 4 and 12 depending on current speed

	/* This assumes travel along the X or Y map axis, not diagonally. See GetAdvanceDistance, GetAdvanceSpeed. */
	const int64_t ticks_per_tile = (192 * 16 * 4 / 3) / t->cur_speed;

	const int64_t ticks = ticks_per_tile * look_ahead_distance;

	return _state_ticks + ticks;
}

/** Removes all speed restrictions from all signals */
void ClearAllSignalSpeedRestrictions()
{
	_signal_speeds.clear();
}

void AdjustAllSignalSpeedRestrictionTickValues(StateTicksDelta delta)
{
	for (auto &it : _signal_speeds) {
		it.second.time_stamp += delta;
	}
}

/** Removes all speed restrictions which have passed their timeout from all signals */
void ClearOutOfDateSignalSpeedRestrictions()
{
	for (auto key_value_pair = _signal_speeds.begin(); key_value_pair != _signal_speeds.end(); ) {
		if (key_value_pair->second.IsOutOfDate()) {
			key_value_pair = _signal_speeds.erase(key_value_pair);
		} else {
			++key_value_pair;
		}
	}
}

inline void ClearLookAheadIfInvalid(Train *v)
{
	if (v->lookahead != nullptr && !ValidateLookAhead(v)) v->lookahead.reset();
}

/** Initial x subtile coordinate of rail vehicles for each direction. */
static constexpr DiagDirectionIndexArray<uint8_t> _vehicle_initial_x_fract{10, 8, 4,  8};
/** Initial y subtile coordinate of rail vehicles for each direction. */
static constexpr DiagDirectionIndexArray<uint8_t> _vehicle_initial_y_fract{ 8, 4, 8, 10};

/** @copydoc IsValidImageIndex */
template <>
bool IsValidImageIndex<VehicleType::Train>(uint8_t image_index)
{
	return image_index < lengthof(_engine_sprite_base);
}


/**
 * Return the cargo weight multiplier to use for a rail vehicle
 * @param cargo Cargo type to get multiplier for
 * @return Cargo weight multiplier
 */
uint8_t FreightWagonMult(CargoType cargo)
{
	if (!CargoSpec::Get(cargo)->is_freight) return 1;
	return _settings_game.vehicle.freight_trains;
}

/** Checks if lengths of all rail vehicles are valid. If not, shows an error message. */
void CheckTrainsLengths()
{
	bool first = true;

	for (const Train *v : Train::IterateFrontOnly()) {
		if (!v->vehstatus.Test(VehState::Crashed) && !v->IsVirtual()) {
			for (const Train *u = v->GetMovingFront(), *w = v->GetMovingNext(); w != nullptr; u = w, w = w->GetMovingNext()) {
				if (u->track != TRACK_BIT_DEPOT) {
					if ((w->track != TRACK_BIT_DEPOT &&
							std::max(abs(u->x_pos - w->x_pos), abs(u->y_pos - w->y_pos)) != u->CalcNextVehicleOffset()) ||
							(w->track == TRACK_BIT_DEPOT && TicksToLeaveDepot(u) <= 0)) {
						ShowErrorMessage(GetEncodedString(STR_BROKEN_VEHICLE_LENGTH, v->index, v->owner), {}, WarningLevel::Critical);

						if (!_networking && first) {
							first = false;
							Command<Commands::Pause>::Post(PauseMode::Error, true);
						}
						/* Break so we warn only once for each train. */
						break;
					}
				}
			}
		}
	}
}

/**
 * Checks the breakdown flags (VehicleRailFlags 9-12) and sets the correct value in the first vehicle of the consist.
 * This function is generally only called to check if a flag may be cleared.
 * @param v the front engine
 * @param flags bitmask of the flags to check.
 */
void CheckBreakdownFlags(Train *v)
{
	dbg_assert(v->IsFrontEngine());
	/* clear the flags we're gonna check first, we'll set them again later (if applicable) */
	v->flags.Reset(VehicleRailFlag::BreakdownBraking);
	v->flags.Reset(VehicleRailFlagsIsBroken);

	for (const Train *w = v; w != nullptr; w = w->Next()) {
		if (v->IsEngine() || w->IsMultiheaded()) {
			if (w->breakdown_ctr == 2) {
				v->flags.Set(VehicleRailFlag::BreakdownBraking);
			} else if (w->breakdown_ctr == 1) {
				switch (w->breakdown_type) {
					case BREAKDOWN_CRITICAL:
					case BREAKDOWN_RV_CRASH:
					case BREAKDOWN_EM_STOP:
						v->flags.Set(VehicleRailFlag::BreakdownStopped);
						break;

					case BREAKDOWN_LOW_SPEED:
						v->flags.Set(VehicleRailFlag::BreakdownSpeed);
						break;

					case BREAKDOWN_LOW_POWER:
						v->flags.Set(VehicleRailFlag::BreakdownPower);
						break;
				}
			}
		}
	}
}

uint16_t GetTrainVehicleMaxSpeed(const Train *u, const RailVehicleInfo &rvi_u, const Train *front)
{
	/* R3R: a de-articulated-group override (the baked snapshot speed) takes
	 * precedence as the base speed; breakdown / accident clamps still apply. */
	const uint16_t base_speed = u->max_speed_override != UINT16_MAX
		? u->max_speed_override
		: GetVehicleProperty(u, PROP_TRAIN_SPEED, rvi_u.max_speed);
	uint16_t speed = base_speed;
	if (u->flags.Test(VehicleRailFlag::NeedRepair) && front->IsFrontEngine()) {
		for (uint i = 0; i < u->critical_breakdown_count; i++) {
			speed = std::min<uint16_t>(speed - (speed / (front->tcache.cached_num_engines + 2)) + 1, speed);
		}
	}

	/* clamp speed to be no less than lower of 5mph and 1/8 of base speed */
	speed = std::max<uint16_t>(speed, std::min<uint16_t>(5, (base_speed + 7) >> 3));

	if (u->flags.Test(VehicleRailFlag::HasHitRoadVehicle) && front->IsFrontEngine()) {
		speed = std::min<uint16_t>(speed, 30);
	}
	return speed;
}

/**
 * Recalculates the cached stuff of a train. Should be called each time a vehicle is added
 * to/removed from the chain, and when the game is loaded.
 * Note: this needs to be called too for 'wagon chains' (in the depot, without an engine)
 * @param allowed_changes Stuff that is allowed to change.
 */
void Train::ConsistChanged(ConsistChangeFlags allowed_changes)
{
	uint16_t max_speed = UINT16_MAX;

	dbg_assert(this->IsFrontEngine() || this->IsFreeWagon() || this->IsFrontWagon());

	const RailVehicleInfo *rvi_v = RailVehInfo(this->engine_type);
	EngineID first_engine = this->IsFrontEngine() ? this->engine_type : EngineID::Invalid();
	this->gcache.cached_total_length = 0;
	this->compatible_railtypes = {};
	this->tcache.cached_num_engines = 0;

	bool train_can_tilt = true;
	bool speed_varies_by_railtype = false;
	int16_t min_curve_speed_mod = INT16_MAX;

	const bool driving_backwards = this->vehicle_flags.Test(VehicleFlag::DrivingBackwards);

	Direction normalised_direction = Direction::Invalid;
	if (allowed_changes.Test(ConsistChangeFlag::DepotDirection) && IsRailDepotTile(this->tile)) {
		normalised_direction = DiagDirToDir(GetRailDepotDirection(this->tile));
		if (driving_backwards) normalised_direction = ReverseDir(normalised_direction);
	}

	for (Train *u = this; u != nullptr; u = u->Next()) {
		const RailVehicleInfo *rvi_u = RailVehInfo(u->engine_type);

		u->vehicle_flags.Set(VehicleFlag::DrivingBackwards, driving_backwards);

		/* Set TCF_MOVING_UNIT_START flags according to movement direction. */
		if (driving_backwards) {
			u->tcache.cached_tflags = (u->Next() == nullptr || !u->Next()->IsArticGroupMember()) ? TCF_MOVING_UNIT_START : TCF_NONE;
		} else {
			u->tcache.cached_tflags = !u->IsArticGroupMember() ? TCF_MOVING_UNIT_START : TCF_NONE;
		}

		/* Normalise direction of all train parts. */
		if (allowed_changes.Test(ConsistChangeFlag::DepotDirection) && IsRailDepotTile(this->tile)) {
			u->direction = normalised_direction;
		}

		/* Check the this->first cache. */
		dbg_assert_msg(u->First() == this, "u: {}, this: {}",
				VehicleInfoDumper(u), VehicleInfoDumper(this));

		/* update the 'first engine' */
		u->gcache.first_engine = this == u ? EngineID::Invalid() : first_engine;
		u->railtypes = rvi_u->railtypes;

		if (u->IsEngine()) first_engine = u->engine_type;

		/* Set user defined data to its default value */
		u->tcache.user_def_data = rvi_u->user_def_data;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}

	for (Train *u = this; u != nullptr; u = u->Next()) {
		/* Update user defined data (must be done before other properties) */
		u->tcache.user_def_data = GetVehicleProperty(u, PROP_TRAIN_USER_DATA, u->tcache.user_def_data);
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();

		if (!u->IsArticGroupMember()) {
			if (u->IsEngine() || u->IsMultiheaded()) {
				this->tcache.cached_num_engines++;
			}
		}
	}

	Vehicle *last_vis_effect = this;
	for (Train *u = this; u != nullptr; u = u->Next()) {
		const Engine *e_u = u->GetEngine();
		const RailVehicleInfo &rvi_u = e_u->VehInfo<RailVehicleInfo>();

		if (!e_u->info.misc_flags.Test(EngineMiscFlag::RailTilts)) train_can_tilt = false;
		if (e_u->callbacks_used & SGCU_CB36_SPEED_RAILTYPE) speed_varies_by_railtype = true;
		min_curve_speed_mod = std::min(min_curve_speed_mod, u->GetCurveSpeedModifier());

		/* Cache wagon override sprite group. nullptr is returned if there is none */
		u->tcache.cached_override = GetWagonOverrideSpriteSet(u->engine_type, u->cargo_type, u->gcache.first_engine);

		/* Reset colour map */
		u->colourmap = PAL_NONE;

		/* Update powered-wagon-status and visual effect */
		u->UpdateVisualEffect(true);
		ClrBit(u->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);
		if (!(HasBit(u->vcache.cached_vis_effect, VE_ADVANCED_EFFECT) && GB(u->vcache.cached_vis_effect, 0, VE_ADVANCED_EFFECT) == 0 /* VisualEffectSpawnModel::None */)) last_vis_effect = u;

		if (rvi_v->pow_wag_power != 0 && rvi_u.railveh_type == RailVehicleType::Wagon &&
				UsesWagonOverride(u) && !HasBit(u->vcache.cached_vis_effect, VE_DISABLE_WAGON_POWER)) {
			/* wagon is powered */
			u->flags.Set(VehicleRailFlag::PoweredWagon); // cache 'powered' status
		} else {
			u->flags.Reset(VehicleRailFlag::PoweredWagon);
		}
		if (!u->IsArticGroupMember()) {
			/* Do not count powered wagons for the compatible railtypes, as wagons always
			   have railtype normal */
			if (rvi_u.power > 0) {
				this->compatible_railtypes.Set(GetAllPoweredRailTypes(u->railtypes));
			}

			/* Some electric engines can be allowed to run on normal rail. It happens to all
			 * existing electric engines when elrails are disabled and then re-enabled */
			if (u->flags.Test(VehicleRailFlag::AllowedOnNormalRail)) {
				u->railtypes.Set(RAILTYPE_RAIL);
				u->compatible_railtypes.Set(RAILTYPE_RAIL);
			}

			/* max speed is the minimum of the speed limits of all vehicles in the consist */
			if ((rvi_u.railveh_type != RailVehicleType::Wagon || _settings_game.vehicle.wagon_speed_limits) && !UsesWagonOverride(u)) {
				uint16_t speed = GetTrainVehicleMaxSpeed(u, rvi_u, this);
				if (speed != 0) max_speed = std::min(speed, max_speed);
			}
		}
		uint16_t new_cap = e_u->DetermineCapacity(u);
		if (allowed_changes.Test(ConsistChangeFlag::Capacity)) {
			/* Update vehicle capacity. */
			if (u->cargo_cap > new_cap) u->cargo.Truncate(new_cap);
			u->refit_cap = std::min(new_cap, u->refit_cap);
			u->cargo_cap = new_cap;
		} else {
			/* Verify capacity hasn't changed. */
			if (new_cap != u->cargo_cap) ShowNewGrfVehicleError(u->engine_type, STR_NEWGRF_BROKEN, STR_NEWGRF_BROKEN_CAPACITY, GRFBug::VehCapacity, true);
		}
		u->vcache.cached_cargo_age_period = GetVehicleProperty(u, PROP_TRAIN_CARGO_AGE_PERIOD, e_u->info.cargo_age_period);

		/* check the vehicle length (callback) */
		uint16_t veh_len = CALLBACK_FAILED;
		if (e_u->GetGRF() != nullptr && e_u->GetGRF()->grf_version >= 8) {
			/* Use callback 36 */
			veh_len = GetVehicleProperty(u, PROP_TRAIN_SHORTEN_FACTOR, CALLBACK_FAILED);
			if (veh_len != CALLBACK_FAILED && veh_len >= VEHICLE_LENGTH) {
				ErrorUnknownCallbackResult(e_u->GetGRFID(), CBID_VEHICLE_LENGTH, veh_len);
			}
		} else if (e_u->info.callback_mask.Test(VehicleCallbackMask::Length)) {
			/* Use callback 11 */
			veh_len = GetVehicleCallback(CBID_VEHICLE_LENGTH, 0, 0, u->engine_type, u);
		}
		if (veh_len == CALLBACK_FAILED) veh_len = rvi_u.shorten_factor;
		veh_len = VEHICLE_LENGTH - Clamp(veh_len, 0, VEHICLE_LENGTH - 1);

		if (allowed_changes.Test(ConsistChangeFlag::Length)) {
			/* Update vehicle length. */
			u->gcache.cached_veh_length = veh_len;
		} else {
			/* Verify length hasn't changed. */
			if (veh_len != u->gcache.cached_veh_length) VehicleLengthChanged(u);
		}
		this->gcache.cached_total_length += u->gcache.cached_veh_length;
		this->InvalidateNewGRFCache();
		u->InvalidateNewGRFCache();
	}
	SetBit(last_vis_effect->vcache.cached_veh_flags, VCF_LAST_VISUAL_EFFECT);

	/* store consist weight/max speed in cache */
	this->vcache.cached_max_speed = max_speed;
	this->tcache.cached_tflags |= (train_can_tilt ? TCF_TILT : TCF_NONE) | (speed_varies_by_railtype ? TCF_SPD_RAILTYPE : TCF_NONE);
	this->tcache.cached_curve_speed_mod = min_curve_speed_mod;
	this->tcache.cached_max_curve_speed = this->GetCurveSpeedLimit();

	if (driving_backwards && !this->Last()->CanLeadTrain()) {
		/* R3R DEBUG: capture the exact state that sets the 32 km/h no-cab limit. */
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "NOCAB-SET this=%d db=%d last=%d lastSub=0x%02X lastEng=%d lastLead=%d\n",
						(int)this->index.base(), (int)driving_backwards,
						(int)this->Last()->index.base(), (int)this->Last()->subtype,
						(int)this->Last()->IsEngine(), (int)this->Last()->CanLeadTrain());
				fclose(dbg);
			}
		}
		this->tcache.cached_tflags |= TCF_NO_DRIVING_CAB;
	}

	extern std::array<RailTypes, 3> _railtypes_acceleration_type_masks;
	uint8_t accel_type = 3;
	for (uint8_t i = 0; i < 3; i++) {
		if (_railtypes_acceleration_type_masks[i].All(this->compatible_railtypes)) {
			accel_type = i;
			break;
		}
	}
	this->tcache.SetCachedAccelType(accel_type);

	/* recalculate cached weights and power too (we do this *after* the rest, so it is known which wagons are powered and need extra weight added) */
	this->CargoChanged();

	this->UpdateAcceleration();
	if (this->IsFrontEngine()) {
		if (!HasBit(this->subtype, GVSF_VIRTUAL)) SetWindowDirty(WindowClass::VehicleDetails, this->index);
		InvalidateWindowData(WindowClass::VehicleRefit, this->index, VIWD_CONSIST_CHANGED);
		InvalidateWindowData(WindowClass::VehicleOrders, this->index, VIWD_CONSIST_CHANGED);
		InvalidateNewGRFInspectWindow(GrfSpecFeature::Trains, this->index.base());

		/* If the consist is changed while in a depot, the vehicle view window must be invalidated to update the availability of refitting. */
		InvalidateWindowData(WindowClass::VehicleView, this->index, VIWD_CONSIST_CHANGED);
	}
	if (allowed_changes.Test(ConsistChangeFlag::Length)) {
		for (Train *u = this->Next(); u != nullptr; u = u->Next()) {
			u->vcache.cached_max_speed = 0;
			u->gcache.cached_weight = 0;
			u->gcache.cached_max_te = 0;
			u->gcache.cached_axle_resistance = 0;
			u->gcache.cached_max_track_speed = 0;
			u->gcache.cached_power = 0;
			u->gcache.cached_air_drag = 0;
			u->gcache.cached_total_length = 0;
			u->tcache.cached_num_engines = 0;
			u->tcache.cached_centre_mass = 0;
			u->tcache.cached_braking_length = 0;
			u->tcache.cached_deceleration = 0;
			u->tcache.cached_uncapped_decel = 0;
			u->tcache.cached_curve_speed_mod = 0;
			u->tcache.cached_max_curve_speed = 0;
		}
	}
}

/**
 * Get the fraction of the vehicle's current tile which is in front of it.
 * This is equal to how many more steps it could travel without having to stop/reverse if it was an end of line.
 *
 * See also wrapper without x_pos, y_pos in train.h
 *
 * @param v              the vehicle to use (not required to be the moving front)
 * @param x_pos          vehicle x position
 * @param y_pos          vehicle y position
 * @return the fraction of the current tile in front of the vehicle
 */
int GetTileMarginInFrontOfTrain(const Train *v, int x_pos, int y_pos)
{
	Direction vdir = v->GetMovingDirection();
	uint8_t rounding = v->IsDrivingBackwards() ? 0 : 1;
	if (IsDiagonalDirection(vdir)) {
		DiagDirection dir = DirToDiagDir(vdir);
		int offset = ((DiagDirToAxis(dir) == Axis::X) ? x_pos : y_pos) & 0xF;
		return ((dir == DiagDirection::SE || dir == DiagDirection::SW) ? TILE_SIZE - 1 - offset : offset) - ((v->gcache.cached_veh_length + rounding) / 2);
	} else {
		/* Calc position within the current tile */
		uint x = x_pos & 0xF;
		uint y = y_pos & 0xF;

		/* for non-diagonal directions, x will be 1, 3, 5, ..., 15 */
		switch (vdir) {
			case Direction::N : x = ~x + ~y + 25; break;
			case Direction::E : x = ~x + y + 9;   break;
			case Direction::S : x = x + y - 7;    break;
			case Direction::W : x = ~y + x + 9;   break;
			default: break;
		}
		x >>= 1; // x is now in range 0 ... 7
		return (TILE_SIZE / 2) - 1 - x - (v->gcache.cached_veh_length + rounding) / 2;
	}
}

/**
 * Get the stop location of (the center) of the front vehicle of a train at
 * a platform of a station.
 *
 * See also wrapper without x_pos, y_pos in train.h
 *
 * @param station_id     the ID of the station where we're stopping
 * @param tile           the tile where the vehicle currently is
 * @param v              the vehicle to get the stop location of
 * @param update_train_state whether the state of the train v may be changed
 * @param station_ahead  'return' the amount of 1/16th tiles in front of the train
 * @param station_length 'return' the station length in 1/16th tiles
 * @return the location, calculated from the begin of the station to stop at.
 */
int GetTrainStopLocation(StationID station_id, TileIndex tile, Train *v, bool update_train_state, int *station_ahead, int *station_length)
{
	Train *front = v->First();
	if (IsRailWaypoint(tile)) {
		*station_ahead = *station_length = TILE_SIZE;
	} else {
		const Station *st = Station::Get(station_id);
		*station_ahead  = st->GetPlatformLength(tile, DirToDiagDir(v->GetMovingDirection())) * TILE_SIZE;
		*station_length = st->GetPlatformLength(tile) * TILE_SIZE;
	}

	/* Default to the middle of the station for stations stops that are not in
	 * the order list like intermediate stations when non-stop is disabled */
	OrderStopLocation osl = OrderStopLocation::Middle;
	if (front->current_order.IsType(OT_GOTO_STATION) && front->current_order.GetDestination() == station_id) {
		osl = front->current_order.GetStopLocation();
	} else if (front->current_order.IsType(OT_LOADING_ADVANCE) && front->current_order.GetDestination() == station_id) {
		osl = OrderStopLocation::Through;
	} else if (front->current_order.IsType(OT_GOTO_WAYPOINT) && front->current_order.GetDestination() == station_id) {
		osl = OrderStopLocation::FarEnd;
	}
	int overhang = front->gcache.cached_total_length - *station_length;
	int adjust = 0;
	if (osl == OrderStopLocation::Through && overhang > 0) {
		for (Train *u = front; u != nullptr; u = u->Next()) {
			/* Passengers may not be through-loaded */
			if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CargoClass::Passengers)) {
				osl = OrderStopLocation::FarEnd;
				break;
			}
		}
	}
	if (osl == OrderStopLocation::Through && overhang > 0) {
		/* The train is longer than the station, and we can run through the station to load/unload */
		bool advance_beyond_platform_end = false;
		if (update_train_state) {
			/* Only advance beyond platform end if there is at least one vehicle with capacity in the active part of the train.
			 * This avoids the entire train being beyond the platform end. */
			for (Train *u = v; u != nullptr; u = u->GetMovingNext()) {
				if (u->cargo_cap != 0) {
					advance_beyond_platform_end = true;
					break;
				}
			}
		}
		for (Train *u = v; u != nullptr; u = u->GetMovingNext()) {
			if (advance_beyond_platform_end && overhang > 0 && !u->flags.Test(VehicleRailFlag::BeyondPlatformEnd) && u->IsMovingUnitStart()) {
				bool skip = true;
				Train *part = u;
				do {
					if (part->cargo_cap != 0) {
						skip = false;
						break;
					}
					part = part->GetMovingNext();
				} while (part != nullptr && !part->IsMovingUnitStart());
				if (skip) {
					/* Skip the whole unit. */
					part = u;
					do {
						part->flags.Set(VehicleRailFlag::BeyondPlatformEnd);
						part = part->GetMovingNext();
					} while (part != nullptr && !part->IsMovingUnitStart());
				}
			}
			if (u->flags.Test(VehicleRailFlag::BeyondPlatformEnd)) {
				overhang -= u->gcache.cached_veh_length;
				adjust += u->gcache.cached_veh_length;
			} else {
				break;
			}
		}
		for (Train *u = v->GetMovingFront(); u != v; u = u->GetMovingNext()) overhang -= u->gcache.cached_veh_length; // only advance until rear of train is in platform
		if (overhang < 0) adjust += overhang;
	} else if (overhang >= 0) {
		/* The train is longer than the station, make it stop at the far end of the platform */
		osl = OrderStopLocation::FarEnd;
	}

	/* The stop location of the FRONT! of the train */
	int stop;
	switch (osl) {
		default: NOT_REACHED();

		case OrderStopLocation::NearEnd:
			stop = front->gcache.cached_total_length;
			break;

		case OrderStopLocation::Middle:
			stop = *station_length - (*station_length - front->gcache.cached_total_length) / 2;
			break;

		case OrderStopLocation::FarEnd:
		case OrderStopLocation::Through:
			stop = *station_length;
			break;
	}

	/* Subtract half the front vehicle length of the train so we get the real
	 * stop location of the train. */
	uint8_t rounding = v->IsDrivingBackwards() ? 2 : 1;
	int result = stop - ((v->gcache.cached_veh_length + rounding) / 2) + adjust;

	if (osl == OrderStopLocation::Through && v != v->GetMovingFront()) {
		/* Check front of train for obstructions */

		Train *moving_front = v->GetMovingFront();

		if (TrainCanLeaveTile(moving_front)) {
			/* Determine the non-diagonal direction in which we will exit this tile */
			DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
			/* Calculate next tile */
			TileIndex next_tile = moving_front->tile + TileOffsByDiagDir(dir);

			/* Determine the track status on the next tile */
			TrackdirBits trackdirbits = GetTileTrackdirBits(next_tile, TRANSPORT_RAIL, 0, ReverseDiagDir(dir)) & DiagdirReachesTrackdirs(dir);

			/* mask unreachable track bits if we are forbidden to do 90deg turns */
			TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
			if (_settings_game.pf.forbid_90_deg) {
				bits &= ~TrackCrossesTracks(FindFirstTrack(moving_front->track));
			}

			if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(front, next_tile, dir, true) || IsRailDepotTile(next_tile) ||
					(KillFirstBit(trackdirbits) == TRACKDIR_BIT_NONE && HasOnewaySignalBlockingTrackdir(next_tile, FindFirstTrackdir(trackdirbits)))) {
				/* next tile is an effective dead end */
				int current_platform_remaining = *station_ahead - TILE_SIZE + GetTileMarginInFrontOfTrain(v);
				uint8_t rounding = v->IsDrivingBackwards() ? 2 : 1;
				int limit = GetTileMarginInFrontOfTrain(moving_front) + (*station_length - current_platform_remaining) - ((v->gcache.cached_veh_length + rounding) / 2);
				result = std::min(limit, result);
			}
		}
	}

	return result;
}


/**
 * Computes train speed limit caused by curves
 * @return imposed speed limit
 */
uint16_t Train::GetCurveSpeedLimit() const
{
	dbg_assert(this->First() == this);

	static const int absolute_max_speed = UINT16_MAX;
	int max_speed = absolute_max_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) return max_speed;

	int curvecount[2] = {0, 0};

	/* first find the curve speed limit */
	int numcurve = 0;
	int sum = 0;
	int pos = 0;
	int lastpos = -1;
	for (const Train *u = this; u->Next() != nullptr; u = u->Next(), pos += u->gcache.cached_veh_length) {
		Direction this_dir = u->direction;
		Direction next_dir = u->Next()->direction;

		DirDiff dirdiff = DirDifference(this_dir, next_dir);
		if (dirdiff == DirDiff::Same) continue;

		if (dirdiff == DirDiff::Left45) curvecount[0]++;
		if (dirdiff == DirDiff::Right45) curvecount[1]++;
		if (dirdiff == DirDiff::Left45 || dirdiff == DirDiff::Right45) {
			if (lastpos != -1) {
				numcurve++;
				sum += pos - lastpos;
				if (pos - lastpos <= static_cast<int>(VEHICLE_LENGTH) && max_speed > 88) {
					max_speed = 88;
				}
			}
			lastpos = pos;
		}

		/* if we have a 90 degree turn, fix the speed limit to 60 */
		if (dirdiff == DirDiff::Left90 || dirdiff == DirDiff::Right90) {
			max_speed = 61;
		}
	}

	if (numcurve > 0 && max_speed > 88) {
		if (curvecount[0] == 1 && curvecount[1] == 1) {
			max_speed = absolute_max_speed;
		} else {
			sum = CeilDiv(sum, VEHICLE_LENGTH);
			sum /= numcurve;
			max_speed = 232 - (13 - Clamp(sum, 1, 12)) * (13 - Clamp(sum, 1, 12));
		}
	}

	if (max_speed != absolute_max_speed) {
		/* Apply the current railtype's curve speed advantage */
		const RailTypeInfo *rti = GetRailTypeInfo(GetRailTypeByTrackBit(this->tile, this->track));
		max_speed += (max_speed / 2) * rti->curve_speed;

		if (this->tcache.cached_tflags & TCF_TILT) {
			/* Apply max_speed bonus of 20% for a tilting train */
			max_speed += max_speed / 5;
		}

		/* Apply max_speed modifier (cached value is fixed-point binary with 8 fractional bits)
		 * and clamp the result to an acceptable range. */
		max_speed += (max_speed * this->tcache.cached_curve_speed_mod) / 256;
		max_speed = Clamp(max_speed, 2, absolute_max_speed);
	}

	return static_cast<uint16_t>(max_speed);
}

void AdvanceOrderIndex(const Vehicle *v, VehicleOrderID &index)
{
	int depth = 0;

	do {
		/* Wrap around. */
		if (index >= v->GetNumOrders()) index = 0;

		const Order *order = v->GetOrder(index);
		dbg_assert(order != nullptr);

		switch (order->GetType()) {
			case OT_GOTO_DEPOT:
				/* Skip service in depot orders when the train doesn't need service. */
				if ((order->GetDepotOrderType().Test(OrderDepotTypeFlag::Service)) && !v->NeedsServicing()) break;
				[[fallthrough]];
			case OT_GOTO_STATION:
			case OT_GOTO_WAYPOINT:
				return;
			case OT_CONDITIONAL: {
				VehicleOrderID next = ProcessConditionalOrder(order, v, PCO_DRY_RUN);
				if (next != INVALID_VEH_ORDER_ID) {
					depth++;
					index = next;
					/* Don't increment next, so no break here. */
					continue;
				}
				break;
			}
			default:
				break;
		}
		/* Don't increment inside the while because otherwise conditional
		 * orders can lead to an infinite loop. */
		++index;
		depth++;
	} while (depth < v->GetNumOrders());

	/* Wrap around. */
	if (index >= v->GetNumOrders()) index = 0;
}

int PredictStationStoppingLocation(const Train *v, const Order *order, int station_length, DestinationID dest)
{
	/* Default to the middle of the station for stations stops that are not in
	 * the order list like intermediate stations when non-stop is disabled */
	OrderStopLocation osl = OrderStopLocation::Middle;
	if (order->IsType(OT_GOTO_STATION) && order->GetDestination() == dest) {
		osl = order->GetStopLocation();
	} else if (order->IsType(OT_LOADING_ADVANCE) && order->GetDestination() == dest) {
		osl = OrderStopLocation::Through;
	} else if (order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == dest) {
		osl = OrderStopLocation::FarEnd;
	}

	int overhang = v->gcache.cached_total_length - station_length;
	int adjust = 0;
	if (osl == OrderStopLocation::Through && overhang > 0) {
		for (const Train *u = v; u != nullptr; u = u->Next()) {
			/* Passengers may not be through-loaded */
			if (u->cargo_cap > 0 && IsCargoInClass(u->cargo_type, CargoClass::Passengers)) {
				osl = OrderStopLocation::FarEnd;
				break;
			}
		}
	}
	if (osl == OrderStopLocation::Through && overhang > 0) {
		/* The train is longer than the station, and we can run through the station to load/unload */
		const Train *moving_front = v->GetMovingFront();

		/* Check whether the train has already reached the platform and set VehicleRailFlag::BeyondPlatformEnd on the front part */
		if (moving_front->flags.Test(VehicleRailFlag::BeyondPlatformEnd)) {
			/* Compute how much of the train should stop beyond the station, using already set flags */
			int beyond = 0;
			for (const Train *u = moving_front; u != nullptr && u->flags.Test(VehicleRailFlag::BeyondPlatformEnd); u = u->GetMovingNext()) {
				beyond += u->gcache.cached_veh_length;
			}
			/* Adjust for the remaining amount of train being less than the station length */
			int overshoot = station_length - std::min(v->gcache.cached_total_length - beyond, station_length);
			adjust = beyond - overshoot;
		} else {
			/* Train hasn't reached the platform yet, or no advancing has occurred, use predictive mode */
			for (const Train *u = moving_front; u != nullptr; u = u->GetMovingNext()) {
				if (overhang > 0 && u->IsMovingUnitStart()) {
					bool skip = true;
					const Train *part = u;
					do {
						if (part->cargo_cap != 0) {
							skip = false;
							break;
						}
						part = part->GetMovingNext();
					} while (part != nullptr && !part->IsMovingUnitStart());
					if (skip) {
						/* Skip the whole unit. */
						part = u;
						do {
							overhang -= part->gcache.cached_veh_length;
							adjust += part->gcache.cached_veh_length;
							part = part->GetMovingNext();
						} while (part != nullptr && !part->IsMovingUnitStart());
						continue;
					}
				}
				break;
			}
			if (overhang < 0) adjust += overhang;
		}
	} else if (overhang >= 0) {
		/* The train is longer than the station, make it stop at the far end of the platform */
		osl = OrderStopLocation::FarEnd;
	}

	int stop;
	switch (osl) {
		default: NOT_REACHED();

		case OrderStopLocation::NearEnd:
			stop = v->gcache.cached_total_length;
			break;

		case OrderStopLocation::Middle:
			stop = station_length - (station_length - v->gcache.cached_total_length) / 2;
			break;

		case OrderStopLocation::FarEnd:
		case OrderStopLocation::Through:
			stop = station_length;
			break;
	}
	if (v->IsDrivingBackwards()) adjust--;
	return stop + adjust;
}

TrainDecelerationStats::TrainDecelerationStats(const Train *t, int z_pos)
{
	this->deceleration_x2 = 2 * t->tcache.cached_deceleration;
	this->uncapped_deceleration_x2 = 2 * t->tcache.cached_uncapped_decel;
	this->z_pos = z_pos;
	this->t = t;
}

static int64_t GetRealisticBrakingDistanceForSpeed(const TrainDecelerationStats &stats, int start_speed, int end_speed, int z_delta)
{
	/* v^2 = u^2 + 2as */

	auto sqr = [](int64_t speed) -> int64_t { return speed * speed; };

	int64_t ke_delta = sqr(start_speed) - sqr(end_speed);

	int64_t dist = ke_delta / stats.deceleration_x2;

	if (z_delta < 0 && _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
		/* descending */
		/* (5/18) is due to KE being in km/h derived units instead of m/s */
		int64_t slope_dist = (ke_delta - (z_delta * ((400 * 5) / 18) * _settings_game.vehicle.train_slope_steepness)) / stats.uncapped_deceleration_x2;
		dist = std::max<int64_t>(dist, slope_dist);
	}
	return dist;
}

static int GetRealisticBrakingSpeedForDistance(const TrainDecelerationStats &stats, int distance, int end_speed, int z_delta)
{
	/* v^2 = u^2 + 2as */

	auto sqr = [](int64_t speed) -> int64_t { return speed * speed; };

	int64_t target_ke = sqr(end_speed);
	int64_t speed_sqr = target_ke + ((int64_t)stats.deceleration_x2 * (int64_t)distance);

	if (speed_sqr <= REALISTIC_BRAKING_MIN_SPEED * REALISTIC_BRAKING_MIN_SPEED) return REALISTIC_BRAKING_MIN_SPEED;

	if (z_delta < 0 && _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) {
		/* descending */
		/* (5/18) is due to KE being in km/h derived units instead of m/s */
		int64_t sloped_ke = target_ke + (z_delta * ((400 * 5) / 18) * _settings_game.vehicle.train_slope_steepness);
		int64_t slope_speed_sqr = sloped_ke + ((int64_t)stats.uncapped_deceleration_x2 * (int64_t)distance);
		if (slope_speed_sqr < speed_sqr &&
				_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && stats.t->GetAccelerationType() != VehicleAccelerationModel::Maglev) {
			/* calculate speed at which braking would be sufficient */

			uint weight = stats.t->gcache.cached_weight;
			int64_t power_w = (stats.t->gcache.cached_power * 746ll) + (stats.t->tcache.cached_braking_length * (int64_t)RBC_BRAKE_POWER_PER_LENGTH);
			int64_t min_braking_force = (stats.t->tcache.cached_braking_length * (int64_t)RBC_BRAKE_FORCE_PER_LENGTH) + stats.t->gcache.cached_axle_resistance + (weight * 16);

			/* F = (7/8) * (F_min + ((power_w * 18) / (5 * v)))
			 * v^2 = sloped_ke + F * s / (4 * m)
			 * let k = sloped_ke + ((7 * F_min * s) / (8 * 4 * m))
			 * v^3 - k * v - (7 * 18 * power_w * s) / (5 * 8 * 4 * m) = 0
			 * v^3 + p * v + q = 0
			 *   where: p = -k
			 *          q = -(7 * 18 * power_w * s) / (5 * 8 * 4 * m)
			 *
			 * v = cbrt(-q / 2 + sqrt((q^2 / 4) - (k^3 / 27))) + cbrt(-q / 2 - sqrt((q^2 / 4) - (k^3 / 27)))
			 * let r = - q / 2 = (7 * 9 * power_w * s) / (5 * 8 * 4 * m)
			 * let l = k / 3
			 * v = cbrt(r + sqrt(r^2 - l^3)) + cbrt(r - sqrt(r^2 - l^3))
			 */
			int64_t l = (sloped_ke + ((7 * min_braking_force * (int64_t)distance) / (8 * weight))) / 3;
			int64_t r = (7 * 9 * power_w * (int64_t)distance) / (160 * weight);
			int64_t sqrt_factor = (r * r) - (l * l * l);
			if (sqrt_factor >= 0) {
				int64_t part = (int64_t)IntSqrt64(sqrt_factor);
				int32_t v_calc = IntCbrt(r + part);
				int cb2 = r - part;
				if (cb2 > 0) {
					v_calc += IntCbrt(cb2);
				} else if (cb2 < 0) {
					v_calc -= IntCbrt(-cb2);
				}
				int64_t v_calc_sq = sqr(v_calc);
				if (v_calc_sq < speed_sqr && v_calc_sq > slope_speed_sqr) {
					return std::max((int)REALISTIC_BRAKING_MIN_SPEED, v_calc);
				}
			}
		}
		speed_sqr = std::min<int64_t>(speed_sqr, slope_speed_sqr);
	}
	if (speed_sqr <= REALISTIC_BRAKING_MIN_SPEED * REALISTIC_BRAKING_MIN_SPEED) return REALISTIC_BRAKING_MIN_SPEED;
	if (speed_sqr > UINT_MAX) speed_sqr = UINT_MAX;

	return IntSqrt((uint) speed_sqr);
}

void LimitSpeedFromLookAhead(int &max_speed, const TrainDecelerationStats &stats, int current_position, int position, int end_speed, int z_delta)
{
	if (position <= current_position) {
		max_speed = std::min(max_speed, std::max(15, end_speed));
	} else if (end_speed < max_speed) {
		int64_t distance = GetRealisticBrakingDistanceForSpeed(stats, max_speed, end_speed, z_delta);
		if (distance + current_position > position) {
			/* Speed is too fast, we would overshoot */
			if (z_delta < 0 && (position - current_position) < stats.t->gcache.cached_total_length) {
				int effective_length = std::min<int>(stats.t->gcache.cached_total_length, stats.t->tcache.cached_centre_mass * 2);
				if ((position - current_position) < effective_length) {
					/* Reduce z delta near target to compensate for target z not taking into account that z varies across the whole train */
					z_delta = (z_delta * (position - current_position)) / effective_length;
				}
			}
			max_speed = std::min(max_speed, GetRealisticBrakingSpeedForDistance(stats, position - current_position, end_speed, z_delta));
		}
	}
}

static void ApplyLookAheadItem(const Train *v, const TrainReservationLookAheadItem &item, int &max_speed, int &advisory_max_speed,
		VehicleOrderID &current_order_index, const Order *&order, StationID &last_station_visited, const TrainDecelerationStats &stats, int current_position)
{
	auto limit_speed = [&](int position, int end_speed, int z) {
		LimitSpeedFromLookAhead(max_speed, stats, current_position, position, end_speed, z - stats.z_pos);
		advisory_max_speed = std::min(advisory_max_speed, max_speed);
	};
	auto limit_advisory_speed = [&](int position, int end_speed, int z) {
		LimitSpeedFromLookAhead(advisory_max_speed, stats, current_position, position, end_speed, z - stats.z_pos);
	};

	switch (item.type) {
		case TRLIT_STATION: {
			const StationID st = static_cast<StationID>(item.data_id);
			if (order->ShouldStopAtStation(last_station_visited, st, Waypoint::GetIfValid(st) != nullptr)) {
				limit_advisory_speed(item.start + PredictStationStoppingLocation(v, order, item.end - item.start, st), 0, item.z_pos);
				last_station_visited = st;
			} else if (order->IsType(OT_GOTO_WAYPOINT) && order->GetDestination() == st && order->GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) {
				limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos);
				if (order->IsWaitTimetabled()) last_station_visited = st;
			}
			if (order->IsBaseStationOrder() && order->GetDestination() == st && v->GetNumOrders() > 0) {
				current_order_index++;
				AdvanceOrderIndex(v, current_order_index);
				order = v->GetOrder(current_order_index);
				uint16_t order_max_speed = order->GetMaxSpeed();
				if (order_max_speed < UINT16_MAX) limit_advisory_speed(item.start, order_max_speed, item.z_pos);
			}
			break;
		}

		case TRLIT_REVERSE:
			limit_advisory_speed(item.start + v->gcache.cached_total_length, 0, item.z_pos);
			break;

		case TRLIT_TRACK_SPEED:
			limit_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SPEED_RESTRICTION:
			if (item.data_id > 0) limit_advisory_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SIGNAL:
			if (_settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
					(v->lookahead->lookahead_end_position == item.start || v->lookahead->lookahead_end_position == item.start + 1)) {
				limit_advisory_speed(item.start, 0, item.z_pos);
			}
			break;

		case TRLIT_CURVE_SPEED:
			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) limit_speed(item.start, item.data_id, item.z_pos);
			break;

		case TRLIT_SPEED_ADAPTATION:
			break;
	}
}

static void AdvanceLookAheadPosition(Train *v)
{
	v->lookahead->current_position++;
	if (v->lookahead->zpos_refresh_remaining > 0) v->lookahead->zpos_refresh_remaining--;

	if (v->lookahead->current_position > v->lookahead->reservation_end_position + 8 && v->track != TRACK_BIT_DEPOT) {
		/* Beyond end of lookahead, delete it, it will be recreated later with a new reservation */
		v->lookahead.reset();
		return;
	}

	if (unlikely(v->lookahead->current_position >= (1 << 30))) {
		/* Prevent signed overflow by rebasing all position values */
		const int32_t old_position = v->lookahead->current_position;
		v->lookahead->current_position = 0;
		v->lookahead->reservation_end_position -= old_position;
		v->lookahead->lookahead_end_position -= old_position;
		v->lookahead->next_extend_position -= old_position;
		for (TrainReservationLookAheadItem &item : v->lookahead->items) {
			item.start -= old_position;
			item.end -= old_position;
		}
		for (TrainReservationLookAheadCurve &curve : v->lookahead->curves) {
			curve.position -= old_position;
		}
	}

	while (!v->lookahead->items.empty() && v->lookahead->items.front().end < v->lookahead->current_position) {
		if (v->lookahead->items.front().type == TRLIT_STATION) {
			int trim_position = v->lookahead->current_position - 4;
			for (const Train *u = v->GetMovingFront(); u != nullptr; u = u->GetMovingNext()) {
				if (u->flags.Test(VehicleRailFlag::BeyondPlatformEnd)) {
					trim_position -= u->gcache.cached_veh_length;
				} else {
					break;
				}
			}
			if (v->lookahead->items.front().end >= trim_position) break;
		}
		v->lookahead->items.pop_front();
	}

	if (v->lookahead->current_position == v->lookahead->next_extend_position) {
		SetTrainReservationLookaheadEnd(v);

		/* This may clear the lookahead if it has become invalid */
		TryLongReserveChooseTrainTrackFromReservationEnd(v, true);
		if (v->lookahead == nullptr) return;

		v->lookahead->SetNextExtendPositionIfUnset();
	}
}

/**
 * Calculates the maximum speed information of the vehicle under its current conditions.
 * @return Maximum speed information of the vehicle.
 */
Train::MaxSpeedInfo Train::GetCurrentMaxSpeedInfoInternal(bool update_state) const
{
	int max_speed = _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL ?
			this->gcache.cached_max_track_speed :
			std::min<int>(this->tcache.cached_max_curve_speed, this->gcache.cached_max_track_speed);

	if (this->current_order.IsType(OT_LOADING_ADVANCE)) max_speed = std::min<int>(max_speed, _settings_game.vehicle.through_load_speed_limit);

	/* If the train is going backwards, without a leading cab, restrict its speed. */
	if (this->tcache.cached_tflags & TCF_NO_DRIVING_CAB) {
		/* R3R DEBUG: log the no-cab limit once per driving-backwards state, so a
		 * stale flag (db already 0 but NO_CAB lingering) is distinguishable from
		 * a genuine driving-backwards restriction. */
		{
			static bool db0_logged = false;
			static bool db1_logged = false;
			const bool db = this->IsDrivingBackwards();
			bool &logged = db ? db1_logged : db0_logged;
			if (!logged) {
				logged = true;
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "NOCAB-LIMIT this=%d db=%d spd=%d last=%d lastLead=%d order=%d\n",
							(int)this->index.base(), (int)db, (int)this->cur_speed,
							(int)this->Last()->index.base(), (int)this->Last()->CanLeadTrain(),
							(int)this->current_order.GetType());
					fclose(dbg);
				}
			}
		}
		constexpr int BACKWARDS_NO_CAB_SPEED_LIMIT = 32;
		max_speed = std::min<int>(max_speed, BACKWARDS_NO_CAB_SPEED_LIMIT);
	}

	int advisory_max_speed = max_speed;

	if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC && this->lookahead == nullptr) {
		Train *v_platform = const_cast<Train *>(this->GetStationLoadingVehicle());
		TileIndex platform_tile = v_platform->tile;
		if (HasStationTileRail(platform_tile)) {
			StationID sid = GetStationIndex(platform_tile);
			if (this->current_order.ShouldStopAtStation(this, sid, IsRailWaypoint(platform_tile))) {
				int station_ahead;
				int station_length;
				int stop_at = GetTrainStopLocation(sid, platform_tile, v_platform, update_state, &station_ahead, &station_length);

				/* The distance to go is whatever is still ahead of the train minus the
				 * distance from the train's stop location to the end of the platform */
				int distance_to_go = station_ahead / TILE_SIZE - (station_length - stop_at) / TILE_SIZE;

				if (distance_to_go > 0) {
					if (this->UsingRealisticBraking()) {
						advisory_max_speed = std::min(advisory_max_speed, 15 * distance_to_go);
					} else {
						int st_max_speed = 120;

						int delta_v = this->cur_speed / (distance_to_go + 1);
						if (max_speed > (this->cur_speed - delta_v)) {
							st_max_speed = this->cur_speed - (delta_v / 10);
						}

						st_max_speed = std::max(st_max_speed, 25 * distance_to_go);
						max_speed = std::min(max_speed, st_max_speed);
					}
				}
			}
		}
	}

	if (this->flags.Test(VehicleRailFlag::ConsistSpeedReduction)) {
		const_cast<Train *>(this)->flags.Reset(VehicleRailFlag::ConsistSpeedReduction);
		for (const Train *u = this; u != nullptr; u = u->Next()) {
			if (u->track == TRACK_BIT_DEPOT) {
				const_cast<Train *>(this)->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
				if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC) {
					max_speed = std::min<int>(max_speed, _settings_game.vehicle.rail_depot_speed_limit);
				}
				continue;
			}

			/* Vehicle is on the middle part of a bridge. */
			if (u->track & TRACK_BIT_WORMHOLE && !u->vehstatus.Test(VehState::Hidden)) {
				const_cast<Train *>(this)->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
				max_speed = std::min<int>(max_speed, GetBridgeSpec(GetBridgeType(u->tile))->speed);
			}
		}
	}

	advisory_max_speed = std::min<int>(advisory_max_speed, this->current_order.GetMaxSpeed());
	if (this->flags.Test(VehicleRailFlag::BreakdownSpeed)) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->GetBreakdownSpeed());
	}
	if (this->speed_restriction != 0) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->speed_restriction);
	}
	if (this->signal_speed_restriction != 0 && _settings_game.vehicle.train_speed_adaptation && !this->flags.Test(VehicleRailFlag::SpeedAdaptationExempt)) {
		advisory_max_speed = std::min<int>(advisory_max_speed, this->signal_speed_restriction);
	}
	if (this->reverse_distance >= 1) {
		advisory_max_speed = std::min<int>(advisory_max_speed, ReversingDistanceTargetSpeed(this));
	}

	if (this->UsingRealisticBraking()) {
		if (this->lookahead != nullptr) {
			if (update_state && this->lookahead->zpos_refresh_remaining == 0) {
				this->lookahead->cached_zpos = this->CalculateOverallZPos();
				this->lookahead->zpos_refresh_remaining = this->GetZPosCacheUpdateInterval();
			}
			TrainDecelerationStats stats(this, this->lookahead->cached_zpos);
			if (this->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) {
				LimitSpeedFromLookAhead(max_speed, stats, this->lookahead->current_position, this->lookahead->reservation_end_position - TILE_SIZE,
						_settings_game.vehicle.rail_depot_speed_limit, this->lookahead->reservation_end_z - stats.z_pos);
			} else {
				LimitSpeedFromLookAhead(max_speed, stats, this->lookahead->current_position, this->lookahead->reservation_end_position,
						0, this->lookahead->reservation_end_z - stats.z_pos);
			}
			advisory_max_speed = std::min(advisory_max_speed, max_speed);
			VehicleOrderID current_order_index = this->cur_real_order_index;
			const Order *order = &(this->current_order);
			StationID last_station_visited = this->last_station_visited;
			for (const TrainReservationLookAheadItem &item : this->lookahead->items) {
				ApplyLookAheadItem(this, item, max_speed, advisory_max_speed, current_order_index, order, last_station_visited, stats, this->lookahead->current_position);
			}
			if (this->lookahead->flags.Test(TrainReservationLookAheadFlag::ApplyAdvisory)) {
				max_speed = std::min(max_speed, advisory_max_speed);
			}
		} else {
			advisory_max_speed = std::min(advisory_max_speed, 30);
		}
	}

	return { max_speed, advisory_max_speed };
}

/**
 * Calculates the maximum speed of the vehicle under its current conditions.
 * @return Maximum speed of the vehicle.
 */
int Train::GetCurrentMaxSpeed() const
{
	MaxSpeedInfo info = this->GetCurrentMaxSpeedInfo();
	return std::min(info.strict_max_speed, info.advisory_max_speed);
}

uint32_t Train::CalculateOverallZPos() const
{
	if (likely(HasBit(this->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST))) {
		return this->z_pos;
	} else {
		int64_t sum = 0;
		for (const Train *u = this; u != nullptr; u = u->Next()) {
			sum += ((int)u->z_pos * (int)u->tcache.cached_veh_weight);
		}
		return sum / this->gcache.cached_weight;
	}
}

static bool IsTrainOnNonRealisticBrakingTrack(const Train *t)
{
	extern RailTypes _railtypes_non_realistic_braking;
	if (likely(!t->compatible_railtypes.Any(_railtypes_non_realistic_braking))) return false;
	if (_railtypes_non_realistic_braking.All(t->railtypes)) return true;
	return GetRailTypeInfo(GetRailTypeByTrackBit(t->tile, t->track))->ctrl_flags.Test(RailTypeCtrlFlag::NoRealisticBraking);
}

/** Update acceleration of the train from the cached power and weight. */
void Train::UpdateAcceleration()
{
	dbg_assert(this->IsFrontEngine() || this->IsFreeWagon() || this->IsFrontWagon());

	uint power = this->gcache.cached_power;
	uint weight = this->gcache.cached_weight;
	assert(weight != 0);
	this->acceleration = Clamp(power / weight * 4, 1, 255);

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && !IsTrainOnNonRealisticBrakingTrack(this) && this->IsFrontEngine()) {
		this->tcache.cached_tflags |= TCF_RL_BRAKING;
		switch (_settings_game.vehicle.train_acceleration_model) {
			default: NOT_REACHED();
			case AM_ORIGINAL:
				this->tcache.cached_uncapped_decel = this->tcache.cached_deceleration = Clamp((this->acceleration * 7) / 2, 1, 200);
				this->tcache.cached_braking_length = this->gcache.cached_total_length;
				break;

			case AM_REALISTIC: {
				VehicleAccelerationModel acceleration_type = this->GetAccelerationType();
				bool maglev = (acceleration_type == VehicleAccelerationModel::Maglev);
				int64_t power_w = power * 746ll;

				/* Increase the effective length used for brake force/power value when using the freight weight multiplier */
				uint length = this->gcache.cached_total_length;
				if (_settings_game.vehicle.freight_trains > 1) {
					uint adjust = (_settings_game.vehicle.freight_trains - 1);
					for (const Train *u = this; u != nullptr; u = u->Next()) {
						if (u->cargo_cap > 0 && CargoSpec::Get(u->cargo_type)->is_freight) {
							length += ((u->gcache.cached_veh_length * adjust) + 1) / 2;
						}
					}
					length = Clamp<uint>(length, 0, UINT16_MAX);
				}
				this->tcache.cached_braking_length = length;

				int64_t min_braking_force = (int64_t)length * (int64_t)RBC_BRAKE_FORCE_PER_LENGTH;
				if (!maglev) {
					/* From GroundVehicle::GetAcceleration()
					 * force = power * 18 / (speed * 5);
					 * resistance += (area * this->gcache.cached_air_drag * speed * speed) / 1000;
					 *
					 * let:
					 * F = force + resistance
					 * P = power
					 * v = speed
					 * d = area * this->gcache.cached_air_drag / 1000
					 *
					 * F = (18 P / 5 v) + d v^2
					 * Minimum occurs at d F / d v = 0
					 * This is v^3 = 9 P / 5 d
					 * If d == 0 or v > max v, evaluate at max v
					 */
					int evaluation_speed = this->vcache.cached_max_speed;
					int area = 14;
					int64_t power_b = power_w + ((int64_t)length * RBC_BRAKE_POWER_PER_LENGTH);
					if (this->gcache.cached_air_drag > 0) {
						uint64_t v_3 = 1800 * (uint64_t)power_b / (area * this->gcache.cached_air_drag);
						evaluation_speed = std::min<int>(evaluation_speed, IntCbrt(v_3));
					}
					if (evaluation_speed > 0) {
						min_braking_force += power_b * 18 / (evaluation_speed * 5);
						min_braking_force += (area * this->gcache.cached_air_drag * evaluation_speed * evaluation_speed) / 1000;
					}

					min_braking_force += this->gcache.cached_axle_resistance;
					int rolling_friction = 16; // 16 is the minimum value of v->GetRollingFriction() for a moving vehicle
					min_braking_force += weight * rolling_friction;
				} else {
					/* From GroundVehicle::GetAcceleration()
					 * Braking force does not decrease with speed,
					 * therefore air drag can be omitted.
					 * There is no rolling/axle drag. */
					min_braking_force += power_w / 25;
				}
				min_braking_force -= (min_braking_force >> 3); // Slightly underestimate braking for defensive driving purposes
				this->tcache.cached_uncapped_decel = Clamp(min_braking_force / (weight * 4), 1, UINT16_MAX);
				this->tcache.cached_deceleration = Clamp(this->tcache.cached_uncapped_decel, 1, GetTrainRealisticBrakingTargetDecelerationLimit(acceleration_type));
				break;
			}
		}
	} else {
		this->tcache.cached_tflags &= ~TCF_RL_BRAKING;
		this->tcache.cached_deceleration = 0;
		this->tcache.cached_uncapped_decel = 0;
		this->tcache.cached_braking_length = this->gcache.cached_total_length;
	}

	if (_settings_game.vehicle.improved_breakdowns) {
		if (_settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
			this->breakdown_chance_factor = std::max(128 * 3 / (this->tcache.cached_num_engines + 2), 5);
		}
	}
}

bool Train::ConsistNeedsRepair() const
{
	if (!this->flags.Test(VehicleRailFlag::ConsistBreakdown)) return false;

	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (u->flags.Test(VehicleRailFlag::NeedRepair)) return true;
	}
	return false;
}

/**
 * Get the offset for train image when it is used as cursor.
 * @return The offset in horizontal direction.
 */
int Train::GetCursorImageOffset() const
{
	if (this->gcache.cached_veh_length != 8 && this->flags.Test(VehicleRailFlag::Flipped) && !EngInfo(this->engine_type)->misc_flags.Test(EngineMiscFlag::RailFlips)) {
		int reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;

		const Engine *e = this->GetEngine();
		if (e->GetGRF() != nullptr && IsCustomVehicleSpriteNum(e->VehInfo<RailVehicleInfo>().image_index)) {
			reference_width = e->GetGRF()->traininfo_vehicle_width;
		}

		return ScaleSpriteTrad((this->gcache.cached_veh_length - (int)VEHICLE_LENGTH) * reference_width / (int)VEHICLE_LENGTH);
	}
	return 0;
}

/**
 * Get the width of a train vehicle image in the GUI.
 * @param offset Additional offset for positioning the sprite; set to nullptr if not needed
 * @return Width in pixels
 */
int Train::GetDisplayImageWidth(Point *offset) const
{
	int reference_width = TRAININFO_DEFAULT_VEHICLE_WIDTH;
	int vehicle_pitch = 0;

	const Engine *e = this->GetEngine();
	if (e->GetGRF() != nullptr && IsCustomVehicleSpriteNum(e->VehInfo<RailVehicleInfo>().image_index)) {
		reference_width = e->GetGRF()->traininfo_vehicle_width;
		vehicle_pitch = e->GetGRF()->traininfo_vehicle_pitch;
	}

	if (offset != nullptr) {
		if (this->flags.Test(VehicleRailFlag::Flipped) && !EngInfo(this->engine_type)->misc_flags.Test(EngineMiscFlag::RailFlips)) {
			offset->x = ScaleSpriteTrad(((int)this->gcache.cached_veh_length - (int)VEHICLE_LENGTH / 2) * reference_width / (int)VEHICLE_LENGTH);
		} else {
			offset->x = ScaleSpriteTrad(reference_width) / 2;
		}
		offset->y = ScaleSpriteTrad(vehicle_pitch);
	}
	return ScaleSpriteTrad(this->gcache.cached_veh_length * reference_width / VEHICLE_LENGTH);
}

static SpriteID GetDefaultTrainSprite(uint8_t spritenum, Direction direction)
{
	dbg_assert(IsValidImageIndex<VehicleType::Train>(spritenum));
	return ((to_underlying(direction) + _engine_sprite_add[spritenum]) & _engine_sprite_and[spritenum]) + _engine_sprite_base[spritenum];
}

/**
 * Get the sprite to display the train.
 * @param direction Direction of view/travel.
 * @param image_type Visualisation context.
 * @param result Sprite sequence to add the to be drawn sprites to.
 */
void Train::GetImage(Direction direction, EngineImageType image_type, VehicleSpriteSeq *result) const
{
	uint8_t spritenum = this->spritenum;

	if (this->flags.Test(VehicleRailFlag::Flipped)) direction = ReverseDir(direction);

	if (IsCustomVehicleSpriteNum(spritenum)) {
		if (spritenum == CUSTOM_VEHICLE_SPRITENUM_REVERSED) direction = ReverseDir(direction);
		GetCustomVehicleSprite(this, direction, image_type, result);
		if (result->IsValid()) return;

		spritenum = this->GetEngine()->original_image_index;
	}

	dbg_assert(IsValidImageIndex<VehicleType::Train>(spritenum));
	SpriteID sprite = GetDefaultTrainSprite(spritenum, direction);

	if (this->cargo.StoredCount() >= this->cargo_cap / 2U) sprite += _wagon_full_adder[spritenum];

	result->Set(sprite);
}

static void GetRailIcon(EngineID engine, bool rear_head, int &y, EngineImageType image_type, VehicleSpriteSeq *result)
{
	const Engine *e = Engine::Get(engine);
	Direction dir = rear_head ? Direction::E : Direction::W;
	uint8_t spritenum = e->VehInfo<RailVehicleInfo>().image_index;

	if (IsCustomVehicleSpriteNum(spritenum)) {
		GetCustomVehicleIcon(engine, dir, image_type, result);
		if (result->IsValid()) {
			if (e->GetGRF() != nullptr) {
				y += ScaleSpriteTrad(e->GetGRF()->traininfo_vehicle_pitch);
			}
			return;
		}

		spritenum = Engine::Get(engine)->original_image_index;
	}

	if (rear_head) spritenum++;

	result->Set(GetDefaultTrainSprite(spritenum, Direction::W));
}

void DrawTrainEngine(int left, int right, int preferred_x, int y, EngineID engine, PaletteID pal, EngineImageType image_type)
{
	const GRFFile *grf = Engine::Get(engine)->GetGRF();
	int vehicle_width = ScaleSpriteTrad(grf == nullptr ? TRAININFO_DEFAULT_VEHICLE_WIDTH : grf->traininfo_vehicle_width);

	if (RailVehInfo(engine)->railveh_type == RailVehicleType::Multihead) {
		int yf = y;
		int yr = y;

		VehicleSpriteSeq seqf, seqr;
		GetRailIcon(engine, false, yf, image_type, &seqf);
		GetRailIcon(engine, true, yr, image_type, &seqr);

		Rect16 rectf = seqf.GetBounds();
		Rect16 rectr = seqr.GetBounds();

		preferred_x = SoftClamp(preferred_x,
				left - UnScaleGUI(rectf.left) + vehicle_width / 2,
				right - UnScaleGUI(rectr.right) - (vehicle_width - vehicle_width / 2));

		seqf.Draw(preferred_x - vehicle_width / 2, yf, pal, pal == PALETTE_CRASH);
		seqr.Draw(preferred_x + (vehicle_width - vehicle_width / 2), yr, pal, pal == PALETTE_CRASH);
	} else {
		VehicleSpriteSeq seq;
		GetRailIcon(engine, false, y, image_type, &seq);

		Rect16 rect = seq.GetBounds();
		preferred_x = SoftClamp(preferred_x,
				left - UnScaleGUI(rect.left),
				right - UnScaleGUI(rect.right));

		seq.Draw(preferred_x, y, pal, pal == PALETTE_CRASH);
	}
}

/**
 * Get the size of the sprite of a train sprite heading west, or both heads (used for lists).
 * @param engine The engine to get the sprite from.
 * @param[out] width The width of the sprite.
 * @param[out] height The height of the sprite.
 * @param[out] xoffs Number of pixels to shift the sprite to the right.
 * @param[out] yoffs Number of pixels to shift the sprite downwards.
 * @param image_type Context the sprite is used in.
 */
void GetTrainSpriteSize(EngineID engine, uint &width, uint &height, int &xoffs, int &yoffs, EngineImageType image_type)
{
	int y = 0;

	VehicleSpriteSeq seq;
	GetRailIcon(engine, false, y, image_type, &seq);

	Rect rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

	width  = UnScaleGUI(rect.Width());
	height = UnScaleGUI(rect.Height());
	xoffs  = UnScaleGUI(rect.left);
	yoffs  = UnScaleGUI(rect.top);

	if (RailVehInfo(engine)->railveh_type == RailVehicleType::Multihead) {
		const GRFFile *grf = Engine::Get(engine)->GetGRF();
		int vehicle_width = ScaleSpriteTrad(grf == nullptr ? TRAININFO_DEFAULT_VEHICLE_WIDTH : grf->traininfo_vehicle_width);

		GetRailIcon(engine, true, y, image_type, &seq);
		rect = ConvertRect<Rect16, Rect>(seq.GetBounds());

		/* Calculate values relative to an imaginary center between the two sprites. */
		width = vehicle_width + UnScaleGUI(rect.right) - xoffs;
		height = std::max<uint>(height, UnScaleGUI(rect.Height()));
		xoffs  = xoffs - vehicle_width / 2;
		yoffs  = std::min(yoffs, UnScaleGUI(rect.top));
	}
}

/**
 * Build a railroad wagon.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
static CommandCost CmdBuildRailWagon(TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **ret)
{
	const RailVehicleInfo *rvi = &e->VehInfo<RailVehicleInfo>();

	/* Check that the wagon can drive on the track in question */
	if (!IsCompatibleRail(rvi->railtypes, GetRailType(tile))) return CommandCost(STR_ERROR_DEPOT_HAS_WRONG_RAIL_TYPE);

	if (flags.Test(DoCommandFlag::Execute)) {
		Train *v = Train::Create();
		*ret = v;
		v->spritenum = rvi->image_index;

		v->engine_type = e->index;
		v->gcache.first_engine = EngineID::Invalid(); // needs to be set before first callback

		DiagDirection dir = GetRailDepotDirection(tile);

		v->direction = DiagDirToDir(dir);
		v->tile = tile;

		int x = TileX(tile) * TILE_SIZE | _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE | _vehicle_initial_y_fract[dir];

		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y, true);
		v->owner = _current_company;
		v->track = TRACK_BIT_DEPOT;
		v->vehstatus = {VehState::Hidden, VehState::DefaultPalette};
		v->reverse_distance = 0;
		v->speed_restriction = 0;
		v->signal_speed_restriction = 0;

		v->SetWagon();

		v->SetFreeWagon();
		InvalidateWindowData(WindowClass::VehicleDepot, v->tile.base());

		v->cargo_type = e->GetDefaultCargoType();
		assert(IsValidCargoType(v->cargo_type));
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;

		v->railtypes = rvi->railtypes;

		v->date_of_last_service = EconTime::CurDate();
		v->date_of_last_service_newgrf = CalTime::CurDate();
		v->build_year = CalTime::CurYear();
		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = Random();

		v->group_id = DEFAULT_GROUP;

		auto prob = TestVehicleBuildProbability(v, BuildProbabilityType::Reversed);
		if (prob.has_value()) v->flags.Set(VehicleRailFlag::Flipped, prob.value());
		AddArticulatedParts(v);

		v->UpdatePosition();
		v->First()->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v->First());

		CheckConsistencyOfArticulatedVehicle(v);

		if (!flags.Test(DoCommandFlag::AutoReplace)) {
			/* Try to connect the vehicle to one of free chains of wagons. */
			std::vector<Train *> candidates;
			for (Train *w = Train::From(GetFirstVehicleOnTile(tile, VehicleType::Train)); w != nullptr; w = w->HashTileNext()) {
				if (w->IsFreeWagon() &&                          ///< A free wagon chain
						w->engine_type == e->index &&            ///< Same type
						w->First() != v &&                       ///< Don't connect to ourself
						!w->vehstatus.Test(VehState::Crashed) && ///< Not crashed/flooded
						w->owner == v->owner) {                  ///< Same owner
					candidates.push_back(w);
				}
			}
			std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
				return a->index < b->index;
			});
			for (Train *w : candidates) {
				if (Command<Commands::MoveRailVehicle>::Do(DoCommandFlag::Execute, v->index, w->Last()->index, MoveRailVehicleFlags::MoveChain).Succeeded()) {
					break;
				}
			}
		}

		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

/* R3R (KI-144 / KI-145 / KI-14): edge-triggered probes for the segment fixes.
 * Both call sites sit ahead of R3RDbgEdge() and of the R3RDbgEdgeTag enum (both
 * defined further down in this file), so the emitters are forward declared
 * here. In a probe-free build (R3R_PROBES=0) the macros expand to nothing, so
 * the calls and their arguments disappear entirely and the published exe stays
 * provably probe-free (release audit 2026-09-19). */
#if R3R_PROBES
static void R3RProbeNoAbsorbSeg(const Train *head);
static void R3RProbeNdhSegKeep(const Train *front, const Train *other, bool moved);
static void R3RProbeKi149Seam(const Train *v, bool at_front, const Train *waiting);
#define R3R_PROBE_NOABSORB_SEG(head) R3RProbeNoAbsorbSeg(head)
#define R3R_PROBE_NDH_SEG_KEEP(front, other, moved) R3RProbeNdhSegKeep(front, other, moved)
#define R3R_PROBE_KI149_SEAM(v, at_front, waiting) R3RProbeKi149Seam(v, at_front, waiting)
#else
#define R3R_PROBE_NOABSORB_SEG(head) ((void)0)
#define R3R_PROBE_NDH_SEG_KEEP(front, other, moved) ((void)0)
#define R3R_PROBE_KI149_SEAM(v, at_front, waiting) ((void)0)
#endif

/**
 * R3R: does this chain already contain a segment (a SegmentFront marker)?
 * @param t Any vehicle of the chain.
 * @return true iff the chain contains at least one segment.
 */
static bool R3RChainHasSegment(const Train *t)
{
	for (const Train *v = t; v != nullptr; v = v->Next()) {
		if (v->IsSegmentFront()) return true;
	}
	return false;
}

/**
 * Move all free vehicles in the depot to the train.
 * @param u The train to move the free vehicles to.
 */
void NormalizeTrainVehInDepot(const Train *u, bool include_front_wagon)
{
	assert(u->IsEngine());
	/* R3R: a segment is a complete, self-contained unit -- it was coupled on as
	 * a whole and will be split off as a whole again. It must never silently
	 * swallow the free wagons standing around in the depot: absorbed wagons
	 * would end up inside the segment (between the segment's front marker and
	 * its rear boundary) and could then no longer be moved on their own. */
	if (R3RChainHasSegment(u)) {
		R3R_PROBE_NOABSORB_SEG(u);
		return;
	}
	std::vector<Train *> candidates;
	for (Train *v = Train::From(GetFirstVehicleOnTile(u->tile, VehicleType::Train)); v != nullptr; v = v->HashTileNext()) {
		if ((v->IsFreeWagon() || (include_front_wagon && v->IsFrontWagon())) &&
				v->track == TRACK_BIT_DEPOT &&
				v->owner == u->owner) {
			candidates.push_back(v);
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
		return a->index < b->index;
	});
	for (Train *v : candidates) {
		/* R3: if the grabbed wagon chain is an independent car-only formation, its
		 * schedule belongs to the formation; hand it over to the engine. */
		OrderList *orders_to_move = nullptr;
		if (include_front_wagon && v->orders != nullptr) orders_to_move = v->orders;

		if (Command<Commands::MoveRailVehicle>::Do(DoCommandFlag::Execute, v->index, u->index, MoveRailVehicleFlags::MoveChain).Failed()) {
			break;
		}

		if (orders_to_move != nullptr) {
			DeleteVehicleOrders(const_cast<Train *>(u));
			const_cast<Train *>(u)->orders = orders_to_move;
			v->orders = nullptr;
			const_cast<Train *>(u)->cur_real_order_index = 0;
			const_cast<Train *>(u)->cur_implicit_order_index = 0;
			InvalidateVehicleOrder(const_cast<Train *>(u), 0);
		}
		if (R3RIsCarOnlyFormation(v)) R3RDestroyCarOnlyFormation(v);
	}
}

/* R3R car-only formation helpers (the former "consist group"): a wagon-only
 * chain is promoted into an independent zero-power train front so every OpenTTD
 * subsystem treats it as a normal train (it can hold orders, appears in the
 * vehicle list, etc.), but it has no traction, so it just waits in a
 * depot/station until a locomotive couples onto it. */

/**
 * Turn a wagon-only chain into an independent car-only formation: give its front
 * the (zero-power) locomotive identity WITHOUT replacing its engine, so the real
 * wagon's engine_type (appearance, capacity, stats) is preserved.
 * @param front_wagon First vehicle of the wagon-only chain.
 * @return The formation front (now an engine), or nullptr on failure.
 */
Train *R3RCreateCarOnlyFormation(Train *front_wagon)
{
	if (front_wagon == nullptr) return nullptr;

	/* The formation is recognised by R3RIsCarOnlyFormation() (front engine whose
	 * rail vehicle type is Wagon). Its cached power is 0; CheckTrainStayInDepot
	 * has an exemption so it is not auto-stopped (equivalent to a 1 hp minimum). */
	front_wagon->SetEngine();
	front_wagon->ClearWagon();
	front_wagon->ClearFreeWagon();
	front_wagon->SetFrontEngine();
	front_wagon->ConsistChanged(CCF_ARRANGE);
	return front_wagon;
}

/**
 * Remove the (zero-power locomotive) formation identity from a chain front,
 * turning it back into an ordinary free wagon.
 * @param front The formation front.
 */
void R3RDestroyCarOnlyFormation(Train *front)
{
	if (front == nullptr) return;

	/* The formation stops being an independent train front (it becomes a plain
	 * free wagon / a wagon inside another chain). Its vehicle windows are only
	 * meaningful while it is a primary vehicle, and a VehicleView viewport
	 * follows its window_number (= the vehicle index) unconditionally:
	 * Vehicle::PreDestructor only closes such windows while the vehicle is
	 * still a primary vehicle, so an orphaned window would keep following an
	 * index whose pool slot is later freed and crash the next UI frame in
	 * UpdateNextViewportPosition() (Vehicle::Get() -> nullptr -> 0x30 read).
	 * The Couple() path closes these windows explicitly; do the same here so
	 * every identity-stripping caller is covered. */
	CloseWindowById(WindowClass::VehicleView, front->index);
	CloseWindowById(WindowClass::VehicleOrders, front->index);
	CloseWindowById(WindowClass::VehicleRefit, front->index);
	CloseWindowById(WindowClass::VehicleDetails, front->index);
	CloseWindowById(WindowClass::VehicleTimetable, front->index);

	front->ClearFrontEngine();
	front->ClearEngine();
	front->SetWagon();
	front->SetFreeWagon();
	/* R3R: the segment right-boundary marker goes away together with the formation
	 * identity: this chain is an ordinary free-wagon chain again. Only this
	 * segment's own run is touched (up to the next segment boundary), so the
	 * markers of following segments are left alone. */
	for (Train *w = front; w != nullptr; w = w->Next()) {
		if (w != front && w->IsSegmentFront()) break;
		w->ClearSegmentBack();
	}
	/* NOTE: no ConsistChanged() here! After ArrangeTrains merged the formation
	 * into the locomotive's chain, `front` is no longer a chain head, and
	 * ConsistChanged requires `this` to be the head (assertion at
	 * train_cmd.cpp:325). The caller (Couple) refreshes the merged chain via
	 * NormaliseTrainHead(v) on the locomotive head afterwards. */
}

/**
 * Whether a train front is a car-only formation (front engine whose rail
 * vehicle type is a Wagon: no real traction, waits to be coupled onto).
 * @param v The train front.
 * @return True when it is such a formation.
 */
bool R3RIsCarOnlyFormation(const Train *v)
{
	if (v == nullptr || !v->IsEngine()) return false;
	return RailVehInfo(v->engine_type)->railveh_type == RailVehicleType::Wagon;
}

/**
 * R3R (KI-62): whether a consist may be the destination of an automatic coupling
 * (and may therefore be coupled onto once a locomotive has reached it).
 *
 * All conditions are required:
 *  - the consist is a real segment: its chain head carries the segment-front
 *    marker (★). This is the very test the depot list uses to tell a segment
 *    ("第 k/N 段") from a loose wagon chain ("散链"), and it is what the depot
 *    "upgrade to segment" command and the couple path grant. A loose chain is
 *    never a couple target, no matter which orders it happens to hold.
 *  - the segment holds a WAIT_COUPLE order, i.e. it is actually waiting to be
 *    coupled onto. Being a segment alone is not enough.
 *  - the segment is an *independent* train, i.e. the head of its own chain
 *    (KI-188). A segment front inside a foreign chain -- or one swallowed by an
 *    earlier coupling into our own chain -- is never a candidate: it is not
 *    ticked, so its marker would never be cleared, and it cannot be coupled onto
 *    through the pair flags, which only ever hold chain heads.
 *
 * The couple pathfinder (destination detection and back-walk safety) and the
 * arrival gate in TrainCoupleHandler() both use this, so the target the path was
 * planned for and the coupling that is finally executed agree on what a target
 * is.
 *
 * @param t Chain head of the candidate consist.
 * @return True when it is a segment waiting for a coupling locomotive.
 */
bool R3RIsCoupleTarget(const Train *t)
{
	if (t == nullptr || !t->IsSegmentFront()) return false;
	/* R3R (KI-188, 玩家规则 2026-09-24): a waiting consist is an *independent*
	 * train -- the head of its own chain. A segment front which has been swallowed
	 * by another chain is not a candidate:
	 *  - only the true chain head is ever ticked (see _tick_train_front_cache), so
	 *    such a vehicle's OT_WAIT_COUPLE marker is a leftover of the coupling which
	 *    merged it and would never be cleared by the game itself;
	 *  - it could never be coupled onto anyway: the pair flags store chain heads
	 *    only (R3RPairCoupleTargets() normalises both sides through First()), and
	 *    the pathfinder normalises its destination the same way (yapf_destrail.hpp
	 *    does t = t->First() before asking here).
	 * Accepting it anyway made a *running* train look like an idle consist waiting
	 * on the track, so locomotives locked onto it (CPL-PAIR with the mid-chain
	 * vehicle's index, which made the log read like a self-lock), the couple
	 * pathfinder aimed at it and never found a valid destination (found=0), and the
	 * scene ended with one chain parked at a platform and another one running
	 * around with a GOTO_COUPLE it could never finish. */
	if (Train::From(t->First()) != t) return false;
	return t->current_order.IsType(OT_WAIT_COUPLE);
}

/**
 * R3R (KI-188, 玩家规则 2026-09-24): drop the stale "等待被挂接" runtime marker of a
 * vehicle which is no longer an independent waiting consist.
 *
 * Called for every vehicle which has been coupled onto (the passive side of a
 * merge) and for stale markers met while scanning for a couple target, so a
 * consist which has been consumed stops advertising itself in the very step it
 * stopped waiting.
 *
 * Only the runtime state (current_order + dest_tile) is cleared. The vehicle's
 * order list is left untouched on purpose: the WAIT_COUPLE *order* belongs to the
 * consist's schedule, and DecoupleTrain() still needs it to find the waiting
 * point again when that consist is cut off later.
 *
 * @param w Vehicle to check; anything but a WAIT_COUPLE holder is left alone.
 */
static void R3RClearStaleWaitMarker(Train *w)
{
	if (w == nullptr || !w->current_order.IsType(OT_WAIT_COUPLE)) return;

	w->current_order.Free();
	w->SetDestTile(INVALID_TILE);

	FILE *dbg = R3RFopenDbg("a");
	if (dbg != nullptr) {
		fprintf(dbg, "[R3R] CPL-WAITCLEAR veh=%d head=%d tile=%d,%d segFront=%d primary=%d\n",
				(int)w->index.base(), (int)Train::From(w->First())->index.base(),
				TileX(w->tile), TileY(w->tile),
				(int)w->IsSegmentFront(), (int)w->IsPrimaryVehicle());
		fclose(dbg);
	}
}

/** R3R (KI-149): which end of a consist carries the seam a decouple left? */
enum class R3RSeamEnd : uint8_t {
	None,  ///< no consist waiting for a coupling is touching us
	Front, ///< one touches our nose: the road out forwards is blocked
	Back,  ///< one touches our tail: the road out backwards is blocked
};

/**
 * R3R (KI-149): is a consist that is waiting to be coupled standing at this
 * train's own @p at_front end, i.e. touching it there?
 *
 * A decouple cuts the released part off seam to seam: its face touches ours and
 * it declares WAIT_COUPLE, which parks it in place until a locomotive collects
 * it. That seam is impassable road for both parts, and nothing else in the game
 * knows about it -- the reverse test and the path finder only look at track and
 * signals, never at the vehicles standing on it, and with plain signals both
 * parts are even inside the same block, so no signal ever turns red. Left alone
 * the part that stayed turns around in place and drives into the part it was
 * just cut off from, and the part that was cut off pulls away straight through
 * it (both were reported from live sessions). The callers therefore treat the
 * seam as blocked road: no reversal towards it (TrainLocoHandler), no path
 * reserved through it. The answer is derived from the current world state only,
 * so it clears by itself as soon as the waiting part has been coupled away or
 * dragged off.
 *
 * @param v Consist to test (any member of the chain).
 * @param at_front Test our nose end instead of our tail end.
 * @return true if a waiting consist is touching that end of ours.
 */
static bool R3RWaitingCoupleAtEnd(const Train *v, bool at_front)
{
	const Train *end = at_front ? v->GetMovingFront() : v->GetMovingBack();
	if (end == nullptr) return false;

	/* Direction pointing away from the train, out past that end. */
	const Trackdir end_td = end->GetVehicleTrackdir();
	/* R3R (KI-108 family): TrackdirToExitdir() asserts on INVALID_TRACKDIR. */
	if (unlikely(end_td == INVALID_TRACKDIR)) return false;
	const DiagDirection away = TrackdirToExitdir(at_front ? end_td : ReverseTrackdir(end_td));
	const int dir_x = (away == DiagDirection::NE || away == DiagDirection::SE) ? 1 : -1;
	const int dir_y = (away == DiagDirection::SE || away == DiagDirection::SW) ? 1 : -1;

	for (const Train *w : Train::IterateFrontOnly()) {
		if (w->First() == v->First()) continue;
		if (w->vehstatus.Test(VehState::Crashed) || w->IsVirtual()) continue;
		if (!w->current_order.IsType(OT_WAIT_COUPLE)) continue;

		/* Either of its faces may be the one that touches us. */
		for (int i = 0; i < 2; i++) {
			const Train *w_end = (i == 0) ? w->GetMovingFront() : w->GetMovingBack();
			if (w_end == nullptr) continue;
			if (abs(w_end->z_pos - end->z_pos) > 5) continue; // under a bridge next to us

			const int dx = w_end->x_pos - end->x_pos;
			const int dy = w_end->y_pos - end->y_pos;

			/* It has to lie out past that end (not somewhere along our
			 * length) ... */
			if (dir_x * dx + dir_y * dy <= 0) continue;

			/* ... and it has to be touching: that is the seam a decouple
			 * leaves behind. */
			const int touch = (end->gcache.cached_veh_length + 1) / 2 + (w_end->gcache.cached_veh_length + 1) / 2;
			if (dx * dx + dy * dy > (touch + 2) * (touch + 2)) continue;

			R3R_PROBE_KI149_SEAM(v, at_front, w);
			return true;
		}
	}
	return false;
}

/**
 * R3R (KI-149): which end of this consist carries the seam a decouple left?
 * @param v Consist to test (any member of the chain).
 * @return The end that a consist waiting for a coupling is touching, or None.
 */
static R3RSeamEnd R3RWaitingCoupleSeam(const Train *v)
{
	if (R3RWaitingCoupleAtEnd(v, false)) return R3RSeamEnd::Back;
	if (R3RWaitingCoupleAtEnd(v, true)) return R3RSeamEnd::Front;
	return R3RSeamEnd::None;
}

static void AddRearEngineToMultiheadedTrain(Train *v)
{
	Train *u = Train::Create();
	v->value >>= 1;
	u->value = v->value;
	u->direction = v->direction;
	u->owner = v->owner;
	u->tile = v->tile;
	u->x_pos = v->x_pos;
	u->y_pos = v->y_pos;
	u->z_pos = v->z_pos;
	u->track = TRACK_BIT_DEPOT;
	u->vehstatus = v->vehstatus;
	u->vehstatus.Reset(VehState::Stopped);
	u->spritenum = v->spritenum + 1;
	u->cargo_type = v->cargo_type;
	u->cargo_subtype = v->cargo_subtype;
	u->cargo_cap = v->cargo_cap;
	u->refit_cap = v->refit_cap;
	u->railtypes = v->railtypes;
	u->engine_type = v->engine_type;
	u->reliability = v->reliability;
	u->reliability_spd_dec = v->reliability_spd_dec;
	u->date_of_last_service = v->date_of_last_service;
	u->date_of_last_service_newgrf = v->date_of_last_service_newgrf;
	u->build_year = v->build_year;
	u->sprite_seq.Set(SPR_IMG_QUERY);
	u->random_bits = Random();
	v->SetMultiheaded();
	u->SetMultiheaded();
	if (v->IsVirtual()) u->SetVirtual();
	v->SetNext(u);
	auto prob = TestVehicleBuildProbability(u, BuildProbabilityType::Reversed);
	if (prob.has_value()) u->flags.Set(VehicleRailFlag::Flipped, prob.value());
	u->UpdatePosition();

	/* Now we need to link the front and rear engines together */
	v->other_multiheaded_part = u;
	u->other_multiheaded_part = v;
}

/**
 * Build a railroad vehicle.
 * @param tile     tile of the depot where rail-vehicle is built.
 * @param flags    type of operation.
 * @param e        the engine to build.
 * @param[out] ret the vehicle that has been built.
 * @return the cost of this operation or an error.
 */
CommandCost CmdBuildRailVehicle(TileIndex tile, DoCommandFlags flags, const Engine *e, Vehicle **ret)
{
	const RailVehicleInfo *rvi = &e->VehInfo<RailVehicleInfo>();

	if (rvi->railveh_type == RailVehicleType::Wagon) return CmdBuildRailWagon(tile, flags, e, ret);

	/* Check if depot and new engine uses the same kind of tracks *
	 * We need to see if the engine got power on the tile to avoid electric engines in non-electric depots */
	if (!HasPowerOnRail(rvi->railtypes, GetRailType(tile))) return CommandCost(STR_ERROR_DEPOT_HAS_WRONG_RAIL_TYPE);

	if (flags.Test(DoCommandFlag::Execute)) {
		DiagDirection dir = GetRailDepotDirection(tile);
		int x = TileX(tile) * TILE_SIZE + _vehicle_initial_x_fract[dir];
		int y = TileY(tile) * TILE_SIZE + _vehicle_initial_y_fract[dir];

		Train *v = Train::Create();
		*ret = v;
		v->direction = DiagDirToDir(dir);
		v->tile = tile;
		v->owner = _current_company;
		v->x_pos = x;
		v->y_pos = y;
		v->z_pos = GetSlopePixelZ(x, y, true);
		v->track = TRACK_BIT_DEPOT;
		v->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
		v->vehstatus = {VehState::Hidden, VehState::Stopped, VehState::DefaultPalette};
		v->spritenum = rvi->image_index;
		v->cargo_type = e->GetDefaultCargoType();
		assert(IsValidCargoType(v->cargo_type));
		v->cargo_cap = rvi->capacity;
		v->refit_cap = 0;
		v->last_station_visited = StationID::Invalid();
		v->last_loading_station = StationID::Invalid();
		v->reverse_distance = 0;
		v->speed_restriction = 0;
		v->signal_speed_restriction = 0;

		v->engine_type = e->index;
		v->gcache.first_engine = EngineID::Invalid(); // needs to be set before first callback

		v->reliability = e->reliability;
		v->reliability_spd_dec = e->reliability_spd_dec;
		v->max_age = e->GetLifeLengthInDays();

		v->railtypes = rvi->railtypes;

		v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_trains);
		v->date_of_last_service = EconTime::CurDate();
		v->date_of_last_service_newgrf = CalTime::CurDate();
		v->build_year = CalTime::CurYear();
		v->sprite_seq.Set(SPR_IMG_QUERY);
		v->random_bits = Random();

		if (e->flags.Test(EngineFlag::ExclusivePreview)) v->vehicle_flags.Set(VehicleFlag::BuiltAsPrototype);
		v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
		v->vehicle_flags.Set(VehicleFlag::AutomateTimetable, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
		v->vehicle_flags.Set(VehicleFlag::TimetableSeparation, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

		v->group_id = DEFAULT_GROUP;

		v->SetFrontEngine();
		v->SetEngine();

		auto prob = TestVehicleBuildProbability(v, BuildProbabilityType::Reversed);
		if (prob.has_value()) v->flags.Set(VehicleRailFlag::Flipped, prob.value());
		v->UpdatePosition();

		if (rvi->railveh_type == RailVehicleType::Multihead) {
			AddRearEngineToMultiheadedTrain(v);
		} else {
			AddArticulatedParts(v);
		}

		v->ConsistChanged(CCF_ARRANGE);
		UpdateTrainGroupID(v);

		CheckConsistencyOfArticulatedVehicle(v);

		InvalidateVehicleTickCaches();
	}

	return CommandCost();
}

static std::vector<Train *> FindGoodVehiclePosList(const Train *src)
{
	EngineID eng = src->engine_type;
	TileIndex tile = src->tile;

	std::vector<Train *> candidates;

	for (Train *dst = Train::From(GetFirstVehicleOnTile(tile, VehicleType::Train)); dst != nullptr; dst = dst->HashTileNext()) {
		if (dst->IsFreeWagon() && !dst->vehstatus.Test(VehState::Crashed) && dst->owner == src->owner) {
			/* check so all vehicles in the line have the same engine. */
			Train *t = dst;
			while (t->engine_type == eng) {
				t = t->Next();
				if (t == nullptr) {
					candidates.push_back(dst);
					break;
				}
			}
		}
	}

	std::sort(candidates.begin(), candidates.end(), [](const Train *a, const Train *b) {
		return a->index < b->index;
	});

	return candidates;
}

/** Helper type for lists/vectors of trains */
typedef std::vector<Train *> TrainList;

/**
 * Make a backup of a train into a train list.
 * @param list to make the backup in
 * @param t    the train to make the backup of
 */
static void MakeTrainBackup(TrainList &list, Train *t)
{
	for (; t != nullptr; t = t->Next()) list.push_back(t);
}

/**
 * Restore the train from the backup list.
 * @param list the train to restore.
 */
static void RestoreTrainBackup(TrainList &list)
{
	/* No train, nothing to do. */
	if (list.empty()) return;

	Train *prev = nullptr;
	/* Iterate over the list and rebuild it. */
	for (Train *t : list) {
		if (prev != nullptr) {
			prev->SetNext(t);
		} else if (t->Previous() != nullptr) {
			/* Make sure the head of the train is always the first in the chain. */
			t->Previous()->SetNext(nullptr);
		}
		prev = t;
	}
}

/**
 * Remove the given wagon from its consist.
 * @param part the part of the train to remove.
 * @param chain whether to remove the whole chain.
 */
static void RemoveFromConsist(Train *part, bool chain = false)
{
	Train *tail;

	if (chain) {
		/* We're moving several vehicles, find the last one in the chain. */
		tail = part;
		while (tail->Next() != nullptr) tail = tail->Next();
	} else {
		/* We're just moving one vehicle, but make sure we get all the articulated parts. */
		tail = part->GetLastEnginePart();
	}

	/* Unlink at the front, but make it point to the next
	 * vehicle after the to be remove part. */
	if (part->Previous() != nullptr) part->Previous()->SetNext(tail->Next());

	/* Unlink at the back */
	tail->SetNext(nullptr);
}

/**
 * Inserts a chain into the train at dst.
 * @param dst   the place where to append after.
 * @param chain the chain to actually add.
 */
static void InsertInConsist(Train *dst, Train *chain)
{
	/* We do not want to add something in the middle of an articulated part. */
	assert(dst != nullptr && (dst->Next() == nullptr || !dst->Next()->IsArticulatedPart()));

	chain->Last()->SetNext(dst->Next());
	dst->SetNext(chain);
}

/**
 * R3R: is this vehicle inside a segment, i.e. between a SegmentFront marker and
 * the SegmentBack that closes that segment?
 * @param t The vehicle to test.
 * @return true iff t belongs to a segment.
 */
static bool R3RIsInsideSegment(const Train *t)
{
	for (const Train *v = t; v != nullptr; v = v->Previous()) {
		if (v->IsSegmentFront()) return true;
		/* Stepping out over another segment's rear boundary means we left the
		 * chain section we started in without ever entering a segment. */
		if (v->Previous() != nullptr && v->Previous()->IsSegmentBack()) return false;
	}
	return false;
}

/**
 * Normalise the dual heads in the train, i.e. if one is
 * missing move that one to this train.
 * @param t the train to normalise.
 */
static void NormaliseDualHeads(Train *t)
{
	for (; t != nullptr; t = t->GetNextVehicle()) {
		if (!t->IsMultiheaded() || !t->IsEngine()) continue;

		/* R3R: in a well-formed dual head exactly one half is an engine (the
		 * front one) and the other is a plain rear car. If the partner also
		 * carries the engine bit the consist is malformed (the rear half was
		 * wrongly promoted to an engine, see SetSegmentTailFakeEngine()): both
		 * halves then look like front engines and the re-arranging below would
		 * move them behind each other for ever -- an infinite loop that freezes
		 * the game (and, being a hang, writes no crash log). A missing partner
		 * would be dereferenced below, so bail out on that too. */
		if (t->other_multiheaded_part == nullptr || t->other_multiheaded_part->IsEngine()) continue;

		/* R3R: inside a segment the two halves of a dual head must stay next to
		 * each other. Upstream moves the rear half behind the last wagon that
		 * follows the front half, which would sandwich any wagon attached behind
		 * the segment between the segment's front marker and its rear boundary:
		 * the depot then reads that wagon as part of the segment and it can no
		 * longer be dragged out on its own (see KI-145). Keeping the rear half
		 * right behind the front half also pushes such a sandwiched wagon back
		 * out of the segment. Outside segments upstream behaviour is unchanged. */
		if (R3RIsInsideSegment(t)) {
			Train *other = t->other_multiheaded_part;
			const bool moved = t->Next() != other;
			if (moved) {
				RemoveFromConsist(other);
				InsertInConsist(t, other);
			}
			R3R_PROBE_NDH_SEG_KEEP(t, other, moved);
			continue;
		}

		/* Make sure that there are no free cars before next engine */
		Train *u;
		for (u = t; u->Next() != nullptr && !u->Next()->IsEngine(); u = u->Next()) {}

		if (u == t->other_multiheaded_part) continue;

		/* Remove the part from the 'wrong' train */
		RemoveFromConsist(t->other_multiheaded_part);
		/* And add it to the 'right' train */
		InsertInConsist(u, t->other_multiheaded_part);
	}
}

/**
 * Normalise the sub types of the parts in this chain.
 * @param chain the chain to normalise.
 */
static void NormaliseSubtypes(Train *chain)
{
	/* Nothing to do */
	if (chain == nullptr) return;

	/* We must be the first in the chain. */
	assert(chain->Previous() == nullptr);

	/* Set the appropriate bits for the first in the chain. */
	if (chain->IsWagon()) {
		chain->SetFreeWagon();
	} else {
		assert(chain->IsEngine());
		chain->SetFrontEngine();
	}

	/* Now clear the bits for the rest of the chain */
	for (Train *t = chain->Next(); t != nullptr; t = t->Next()) {
		t->ClearFreeWagon();
		t->ClearFrontEngine();
	}
}

/**
 * Check/validate whether we may actually build a new train.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @return possible error of this command.
 */
static CommandCost CheckNewTrain(Train *original_dst, Train *dst, Train *original_src, Train *src)
{
	/* Just add 'new' engines and subtract the original ones.
	 * If that's less than or equal to 0 we can be sure we did
	 * not add any engines (read: trains) along the way. */
	if ((src          != nullptr && src->IsEngine()          ? 1 : 0) +
			(dst          != nullptr && dst->IsEngine()          ? 1 : 0) -
			(original_src != nullptr && original_src->IsEngine() ? 1 : 0) -
			(original_dst != nullptr && original_dst->IsEngine() ? 1 : 0) <= 0) {
		return CommandCost();
	}

	/* Get a free unit number and check whether it's within the bounds.
	 * There will always be a maximum of one new train. */
	if (GetFreeUnitNumber(VehicleType::Train) <= _settings_game.vehicle.max_trains) return CommandCost();

	return CommandCost(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
}

/**
 * Check whether the train parts can be attached.
 * @param t the train to check
 * @return possible error of this command.
 */
static CommandCost CheckTrainAttachment(Train *t)
{
	/* No multi-part train, no need to check. */
	if (t == nullptr || t->Next() == nullptr) return CommandCost();

	/* The maximum length for a train. For each part we decrease this by one
	 * and if the result is negative the train is simply too long. */
	int allowed_len = _settings_game.vehicle.max_train_length * TILE_SIZE - t->gcache.cached_veh_length;

	/* For free-wagon chains, check if they are within the max_train_length limit. */
	if (!t->IsEngine()) {
		t = t->Next();
		while (t != nullptr) {
			allowed_len -= t->gcache.cached_veh_length;

			t = t->Next();
		}

		if (allowed_len < 0) return CommandCost(STR_ERROR_TRAIN_TOO_LONG);
		return CommandCost();
	}

	Train *head = t;
	Train *prev = t;

	/* Break the prev -> t link so it always holds within the loop. */
	t = t->Next();
	prev->SetNext(nullptr);

	/* Make sure the cache is cleared. */
	head->InvalidateNewGRFCache();

	while (t != nullptr) {
		allowed_len -= t->gcache.cached_veh_length;

		Train *next = t->Next();

		/* Unlink the to-be-added piece; it is already unlinked from the previous
		 * part due to the fact that the prev -> t link is broken. */
		t->SetNext(nullptr);

		/* Don't check callback for articulated or rear dual headed parts */
		if (!t->IsArticulatedPart() && !t->IsRearDualheaded()) {
			/* Back up and clear the first_engine data to avoid using wagon override group */
			EngineID first_engine = t->gcache.first_engine;
			t->gcache.first_engine = EngineID::Invalid();

			/* We don't want the cache to interfere. head's cache is cleared before
			 * the loop and after each callback does not need to be cleared here. */
			t->InvalidateNewGRFCache();

			uint16_t callback = GetVehicleCallbackParent(CBID_TRAIN_ALLOW_WAGON_ATTACH, 0, 0, head->engine_type, t, head);

			/* Restore original first_engine data */
			t->gcache.first_engine = first_engine;

			/* We do not want to remember any cached variables from the test run */
			t->InvalidateNewGRFCache();
			head->InvalidateNewGRFCache();

			if (callback != CALLBACK_FAILED) {
				/* A failing callback means everything is okay */
				StringID error = STR_NULL;

				if (head->GetGRF()->grf_version < 8) {
					if (callback == 0xFD) error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
					if (callback  < 0xFD) error = GetGRFStringID(head->GetGRF(), GRFSTR_MISC_GRF_TEXT + callback);
					if (callback >= 0x100) ErrorUnknownCallbackResult(head->GetGRFID(), CBID_TRAIN_ALLOW_WAGON_ATTACH, callback);
				} else {
					if (callback < 0x400) {
						error = GetGRFStringID(head->GetGRF(), GRFSTR_MISC_GRF_TEXT + callback);
					} else {
						switch (callback) {
							case 0x400: // allow if railtypes match (always the case for OpenTTD)
							case 0x401: // allow
								break;

							case 0x40F:
								error = GetGRFStringID(head->GetGRFID(), static_cast<GRFStringID>(GetRegister(0x100)));
								break;

							default:    // unknown reason -> disallow
							case 0x402: // disallow attaching
								error = STR_ERROR_INCOMPATIBLE_RAIL_TYPES;
								break;
						}
					}
				}

				if (error != STR_NULL) return CommandCost(error);
			}
		}

		/* And link it to the new part. */
		prev->SetNext(t);
		prev = t;
		t = next;
	}

	if (allowed_len < 0) return CommandCost(STR_ERROR_TRAIN_TOO_LONG);
	return CommandCost();
}

/**
 * Validate whether we are going to create valid trains.
 * @note All vehicles are/were 'heads' of their chains.
 * @param original_dst The original destination chain.
 * @param dst          The destination chain after constructing the train.
 * @param original_src The original source chain.
 * @param src          The source chain after constructing the train.
 * @param check_limit  Whether to check the vehicle limit.
 * @return possible error of this command.
 */
static CommandCost ValidateTrains(Train *original_dst, Train *dst, Train *original_src, Train *src, bool check_limit)
{
	/* Check whether we may actually construct the trains. */
	CommandCost ret = CheckTrainAttachment(src);
	if (ret.Failed()) return ret;
	ret = CheckTrainAttachment(dst);
	if (ret.Failed()) return ret;

	/* Check whether we need to build a new train. */
	return check_limit ? CheckNewTrain(original_dst, dst, original_src, src) : CommandCost();
}

/**
 * Arrange the trains in the wanted way.
 * @param dst_head   The destination chain of the to be moved vehicle.
 * @param dst        The destination for the to be moved vehicle.
 * @param src_head   The source chain of the to be moved vehicle.
 * @param src        The to be moved vehicle.
 * @param move_chain Whether to move all vehicles after src or not.
 */
/* R3R (couple priority, route A): forward declaration -- the depot edit paths
 * (CmdMoveRailVehicle / CmdSellRailWagon) below must re-derive the schedule
 * ownership after they rearranged a chain. Definition sits with the rest of the
 * couple-priority helpers further down in this file. */
static void R3RSyncChainAfterDepotEdit(Train *chain);
static void R3RNormaliseChainGroups(Train *head);

/* R3R diagnostic: the R3R code rewrites order indices by hand in a few places
 * (couple, decouple, schedule hand-over). Each of those blocks is supposed to
 * leave cur_timetable_order_index pointing at the same order as
 * cur_real_order_index. When it does not, UpdateVehicleTimetable used to assert
 * on the next station stop (see the TT-DESYNC log there). Call this right after
 * such a block: it writes one line per break, naming the block that caused it,
 * so the offending spot can be found from a single reproduction. */
static void R3RCheckTtSync(const Vehicle *v, const char *tag)
{
	if (v == nullptr) return;
	if (v->cur_timetable_order_index == INVALID_VEH_ORDER_ID) return;  /* not started yet */
	if (v->cur_timetable_order_index == v->cur_real_order_index) return;  /* the invariant holds */
	FILE *dbg = R3RFopenDbg("a");
	if (dbg == nullptr) return;
	fprintf(dbg, "TT-CHK-BREAK %s veh=%d real=%d tt=%d impl=%d n=%d curType=%d\n",
			tag, (int)v->index.base(), (int)v->cur_real_order_index, (int)v->cur_timetable_order_index,
			(int)v->cur_implicit_order_index, (int)v->GetNumOrders(), (int)v->current_order.GetType());
	fclose(dbg);
}

static void ArrangeTrains(Train **dst_head, Train *dst, Train **src_head, Train *src, bool move_chain)
{
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "ARRANGE-IN dh=%d dst=%d sh=%d src=%d mc=%d\n",
				*dst_head != nullptr ? (int)(*dst_head)->index.base() : -1,
				dst != nullptr ? (int)dst->index.base() : -1,
				*src_head != nullptr ? (int)(*src_head)->index.base() : -1,
				src != nullptr ? (int)src->index.base() : -1,
				move_chain ? 1 : 0);
			fclose(dbg);
		}
	}

	/* First determine the front of the two resulting trains */
	if (*src_head == *dst_head) {
		/* If we aren't moving part(s) to a new train, we are just moving the
		 * front back and there is not destination head. */
		*dst_head = nullptr;
	} else if (*dst_head == nullptr) {
		/* If we are moving to a new train the head of the move train would become
		 * the head of the new vehicle. */
		*dst_head = src;
	}

	if (src == *src_head) {
		/* If we are moving the front of a train then we are, in effect, creating
		 * a new head for the train. Point to that. Unless we are moving the whole
		 * train in which case there is not 'source' train anymore.
		 * In case we are a multiheaded part we want the complete thing to come
		 * with us, so src->GetNextUnit(), however... when we are e.g. a wagon
		 * that is followed by a rear multihead we do not want to include that. */
		*src_head = move_chain ? nullptr :
				(src->IsMultiheaded() ? src->GetNextUnit() : src->GetNextVehicle());
	}

	/* Now it's just simply removing the part that we are going to move from the
	 * source train and *if* the destination is a not a new train add the chain
	 * at the destination location. */
	RemoveFromConsist(src, move_chain);
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) { fprintf(dbg, "ARRANGE-RFC-DONE\n"); fclose(dbg); }
	}
	if (*dst_head != src) InsertInConsist(dst, src);
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) { fprintf(dbg, "ARRANGE-IC-DONE\n"); fclose(dbg); }
	}

	/* Now normalise the dual heads, that is move the dual heads around in such
	 * a way that the head and rear of a dual head are in the same train */
	NormaliseDualHeads(*src_head);
	NormaliseDualHeads(*dst_head);
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) { fprintf(dbg, "ARRANGE-NDH-DONE\n"); fclose(dbg); }
	}

}

/**
 * Normalise the head of the train again, i.e. that is tell the world that
 * we have changed and update all kinds of variables.
 * @param head the train to update.
 */
/**
 * R3R (第 144 轮 / 需求贰): 列车名的「借用与返还」。
 *
 * 名字和排程一样属于「段」：一个段的段头车带着自己的列车名。当这段被并进另一列车、
 * 自己不再是链头时（车库拖动 DEPOT-PARK、折叠修正的身份交接），名字先停进
 * name_backup 再清空，等于把名字借出去；等它重新成为某个链的链头时（
 * NormaliseTrainHead 是所有"链头归位"路径的公共入口）从备份里取回，等于还回来。
 *
 * 旧代码只做 name.clear()、没有任何备份，这正是玩家报的"车库内拖动导致耦合之后，
 * 把后半条链的控制段拖出来，列车名称会变回默认"的成因。
 *
 * non_leading_engines_keep_name 打开时原生语义就是保留非链头引擎的名字，这里照旧
 * 什么都不做（名字仍留在本车上，也就没有"借出"一说）。
 */
static void R3RParkTrainName(Vehicle *v)
{
	if (v == nullptr) return;
	if (_settings_game.vehicle.non_leading_engines_keep_name) return;
	if (v->name_backup.empty() && !v->name.empty()) v->name_backup = v->name;
	v->name.clear();
}

/** 见 R3RParkTrainName()：段重新成为链头时，把自己停放的列车名取回。 */
static void R3RRestoreTrainName(Vehicle *v)
{
	if (v == nullptr || v->name_backup.empty()) return;
	if (v->name.empty()) {
		v->name = v->name_backup;
		v->name_backup.clear();
	}
}

/**
 * R3R (第 144 轮 / 需求伍): 列车分组（玩家看到的"列车分组"）的段级借用/返还，与列车名、
 * 车号同构。
 *
 * 玩家口径：一条链里每个段各自带着自己的列车分组；耦合/车库拖动把整条链的分组统一到
 * 控制段（见 R3RNormaliseChainGroups）时，被统一掉的那些段，它们**原来的**分组只是
 * "借出去"了，不能丢 —— 等该段重新成为某个链的链头时，要原样还回来。
 *
 * 旧行为：R3RNormaliseChainGroups 直接 SetTrainGroupID() 把整链刷成控制段的分组，段自己
 * 的分组没有任何备份，于是"拖出来之后归属变成默认值/别人的组"。
 */
/** R3R (第 144 轮 / 需求壹): num 是否正被别的活车当作活号使用 —— 判断一个号能不能回收。 */
static bool R3RUnitNumberUsedByOther(const Vehicle *self, uint16_t num)
{
	if (num == 0) return false;
	for (const Vehicle *w : Vehicle::Iterate()) {
		if (w == self || w->unitnumber != num) continue;
		return true;
	}
	return false;
}

static void R3RParkTrainGroupID(Vehicle *v)
{
	if (v == nullptr) return;
	if (v->group_id_backup != GroupID::Invalid()) return;
	v->group_id_backup = v->group_id;
	R3RDbgWrite("GRP-PARK veh=%d g=%u\n", (int)v->index.base(), (uint)v->group_id.base());
}

/** 见 R3RParkTrainGroupID()：段重新成为链头时，把自己借出的列车分组取回。 */
static void R3RRestoreTrainGroupID(Train *v)
{
	if (v == nullptr || v->group_id_backup == GroupID::Invalid()) return;
	/* 只有真正成为链头（前端列）时才取回；段内普通车保持"跟随链头"的现状。 */
	if (!v->IsFrontEngine() && !v->IsFrontWagon()) return;

	const GroupID back = v->group_id_backup;
	if (back == v->group_id) {
		v->group_id_backup = GroupID::Invalid();
		return;
	}

	/* num_vehicle 以「前端列」为单位簿记：本车此刻正被计入"借到的那一组"，先摘出、
	 * 换回原分组后再计入（调用方 Couple/Decouple/车库编辑的 CountVehicle 配对管的是
	 * "这一列还算不算前端列"，与本处"归属搬到哪个分组"正交，不会重复计数）。 */
	if (v->group_id != GroupID::Invalid()) GroupStatistics::CountVehicle(v, -1);
	v->group_id = back;
	GroupStatistics::CountVehicle(v, 1);
	v->group_id_backup = GroupID::Invalid();
	R3RDbgWrite("GRP-RESTORE veh=%d g=%u\n", (int)v->index.base(), (uint)back.base());
}

/** 见 R3RParkTrainGroupID()/R3RRestoreTrainName()：段重新成为链头时，把自己停放/借出的车号取回。
 *  R3R (第 147 轮续 / 站台挂接解挂): 车库拖动（DEPOT-PARK）与耦合借用交接会把失去链头身份的段的号
 *  停进 unitnumber_backup，同时**保持在号池里的占用**（不 ReleaseID）。旧代码在段重新成为链头时
 *  直接走下面的 "If we don't have a unit number yet" 领一个**新**号，于是站台上解挂出来的车底会换号
 *  （玩家看到的"列车 2"变成别的号），而它自己的号 2 永远冻在 unitnumber_backup 里、占着池位没人还。
 *  这里与 R3RRestoreTrainName() 对称地先取回自己的号；只有真的没有备份时才让调用方去领新号。 */
static void R3RRestoreUnitNumber(Train *head)
{
	if (head == nullptr || head->unitnumber != 0 || head->unitnumber_backup == 0) return;

	if (R3RUnitNumberUsedByOther(head, head->unitnumber_backup)) {
		/* 号已被别的活车占用（旧档 / 手工改档）：放弃取回，交给调用方按老规矩领新号。 */
		head->unitnumber_backup = 0;
		return;
	}

	const uint16_t restored = head->unitnumber_backup;
	head->unitnumber = restored;
	head->unitnumber_backup = 0;
	/* 号池里的位一直占着（停放时不 ReleaseID），这里只是补记一次使用登记，与车库路径同款。 */
	if (!HasBit(head->subtype, GVSF_VIRTUAL)) {
		Company::Get(head->owner)->freeunits[head->type].UseID(restored);
	}
	R3RDbgWrite("UNIT-RESTORE veh=%d id=%u\n", (int)head->index.base(), (unsigned)restored);
}

static void NormaliseTrainHead(Train *head)
{
	/* Not much to do! */
	if (head == nullptr) return;

	/* Tell the 'world' the train changed. */
	head->ConsistChanged(CCF_ARRANGE);

	/* R3R (第 144 轮 / 需求伍): 段头身份归位时先把自己借出的「列车分组」取回，必须早于
	 * UpdateTrainGroupID() —— 后者按链头的 group_id 把整条链统一，晚一步就会用被借出去
	 * 的那个分组覆盖掉本段自己的归属。 */
	R3RRestoreTrainGroupID(head);

	UpdateTrainGroupID(head);
	head->flags.Set(VehicleRailFlag::ConsistSpeedReduction);

	/* Not a front engine, i.e. a free wagon chain. No need to do more. */
	if (!head->IsFrontEngine()) return;

	/* R3R (第 144 轮 / 需求贰): 名字与车号同属"段头身份" —— 这一段重新成为链头时
	 * 把自己停放/借出的列车名取回。必须放在下面 unitnumber 的提前 return 之前，
	 * 因为停放过的链头车号非 0，会从那里直接返回。 */
	R3RRestoreTrainName(head);
	/* R3R (第 147 轮续): 车号与名字同属段头身份 —— 先把停放/借出的号取回，再让下面的
	 * "If we don't have a unit number yet" 去处理真正没有号的情形（否则站台解挂出来的
	 * 车底会换新号、自己的旧号永久冻在 backup 里占着池位）。 */
	R3RRestoreUnitNumber(head);

	/* Update the refit button and window */
	InvalidateWindowData(WindowClass::VehicleRefit, head->index, VIWD_CONSIST_CHANGED);
	SetWindowWidgetDirty(WindowClass::VehicleView, head->index, WID_VV_REFIT);

	/* If we don't have a unit number yet, set one. */
	if (head->unitnumber != 0 || HasBit(head->subtype, GVSF_VIRTUAL)) return;
	head->unitnumber = Company::Get(head->owner)->freeunits[head->type].UseID(GetFreeUnitNumber(VehicleType::Train));
}

CommandCost CmdMoveVirtualRailVehicle(DoCommandFlags flags, VehicleID src_veh, VehicleID dest_veh, MoveRailVehicleFlags move_flags)
{
	Train *src = Train::GetIfValid(src_veh);
	if (src == nullptr || !src->IsVirtual()) return CMD_ERROR;

	return CmdMoveRailVehicle(flags, src_veh, dest_veh, move_flags | MoveRailVehicleFlags::Virtual);
}

/* Defined further down, next to the segment helpers; needed here to tell a
 * merged-on block which may act as a segment from a loose wagon chain. */
static bool TrainHasEngine(const Train *v);

/**
 * Move a rail vehicle around inside the depot.
 * @param flags type of operation
 *              Note: DoCommandFlag::AutoReplace is set when autoreplace tries to undo its modifications or moves vehicles to temporary locations inside the depot.
 * @param src_veh source vehicle index
 * @param dest_veh what wagon to put the source wagon AFTER, XXX - VehicleID::Invalid() to make a new line
 * @param move_chain move all vehicles following the source vehicle
 * @return the cost of this operation or an error
 */
CommandCost CmdMoveRailVehicle(DoCommandFlags flags, VehicleID src_veh, VehicleID dest_veh, MoveRailVehicleFlags move_flags)
{
	bool move_chain = HasFlag(move_flags, MoveRailVehicleFlags::MoveChain);
	bool new_head = HasFlag(move_flags, MoveRailVehicleFlags::NewHead);

	Train *src = Train::GetIfValid(src_veh);
	if (src == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(src->owner);
	if (ret.Failed()) return ret;

	/* Do not allow moving crashed vehicles inside the depot, it is likely to cause asserts later */
	if (src->vehstatus.Test(VehState::Crashed)) return CMD_ERROR;

	if (src->IsVirtual() != HasFlag(move_flags, MoveRailVehicleFlags::Virtual)) return CMD_ERROR;

	/* if nothing is selected as destination, try and find a matching vehicle to drag to. */
	Train *dst;
	if (dest_veh == VehicleID::Invalid()) {
		if (!src->IsEngine() && !src->IsVirtual() && !flags.Test(DoCommandFlag::AutoReplace)) {
			/* Try each possible destination target, if none succeed do not append to a free wagon chain */
			std::vector<Train *> destination_candidates = FindGoodVehiclePosList(src);
			for (Train *try_dest : destination_candidates) {
				CommandCost cost = CmdMoveRailVehicle(flags, src_veh, try_dest->index, move_flags);
				if (cost.Succeeded()) return cost;
			}
		}
		dst = nullptr;
	} else {
		dst = Train::GetIfValid(dest_veh);
		if (dst == nullptr) return CMD_ERROR;

		CommandCost ret = CheckOwnership(dst->owner);
		if (ret.Failed()) return ret;

		/* Do not allow appending to crashed vehicles, too */
		if (dst->vehstatus.Test(VehState::Crashed)) return CMD_ERROR;

		if (dst->IsVirtual() != HasFlag(move_flags, MoveRailVehicleFlags::Virtual)) return CMD_ERROR;
	}

	/* if an articulated part is being handled, deal with its parent vehicle */
	src = src->GetFirstEnginePart();
	if (dst != nullptr) {
		dst = dst->GetFirstEnginePart();
	}

	/* don't move the same vehicle.. */
	if (src == dst) return CommandCost();

	/* locate the head of the two chains */
	Train *src_head = src->First();
	assert(HasBit(src_head->subtype, GVSF_VIRTUAL) == HasBit(src->subtype, GVSF_VIRTUAL));
	Train *dst_head;
	if (dst != nullptr) {
		dst_head = dst->First();
		assert(HasBit(dst_head->subtype, GVSF_VIRTUAL) == HasBit(dst->subtype, GVSF_VIRTUAL));
		if (dst_head->tile != src_head->tile) return CMD_ERROR;
		/* Now deal with articulated part of destination wagon */
		dst = dst->GetLastEnginePart();
	} else {
		dst_head = nullptr;
	}

	if (src->IsRearDualheaded()) return CommandCost(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	/* When moving all wagons, we can't have the same src_head and dst_head */
	if (move_chain && src_head == dst_head) return CommandCost();

	/* When moving a multiheaded part to be place after itself, bail out. */
	if (!move_chain && dst != nullptr && dst->IsRearDualheaded() && src == dst->other_multiheaded_part) return CommandCost();

	/* R3R (KI-212): a coupled-on segment is one operational unit and behaves like
	 * an articulated block - nothing may be spliced into its interior. Inserting a
	 * block behind dst means inserting it in front of dst->Next(), so refuse when
	 * that vehicle sits inside a segment (i.e. it is neither the segment front nor
	 * behind the segment's rear boundary, which R3RIsInsideSegment() tells apart).
	 * Inserting at the end of a chain, right in front of a segment front and
	 * re-attaching the block where it already is stay allowed. */
	if (dst != nullptr && dst->Next() != nullptr && dst->Next() != src &&
			!dst->Next()->IsSegmentFront() && R3RIsInsideSegment(dst->Next())) {
		return CMD_ERROR;
	}

	/* Check if all vehicles in the source train are stopped inside a depot. */
	/* Do this check only if the vehicle to be moved is non-virtual */
	if (!HasFlag(move_flags, MoveRailVehicleFlags::Virtual)) {
		if (!src_head->IsStoppedInDepot()) return CommandCost(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
	}

	/* Check if all vehicles in the destination train are stopped inside a depot. */
	/* Do this check only if the destination vehicle is non-virtual */
	if (!HasFlag(move_flags, MoveRailVehicleFlags::Virtual)) {
		if (dst_head != nullptr && !dst_head->IsStoppedInDepot()) return CommandCost(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
	}

	/* First make a backup of the order of the trains. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original_src;
	TrainList original_dst;

	MakeTrainBackup(original_src, src_head);
	MakeTrainBackup(original_dst, dst_head);

	/* Also make backup of the original heads as ArrangeTrains can change them.
	 * For the destination head we do not care if it is the same as the source
	 * head because in that case it's just a copy. */
	Train *original_src_head = src_head;
	Train *original_dst_head = (dst_head == src_head ? nullptr : dst_head);

	/* We want this information from before the rearrangement, but execute this after the validation.
	 * original_src_head can't be nullptr; src is by definition != nullptr, so src_head can't be nullptr as
	 * src->GetFirst() always yields non-nullptr, so eventually original_src_head != nullptr as well. */
	bool original_src_head_front_engine = original_src_head->IsFrontEngine();
	bool original_dst_head_front_engine = original_dst_head != nullptr && original_dst_head->IsFrontEngine();

	/* R3R (KI-169b): identity of the block which is about to be dragged. src is its
	 * first vehicle, so with MoveChain its last vehicle is the end of the source
	 * chain *now* -- after ArrangeTrains() has spliced the block into another chain
	 * Last() would return the end of the merged chain instead. Whether the block was
	 * a car-only formation has to be sampled before the move as well, because this
	 * drag may strip its fake front engine (cases #2/#3 below). */
	Train *const moved_block_tail = move_chain ? src->Last() : nullptr;
	const bool moved_was_caronly = move_chain && R3RIsCarOnlyFormation(src);

	/* (Re)arrange the trains in the wanted arrangement. */
	ArrangeTrains(&dst_head, dst, &src_head, src, move_chain);

	if (!flags.Test(DoCommandFlag::AutoReplace)) {
		/* If the autoreplace flag is set we do not need to test for the validity
		 * because we are going to revert the train to its original state. As we
		 * assume the original state was correct autoreplace can skip this. */
		ret = ValidateTrains(original_dst_head, dst_head, original_src_head, src_head, true);
		if (ret.Failed()) {
			/* Restore the train we had. */
			RestoreTrainBackup(original_src);
			RestoreTrainBackup(original_dst);
			return ret;
		}
	}

	/* do it? */
	if (flags.Test(DoCommandFlag::Execute)) {
		/* Remove old heads from the statistics */
		if (original_src_head_front_engine) GroupStatistics::CountVehicle(original_src_head, -1);
		if (original_dst_head_front_engine) GroupStatistics::CountVehicle(original_dst_head, -1);

		/* First normalise the sub types of the chains. */
		NormaliseSubtypes(src_head);
		NormaliseSubtypes(dst_head);

		/* There are 14 different cases:
		 *  1) front engine gets moved to a new train, it stays a front engine.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  2) front engine gets moved to another train, it is not a front engine anymore
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  3) front engine gets moved to later in the current train, it is not a front engine anymore.
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *  4) free wagon gets moved
		 *     a) the 'next' part is a wagon that becomes a free wagon chain.
		 *     b) the 'next' part is an engine that becomes a front engine.
		 *     c) there is no 'next' part, nothing else happens
		 *  5) non front engine gets moved and becomes a new train, nothing else happens
		 *  6) non front engine gets moved within a train / to another train, nothing happens
		 *  7) wagon gets moved, nothing happens
		 */
		if (src == original_src_head && src->IsEngine() && (!src->IsFrontEngine() || new_head)) {
			/* Cases #2 and #3: the front engine gets trashed. */
			CloseWindowById(WindowClass::VehicleView, src->index);
			CloseWindowById(WindowClass::VehicleOrders, src->index);
			CloseWindowById(WindowClass::VehicleRefit, src->index);
			CloseWindowById(WindowClass::VehicleDetails, src->index);
			CloseWindowById(WindowClass::VehicleTimetable, src->index);
			CloseWindowById(WindowClass::ScheduledDispatchSlots, src->index);
			CloseWindowById(WindowClass::VehicleOrderImportErrors, src->index);
			DeleteNewGRFInspectWindow(GrfSpecFeature::Trains, src->index.base());
			SetWindowDirty(WindowClass::Company, _current_company);

			if (src_head != nullptr && src_head->IsFrontEngine()) {
				/* Cases #?b: Transfer order, unit number and other stuff
				 * to the new front engine. */
				/* R3R (couple priority, route A): a front engine which coupled its
				 * train onto another consist runs the schedule of the consist's
				 * command owner and has parked its own schedule in orders_backup.
				 * Hand the schedule the dragged-out train *owns* over to the new
				 * head; otherwise the consist's schedule is transferred (and then
				 * removed below), leaving the train with an empty schedule. */
				if (src->r3r_orders_borrowed) {
					src->orders = src->orders_backup;
					src->orders_backup = nullptr;
					src->orders_backup_real_index = INVALID_VEH_ORDER_ID;
					src->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
					src->r3r_orders_borrowed = false;
				}
				if (src_head->orders == nullptr) {
					src_head->orders = src->orders;
					if (src_head->orders != nullptr) src_head->AddToShared(src);
				}
				src_head->CopyVehicleConfigAndStatistics(src);
			}
			/* Remove stuff not valid anymore for non-front engines. */
			if (src_head != src) {
				/* R3R（2026-09-23 玩家报：车库拖动耦合后排程恢复异常）：上面 2755 的
				 * 交接只在「源链还剩一个前端列」时才会发生(src_head != nullptr &&
				 * src_head->IsFrontEngine())。整列被拖进另一列时 MoveChain 把源链
				 * 搬空，src_head 为 nullptr，这个车头自己的排程就在下面被
				 * DeleteVehicleOrders() 直接销毁 —— 玩家再把它拖出来时排程已经没了。
				 * 修法与 Couple() 的路线 A 借用一致：把这一列自己的排程停进
				 * orders_backup（订单表本身不释放），并置 r3r_orders_borrowed 作为
				 * 「我的排程在备份里」的标记；日后该车再成为某个链的链头时，
				 * R3RSyncChainAfterDepotEdit() → R3RSyncDrivingOrders() 会把它还原。 */
				/* 共享排程(原生 Ctrl 共享、或读档时由 vehicle_sl.cpp 按共享链补齐的那一份)
				 * 不能停进 orders_backup：它在别的车上还有成员，而备份只有本车看得见。 */
				const bool shared_orders = src->IsOrderListShared();
				if (!shared_orders && src->orders_backup == nullptr && src->orders != nullptr && !src->r3r_orders_borrowed) {
					src->orders_backup = src->orders;
					src->orders_backup_real_index = src->cur_real_order_index;
					src->orders_backup_implicit_index = src->cur_implicit_order_index;
				}
				if (!shared_orders && src->orders_backup != nullptr) {
					/* 有排程要留在备份里：先把活动指针与借用标志摘掉，DeleteVehicleOrders()
					 * 才不会把这份排程挪回 v->orders 之后释放掉（它是唯一持有者）。 */
					src->orders = nullptr;
					src->r3r_orders_borrowed = false;
					DeleteVehicleOrders(src);
					src->r3r_orders_borrowed = true;
					/* 车号一起停：和 Couple() 一样把本车车号存进 unitnumber_backup，
					 * 日后它重新成为链头时 R3RSyncChainAfterDepotEdit() 会把原号还给它
					 * (否则 "Train 1" 会被重新编号成别的号)。 */
					if (src->unitnumber_backup == 0) src->unitnumber_backup = src->unitnumber;
					R3RDbgWrite("DEPOT-PARK veh=%d bk_real=%d bk_impl=%d unit=%u\n",
							(int)src->index.base(), (int)src->orders_backup_real_index,
							(int)src->orders_backup_implicit_index, (unsigned)src->unitnumber_backup);
				} else if (!shared_orders || src->r3r_orders_borrowed) {
					/* R3R: when the vehicle is (still) the head of its own resulting
					 * chain, the transfer above was a self-assignment; deleting the
					 * schedule here would throw away the very schedule the train keeps.
					 * 借来的共享排程也必须走这里：DeleteVehicleOrders() 对共享表的效果是
					 * RemoveFromShared()，恰好把"离开租来的共享链"这件事做干净（KI-93 的
					 * r3r_borrow_is_shared 例外会保住 orders_backup 里本车自己的那份）。 */
					DeleteVehicleOrders(src);
				} else {
					/* R3R (第 147 轮 / 需求壹续, 玩家 2026-09-28 实报): 本车**自己拥有**的
					 * 共享排程（原生 Ctrl 共享，或读档时按共享链补齐的那一份）必须原样留在
					 * 车上 —— 既不停进 orders_backup（备份只有本车看得见，而这份表还有别的
					 * 成员），更不能像旧代码那样落进上面的 DeleteVehicleOrders()：那是
					 * RemoveFromShared()，效果就是玩家报的"拖回来后排程被清空 + 被移出共享
					 * 调度"，与我们的排程借用/返还理念不符。
					 * 引擎只认 IsFrontEngine() 的链头，中段挂着 orders 指针不会被跑；这车
					 * 日后重新成为链头时 R3RSyncDrivingOrders() 见 owner == chain 直接放行，
					 * 排程与共享成员身份都还在。若另一侧成员先被卖掉，KI-93 的
					 * R3RFindOrderListReferrer() 会把订单表移交给仍持有它的车，不会悬垂。 */
					const Vehicle *shared_head = src->FirstShared();
					R3RDbgWrite("DEPOT-PARK-SHARED veh=%d shared_head=%d share=%u\n",
							(int)src->index.base(),
							(int)(shared_head != nullptr ? shared_head->index.base() : 0),
							(unsigned)(src->orders != nullptr ? src->orders->GetNumVehicles() : 0));
				}
			}
			/* R3R (第 144 轮 / 需求壹): src 在这里失去了链头身份，它的车号按玩家口径
			 * "冻结"：停进 unitnumber_backup 并**保持占用池位**（不再
			 * ReleaseUnitNumber()）——于是其它列车无法占用这个编号，直到该编号所属
			 * 的段被销毁（PreDestructor / CmdDemoteSegment 才 ReleaseID）。
			 * 等它重新成为链头时由 R3RSyncChainAfterDepotEdit() 取回原号。
			 * 旧代码在这里无条件 ReleaseUnitNumber() 把号还回公司池，别的列车立刻
			 * 就能领到它，正是玩家报的"编号被抢走"。 */
			if (src->unitnumber_backup == 0 && src->unitnumber != 0) {
				src->unitnumber_backup = src->unitnumber;
			}
			src->unitnumber = 0;
			R3RDbgWrite("UNIT-PARK veh=%d bk=%u\n", (int)src->index.base(), (unsigned)src->unitnumber_backup);
			src->dispatch_records.clear();
			/* R3R (第 144 轮 / 需求贰): 名字与排程、车号一起停放 —— src 不再是链头，
			 * 它自己的列车名先存进 name_backup（而不是直接丢掉），等它重新成为链头
			 * 时由 NormaliseTrainHead() → R3RRestoreTrainName() 取回。 */
			R3RParkTrainName(src);
			if (src->vehicle_flags.Test(VehicleFlag::HaveSlot)) {
				TraceRestrictRemoveVehicleFromAllSlots(src->index);
				src->vehicle_flags.Reset(VehicleFlag::HaveSlot);
			}
			src->vehicle_flags.Reset(VehicleFlag::ReplacementPending);
			OrderBackup::ClearVehicle(src);
		}

		/* We weren't a front engine but are becoming one. So
		 * we should be put in the default group. */
		if ((original_src_head != src || new_head) && dst_head == src) {
			SetTrainGroupID(src, DEFAULT_GROUP);
			SetWindowDirty(WindowClass::Company, _current_company);
		}

		/* Handle 'new engine' part of cases #1b, #2b, #3b, #4b and #5 in NormaliseTrainHead. */
		NormaliseTrainHead(src_head);
		NormaliseTrainHead(dst_head);

		/* Add new heads to statistics.
		 * This should be done after NormaliseTrainHead due to engine total limit checks in GetFreeUnitNumber. */
		if (src_head != nullptr && src_head->IsFrontEngine()) GroupStatistics::CountVehicle(src_head, 1);
		if (dst_head != nullptr && dst_head->IsFrontEngine()) GroupStatistics::CountVehicle(dst_head, 1);

		/* R3R (KI-169b): a chain dragged into another one in the depot is a
		 * coupled-on segment whichever route it took to get there, so it gets the
		 * same markers Couple() puts on a merged-on group: ★ (SegmentFront) on its
		 * first vehicle and ⊗ (SegmentBack) on its own last vehicle. Without ⊗ the
		 * segment looks open-ended, so vehicles the player later drags behind it are
		 * mistaken for its contents and travel/sell/decouple along with it
		 * (TrainDepotGetSegmentTail walks forward to the next ★), and without ★ the
		 * block is invisible to the segment table altogether.
		 * Only real merges are marked: the block must end up inside another chain
		 * (original_dst_head is null when it is split off into a new chain or moved
		 * within its own) and must be an engine or a car-only formation, which is
		 * exactly Couple()'s u_was_caronly || TrainHasEngine() rule. Virtual
		 * (template) trains and the autoreplace shuffles keep their state as is. */
		if (move_chain && original_dst_head != nullptr && moved_block_tail != nullptr &&
				!HasFlag(move_flags, MoveRailVehicleFlags::Virtual) &&
				(moved_was_caronly || TrainHasEngine(src))) {
			src->SetSegmentFront();
			moved_block_tail->SetSegmentBack();
		}

		/* R3R (couple priority, route A): a depot edit can split or join chains,
		 * which changes which segment owns the schedule. Re-derive the ranking and
		 * re-point each resulting chain head at its own command owner, so that a
		 * locomotive which was coupled (and therefore executing the schedule of
		 * the consist it picked up) gets its own parked schedule back as soon as
		 * it is dragged out of that consist again. */
		R3RSyncChainAfterDepotEdit(src_head);
		R3RSyncChainAfterDepotEdit(dst_head);

		if (!flags.Test(DoCommandFlag::NoCargoCapacityCheck)) {
			CheckCargoCapacity(src_head);
			CheckCargoCapacity(dst_head);
		}

		if (src_head != nullptr) {
			src_head->last_loading_station = StationID::Invalid();
			src_head->vehicle_flags.Reset(VehicleFlag::LastLoadStationSeparate);
		}
		if (dst_head != nullptr) {
			dst_head->last_loading_station = StationID::Invalid();
			dst_head->vehicle_flags.Reset(VehicleFlag::LastLoadStationSeparate);
		}

		if (src_head != nullptr) src_head->First()->MarkDirty();
		if (dst_head != nullptr) dst_head->First()->MarkDirty();

		/* We are undoubtedly changing something in the depot and train list. */
		/* But only if the moved vehicle is not virtual */
		if (!HasBit(src->subtype, GVSF_VIRTUAL)) {
			InvalidateWindowData(WindowClass::VehicleDepot, src->tile.base());
			InvalidateVehicleListWindows(VehicleType::Train);
		}
	} else {
		/* We don't want to execute what we're just tried. */
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
	}

	InvalidateVehicleTickCaches();

	return CommandCost();
}

/**
 * Sell a (single) train wagon/engine.
 * @param flags type of operation
 * @param t     the train wagon to sell
 * @param sell_chain  the selling mode
 * - sell_chain = false: only sell the single dragged wagon/engine (and any belonging rear-engines)
 * - sell_chain = true:  sell the vehicle and all vehicles following it in the chain
 *                       if the wagon is dragged, don't delete the possibly belonging rear-engine to some front
 * @param backup_order make order backup?
 * @param user  the user for the order backup.
 * @return the cost of this operation or an error
 */
CommandCost CmdSellRailWagon(DoCommandFlags flags, Vehicle *t, bool sell_chain, bool backup_order, ClientID user)
{
	Train *v = Train::From(t)->GetFirstEnginePart();
	Train *first = v->First();

	if (v->IsRearDualheaded()) return CommandCost(STR_ERROR_REAR_ENGINE_FOLLOW_FRONT);

	/* First make a backup of the order of the train. That way we can do
	 * whatever we want with the order and later on easily revert. */
	TrainList original;
	MakeTrainBackup(original, first);

	/* We need to keep track of the new head and the head of what we're going to sell. */
	Train *new_head = first;
	Train *sell_head = nullptr;

	/* Split the train in the wanted way. */
	ArrangeTrains(&sell_head, nullptr, &new_head, v, sell_chain);

	/* We don't need to validate the second train; it's going to be sold. */
	CommandCost ret = ValidateTrains(nullptr, nullptr, first, new_head, !flags.Test(DoCommandFlag::AutoReplace));
	if (ret.Failed()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		return ret;
	}

	if (first->orders == nullptr && !OrderList::CanAllocateItem()) {
		/* Restore the train we had. */
		RestoreTrainBackup(original);
		return CommandCost(STR_ERROR_NO_MORE_SPACE_FOR_ORDERS);
	}

	CommandCost cost(ExpensesType::NewVehicles);
	for (Train *part = sell_head; part != nullptr; part = part->Next()) cost.AddCost(-part->value);

	/* do it? */
	if (flags.Test(DoCommandFlag::Execute)) {
		/* First normalise the sub types of the chain. */
		NormaliseSubtypes(new_head);

		if (v == first && !sell_chain && new_head != nullptr && new_head->IsFrontEngine()) {
			if (v->IsEngine()) {
				/* We are selling the front engine. In this case we want to
				 * 'give' the order, unit number and such to the new head. */
				new_head->orders = first->orders;
				new_head->AddToShared(first);
				DeleteVehicleOrders(first);

				/* Copy other important data from the front engine */
				new_head->CopyVehicleConfigAndStatistics(first);
				new_head->speed_restriction = first->speed_restriction;
				Train::From(new_head)->flags.Set(VehicleRailFlag::SpeedAdaptationExempt, Train::From(first)->flags.Test(VehicleRailFlag::SpeedAdaptationExempt));
			}
			GroupStatistics::CountVehicle(new_head, 1); // after copying over the profit, if required
		} else if (v->IsPrimaryVehicle() && backup_order) {
			OrderBackup::Backup(v, user);
		}

		/* We need to update the information about the train. */
		NormaliseTrainHead(new_head);

		/* R3R (couple priority, route A): selling part of a consist can remove the
		 * very segment that owned the schedule (e.g. selling the consist while the
		 * locomotive was executing its schedule). Re-point the survivors at their
		 * own owner *before* the sold part releases the order list, otherwise the
		 * remaining head keeps a pointer into a list that is about to be freed. */
		R3RSyncChainAfterDepotEdit(new_head);

		/* We are undoubtedly changing something in the depot and train list. */
		/* Unless its a virtual train */
		if (!HasBit(v->subtype, GVSF_VIRTUAL)) {
			InvalidateWindowData(WindowClass::VehicleDepot, v->tile.base());
			InvalidateVehicleListWindows(VehicleType::Train);
		}

		/* Actually delete the sold 'goods' */
		delete sell_head;
	} else {
		/* We don't want to execute what we're just tried. */
		RestoreTrainBackup(original);
	}

	return cost;
}

void Train::UpdateDeltaXY()
{
	/* Set common defaults. */
	this->bounds = {{-1, -1, 0}, {3, 3, 6}, {}};

	/* Set if flipped and engine is NOT flagged with custom flip handling. */
	int flipped = this->flags.Test(VehicleRailFlag::Flipped) && !EngInfo(this->engine_type)->misc_flags.Test(EngineMiscFlag::RailFlips);
	/* If flipped and vehicle length is odd, we need to adjust the bounding box offset slightly. */
	int flip_offs = flipped && (this->gcache.cached_veh_length & 1);

	Direction dir = this->direction;
	if (flipped) dir = ReverseDir(dir);

	if (!IsDiagonalDirection(dir)) {
		static constexpr DiagDirectionIndexArray<Point> _sign_table{{{
			/* x, y */
			{-1, -1}, // DiagDirection::N
			{-1,  1}, // DiagDirection::E
			{ 1,  1}, // DiagDirection::S
			{ 1, -1}, // DiagDirection::W
		}}};

		int half_shorten = (VEHICLE_LENGTH - this->gcache.cached_veh_length + flipped) / 2;

		/* For all straight directions, move the bound box to the centre of the vehicle, but keep the size. */
		this->bounds.offset.x -= half_shorten * _sign_table[DirToDiagDir(dir)].x;
		this->bounds.offset.y -= half_shorten * _sign_table[DirToDiagDir(dir)].y;
	} else {
		switch (dir) {
				/* Shorten southern corner of the bounding box according the vehicle length
				 * and center the bounding box on the vehicle. */
			case Direction::NE:
				this->bounds.origin.x = -(this->gcache.cached_veh_length + 1) / 2 + flip_offs;
				this->bounds.extent.x = this->gcache.cached_veh_length;
				this->bounds.offset.x = 1;
				break;

			case Direction::NW:
				this->bounds.origin.y = -(this->gcache.cached_veh_length + 1) / 2 + flip_offs;
				this->bounds.extent.y = this->gcache.cached_veh_length;
				this->bounds.offset.y = 1;
				break;

				/* Move northern corner of the bounding box down according to vehicle length
				 * and center the bounding box on the vehicle. */
			case Direction::SW:
				this->bounds.origin.x = -(this->gcache.cached_veh_length) / 2 - flip_offs;
				this->bounds.extent.x = this->gcache.cached_veh_length;
				this->bounds.offset.x = 1 - (VEHICLE_LENGTH - this->gcache.cached_veh_length);
				break;

			case Direction::SE:
				this->bounds.origin.y = -(this->gcache.cached_veh_length) / 2 - flip_offs;
				this->bounds.extent.y = this->gcache.cached_veh_length;
				this->bounds.offset.y = 1 - (VEHICLE_LENGTH - this->gcache.cached_veh_length);
				break;

			default:
				NOT_REACHED();
		}
	}
}

/**
 * Mark a train as stuck and stop it if it isn't stopped right now.
 * @param consist %Train to mark as being stuck.
 */
static void MarkTrainAsStuck(Train *consist, bool waiting_restriction = false)
{
	if (!consist->flags.Test(VehicleRailFlag::Stuck)) {
		/* It is the first time the problem occurred, set the "train stuck" flag. */
		consist->flags.Set(VehicleRailFlag::Stuck);
		consist->flags.Set(VehicleRailFlag::WaitingRestriction, waiting_restriction);

		consist->wait_counter = 0;

		/* Stop train */
		consist->cur_speed = 0;
		consist->subspeed = 0;
		consist->SetLastSpeed();

		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
	} else if (waiting_restriction != consist->flags.Test(VehicleRailFlag::WaitingRestriction)) {
		consist->flags.Flip(VehicleRailFlag::WaitingRestriction);
		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
	}
}

/**
 * Swap the two up/down flags in two ways:
 * - Swap values of \a swap_flag1 and \a swap_flag2, and
 * - If going up previously (#GVF_GOINGUP_BIT set), the #GVF_GOINGDOWN_BIT is set, and vice versa.
 * @param[in,out] swap_flag1 First train flag.
 * @param[in,out] swap_flag2 Second train flag.
 */
static void SwapTrainFlags(uint16_t *swap_flag1, uint16_t *swap_flag2)
{
	uint16_t flag1 = *swap_flag1;
	uint16_t flag2 = *swap_flag2;

	/* Clear the flags */
	ClrBit(*swap_flag1, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	ClrBit(*swap_flag1, GVF_CHUNNEL_BIT);
	ClrBit(*swap_flag2, GVF_GOINGUP_BIT);
	ClrBit(*swap_flag2, GVF_GOINGDOWN_BIT);
	ClrBit(*swap_flag2, GVF_CHUNNEL_BIT);

	/* Reverse the rail-flags (if needed) */
	if (HasBit(flag1, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag1, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag2, GVF_GOINGUP_BIT);
	}
	if (HasBit(flag2, GVF_GOINGUP_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGDOWN_BIT);
	} else if (HasBit(flag2, GVF_GOINGDOWN_BIT)) {
		SetBit(*swap_flag1, GVF_GOINGUP_BIT);
	}
	if (HasBit(flag1, GVF_CHUNNEL_BIT)) {
		SetBit(*swap_flag2, GVF_CHUNNEL_BIT);
	}
	if (HasBit(flag2, GVF_CHUNNEL_BIT)) {
		SetBit(*swap_flag1, GVF_CHUNNEL_BIT);
	}
}

/**
 * Updates some variables after swapping the vehicle.
 * @param v swapped vehicle
 * @param reverse Should we reverse the direction of the vehicle?
 */
static void UpdateStatusAfterSwap(Train *v, bool reverse = true)
{
	v->InvalidateImageCache();

	/* Maybe reverse the direction. */
	if (reverse) v->direction = ReverseDir(v->direction);

	v->UpdateIsDrawn();

	/* Call the proper EnterTile function unless we are in a wormhole. */
	if (!(v->track & TRACK_BIT_WORMHOLE)) {
		/* R3R (KI-130): Never re-run the tile entry logic for a vehicle standing on
		 * a rail depot tile. VehicleEnterTile_Rail() would read the just-reversed
		 * vehicle as *entering* the depot - the forced turn-around in
		 * ReverseTrainDirection() clears DrivingBackwards before this call, so the
		 * vehicle suddenly faces into the depot while sitting on the depot's
		 * parking coordinate - and then call VehicleEnterDepot() for v->First().
		 * While a consist is in the middle of entering the depot the chain head is
		 * still on plain track, so VehicleEnterDepot() would update the signals of
		 * a non-depot tile with DiagDirection::Invalid and trip the assertion in
		 * TileOffsByDiagDir(). Depot bookkeeping is done once by VehicleEnterDepot()
		 * when the consist really enters (from TrainController) and needs no
		 * refresh here. */
		if (!IsRailDepotTile(v->tile)) VehicleEnterTile(v, v->tile, v->x_pos, v->y_pos);
	} else {
		/* VehicleEnterTile_TunnelBridge() may set TRACK_BIT_WORMHOLE when the vehicle
		 * is on the last bit of the bridge head (frame == TILE_SIZE - 1).
		 * If we were swapped with such a vehicle, we have set TRACK_BIT_WORMHOLE,
		 * when we shouldn't have. Check if this is the case. */
		TileIndex vt = TileVirtXY(v->x_pos, v->y_pos);
		if (IsTileType(vt, TileType::TunnelBridge)) {
			VehicleEnterTile(v, vt, v->x_pos, v->y_pos);
			if (!(v->track & TRACK_BIT_WORMHOLE) && IsBridgeTile(v->tile)) {
				/* We have just left the wormhole, possibly set the
				 * "goingdown" bit. UpdateInclination() can be used
				 * because we are at the border of the tile. */
				v->UpdatePosition();
				v->UpdateInclination(true, true);
				return;
			}
		}
	}

	v->UpdatePosition();
	if (v->track & TRACK_BIT_WORMHOLE) v->UpdateInclination(false, false, true);
	v->UpdateViewport(true, true);
}

/**
 * Swap vehicles \a l and \a r in consist \a v, and reverse their direction.
 * UpdateStatusAfterSwap calls should be made after all ReverseTrainSwapVeh calls have been completed.
 * @param v Consist to change.
 * @param l %Vehicle index in the consist of the first vehicle.
 * @param r %Vehicle index in the consist of the second vehicle.
 */
static void ReverseTrainSwapVeh(Train *v, int l, int r)
{
	Train *a, *b;

	/* locate vehicles to swap */
	for (a = v; l != 0; l--) a = a->Next();
	for (b = v; r != 0; r--) b = b->Next();

	if (a != b) {
		/* swap the hidden bits */
		{
			bool a_hidden = a->vehstatus.Test(VehState::Hidden);
			bool b_hidden = b->vehstatus.Test(VehState::Hidden);
			b->vehstatus.Set(VehState::Hidden, a_hidden);
			a->vehstatus.Set(VehState::Hidden, b_hidden);
		}

		std::swap(a->track, b->track);
		std::swap(a->direction, b->direction);
		std::swap(a->x_pos, b->x_pos);
		std::swap(a->y_pos, b->y_pos);
		std::swap(a->tile,  b->tile);
		std::swap(a->z_pos, b->z_pos);

		SwapTrainFlags(&a->gv_flags, &b->gv_flags);
	} else {
		/* Swap GVF_GOINGUP_BIT/GVF_GOINGDOWN_BIT.
		 * This is a little bit redundant way, a->gv_flags will
		 * be (re)set twice, but it reduces code duplication */
		SwapTrainFlags(&a->gv_flags, &a->gv_flags);
	}
}

/**
 * Swap vehicles in chain starting from \a v, and reverse their direction.
 * @param v First vehicle in chain to change.
 */
void ReverseTrainSwapVehicles(Train *v)
{
	int r = CountVehiclesInChain(v) - 1;  // number of vehicles - 1

	/* swap start<>end, start+1<>end-1, ... */
	int l = 0;
	do {
		ReverseTrainSwapVeh(v, l++, r--);
	} while (l <= r);

	for (Train *u = v; u != nullptr; u = u->Next()) {
		UpdateStatusAfterSwap(u);
	}
}

/**
 * Check if a level crossing tile has a train on it
 * @param tile tile to test
 * @return true if a train is on the crossing
 * @pre tile is a level crossing
 */
bool TrainOnCrossing(TileIndex tile)
{
	assert(IsLevelCrossingTile(tile));

	return GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr;
}

/**
 * Checks if a train is approaching a rail-road crossing
 * @param v vehicle on tile
 * @param tile tile with crossing we are testing
 * @return true if v is approaching a crossing
 */
static bool TrainApproachingCrossingEnum(const Train *t, TileIndex tile)
{
	if (t->vehstatus.Test(VehState::Crashed)) return false;

	if (!t->IsMovingFront()) return false;

	return TrainApproachingCrossingTile(t) == tile;
}


/**
 * Finds a vehicle approaching rail-road crossing
 * @param tile tile to test
 * @return true if a vehicle is approaching the crossing
 * @pre tile is a rail-road crossing
 */
static bool TrainApproachingCrossing(TileIndex tile)
{
	dbg_assert_tile(IsLevelCrossingTile(tile), tile);

	DiagDirection dir = AxisToDiagDir(GetCrossingRailAxis(tile));
	TileIndex tile_from = tile + TileOffsByDiagDir(dir);

	if (HasVehicleOnTile<VehicleType::Train>(tile_from, [&](const Train *t) {
			return TrainApproachingCrossingEnum(t, tile);
		})) return true;

	dir = ReverseDiagDir(dir);
	tile_from = tile + TileOffsByDiagDir(dir);

	return HasVehicleOnTile<VehicleType::Train>(tile_from, [&](const Train *t) {
		return TrainApproachingCrossingEnum(t, tile);
	});
}

/** Check if the crossing should be closed
 *  @return train on crossing || train approaching crossing || reserved
 */
static inline bool CheckLevelCrossing(TileIndex tile)
{
	/* reserved || train on crossing || train approaching crossing */
	return HasCrossingReservation(tile) || TrainOnCrossing(tile) || TrainApproachingCrossing(tile);
}

/**
 * Sets correct crossing state
 * @param tile tile to update
 * @param sound should we play sound?
 * @param is_forced force set the crossing state to that of forced_state
 * @param forced_state the crossing state to set when using is_forced
 * @pre tile is a rail-road crossing
 */
static void UpdateLevelCrossingTile(TileIndex tile, bool sound, bool is_forced, bool forced_state)
{
	dbg_assert_tile(IsLevelCrossingTile(tile), tile);
	bool new_state;

	if (is_forced) {
		new_state = forced_state;
	} else {
		new_state = CheckLevelCrossing(tile);
	}

	if (new_state != IsCrossingBarred(tile)) {
		if (new_state && sound) {
			if (_settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, tile);
		}
		SetCrossingBarred(tile, new_state);
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
}

/**
 * Cycles the adjacent crossings and sets their state
 * @param tile tile to update
 * @param sound should we play sound?
 * @param force_close force close the crossing
 */
void UpdateLevelCrossing(TileIndex tile, bool sound, bool force_close)
{
	bool forced_state = force_close;
	if (!IsLevelCrossingTile(tile)) return;

	const Axis axis = GetCrossingRoadAxis(tile);
	const DiagDirection dir = AxisToDiagDir(axis);
	const DiagDirection reverse_dir = ReverseDiagDir(dir);

	const bool adjacent_crossings = _settings_game.vehicle.adjacent_crossings;
	if (adjacent_crossings) {
		for (TileIndex t = tile; !forced_state && t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, dir)) {
			forced_state |= CheckLevelCrossing(t);
		}
		for (TileIndex t = TileAddByDiagDir(tile, reverse_dir); !forced_state && t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, reverse_dir)) {
			forced_state |= CheckLevelCrossing(t);
		}
	}

	UpdateLevelCrossingTile(tile, sound, adjacent_crossings || force_close, forced_state);
	for (TileIndex t = TileAddByDiagDir(tile, dir); t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, dir)) {
		UpdateLevelCrossingTile(t, sound, adjacent_crossings, forced_state);
	}
	for (TileIndex t = TileAddByDiagDir(tile, reverse_dir); t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == axis; t = TileAddByDiagDir(t, reverse_dir)) {
		UpdateLevelCrossingTile(t, sound, adjacent_crossings, forced_state);
	}
}

void MarkDirtyAdjacentLevelCrossingTilesOnAdd(TileIndex tile, Axis road_axis)
{
	if (!_settings_game.vehicle.adjacent_crossings) return;

	const DiagDirection dir1 = AxisToDiagDir(road_axis);
	const DiagDirection dir2 = ReverseDiagDir(dir1);
	for (DiagDirection dir : { dir1, dir2 }) {
		const TileIndex t = TileAddByDiagDir(tile, dir);
		if (t < Map::Size() && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis) {
			MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
		}
	}
}

void UpdateAdjacentLevelCrossingTilesOnRemove(TileIndex tile, Axis road_axis)
{
	const DiagDirection dir1 = AxisToDiagDir(road_axis);
	const DiagDirection dir2 = ReverseDiagDir(dir1);
	for (DiagDirection dir : { dir1, dir2 }) {
		const TileIndexDiff diff = TileOffsByDiagDir(dir);
		bool occupied = false;
		for (TileIndex t = tile + diff; IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis; t += diff) {
			occupied |= CheckLevelCrossing(t);
		}
		if (occupied) {
			/* Mark the immediately adjacent tile dirty */
			const TileIndex t = tile + diff;
			if (IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis) {
				MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
			}
		} else {
			/* Unbar the crossing tiles in this direction as necessary */
			for (TileIndex t = tile + diff; IsValidTile(t) && IsLevelCrossingTile(t) && GetCrossingRoadAxis(t) == road_axis; t += diff) {
				if (IsCrossingBarred(t)) {
					/* The crossing tile is barred, unbar it and continue to check the next tile */
					SetCrossingBarred(t, false);
					MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
				} else {
					/* The crossing tile is already unbarred, mark the tile dirty and stop checking */
					MarkTileDirtyByTile(t, VMDF_NOT_MAP_MODE);
					break;
				}
			}
		}
	}
}

/**
 * Check if the level crossing is occupied by road vehicle(s).
 * @param t The tile to query.
 * @pre IsLevelCrossing(t)
 * @return True if the level crossing is marked as occupied.
 */
bool IsCrossingOccupiedByRoadVehicle(TileIndex t)
{
	if (!IsCrossingPossiblyOccupiedByRoadVehicle(t)) return false;
	const bool occupied = IsTrainCollidableRoadVehicleOnGround(t);
	SetCrossingOccupiedByRoadVehicle(t, occupied);
	return occupied;
}


/**
 * Bars crossing and plays ding-ding sound if not barred already
 * @param tile tile with crossing
 * @pre tile is a rail-road crossing
 */
static inline void MaybeBarCrossingWithSound(TileIndex tile)
{
	if (!IsCrossingBarred(tile)) {
		UpdateLevelCrossing(tile, true, true);
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called before the train is reversed.
 * @param moving_front Moving front vehicle
 */
static void AdvanceWagonsBeforeSwap(Train *moving_front)
{
	Train *base = moving_front;
	Train *first = base; // first vehicle to move
	Train *last = moving_front->GetMovingBack(); // last vehicle to move
	uint length = CountVehiclesInChain(moving_front->First());

	while (length > 2) {
		last = last->GetMovingPrev();
		first = first->GetMovingNext();

		int differential = base->CalcNextVehicleOffset() - last->CalcNextVehicleOffset();

		/* do not update images now
		 * negative differential will be handled in AdvanceWagonsAfterSwap() */
		for (int i = 0; i < differential; i++) TrainController(first, last->GetMovingNext());

		base = first; // == base->GetMovingNext()
		length -= 2;
	}
}


/**
 * Advances wagons for train reversing, needed for variable length wagons.
 * This one is called after the train is reversed.
 * @param moving_front Moving front vehicle
 */
static void AdvanceWagonsAfterSwap(Train *moving_front)
{
	/* first of all, fix the situation when the train was entering a depot */
	Train *dep = moving_front; // last vehicle in front of just left depot
	while (dep->GetMovingNext() != nullptr && (dep->track == TRACK_BIT_DEPOT || dep->GetMovingNext()->track != TRACK_BIT_DEPOT)) {
		dep = dep->GetMovingNext(); // find first vehicle outside of a depot, with next vehicle inside a depot
	}

	Train *leave = dep->GetMovingNext(); // first vehicle in a depot we are leaving now

	if (leave != nullptr) {
		/* 'pull' next wagon out of the depot, so we won't miss it (it could stay in depot forever) */
		int d = TicksToLeaveDepot(dep);

		if (d <= 0) {
			leave->vehstatus.Reset(VehState::Hidden); // move it out of the depot
			leave->track = TrackToTrackBits(GetRailDepotTrack(leave->tile));
			for (int i = 0; i >= d; i--) TrainController(leave, nullptr); // maybe move it, and maybe let another wagon leave
		}
	} else {
		dep = nullptr; // no vehicle in a depot, so no vehicle leaving a depot
	}

	Train *base = moving_front;
	Train *first = base; // first vehicle to move
	Train *last = moving_front->GetMovingBack(); // last vehicle to move
	uint length = CountVehiclesInChain(moving_front->First());

	/* We have to make sure all wagons that leave a depot because of train reversing are moved correctly
	 * they have already correct spacing, so we have to make sure they are moved how they should */
	bool nomove = (dep == nullptr); // If there is no vehicle leaving a depot, limit the number of wagons moved immediately.

	while (length > 2) {
		/* we reached vehicle (originally) in front of a depot, stop now
		 * (we would move wagons that are already moved with new wagon length). */
		if (base == dep) break;

		/* the last wagon was that one leaving a depot, so do not move it anymore */
		if (last == dep) nomove = true;

		last = last->GetMovingPrev();
		first = first->GetMovingNext();

		int differential = last->CalcNextVehicleOffset() - base->CalcNextVehicleOffset();

		/* do not update images now */
		for (int i = 0; i < differential; i++) TrainController(first, (nomove ? last->GetMovingNext() : nullptr));

		base = first; // == base->GetMovingNext()
		length -= 2;
	}
}

static bool IsWholeTrainInsideDepot(const Train *v)
{
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return false;
	}
	return true;
}

/**
 * Turn a train around.
 * @param consist %Train to turn around.
 */
static void ReverseTrainDirection(Train *consist)
{
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "REVERSEDIR veh=%d tile=%d,%d dir=%d order=%d spd=%d nv=%d rev=%d stuck=%d db=%d\n",
					(int)consist->index.base(), (int)TileX(consist->tile), (int)TileY(consist->tile),
					(int)consist->direction, (int)consist->current_order.GetType(), (int)consist->cur_speed,
					(int)CountVehiclesInChain(consist),
					(int)consist->flags.Test(VehicleRailFlag::Reversing),
					(int)consist->flags.Test(VehicleRailFlag::Stuck),
					(int)consist->vehicle_flags.Test(VehicleFlag::DrivingBackwards));
			for (const Train *w = consist; w != nullptr; w = w->Next()) {
				fprintf(dbg, "  RF idx=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X\n",
						(int)w->index.base(), (int)w->x_pos, (int)w->y_pos,
						(int)TileX(w->tile), (int)TileY(w->tile), (int)w->direction, (uint)w->track);
			}
			fclose(dbg);
		}
	}
	Train *moving_front = consist->GetMovingFront();
	if (IsRailDepotTile(moving_front->tile)) {
		if (IsWholeTrainInsideDepot(consist)) return;
		InvalidateWindowData(WindowClass::VehicleDepot, moving_front->tile.base());
	}

	if (_local_company == consist->owner && (consist->current_order.IsType(OT_LOADING_ADVANCE) || moving_front->flags.Test(VehicleRailFlag::BeyondPlatformEnd))) {
		EncodedString msg = GetEncodedString(STR_VEHICLE_LOAD_THROUGH_ABORTED_INSUFFICIENT_TRACK, consist->index, consist->current_order.GetDestination().ToStationID());
		AddNewsItem(std::move(msg), NewsType::Advice, NewsStyle::Small, {NewsFlag::InColour, NewsFlag::VehicleParam0},
				consist->index, consist->current_order.GetDestination().ToStationID());
	}

	Train *moving_back = consist->GetMovingBack();

	if (consist->current_order.IsType(OT_LOADING_ADVANCE)) {
		consist->LeaveStation();

		/* Only advance to next order if we are loading at the current one */
		const Order *order = consist->GetOrder(consist->cur_implicit_order_index);
		if (order != nullptr && order->IsType(OT_GOTO_STATION) && order->GetDestination() == consist->last_station_visited) {
			consist->IncrementImplicitOrderIndex();
		}
	} else if (consist->current_order.IsAnyLoadingType()) {
		/* Not a station || different station --> leave the station */
		if (!IsTileType(moving_back->tile, TileType::Station) || !IsTileType(moving_front->tile, TileType::Station) ||
				GetStationIndex(moving_back->tile) != GetStationIndex(moving_front->tile) ||
				moving_front->flags.Test(VehicleRailFlag::BeyondPlatformEnd)) {
			consist->LeaveStation();
		}
	}

	for (Train *u = consist; u != nullptr; u = u->Next()) {
		u->flags.Reset({VehicleRailFlag::BeyondPlatformEnd, VehicleRailFlag::NotYetInPlatform});
	}

	consist->reverse_distance = 0;

	bool no_near_end_unreserve = false;
	bool no_far_end_unreserve = false;
	{
		/* Temporarily clear and restore reservations to bidi tunnel/bridge entrances when reversing train inside,
		 * to avoid outgoing and incoming reservations becoming merged */
		auto find_train_reservations = [consist](TileIndex tile, bool &found_reservation) {
			TrackBits reserved = GetAcrossTunnelBridgeReservationTrackBits(tile);
			Track track;
			while ((track = RemoveFirstTrack(&reserved)) != INVALID_TRACK) {
				Train *res_train = GetTrainForReservation(tile, track);
				if (res_train != nullptr && res_train != consist) {
					found_reservation = true;
				}
			}
		};
		if (IsTunnelBridgeWithSignalSimulation(moving_front->tile) && IsTunnelBridgeSignalSimulationBidirectional(moving_front->tile)) {
			find_train_reservations(moving_front->tile, no_near_end_unreserve);
			find_train_reservations(GetOtherTunnelBridgeEnd(moving_front->tile), no_far_end_unreserve);
		}
	}

	/* Clear path reservation in front if train is not stuck. */
	if (!consist->flags.Test(VehicleRailFlag::Stuck) && !no_near_end_unreserve && !no_far_end_unreserve) {
		FreeTrainTrackReservation(consist);
	} else {
		consist->lookahead.reset();
	}

	if ((moving_front->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(moving_front->tile)) {
		/* Clear exit tile reservation if train was on approach to exit and had reserved it */
		Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(moving_front->tile));
		DiagDirection axial_dir = DirToDiagDirAlongAxis(moving_front->GetMovingDirection(), axis);
		TileIndex next_tile = TileVirtXY(moving_front->x_pos, moving_front->y_pos) + TileOffsByDiagDir(axial_dir);
		if ((!no_near_end_unreserve && next_tile == moving_front->tile) || (!no_far_end_unreserve && next_tile == GetOtherTunnelBridgeEnd(moving_front->tile))) {
			Trackdir exit_td = GetTunnelBridgeExitTrackdir(next_tile);
			CFollowTrackRail ft(GetTileOwner(next_tile), consist->GetIndirectCompatibleRailTypes());
			if (ft.Follow(next_tile, exit_td)) {
				TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));
				if (reserved == TRACKDIR_BIT_NONE) {
					UnreserveBridgeTunnelTile(next_tile);
					MarkTileDirtyByTile(next_tile, VMDF_NOT_MAP_MODE);
				}
			} else {
				UnreserveBridgeTunnelTile(next_tile);
				MarkTileDirtyByTile(next_tile, VMDF_NOT_MAP_MODE);
			}
		}
	}

	/* Check if we were approaching a rail/road-crossing */
	TileIndex crossing = TrainApproachingCrossingTile(moving_front);

	/* Check if we should back up or flip the train. */
	if (consist->vehicle_flags.Test(VehicleFlag::DrivingBackwards) || _settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None || consist->Last()->CanLeadTrain()) {
		/* The train will back up. */
		for (Train *u = consist; u != nullptr; u = u->Next()) {
			u->vehicle_flags.Flip(VehicleFlag::DrivingBackwards);

			/* Invert going up/down */
			if (HasBit(u->gv_flags, GVF_GOINGUP_BIT) || HasBit(u->gv_flags, GVF_GOINGDOWN_BIT)) {
				ToggleBit(u->gv_flags, GVF_GOINGDOWN_BIT);
				ToggleBit(u->gv_flags, GVF_GOINGUP_BIT);
			}
			UpdateStatusAfterSwap(u, false);
		}
		/* We may have entered a depot and stopped driving backwards. */
		std::swap(moving_front, moving_back);
	} else {
		/* The train will flip. */
		AdvanceWagonsBeforeSwap(moving_front);

		/* swap start<>end, start+1<>end-1, ... */
		ReverseTrainSwapVehicles(consist);

		AdvanceWagonsAfterSwap(moving_front);
	}

	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "REVERSEDONE db=%d mvfront=%d\n",
					(int)consist->vehicle_flags.Test(VehicleFlag::DrivingBackwards),
					(int)consist->GetMovingFront()->index.base());
			for (const Train *w = consist; w != nullptr; w = w->Next()) {
				fprintf(dbg, "  RA idx=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X\n",
						(int)w->index.base(), (int)w->x_pos, (int)w->y_pos,
						(int)TileX(w->tile), (int)TileY(w->tile), (int)w->direction, (uint)w->track);
			}
			fclose(dbg);
		}
	}

	ClrBit(consist->vcache.cached_veh_flags, VCF_GV_ZERO_SLOPE_RESIST);

	if (IsRailDepotTile(moving_front->tile)) {
		InvalidateWindowData(WindowClass::VehicleDepot, moving_front->tile.base());
	}

	consist->flags.Flip(VehicleRailFlag::Reversed);
	consist->flags.Reset(VehicleRailFlag::Reversing);

	/* recalculate cached data */
	consist->ConsistChanged(CCF_TRACK);

	/* update all images */
	for (Train *u = consist; u != nullptr; u = u->Next()) u->UpdateViewport(false, false);

	/* update crossing we were approaching */
	if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

	/* maybe we are approaching crossing now, after reversal */
	crossing = TrainApproachingCrossingTile(moving_front);
	if (crossing != INVALID_TILE) MaybeBarCrossingWithSound(crossing);

	if (consist->flags.Test(VehicleRailFlag::PendingSpeedRestriction)) {
		for (auto it = _pending_speed_restriction_change_map.lower_bound(consist->index); it != _pending_speed_restriction_change_map.end() && it->first == consist->index;) {
			it->second.distance = (consist->gcache.cached_total_length + (HasBit(it->second.flags, PSRCF_DIAGONAL) ? 8 : 4)) - it->second.distance;
			if (it->second.distance == 0) {
				consist->speed_restriction = it->second.prev_speed;
				it = _pending_speed_restriction_change_map.erase(it);
			} else {
				std::swap(it->second.prev_speed, it->second.new_speed);
				++it;
			}
		}
	}

	/* If we are inside a depot after reversing, don't bother with path reserving. */
	if (moving_front->track == TRACK_BIT_DEPOT) {
		/* Can't be stuck here as inside a depot is always a safe tile. */
		if (consist->flags.Test(VehicleRailFlag::Stuck)) SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		consist->flags.Reset(VehicleRailFlag::Stuck);
		return;
	}

	auto update_check_tunnel_bridge_signal_counters = [](Train *t) {
		if (!(t->track & TRACK_BIT_WORMHOLE)) {
			/* Not in wormhole, clear counters */
			t->tunnel_bridge_tile_ctr = 0;
			t->tunnel_bridge_signal_num = 0;
			return;
		}

		DiagDirection tb_dir = GetTunnelBridgeDirection(t->tile);
		if (DirToDiagDirAlongAxis(t->GetMovingDirection(), DiagDirToAxis(tb_dir)) == tb_dir) {
			/* Now going in correct direction, fix counters */
			const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(t->tile);
			const uint delta = DistanceManhattan(t->tile, TileVirtXY(t->x_pos, t->y_pos));
			t->tunnel_bridge_tile_ctr = static_cast<uint8_t>((simulated_wormhole_signals - 1) - (delta % simulated_wormhole_signals));
			t->tunnel_bridge_signal_num = delta / simulated_wormhole_signals;
		} else {
			/* Now going in wrong direction, all bets are off.
			 * Prevent setting the wrong signals by making tunnel_bridge_tile_ctr TBS_INVALID_DISTANCE.
			 * This is a large value so that the train will reverse again if there is another vehicle coming the other way.
			 */
			t->tunnel_bridge_tile_ctr = Train::TBS_INVALID_DISTANCE;
			t->tunnel_bridge_signal_num = 0;
		}
	};

	if (IsTunnelBridgeWithSignalSimulation(moving_back->tile) && IsTunnelBridgeSignalSimulationEntrance(moving_back->tile)) {
		update_check_tunnel_bridge_signal_counters(moving_back);
	}

	/* We are inside tunnel/bridge with signals, reversing will close the entrance. */
	if (IsTunnelBridgeWithSignalSimulation(moving_front->tile) && IsTunnelBridgeSignalSimulationEntrance(moving_front->tile)) {
		/* Flip signal on tunnel entrance tile red. */
		SetTunnelBridgeEntranceSignalState(moving_front->tile, SignalState::Red);
		if (_extra_aspects > 0) {
			PropagateAspectChange(moving_front->tile, GetTunnelBridgeEntranceTrackdir(moving_front->tile), 0);
		}
		MarkTileDirtyByTile(moving_front->tile, VMDF_NOT_MAP_MODE);
		update_check_tunnel_bridge_signal_counters(moving_front);
		if ((moving_front->track & TRACK_BIT_WORMHOLE) || TrackdirEntersTunnelBridge(moving_front->tile, moving_front->GetVehicleTrackdir())) {
			consist->flags.Reset(VehicleRailFlag::Stuck);
			return;
		}
	}

	/* VehicleExitDir does not always produce the desired dir for depots and
	 * tunnels/bridges that is needed for UpdateSignalsOnSegment. */
	DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
	if (IsRailDepotTile(moving_front->tile) || (IsTileType(moving_front->tile, TileType::TunnelBridge) && (moving_front->track & TRACK_BIT_WORMHOLE || dir == GetTunnelBridgeDirection(moving_front->tile)))) dir = DiagDirection::Invalid;

	if (UpdateSignalsOnSegment(moving_front->tile, dir, consist->owner) == SigSegState::Path || _settings_game.pf.reserve_paths) {
		/* R3R (KI-108): TrackdirToExitdir() asserts on INVALID_TRACKDIR (track_func.h:396),
		 * so resolve the trackdir once and skip the trackdir dependent checks if we do not
		 * have one (crashed vehicle). */
		const Trackdir moving_front_td = moving_front->GetVehicleTrackdir();

		/* If we are currently on a tile with conventional signals, we can't treat the
		 * current tile as a safe tile or we would enter a PBS block without a reservation. */
		bool first_tile_okay = moving_front_td == INVALID_TRACKDIR || !HasBlockSignalOnTrackdir(moving_front->tile, moving_front_td);

		/* If we are on a depot tile facing outwards, do not treat the current tile as safe. */
		if (moving_front_td != INVALID_TRACKDIR && IsRailDepotTile(moving_front->tile) && TrackdirToExitdir(moving_front_td) == GetRailDepotDirection(moving_front->tile)) first_tile_okay = false;

		/* If we are on a signalled tunnel/bridge end-tile in the exit direction, do not treat the current tile as safe. */
		if (moving_front_td != INVALID_TRACKDIR && IsTunnelBridgeWithSignalSimulation(moving_front->tile) && !(moving_front->track & TRACK_BIT_WORMHOLE) && TrackdirExitsTunnelBridge(moving_front->tile, moving_front_td)) first_tile_okay = false;

		if (moving_front_td != INVALID_TRACKDIR && IsRailStationTile(moving_front->tile)) SetRailStationPlatformReservation(moving_front->tile, TrackdirToExitdir(moving_front_td), true);
		if (TryPathReserve(consist, false, first_tile_okay)) {
			/* Do a look-ahead now in case our current tile was already a safe tile. */
			CheckNextTrainTile(moving_front);
		} else if (consist->current_order.GetType() != OT_LOADING) {
			/* Do not wait for a way out when we're still loading */
			MarkTrainAsStuck(consist);
		}
	} else if (consist->flags.Test(VehicleRailFlag::Stuck)) {
		/* A train not inside a PBS block can't be stuck. */
		consist->flags.Reset(VehicleRailFlag::Stuck);
		consist->wait_counter = 0;
	}
}

/**
 * Reverse train.
 * @param flags type of operation
 * @param veh_id train to reverse
 * @param reverse_single_veh if true, reverse a unit in a train (needs to be in a depot)
 * @return the cost of this operation or an error
 */
CommandCost CmdReverseTrainDirection(DoCommandFlags flags, VehicleID veh_id, bool reverse_single_veh)
{
	Train *v = Train::GetIfValid(veh_id);
	if (v == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (reverse_single_veh) {
		/* turn a single unit around */

		if (v->IsMultiheaded() || EngInfo(v->engine_type)->callback_mask.Test(VehicleCallbackMask::ArticEngine)) {
			return CommandCost(STR_ERROR_CAN_T_REVERSE_DIRECTION_RAIL_VEHICLE_MULTIPLE_UNITS);
		}

		Train *front = v->First();
		/* make sure the vehicle is stopped in the depot */
		if (!front->IsStoppedInDepot() && !front->IsVirtual()) {
			return CommandCost(STR_ERROR_TRAINS_CAN_ONLY_BE_ALTERED_INSIDE_A_DEPOT);
		}

		if (flags.Test(DoCommandFlag::Execute)) {
			v->flags.Flip(VehicleRailFlag::Flipped);

			front->ConsistChanged(CCF_ARRANGE);
			SetWindowDirty(WindowClass::VehicleDepot, front->tile.base());
			SetWindowDirty(WindowClass::VehicleDetails, front->index);
			SetWindowDirty(WindowClass::VehicleView, front->index);
			DirtyVehicleListWindowForVehicle(front);
		}
	} else {
		/* turn the whole train around */
		if (!v->IsPrimaryVehicle()) return CMD_ERROR;
		if (v->vehstatus.Test(VehState::Crashed) || v->flags.Test(VehicleRailFlag::BreakdownStopped)) return CMD_ERROR;

		if (flags.Test(DoCommandFlag::Execute)) {
			/* Properly leave the station if we are loading and won't be loading anymore */
			if (v->current_order.IsAnyLoadingType()) {
				const Train *moving_front = v->GetMovingFront();
				const Train *moving_back = v->GetMovingBack();

				/* not a station || different station --> leave the station */
				if (!IsTileType(moving_back->tile, TileType::Station) || !IsTileType(moving_front->tile, TileType::Station) ||
						GetStationIndex(moving_back->tile) != GetStationIndex(moving_front->tile) ||
						moving_front->flags.Test(VehicleRailFlag::BeyondPlatformEnd) ||
						v->current_order.IsType(OT_LOADING_ADVANCE)) {
					v->LeaveStation();
				}
			}

			/* We cancel any 'skip signal at dangers' here */
			v->force_proceed = TFP_NONE;
			InvalidateWindowData(WindowClass::VehicleView, v->index);

			if (_settings_game.vehicle.train_acceleration_model != AM_ORIGINAL && v->cur_speed != 0) {
				v->flags.Flip(VehicleRailFlag::Reversing);
			} else {
				v->cur_speed = 0;
				v->SetLastSpeed();
				HideFillingPercent(&v->fill_percent_te_id);
				ReverseTrainDirection(v);
			}

			/* Unbunching data is no longer valid. */
			v->ResetDepotUnbunching();
		}
	}
	return CommandCost();
}

/**
 * Determine to what force_proceed should be changed.
 * If we are forced to proceed, cancel that order.
 * If we are marked stuck we would want to force the train to
 * proceed to the next signal unless we are stuck just before
 * the next signal. In the other cases we would like to pass
 * the signal at danger and run till the next signal we encounter.
 * @param t The train to determine the new value of force_proceed for.
 * @return The next state of force_proceed.
 */
static TrainForceProceeding DetermineNextTrainForceProceeding(const Train *t)
{
	if (t->vehstatus.Test(VehState::Crashed) || t->force_proceed == TFP_SIGNAL) return TFP_NONE;
	if (!t->flags.Test(VehicleRailFlag::Stuck)) return t->IsChainInDepot() ? TFP_STUCK : TFP_SIGNAL;

	const Train *moving_front = t->GetMovingFront();
	/* R3R (KI-108): TrackdirToExitdir() asserts on INVALID_TRACKDIR (track_func.h:396). */
	const Trackdir moving_front_td = moving_front->GetVehicleTrackdir();
	if (unlikely(moving_front_td == INVALID_TRACKDIR)) return TFP_STUCK;
	const DiagDirection moving_front_exitdir = TrackdirToExitdir(moving_front_td);
	TileIndex next_tile = TileAddByDiagDir(moving_front->tile, moving_front_exitdir);
	if (next_tile == INVALID_TILE || !IsTileType(next_tile, TileType::Railway) || !HasSignals(next_tile)) return TFP_STUCK;
	TrackBits new_tracks = DiagdirReachesTracks(moving_front_exitdir) & GetTrackBits(next_tile);
	return new_tracks != TRACK_BIT_NONE && HasSignalOnTrack(next_tile, FindFirstTrack(new_tracks)) ? TFP_SIGNAL : TFP_STUCK;
}

/**
 * Force a train through a red signal
 * @param flags type of operation
 * @param veh_id train to ignore the red signal
 * @return the cost of this operation or an error
 */
CommandCost CmdForceTrainProceed(DoCommandFlags flags, VehicleID veh_id)
{
	Train *t = Train::GetIfValid(veh_id);
	if (t == nullptr) return CMD_ERROR;

	if (!t->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(t);
	if (ret.Failed()) return ret;


	if (flags.Test(DoCommandFlag::Execute)) {
		t->force_proceed = DetermineNextTrainForceProceeding(t);
		InvalidateWindowData(WindowClass::VehicleView, t->index);

		/* Unbunching data is no longer valid. */
		t->ResetDepotUnbunching();
	}

	return CommandCost();
}

/**
 * Check whether the train can be decoupled at the current location.
 * @param v %Train to check.
 * @return True if the train can be decoupled.
 */
static bool CanDecouple(const Train *v)
{
	if (v->IsInDepot()) return false;
	if (CountVehiclesInChain(v) < 2) return false;
	if (v->GetNextUnit() == nullptr) return false;
	return true;
}

/**
 * Whether the chain contains an engine, i.e. is a powered segment that can
 * hold its own schedule. Unpowered groups (pure wagon chains) cannot carry
 * orders, so they are never treated as decouplable segments.
 * @param v Chain head.
 * @return True if the chain contains at least one engine.
 */
static bool TrainHasEngine(const Train *v)
{
	for (const Train *t = v; t != nullptr; t = t->GetNextVehicle()) {
		if (t->IsEngine()) return true;
	}
	return false;
}

/**
 * Get the head of the Nth trailing segment of a coupled train.
 * A segment is a chain that was coupled onto this train: its front vehicle
 * carries the SegmentFront marker. The front (engine) part of the train is
 * not a segment itself.
 * @param v            %Train to decouple from.
 * @param num_segments Number of trailing segments to take (1 = the last segment).
 * @return The first vehicle of the decoupled part, or nullptr when the train
 *         has no coupled-on segments.
 */
static Train *GetSegmentHeadFromRear(Train *v, uint num_segments)
{
	std::vector<Train *> segment_heads;
	/* R3R: the chain's own front (v) is never a coupled-on segment, even when a
	 * stale SegmentFront marker survived a depot split / re-couple of a once
	 * detached segment that is now running as its own train — skip it so the
	 * decouple never tries to split off the locomotive itself. */
	for (Train *t = v->GetNextVehicle(); t != nullptr; t = t->GetNextVehicle()) {
		if (t->IsSegmentFront()) segment_heads.push_back(t);
	}
	if (segment_heads.empty()) return nullptr;
	if (num_segments == 0) num_segments = 1;
	if (num_segments > segment_heads.size()) num_segments = static_cast<uint>(segment_heads.size());
	return segment_heads[segment_heads.size() - num_segments];
}

/**
 * R3R: Get the first vehicle released when splitting a coupled train between
 * segment n and n + 1, counted from the head WITH the train's own front part as
 * segment 1 (the same numbering `R3RGetSegmentHeads()` and the order label use).
 * So n = 1 keeps only the locomotive's own segment (releases every coupled-on
 * segment) and n = number of coupled-on segments releases just the last one.
 *
 * The boundary value n is clamped into [1, coupled]: when the train now has
 * fewer segments than the order requests (e.g. the consist changed since the
 * order was written), the split is reduced to the last segment boundary so at
 * least one segment stays behind. Returns nullptr only when the train has no
 * coupled-on segment at all, letting the caller fall back to the auto heuristic.
 * @param v               %Train to decouple from.
 * @param boundary_segments Segment index n (split between segment n and n + 1).
 * @return The first vehicle of the rear part, or nullptr on failure.
 */
static Train *GetSegmentBoundaryFromHead(Train *v, uint boundary_segments)
{
	std::vector<Train *> segment_heads;
	for (Train *t = v->GetNextVehicle(); t != nullptr; t = t->GetNextVehicle()) {
		if (t->IsSegmentFront()) segment_heads.push_back(t);
	}
	const uint coupled = (uint)segment_heads.size();
	if (coupled == 0) return nullptr; /* Nothing to split off at all. */

	/* R3R (2026-09-23): n counts segments from the head INCLUDING the train's
	 * own front part, exactly as the order label ("split between segment n and
	 * n + 1") and R3RGetSegmentHeads() number them (segment 1 = the chain head's
	 * own segment). So n = 1 releases everything behind the locomotive
	 * (segment_heads[0]) and n = coupled releases only the last one.
	 * The old code indexed segment_heads[] one-based over the coupled-on
	 * segments only, which shifted every request by one segment: for a train
	 * with two coupled-on segments every n >= 1 released just the tail segment
	 * (玩家实测 2026-09-23 "只会解挂尾部段"). */
	uint n = std::min(boundary_segments, coupled);
	if (n == 0) n = 1;
	return segment_heads[n - 1];
}

/**
 * Automatically determine the number of vehicles to decouple,
 * based on the consist layout (engines at front/back, wagons in between).
 * @param v %Train to decouple from.
 * @return Number of vehicles to decouple.
 */
static uint GetDecoupleVehicleAuto(const Train *v)
{
	uint engines_front = 0;
	uint engines_back = 0;
	uint pos = 1;
	bool has_wagons = false;
	bool multihead_front = false;
	for (const Train *t = v; t != nullptr; t = t->GetNextVehicle(), pos++) {
		if (t->IsEngine()) {
			if (t->IsMultiheaded()) {
				if (multihead_front) return pos;
				multihead_front = true;
			}
			if (has_wagons) {
				engines_back++;
			} else {
				engines_front++;
			}
		} else {
			has_wagons = true;
		}
	}
	if (engines_front > 0 && has_wagons) return engines_front;
	if (engines_back > 0 && has_wagons) return pos - 1 - engines_back; /* pos counts independent vehicles (articulated parts skipped). */
	return 1;
}

/**
 * R3R (couple priority, route A): enumerate the segment heads of a consist.
 *
 * The segments of a consist are its chain head plus every vehicle carrying the
 * VehicleRailFlag::SegmentFront marker, in chain order -- the same split
 * R3RFlipChainBySegments uses. The chain head is always a segment head even
 * when it carries no marker (a plain locomotive + wagons consist is one
 * segment).
 *
 * @param chain Head of the consist.
 * @return The segment heads, front to tail.
 */
static std::vector<Train *> R3RGetSegmentHeads(Train *chain)
{
	std::vector<Train *> segs;
	if (chain == nullptr) return segs;
	segs.push_back(chain);
	for (Train *w = chain->Next(); w != nullptr; w = w->Next()) {
		if (w->IsSegmentFront()) segs.push_back(w);
	}
	return segs;
}

/**
 * R3R (couple priority, route A): the consist's "command owner" -- the segment
 * with the lowest r3r_priority. Its schedule is the one the consist executes
 * and displays.
 * @param chain Head of the consist.
 * @return The command-owner segment (never nullptr for a valid chain).
 */
static Train *R3RGetLowestPriority(const std::vector<Train *> &segs)
{
	Train *best = nullptr;
	for (Train *s : segs) {
		if (s != nullptr && (best == nullptr || s->r3r_priority < best->r3r_priority)) best = s;
	}
	return best;
}

static Train *R3RGetPriorityHead(Train *chain)
{
	Train *best = R3RGetLowestPriority(R3RGetSegmentHeads(chain));
	return (best != nullptr) ? best : chain;
}

/**
 * R3R (KI-215b, 2026-09-26): push the driven position back onto the command owner.
 *
 * During a borrow only the chain head is ticked, so the position it reaches lives
 * in the head alone while the command owner keeps the index it had when the
 * borrow started. Every hand-over, however, inherits the progress from the
 * OWNER (R3RSyncDrivingOrders(chain, true) reads owner_real), so that frozen
 * index is silently fed back: 现场 4155 行 DECOUPLE-DONE u=0 real=15 —— 解出的
 * 车底被钉在 15（一条已经跑过的 DECOUPLE），而它刚跑到的是 18；下一台机车挂上
 * 时又会照抄 15 再跳一次。凡是"借用结束/驱动者换人"的交接点（解挂这里是主要
 * 一处），都要把链头当前位置写回所有者，让"所有者索引"与"实际跑到的位置"
 * 始终是同一个值。
 *
 * 只在所有者确实指着同一张表时写；表不同（另一侧的排程）绝不碰。
 */
static void R3RPushProgressToOwner(Train *chain)
{
	if (chain == nullptr || chain->orders == nullptr) return;
	Train *const owner = R3RGetPriorityHead(chain);
	if (owner == nullptr || owner == chain || owner->orders != chain->orders) return;
	if (owner->cur_real_order_index == chain->cur_real_order_index &&
			owner->cur_implicit_order_index == chain->cur_implicit_order_index) {
		return;
	}
	FILE *dbg = R3RFopenDbg("a");
	if (dbg != nullptr) {
		fprintf(dbg, "ORD-PUSH head=%d owner=%d real=%d->%d\n",
			(int)chain->index.base(), (int)owner->index.base(),
			(int)owner->cur_real_order_index, (int)chain->cur_real_order_index);
		fclose(dbg);
	}
	owner->cur_real_order_index = chain->cur_real_order_index;
	owner->cur_implicit_order_index = chain->cur_implicit_order_index;
	owner->cur_timetable_order_index = chain->cur_timetable_order_index;
}

/**
 * R3R (couple priority, route A): apply the couple rule "passive list first,
 * active list appended". Every passive segment must outrank every active one,
 * each side keeping its own internal order -- done by shifting the active side
 * above the passive side; R3RRenumberPriorities() compacts afterwards.
 * The two lists are captured BEFORE the merge, because a fold-fix flip may
 * relocate the segment markers during TryTrainCouple.
 * @param passive_segs Segments of the waiting consist.
 * @param active_segs Segments of the coupling locomotive.
 */
static void R3RMergePriorities(const std::vector<Train *> &passive_segs, const std::vector<Train *> &active_segs)
{
	const uint16_t offset = (uint16_t)passive_segs.size();
	for (Train *s : active_segs) {
		if (s != nullptr) s->r3r_priority = (uint16_t)(s->r3r_priority + offset);
	}
}

/**
 * R3R (couple priority, route A): renumber the segments' priorities to 1..n,
 * preserving their current relative order.
 *
 * Called after a couple (passive list first, active list appended -- see
 * R3RMergePriorities) and after a decouple (each part compacted on its own), so
 * that priority is always a contiguous rank where "lowest" == "first segment".
 *
 * @param chain Head of the consist.
 */
static void R3RRenumberPriorities(Train *chain)
{
	std::vector<Train *> segs = R3RGetSegmentHeads(chain);
	uint16_t next = 1;
	/* Repeatedly take the lowest remaining priority; scanning left to right
	 * with a strict < keeps ties in physical order. Segment counts are tiny. */
	while (!segs.empty()) {
		size_t best = 0;
		for (size_t i = 1; i < segs.size(); i++) {
			if (segs[i]->r3r_priority < segs[best]->r3r_priority) best = i;
		}
		segs[best]->r3r_priority = next++;
		segs.erase(segs.begin() + best);
	}
}

/**
 * R3R (couple priority, route A): rebuild the runtime priority/borrow state of a
 * freshly loaded consist.
 *
 * The order *pointers* do survive a load: an OrderList is saved by id and shared
 * between vehicles the native way, and starting a borrow never moved the owner's
 * pointer. The borrow relation is therefore still recoverable -- the chain head
 * is borrowing exactly when its orders pointer is the one of a non-head segment,
 * which is the command owner. Everything else follows from that: the owner takes
 * priority 1 and the remaining segments follow in physical order.
 *
 * Since KI-169a the park state itself (orders_backup, the parked order position,
 * the inherited unit number, the borrow flag and the segment ranks) is part of
 * the savegame as well (R3VP chunk), so a head which still owns a parked schedule
 * keeps it: it is borrowing by construction (its orders pointer is the owner's, or
 * nullptr when the schedule was parked by a depot drag), and the schedule must
 * survive until R3RSyncDrivingOrders() hands it back. Such a consist only has its
 * ranks normalised. Savegames written before that chunk existed carry no park
 * state at all, and there the relation is derived from the order pointers.
 *
 * @param chain Head of the consist.
 */
void R3RRebuildCouplePriorities(Train *chain)
{
	if (chain == nullptr) return;

	/* R3R (KI-169a): a loaded head which parked its own schedule is borrowing
	 * whatever its orders pointer says. Dropping the park here is what made the
	 * vehicle come back as "no schedule" after a save/load. */
	if (chain->orders_backup != nullptr) {
		chain->r3r_orders_borrowed = true;
		R3RRenumberPriorities(chain);
		return;
	}

	std::vector<Train *> segs = R3RGetSegmentHeads(chain);

	Train *owner = chain;
	if (chain->orders != nullptr) {
		for (Train *s : segs) {
			if (s != chain && s->orders == chain->orders) { owner = s; break; }
		}
	}

	uint16_t next = 1;
	owner->r3r_priority = next++;
	for (Train *s : segs) {
		if (s != owner) s->r3r_priority = next++;
	}

	chain->r3r_orders_borrowed = (owner != chain);
	chain->orders_backup = nullptr;
	chain->orders_backup_real_index = INVALID_VEH_ORDER_ID;
	chain->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
}

/**
 * R3R (KI-239, 2026-09-28): 这张排程表在本条链上还有主人吗？
 *
 * 排程是「段」的财产：一段的段头自己持有它（orders），或者把它停在自己的
 * orders_backup 里（车库拖动把段变成段中段时形成）。链头只是**借用**命令所有者的表，
 * 借用是裸指针 + r3r_orders_borrowed 标记，不登记到共享环上（KI-93 口径），所以
 * 一旦表的主人被解出/拖走，链上的指针就成了孤儿 —— 链会继续跑、继续「共享」
 * 一张已经不属于它的表（玩家 2026-09-28 现场：三段落 idx9/idx0/idx27，解出持表的
 * 控制段 idx9 之后，留下的链头 idx27 与命令所有者 idx0 都还指着 idx9 那张原生共享表）。
 *
 * 判据（三条任一成立即"有主"）：
 *  1) 链上某车把这张表停在自己的 orders_backup 里 —— 车在自己跑别人的表时，这份
 *     停放的表仍是它自己的财产（含"读档后备份丢失"以外的全部停放态）；
 *  2) 这张表**不是**共享表，而链上某车 orders == 它且不处于 R3R 借用态 —— 段自持；
 *  3) 这张表是原生共享表，而链上某车**真的挂在它的共享环上**（读档后借用被按共享链
 *     补齐成原生共享的 KI-93 形态）。共享表的"主人"就是它的环上成员，所以只在
 *     环上出现才算持有：本会话内正常产生的 R3R 裸借用（不在环上）不算。
 *
 * @param front 链头。
 * @param ol    待判定的排程表（nullptr 视为"有主"，无需归还）。
 * @return 本链上还有人拥有这张表。
 */
static bool R3ROrderListOwnedInChain(const Train *front, const OrderList *ol)
{
	if (ol == nullptr) return true;
	const bool shared = ol->IsShared();
	for (const Train *s = front; s != nullptr; s = s->Next()) {
		if (s->orders_backup == ol) return true;
		if (s->orders != ol) continue;
		if (shared) {
			if (s->PreviousShared() != nullptr || s->FirstShared() == s) return true;
		} else if (!s->r3r_orders_borrowed) {
			return true;
		}
	}
	return false;
}

/**
 * R3R: 归还本车借来的排程（取回自己 orders_backup 里那张）。
 *
 * 原先是 R3RSyncDrivingOrders() 里 `owner == chain` 分支的内联代码，KI-239 起抽出来
 * 给「租主已经不在链上」的孤儿表归还复用，口径与第 147 轮续（KI-238）完全一致。
 *
 * @param chain  归还方（链头，也可能就是命令所有者）。
 * @param orphan true = 借来那张表的主人已经被解出/拖走（判据见 R3ROrderListOwnedInChain），
 *               归还之后**绝不能**再继续跑它：自己没有表可取时持空排程。false = 常规归位
 *               （链头重新成为命令所有者），读档后没有备份时保持原样继续跑（第 147 轮口径）。
 * @return 是否真的归还了（改变了本车的驱动表或借用标记）。
 */
static bool R3RReturnBorrowedOrders(Train *chain, bool orphan)
{
	if (chain == nullptr || !chain->r3r_orders_borrowed) return false;
	OrderList *const borrowed = chain->orders;
	/* 读档后借用会被按共享链补齐成原生共享（KI-93 口径），此时本车**真的挂在借来那张表的
	 * 共享环上**，必须先经由 RemoveFromShared() 离开 —— 直接把 orders 指回自己的表会把其它
	 * 成员留在一条指向本车的共享链上（它们一旦释放该表就是悬垂）。DeleteVehicleOrders()
	 * 里有同款口径；本次会话内正常产生的借用不在共享环上，不会走到这里。 */
	bool left_shared_ring = false;
	if (borrowed != nullptr && borrowed->IsShared() &&
			(chain->PreviousShared() != nullptr || chain->FirstShared() == chain)) {
		extern void UpdateDeparturesWindowVehicleFilter(const OrderList *order_list, bool remove);
		UpdateDeparturesWindowVehicleFilter(borrowed, false);
		chain->RemoveFromShared();
		chain->orders = nullptr;
		left_shared_ring = true;
		R3RDbgWrite("ORD-RETURN-SHARED veh=%d\n", (int)chain->index.base());
	}
	/* The parked schedule only exists when the borrow was started in this
	 * session. After a load it is gone (see R3RRebuildCouplePriorities);
	 * clearing orders here would leave the head without any schedule, so
	 * keep driving what it already has and just drop the borrow marker.
	 *
	 * R3R (KI-239): ...除非借来的那张表的主人已经不在链上（orphan）——那就必须放下它，
	 * 否则链会一直跑着一张属于别人的表，而"别人的表"随时可能被它的主人改写/销毁。 */
	if (chain->orders_backup != nullptr) {
		chain->orders = chain->orders_backup;
		chain->cur_real_order_index = chain->orders_backup_real_index;
		chain->cur_implicit_order_index = chain->orders_backup_implicit_index;
		chain->cur_timetable_order_index = chain->cur_real_order_index;
		chain->DeleteUnreachedImplicitOrders();
	} else if (orphan) {
		chain->orders = nullptr;
	}
	chain->orders_backup = nullptr;
	chain->orders_backup_real_index = INVALID_VEH_ORDER_ID;
	chain->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
	chain->r3r_orders_borrowed = false;
	InvalidateVehicleOrder(chain, 0);
	R3RCheckTtSync(chain, orphan ? "sync-return-orphan" : "sync-return-borrow");
	return true;
}

/**
 * R3R (couple priority, route A): make the consist's chain head execute the
 * command owner's schedule.
 *
 * Only the chain head (Previous() == nullptr) is ticked and runs
 * ProcessOrders(), yet the command owner may be a segment further back. Instead
 * of moving the order list -- which would rob its owner, the old behaviour --
 * the head now borrows it: the owner keeps its pointer and the head parks its
 * own schedule in orders_backup. The borrow is flagged with
 * r3r_orders_borrowed so release sites know they must not free that list.
 *
 * When the head *is* the command owner the borrow is given back on the spot.
 * That is what hands a locomotive its own schedule back once the consist has
 * been split down to it (the T8701 "restore orders_1" step).
 *
 * @param chain            Head of the consist.
 * @param inherit_progress When starting a borrow, also adopt the owner's order
 *                         position. True right after a couple (the
 *                         wait-for-couple order has just been fulfilled, so the
 *                         consist continues at the position the owner was
 *                         parked at); false on decouple, where the head keeps
 *                         the progress it already had.
 */
static void R3RSyncDrivingOrders(Train *chain, bool inherit_progress)
{
	if (chain == nullptr) return;
	Train *owner = R3RGetPriorityHead(chain);

	if (owner == chain) {
		/* Head is the command owner: give the borrow back. */
		R3RReturnBorrowedOrders(chain, false);
		return;
	}

	/* R3R (KI-239, 2026-09-28) 排程返还：链条上任何人都不再拥有"手上那张表"时把它还掉。
	 *
	 * 玩家现场：三段落 idx9(控制段，持一张与别的列车共享的原生共享表) / idx0 / idx27，
	 * 列车在 idx9 与 idx0 之间解耦。idx9 带着自己的表走了，但留下的链（链头 idx27，
	 * 命令所有者 idx0）**两个都还指着那张表**：idx27 是耦合时借的（r3r_orders_borrowed），
	 * idx0 的 orders 也被借用期同步成了同一个指针。此时 owner(0) != chain(27)，于是
	 * 旧逻辑走"继续借用"一支：owner_orders 是那张（已离开链的）表，chain->orders 已经
	 * 等于它 ⇒ 直接 early return ⇒ 留下的一半继续跑、继续"共享"着一张不属于它的表。
	 *
	 * 判据不是"我是不是命令所有者"，而是"这张表在本链上还有没有主人"
	 * （R3ROrderListOwnedInChain）—— 解耦把控制段带走后，链上的指针就是孤儿。
	 * 逐段归还（不只是链头）：命令所有者 idx0 自己也在借用态，它必须先取回自己的表，
	 * 链头才能按正常规则借用"命令所有者的表"（下面那段逻辑），留下的一半于是跑回
	 * 车底自己的计划，而不是被解出方的共享表。 */
	for (Train *s = chain; s != nullptr; s = s->Next()) {
		if (!s->r3r_orders_borrowed || s->orders == nullptr) continue;
		if (R3ROrderListOwnedInChain(chain, s->orders)) continue;
		R3RDbgWrite("ORD-RETURN-ORPHAN veh=%d head=%d owner=%d\n",
				(int)s->index.base(), (int)chain->index.base(), (int)owner->index.base());
		R3RReturnBorrowedOrders(s, true);
	}

	/* R3R (KI-214): the schedule the owner hands out is its own, so it normally
	 * sits in owner->orders. A depot edit which turned the owner into a middle
	 * segment parks it in owner->orders_backup first (the R3R DEPOT-PARK block in
	 * CmdMoveRailVehicle) -- the owner is still the command owner, so that parked
	 * plan is what the chain has to run, e.g. the consist a block was just
	 * dropped in front of. Progress indices come from wherever the plan sits. */
	OrderList *owner_orders = owner->orders;
	VehicleOrderID owner_real = owner->cur_real_order_index;
	VehicleOrderID owner_implicit = owner->cur_implicit_order_index;
	if (owner_orders == nullptr && owner->orders_backup != nullptr) {
		owner_orders = owner->orders_backup;
		owner_real = owner->orders_backup_real_index;
		owner_implicit = owner->orders_backup_implicit_index;
	}
	if (owner_orders == nullptr) return;  /* Nothing to drive yet. */
	/* Parked without a recorded position: adopting it would corrupt the chain's
	 * indices, so keep driving what it has and let the owner hand over later. */
	if (owner_real == INVALID_VEH_ORDER_ID) inherit_progress = false;

	if (chain->orders == owner_orders) {
		/* Already driving the right list; only adopt the progress if asked. */
		if (inherit_progress) {
			chain->cur_real_order_index = owner_real;
			chain->cur_implicit_order_index = owner_implicit;
			/* R3R: keep the timetable index in sync with the adopted progress,
			 * otherwise UpdateVehicleTimetable asserts on the next station stop. */
			chain->cur_timetable_order_index = chain->cur_real_order_index;
		}
		return;
	}

	/* Start or switch the borrow. Park our own schedule the first time only:
	 * on a later switch orders_backup already holds the one we own.
	 *
	 * R3R (第 144 轮): 只有"当前手里真有一份排程"才把它停进备份。旧代码无条件写
	 * orders_backup = chain->orders，当 chain 已经因为之前的车库拖动把排程停进备份
	 * （orders 为 nullptr、orders_backup 有货）而借用标志又被清掉时，这一行会把
	 * 那份自己的排程覆盖成 nullptr —— 后半条链解耦后"排程变空"的成因之一。 */
	if (!chain->r3r_orders_borrowed) {
		if (chain->orders != nullptr) {
			chain->orders_backup = chain->orders;
			chain->orders_backup_real_index = chain->cur_real_order_index;
			chain->orders_backup_implicit_index = chain->cur_implicit_order_index;
		}
		chain->r3r_orders_borrowed = true;
	}
	/* Switching to a different list: adopt the owner's position. The indices just
	 * parked above describe the plan we were driving, not the owner's, so keeping
	 * them would leave the chain on the wrong order -- or on none at all, since
	 * GetOrderAt() returns nullptr past the end of the list. A chain that is
	 * already on the owner's list returned early above and keeps its progress,
	 * which is why the depot-edit caller can pass inherit_progress = false
	 * without rewinding: the owner it switches to is the segment which drove
	 * that list until this edit (the consist's head, parked by the drag). */
	chain->orders = owner_orders;
	chain->cur_real_order_index = owner_real;
	chain->cur_implicit_order_index = owner_implicit;
	chain->cur_timetable_order_index = chain->cur_real_order_index;
	chain->DeleteUnreachedImplicitOrders();
	InvalidateVehicleOrder(chain, 0);
	R3RCheckTtSync(chain, "sync-borrow");
}

/**
 * R3R (couple priority, route A): re-derive the schedule ownership of one chain
 * after a depot edit rearranged it (a drag in the depot list, selling a wagon).
 *
 * Such an edit splits a consist or joins two chains, which changes which segment
 * is the command owner, but the priorities left over from the last couple or
 * decouple (and the borrow flag of the old head) stay as they were. Renumbering
 * first compacts the leftovers while preserving their relative order (ties fall
 * back to physical order, so the chain head wins over a segment dragged in
 * behind it), which is exactly what DecoupleTrain() does for its parts; the sync
 * then either hands the borrowed schedule back -- the case a locomotive hits
 * when it is dragged out of the consist it had coupled to -- or switches the
 * head to the new owner. Both steps are no-ops for a plain free-wagon chain,
 * where the head is always its own owner.
 *
 * @param chain Head of one of the chains resulting from the edit (may be nullptr).
 */
static void R3RSyncChainAfterDepotEdit(Train *chain)
{
	if (chain == nullptr) return;

	R3RRenumberPriorities(chain);
	R3RSyncDrivingOrders(chain, false);

	/* R3R (couple priority, route A): a depot edit must neither leave an
	 * independent train without a schedule nor silently renumber it.
	 *
	 * Unit number: a segment which was coupled to another train lent its number to
	 * the merged train (Couple parks its own number in unitnumber_backup). Now that
	 * it is on its own again it takes its own number back instead of being given a
	 * fresh one, which is what made "Train 1" turn into "Train 3".
	 *
	 * Schedule: the head may have ended up without a schedule while the consist
	 * still carries one (e.g. the schedule stayed behind on the old head after the
	 * head state was trashed by a drag). Adopt the parked own schedule, or failing
	 * that the first schedule still present in the chain, rather than coming out of
	 * the depot empty. */
	if (!chain->r3r_orders_borrowed && chain->unitnumber_backup != 0 && chain->IsFrontEngine()) {
		/* R3R (第 144 轮 / 需求壹): 只有"确实是本车从池里新领的号"才能在这里回收 ——
		 * 这个号也可能是合并期间由别的车接管/共用的号，清掉它等于把别人的编号弄丢。 */
		if (chain->unitnumber != 0 && chain->unitnumber != chain->unitnumber_backup &&
				!R3RUnitNumberUsedByOther(chain, chain->unitnumber)) {
			Company::Get(chain->owner)->freeunits[chain->type].ReleaseID(chain->unitnumber);
		}
		chain->unitnumber = chain->unitnumber_backup;
		chain->unitnumber_backup = 0;
		Company::Get(chain->owner)->freeunits[chain->type].UseID(chain->unitnumber);
	}
	if (!chain->r3r_orders_borrowed && chain->orders == nullptr &&
			(chain->IsFrontEngine() || chain->orders_backup != nullptr)) {
		/* R3R (第 144 轮, 玩家报"车库拖动耦合后解耦，后半条链排程变空"): 停在本车
		 * orders_backup 里的那份排程是"本车自己的"，必须连同备份时的订单位置一起
		 * 取回。旧代码在这里把 donor 指向 chain 自己、再读 donor->orders（此刻就是
		 * nullptr）⇒ chain->orders 依旧为空，末尾却把 orders_backup 清成 nullptr ——
		 * 排程被永久丢弃（订单表也随之泄漏），链头出来就是"无排程"。 */
		if (chain->orders_backup != nullptr) {
			chain->orders = chain->orders_backup;
			chain->cur_real_order_index = (chain->orders_backup_real_index != INVALID_VEH_ORDER_ID) ? chain->orders_backup_real_index : 0;
			chain->cur_implicit_order_index = (chain->orders_backup_implicit_index != INVALID_VEH_ORDER_ID) ? chain->orders_backup_implicit_index : 0;
			chain->cur_timetable_order_index = chain->cur_real_order_index;
			chain->DeleteUnreachedImplicitOrders();
			chain->orders_backup = nullptr;
			chain->orders_backup_real_index = INVALID_VEH_ORDER_ID;
			chain->orders_backup_implicit_index = INVALID_VEH_ORDER_ID;
			InvalidateVehicleOrder(chain, 0);
		} else {
			Train *donor = nullptr;
			for (Train *w = chain; w != nullptr; w = w->Next()) {
				if (w->orders != nullptr) {
					donor = w;
					break;
				}
			}
			if (donor != nullptr) {
				chain->orders = donor->orders;
				chain->cur_real_order_index = donor->cur_real_order_index;
				chain->cur_implicit_order_index = donor->cur_implicit_order_index;
				chain->cur_timetable_order_index = chain->cur_real_order_index;
				chain->DeleteUnreachedImplicitOrders();
				if (donor != chain) chain->AddToShared(donor);
				InvalidateVehicleOrder(chain, 0);
			}
		}
	}

	/* R3R (KI-153, 车库内解耦再耦合卡死): a manual depot edit re-forms the chain,
	 * so R3RStopChainInDepot's "parked by an R3R depot tool, waiting for the
	 * in-depot GOTO_COUPLE" state no longer applies. That helper sets both
	 * r3r_parked and VehState::Stopped; if the flag survives a hand re-couple,
	 * TrainLocoHandler's in-depot couple exemption keeps firing (r3r_parked &&
	 * pending GOTO_COUPLE(this depot)), the couple scan finds nothing left to
	 * couple, the train is re-stopped every tick and CheckTrainStayInDepot's
	 * GOTO_COUPLE arrival guard keeps returning true -- the train is pinned in
	 * the depot and can never leave. The player performing an edit means "I took
	 * over": drop the parking flag and let Start/Stop plus normal order
	 * processing decide. Only a chain that is genuinely still executing an
	 * in-depot couple is re-parked by the running locomotive path on the next
	 * tick, so this cannot disable the auto-couple feature. */
	chain->r3r_parked = false;

	/* R3R (KI-153, "0 号车"陈旧行): a head that lost its number (Couple parks it
	 * in unitnumber_backup / the depot split moved the identity) must never be
	 * left at 0 -- the depot list draws it as a bogus "Train 0" row that then
	 * sticks around as a phantom. Assign a fresh one from the chain's OWN company
	 * pool; GetFreeUnitNumber() must not be used because it reads the global
	 * _current_company, which is meaningless while a vehicle is being ticked. */
	if (chain->IsFrontEngine() && chain->unitnumber == 0) {
		uint16_t n = Company::Get(chain->owner)->freeunits[VehicleType::Train].NextID();
		if (n != UINT16_MAX) {
			chain->unitnumber = n;
			Company::Get(chain->owner)->freeunits[VehicleType::Train].UseID(n);
		}
	}

	/* R3R (第 144 轮 / 需求贰): 名字与车号一起返还 —— 车库编辑后重新成为链头的段
	 * 把自己停放的列车名取回（幂等；调车路径上 NormaliseTrainHead() 已经先做过一次）。 */
	R3RRestoreTrainName(chain);

	/* R3R (第 144 轮 / 需求伍): 车库编辑同样是"链结构变化"，整链的列车分组要收敛到实际
	 * 控制段；R3RNormaliseChainGroups() 会在收敛前把被改写段自己的分组停放进
	 * group_id_backup（等它重新成为链头时由 NormaliseTrainHead 取回）。
	 * num_vehicle 以「前端列」为单位簿记，故按同样的 -1/+1 配对包住这次收敛，且只有
	 * 真正的前端列才需要（也才可以）簿记。 */
	if (chain->IsFrontEngine()) {
		GroupStatistics::CountVehicle(chain, -1);
		R3RNormaliseChainGroups(chain);
		GroupStatistics::CountVehicle(chain, 1);
	}
}

/**
 * R3R (KI-150/153): after an in-depot couple or decouple the depot's vehicle
 * list changed membership and the resulting heads may carry a brand-new
 * identity (or a number that was parked away). Regenerate every open depot
 * window the chain touches so a stale row -- typically the absorbed head,
 * whose number is now 0 -- and its unrefreshed sprite disappear on the same
 * frame instead of lingering until some unrelated redraw. Also refresh the
 * vehicle-list windows, which cache the same grouping.
 * @param v Head (or any member) of a chain that was just re-formed.
 */
static void R3RInvalidateDepotWindowsForChain(const Train *v)
{
	if (v == nullptr) return;
	for (const Train *t = v; t != nullptr; t = t->Next()) {
		if (IsRailDepotTile(t->tile)) {
			InvalidateWindowData(WindowClass::VehicleDepot, t->tile.base());
			break;
		}
	}
	InvalidateVehicleListWindows(VehicleType::Train);
}

/**
 * R3R（2026-09-22 玩家口径）：多段链的「列车分组」与「挂接分组」等于实际控制段的分组。
 *
 * 「实际控制段」= 排程归属段，即 R3RGetChainScheduleOwner() 选出的 r3r_priority
 * 最小者（与 Couple() 决定谁的排程胜出用的是同一判据）。现状：耦合会把被并入的
 * 段清零到 DEFAULT_GROUP（Couple 内 SetTrainGroupID(u, DEFAULT_GROUP)），解挂会把
 * 解出的部分清零（DecoupleTrain 内同一行）；而挂接分组本来就只按段存
 * (Vehicle::couple_groups 只写在段头)，于是同一条物理链上的各段分组各不相同 ——
 * 玩家在分组窗口/挂接白名单里看到的就是散开的几条。这里在每次链结构变化后把两者
 * 重新收敛到控制段。
 *
 * num_vehicle 簿记不在这里做：它以「前端列」为单位，由调用方按现有
 * CountVehicle(-1)/(+1) 配对负责（本函数被调用时前端列尚未/已经计入的时序见各调用点）；
 * 这里只推平 group_id 并维护 num_engines（SetTrainGroupID），以及把控制段的挂接分组
 * 掩码复制到链内每一个段头。
 *
 * @param head 该链的任一车辆（内部先用 First() 归一到链头）。
 */
static void R3RNormaliseChainGroups(Train *head)
{
	if (head == nullptr) return;
	Train *front = Train::From(head->First());
	if (front == nullptr) return;

	const Train *owner = nullptr;
	R3RGetChainScheduleOwner(front, &owner, nullptr, nullptr);
	if (owner == nullptr) return;

	/* 列车分组：整链推平为控制段的分组（SetTrainGroupID 顺带维护 num_engines）。
	 * 判据必须扫全链，不能只比链头：耦合刚把一段并进来时链头往往已经等于目标分组，
	 * 不一致的恰恰是被并入的那一段。 */
	if (front->IsFrontEngine()) {
		bool uniform = true;
		for (const Vehicle *w = front; w != nullptr; w = w->Next()) {
			if (w->group_id != owner->group_id) { uniform = false; break; }
		}
		if (!uniform) {
			const GroupID old_g = front->group_id;
			/* R3R (第 144 轮 / 需求伍): 整链统一到控制段分组之前，先把"分组会被改写"的段头
			 * （含链头）自己的分组停进 group_id_backup —— 这是"借出"，等该段重新成为链头时
			 * 由 R3RRestoreTrainGroupID() 还回来（入口在 NormaliseTrainHead）。 */
			for (Train *t = front; t != nullptr; ) {
				if ((t->Previous() == nullptr || t->IsSegmentFront()) && t->group_id != owner->group_id) {
					R3RParkTrainGroupID(t);
				}
				Vehicle *next = t->Next();
				t = (next != nullptr) ? Train::From(next) : nullptr;
			}
			SetTrainGroupID(front, owner->group_id);
			R3RDbgWrite("GRP-NORM head=%d old=%u new=%u\n", (int)front->index.base(),
					(uint)old_g.base(), (uint)owner->group_id.base());
		}
	}

	/* 挂接分组：把控制段的掩码复制到链内每一个段头，使整条链的白名单处处一致。 */
	const CoupleGroupMask target = R3RGetCoupleGroupsOfSegment(owner);
	for (Train *t = front; t != nullptr; ) {
		if (t->Previous() == nullptr || t->IsSegmentFront()) {
			if (R3RGetCoupleGroupsOfSegment(t) != target) {
				R3RClearCoupleGroupsOfSegment(t);
				for (uint i = 0; i < R3R_COUPLE_GROUP_MASK_BITS; i++) {
					if ((target & (CoupleGroupMask(1) << i)) != 0) {
						R3RAddCoupleGroupToSegment(t, CoupleGroupID(static_cast<uint16_t>(i)));
					}
				}
				R3RDbgWrite("CGRP-NORM seg=%d mask=%llu\n", (int)t->index.base(),
						(unsigned long long)target);
			}
		}
		Vehicle *next = t->Next();
		t = (next != nullptr) ? Train::From(next) : nullptr;
	}
}

/* --- KI-14 (1): edge-triggered debug output -------------------------------
 * The probes below (FOLDCHK / FOLDCHK-DIR, RESERVECONSIST, SKIP-STOPPED) all
 * sit on code that runs *every tick* while nothing changes: the fold-fix retry
 * loop (KI-06) re-runs up to three fold checks per tick, and a consist parked
 * on a platform re-reserves its tracks every tick (Train::ReserveTrackUnderConsist
 * is called from the R3R platform-waiter gate). Measured on net7probe.sav:
 * 28 838 lines / 2.1 MB of R3R_debug.log in 38 s, every line its own
 * fopen/fprintf/fclose. R3RDbgEdge() lets a call site emit its line only when
 * that probe's state is seen for the first time in the current window or when
 * it actually changes; repeats are counted and summarised once per window in
 * R3R_perf.log (dbgEdge / dbgEdgeSkip), so the flood disappears without losing
 * the diagnostic content. */
enum R3RDbgEdgeTag : uint32_t {
	R3REDGE_FOLDCHK = 0,
	R3REDGE_FOLDCHK_DIR,
	R3REDGE_RESERVECONSIST,
	R3REDGE_SKIPSTOPPED,
	R3REDGE_TTB,
	R3REDGE_COUPLEFAIL, ///< TrainCoupleHandler: no coupling target found
	R3REDGE_CPLS0,      ///< TrainLocoHandler: stopped-coupler check
	R3REDGE_COUPLEGATE, ///< R3RCanCoupleNow: a candidate consist was rejected by the couple gate
	R3REDGE_CPLGEO,      ///< GetCouplePosition: exact end-to-end hit (nominal geometry, no overlap)
	R3REDGE_CPLHIT,      ///< CheckTrainCollision: touch/overlap test fired the couple (min_diff has -1)
	R3REDGE_CPLGEOCOMMIT, ///< KI-106 fix 1: an exact hit was made while ROLLING (+ whether the merge committed)
	R3REDGE_VEHTD,       ///< KI-108: GetVehicleTrackdir() had to coerce a direction/track mismatch
	R3REDGE_NOABSORBSEG, ///< KI-144: a segment refused to absorb the free wagons standing in the depot
	R3REDGE_NDHSEGKEEP,  ///< KI-145: a dual head inside a segment was kept adjacent to its partner
	R3REDGE_KI149SEAM,   ///< KI-149: a decoupled part (WAIT_COUPLE) was found touching this train
	R3REDGE_CPLPAIR,      ///< KI-182: a locomotive locked onto a waiting consist (the pair flag was set)
	R3REDGE_CPLPAIRSTEAL, ///< KI-182: an *earlier* starter took a consist away from a later one
	R3REDGE_OWNERMOVE,    ///< KI-187: a segment head handed its schedule/priority to the new head
	R3REDGE_TRP,          ///< KI-199: a TryPathReserveWithResultFlags() attempt (result + order/destination context)
	R3REDGE_SEAMFREE,     ///< KI-240: a touch/overlap on a decouple seam was released instead of crashing
	R3REDGE_COUNT,
};

struct R3RDbgEdgeState {
	std::unordered_map<uint64_t, uint64_t> seen; ///< (tag << 56 | key) -> payload
	uint64_t emitted[R3REDGE_COUNT];
	uint64_t suppressed[R3REDGE_COUNT];

	R3RDbgEdgeState() : emitted(), suppressed() {}
};

/* --- KI-14 (2): frame-budget breakdown for the train tick ------------------
 * PFE_GL_TRAINS reports ~135 ms/frame for the train tick while every R3R probe
 * on the couple/decouple path reports 0 -- so the cost sits on the plain tick
 * path. These per-frame counters split that path into named buckets so the
 * next 'fps' run says which bucket owns the 135 ms. All counters are static
 * to this TU on purpose: r3r_perf.h holds the shared ones, and touching a
 * header would force a full rebuild (KI-15). */
static uint64_t r3r_loco_calls = 0, r3r_loco_ns = 0;   ///< TrainLocoHandler, both modes
static uint64_t r3r_ch_calls = 0, r3r_ch_ns = 0;       ///< TrainCoupleHandler (called per movement step)
static uint64_t r3r_plat_calls = 0, r3r_plat_ns = 0;   ///< R3R platform re-reservation gate
static uint64_t r3r_resv_calls = 0, r3r_resv_ns = 0;   ///< Train::ReserveTrackUnderConsist
static uint64_t r3r_edge_calls = 0, r3r_edge_ns = 0;   ///< R3RDbgEdge gate itself
static uint64_t r3r_dump_ns = 0;                       ///< R3RPerfDumpAndReset itself
static uint64_t r3r_ctrl_calls = 0, r3r_ctrl_ns = 0;   ///< TrainController (movement + path reservation)
static uint64_t r3r_coll_calls = 0, r3r_coll_ns = 0;   ///< CheckTrainCollision(moving_front)
static uint64_t r3r_spd_calls = 0, r3r_spd_ns = 0;     ///< MaxSpeedInfo + UpdateSpeed (walks the consist)
static uint64_t r3r_vp_calls = 0, r3r_vp_ns = 0;       ///< UpdateViewport loop (walks the consist)
static uint64_t r3r_mov_iters = 0;                     ///< per-vehicle iterations of the TrainController loop
/* KI-26 (step 2): the 09-14 Release run left 225 ms/s = 8.33 ms/frame inside
 * 'loco' with no bucket to its name. These wrap the handler's own passes: the
 * order machinery, the signal + path-reserve attempt, and the two per-consist
 * state passes. Whatever is left over (physics, station entry, reverse fallback,
 * TrainEnterStation) is printed as 'resid' so the split can be checked. */
static uint64_t r3r_ord_calls = 0, r3r_ord_ns = 0;     ///< ProcessOrders + CheckReverseTrain, from TrainLocoHandler
static uint64_t r3r_prv_calls = 0, r3r_prv_ns = 0;     ///< UpdateSignalsOnSegment + TryPathReserve, from TrainLocoHandler
static uint64_t r3r_load_calls = 0, r3r_load_ns = 0;   ///< consist->HandleLoading
static uint64_t r3r_dep_calls = 0, r3r_dep_ns = 0;     ///< CheckTrainStayInDepot
/* KI-26 (step 2b, 09-14): the Release run still left 136 ms/s = 2.67 ms/frame in
 * 'resid'. These wrap the handler sections that had no bucket at all: the
 * DECOUPLE gate (the whole stopped-arrival block, incl. the DEPOT-ARR snapshot),
 * the waypoint reverse-on-arrival fallback, the stuck-train path, and the
 * 'Reversing' flag turn-around. 'cpl' and 'eg' are the pre-existing couple/edge
 * buckets, which also live inside the handler and were simply not subtracted.
 * ('wpr' is retained only for the PERF line; the reverse-on-arrival feature it
 * used to wrap was removed, so it now always reads 0.) */
static uint64_t r3r_dec_calls = 0, r3r_dec_ns = 0;     ///< DECOUPLE gate (stopped-arrival block)
static uint64_t r3r_wpr_calls = 0, r3r_wpr_ns = 0;     ///< retired: reverse-on-arrival fallback (always 0)
static uint64_t r3r_stk_calls = 0, r3r_stk_ns = 0;     ///< stuck-train handling (path reserve retry)
static uint64_t r3r_rev_calls = 0, r3r_rev_ns = 0;     ///< Reversing-flag turn-around (ReverseTrainDirection)
/* KI-26 (step 3): the in-game "GL train ticks" figure, exposed by framerate_gui.cpp
 * so a Release run reports the same metric vanilla's 'framerates' output uses. */
extern double R3RGetTrainsMs();

/** RAII wall-clock accumulator for the KI-14 (2) probes. */
struct R3RScopeTimer {
	uint64_t *ns;    ///< accumulator for elapsed ns
	uint64_t *calls; ///< optional call counter
	uint64_t t0;
	bool on;         ///< KI-26: R3RPerfOn() snapshot, so ctor/dtor agree

	R3RScopeTimer(uint64_t *n, uint64_t *c = nullptr) : ns(n), calls(c), t0(0), on(R3RPerfOn())
	{
		if (!this->on) return;
		this->t0 = R3RPerfNowNs();
		if (this->calls != nullptr) (*this->calls)++;
	}
	~R3RScopeTimer() { if (this->on) *this->ns += R3RPerfNowNs() - this->t0; }
};

/** Process-wide edge state (single-threaded game logic). */
static R3RDbgEdgeState &R3REdgeState()
{
	static R3RDbgEdgeState s;
	return s;
}

/**
 * R3R (KI-182): last tick a couple-target re-selection was attempted for a
 * chain, keyed by the chain head's index. An entry only exists while a
 * locomotive is *failing* to find a target -- it is dropped as soon as a lock
 * is taken or the order ends -- so a locomotive which can never resolve a
 * target does not walk the whole vehicle pool on every single tick. The very
 * first attempt is never delayed, because the entry does not exist yet: that
 * matters for the in-depot couple path, which expects the lock to be there
 * within the same tick.
 */
static std::unordered_map<uint32_t, uint64_t> _r3r_pair_scan_tick;

/**
 * R3R (KI-182, player rule 2026-09-24): the tick on which a chain entered its
 * *current* "go to and couple" order, keyed by the chain head's index. This is
 * what decides which locomotive gets a contended waiting consist -- "先来的
 * （进入前往挂接状态的）优先获得挂接权": the one which started the order first
 * outranks the one which merely started looking first, so the answer does not
 * depend on the order the vehicle pool happens to be walked in.
 *
 * Entries are dropped as soon as the chain stops running the order, and every
 * chain head passes through R3REnsureCouplePair() once per tick, so the map
 * holds no steady state and cannot keep a stale timestamp alive.
 */
struct R3RCoupleEnterStamp {
	const Train *owner; ///< the chain head this stamp was issued to
	uint64_t tick;      ///< the tick on which @c owner entered its couple order
};
static std::unordered_map<uint32_t, R3RCoupleEnterStamp> _r3r_couple_enter_tick;

/** R3R (KI-182): @return the stamp stored for @p index, or nullptr if absent/stale. */
static const R3RCoupleEnterStamp *R3RFindCoupleEnterStamp(uint32_t index, const Train *owner)
{
	auto it = _r3r_couple_enter_tick.find(index);
	if (it == _r3r_couple_enter_tick.end()) return nullptr;
	/* A freed index can be handed to a new vehicle. Without this check the
	 * newcomer would inherit the old run's (very early) stamp and win every
	 * contention. */
	if (it->second.owner != owner) return nullptr;
	return &it->second;
}

/**
 * R3R (KI-182): the priority stamp of a chain's current couple run.
 * @return tick on which the chain entered the order, or 0 when it is not
 *         running a GOTO_COUPLE order right now (i.e. it has no priority).
 */
static uint64_t R3RCoupleEnterTick(Train *v)
{
	if (v == nullptr) return 0;
	Train *head = Train::From(v->First());
	if (head == nullptr || !head->current_order.IsType(OT_GOTO_COUPLE)) return 0;
	const R3RCoupleEnterStamp *stamp = R3RFindCoupleEnterStamp(head->index.base(), head);
	return stamp == nullptr ? 0 : stamp->tick;
}

/**
 * R3R (KI-182): may @p challenger take a consist which @p holder has locked?
 * Only when the challenger entered its couple order strictly earlier. Equal
 * stamps -- and an unknown stamp on either side -- keep the lock with the
 * holder, i.e. the ordinary scan order decides (the player's "否则看游戏心情
 * 排先后").
 */
static bool R3RCouplePairOutranks(Train *challenger, Train *holder)
{
	const uint64_t mine = R3RCoupleEnterTick(challenger);
	const uint64_t his = R3RCoupleEnterTick(holder);
	if (mine == 0 || his == 0) return false;
	return mine < his;
}

/**
 * Edge-triggered gate for a per-tick probe.
 * @param tag     probe id (R3RDbgEdgeTag).
 * @param key     identity of the logged object (vehicle, tile, ...); <= 56 bits.
 * @param payload the mutable part of the line -- a change forces a new line.
 * @return true when the caller should write its line now.
 */
static bool R3RDbgEdge(uint32_t tag, uint64_t key, uint64_t payload)
{
	/* R3R (release audit 2026-09-19): this gate is reached once per vehicle per
	 * tick from ReserveTrackUnderConsist() and once per collision pair per
	 * movement step, and it used to do an unordered_map lookup (and, on a miss,
	 * an insert) every time even with the probes off. With the log off nothing
	 * can ever be emitted, so bail out first: with R3R_PROBES=0 R3RDbgOn() is a
	 * constant, the whole body disappears, and callers collapse to their
	 * non-logging branch. */
	if (!R3RDbgOn()) return false;
	R3RScopeTimer r3r_edge_timer(&r3r_edge_ns, &r3r_edge_calls);
	R3RDbgEdgeState &s = R3REdgeState();
	const uint64_t k = ((uint64_t)tag << 56) | (key & 0x00FFFFFFFFFFFFFFULL);
	auto it = s.seen.find(k);
	if (it != s.seen.end() && it->second == payload) {
		s.suppressed[tag]++;
		return false;
	}
	s.seen[k] = payload;
	s.emitted[tag]++;
	return true;
}

/**
 * R3R (KI-144 probe, forward declared near NormalizeTrainVehInDepot): the guard
 * "a segment never absorbs the free wagons of the depot" runs once per tick
 * while the segment stands in the depot, so the line is edge-triggered on
 * (segment head, number of free wagons on the tile) -- KI-14 口径, no flood.
 * @param head Any vehicle of the segment-bearing chain (the guard's argument).
 */
static void R3RProbeNoAbsorbSeg(const Train *head)
{
	if (!R3RDbgOn()) return;
	uint free_wagons = 0;
	for (const Train *v = Train::From(GetFirstVehicleOnTile(head->tile, VehicleType::Train)); v != nullptr; v = v->HashTileNext()) {
		if (v->IsFreeWagon() || v->IsFrontWagon()) free_wagons++;
	}
	const uint64_t n_key = (uint64_t)head->index.base();
	if (R3RDbgEdge(R3REDGE_NOABSORBSEG, n_key, (uint64_t)free_wagons)) {
		R3RDbgWrite("NOABSORB-SEG veh=%d n=%d len=%d\n", (int)head->index.base(), (int)free_wagons,
				(int)CountVehiclesInChain(head));
	}
}

/**
 * R3R (KI-145 probe, forward declared near NormalizeTrainVehInDepot): the
 * "both halves of a dual head stay adjacent inside a segment" branch. Edge-
 * triggered on the pair, with the payload telling whether the rear half had to
 * be moved back (which is also the case that pushes a sandwiched wagon out).
 * @param front The front half (the engine half).
 * @param other The rear half.
 * @param moved Whether the rear half actually had to be re-inserted.
 */
static void R3RProbeNdhSegKeep(const Train *front, const Train *other, bool moved)
{
	if (!R3RDbgOn()) return;
	const uint64_t n_key = ((uint64_t)front->index.base() << 32) ^ (uint64_t)other->index.base();
	if (R3RDbgEdge(R3REDGE_NDHSEGKEEP, n_key, moved ? 1ULL : 0ULL)) {
		R3RDbgWrite("NDH-SEG-KEEP front=%d rear=%d moved=%d\n",
				(int)front->index.base(), (int)other->index.base(), moved ? 1 : 0);
	}
}

/**
 * R3R (KI-149): a consist waiting to be coupled is standing touching this train,
 * i.e. a decouple seam runs over one of our ends and that direction is blocked
 * for us. Edge-triggered: the state holds for as long as both parts stand next
 * to each other, which is exactly the situation a probe per tick would flood.
 * @param v Train that has the seam (any member of the chain).
 * @param at_front The waiting consist touches our nose (else our tail).
 * @param waiting The waiting consist touching us.
 */
static void R3RProbeKi149Seam(const Train *v, bool at_front, const Train *waiting)
{
	if (!R3RDbgOn()) return;
	const uint64_t key = ((uint64_t)v->index.base() << 32) ^ (uint64_t)waiting->index.base();
	if (R3RDbgEdge(R3REDGE_KI149SEAM, key, at_front ? 1ULL : 0ULL)) {
		R3RDbgWrite("KI149-SEAM veh=%d seam=%s other=%d tile=%d,%d\n", (int)v->index.base(),
				at_front ? "front" : "back", (int)waiting->index.base(), TileX(v->tile), TileY(v->tile));
	}
}

/** Stable hash of a probe's tag string, so different tags never share an edge key. */
static uint64_t R3RDbgTagHash(const char *tag)
{
	uint64_t h = 1469598103934665603ULL; /* FNV-1a offset basis */
	for (const char *c = tag; *c != '\0'; c++) h = (h ^ (uint64_t)(uint8_t)*c) * 1099511628211ULL;
	return h & 0xFFFFFFFFULL;
}

/** Emitted/suppressed totals of the current window (for the PERF line). */
static void R3RDbgEdgeTotals(uint64_t &emitted, uint64_t &suppressed)
{
	R3RDbgEdgeState &s = R3REdgeState();
	emitted = 0;
	suppressed = 0;
	for (uint i = 0; i < R3REDGE_COUNT; i++) {
		emitted += s.emitted[i];
		suppressed += s.suppressed[i];
	}
}

/** Start a new edge-trigger window: a repeating site logs at most once per window. */
static void R3RDbgEdgeReset()
{
	R3RDbgEdgeState &s = R3REdgeState();
	s.seen.clear();
	for (uint i = 0; i < R3REDGE_COUNT; i++) {
		s.emitted[i] = 0;
		s.suppressed[i] = 0;
	}
}

/**
 * R3R perf probe (KI-14): world snapshot + one aggregate line to R3R_perf.log
 * per 128 rendered frames, then reset the counters.
 *
 * All rates are per *second*, derived from the window length, so a line reads
 * like "... dbgWrite=812.4/s 1263ms/s 3.71MB/s ..." -- i.e. the R3R debug log
 * alone consumed 1263 ms of CPU per second of wall clock. That is the smoking
 * gun for the fold-fix retry loop (KI-06) writing to disk every tick.
 */
void R3RPerfDumpAndReset()
{
	/* R3R (release audit 2026-09-19): guard here as well as at the frame tick,
	 * because the body is expensive for a "switched off" probe -- a full
	 * Train::Iterate() world scan, an fopen/ftell/fclose of R3R_debug.log and
	 * the fopen/fprintf/fclose of R3R_perf.log (which also creates a stray log
	 * file next to a published exe). With R3R_PROBES=0 R3RPerfOn() is a
	 * constant false, so MSVC keeps only the `ret`. */
	if (!R3RPerfOn()) return;

	/* Measures the world snapshot + log accounting below (not the fprintf
	 * itself, which runs after this value is read). */
	R3RScopeTimer r3r_dump_timer(&r3r_dump_ns);
	R3RPerfCounters &p = R3RP();

	/* World snapshot: chain count/length and how many segments this mod splits
	 * them into -- the O(n^2) suspects scale with these. */
	uint64_t chains = 0, vehs = 0, max_chain = 0, segs = 0, artics = 0, wait_couple = 0;
	for (const Train *t : Train::Iterate()) {
		vehs++;
		if (t->IsArticulatedPart()) artics++;
		if (t->IsSegmentFront()) segs++;
		if (t->current_order.IsType(OT_WAIT_COUPLE)) wait_couple++;
		if (t->Previous() == nullptr) {
			chains++;
			uint64_t n = 0;
			for (const Train *w = t; w != nullptr; w = w->Next()) n++;
			if (n > max_chain) max_chain = n;
		}
	}

	/* Growth of R3R_debug.log since the previous dump. */
	static long long last_size = 0;
	long long size = 0;
	FILE *lf = fopen("R3R_debug.log", "rb");
	if (lf != nullptr) {
		if (fseek(lf, 0, SEEK_END) == 0) size = ftell(lf);
		fclose(lf);
	}

	const double win_ms = (double)p.frame_ms_sum;
	const double per_s = (win_ms > 0.0) ? 1000.0 / win_ms : 0.0;  /* ns counter -> per second */
	const double ms_per_s = per_s / 1e6;                          /* ns counter -> ms per second */
	const double fps = (p.frames > 0 && win_ms > 0.0) ? 1000.0 * (double)p.frames / win_ms : 0.0;

	/* KI-14 (1): how much the edge-trigger gate swallowed this window. */
	uint64_t edge_written = 0, edge_skipped = 0;
	R3RDbgEdgeTotals(edge_written, edge_skipped);

	FILE *f = fopen("R3R_perf.log", "a");
	if (f != nullptr) {
		fprintf(f, "PERF frames=%llu fps=%.1f frameMs avg=%.1f max=%llu | "
				"tryCouple=%.1f/s %.0fms/s | foldCheck=%.1f/s %.0fms/s | dumpChain=%.1f/s dumpIdent=%.1f/s | "
				"dbgWrite=%.1f/s %.0fms/s %.2fMB/s | posHelper=%.1f/s steps=%.0f/s maxSteps=%llu %.0fms/s | "
				"dbgEdge=%.1f/s skipped=%.0f/s | "
				"chains=%llu vehs=%llu maxChain=%llu segs=%llu artic=%llu waitCouple=%llu\n",
				(unsigned long long)p.frames, fps,
				(double)p.frame_ms_sum / (double)(p.frames != 0 ? p.frames : 1), (unsigned long long)p.frame_ms_max,
				(double)p.try_couple * per_s, (double)p.try_couple_ns * ms_per_s,
				(double)p.fold_check * per_s, (double)p.fold_check_ns * ms_per_s,
				(double)p.dump_chain * per_s, (double)p.dump_ident * per_s,
				(double)p.dbg_writes * per_s, (double)p.dbg_ns * ms_per_s,
				(double)(size - last_size) * per_s / (1024.0 * 1024.0),
				(double)p.pos_helper * per_s, (double)p.pos_steps * per_s, (unsigned long long)p.pos_max,
				(double)p.pos_ns * ms_per_s,
				(double)edge_written * per_s, (double)edge_skipped * per_s,
				(unsigned long long)chains, (unsigned long long)vehs, (unsigned long long)max_chain,
				(unsigned long long)segs, (unsigned long long)artics, (unsigned long long)wait_couple);

		/* KI-14 (2): where the train-tick milliseconds actually go. 'loco' is the
		 * whole TrainLocoHandler body (both modes) and should track PFE_GL_TRAINS;
		 * plat/resv/edgeGate/coupleH are the R3R additions that sit inside it,
		 * while ctrl/coll/spd/vp are the *native* per-tick passes -- movement,
		 * collision scan, the consist-wide speed computation and the per-vehicle
		 * viewport refresh -- which an R3R-merged mega-chain multiplies. */
		/* KI-26 (step 2): what the named sub-buckets of 'loco' add up to, and the
		 * residue they leave. 'resv' is left out on purpose: it is the inner loop
		 * of 'plat', so counting both would subtract it twice (same for 'coll',
		 * which sits inside 'ctrl'). 'ctrl' also runs from the reversal helpers
		 * outside the handler, so 'sub' can slightly overshoot -- read resid as an
		 * upper bound of the still-unexplained part.
		 *
		 * KI-26 (step 2b, 09-14): 'coupleH', 'edgeGate', 'dec', 'wpr', 'stk' and
		 * 'rev' also run inside the handler and were simply missing from the sum.
		 * 'nl' (non-loco) is the part of the trains tick that is not the handler at
		 * all, i.e. PFE_GL_TRAINS minus 'loco' -- the remainder of the 11.47ms. */
		const double sub_ms = (double)(r3r_ctrl_ns + r3r_spd_ns + r3r_vp_ns + r3r_plat_ns +
				r3r_ord_ns + r3r_prv_ns + r3r_load_ns + r3r_dep_ns +
				r3r_ch_ns + r3r_edge_ns + r3r_dec_ns + r3r_wpr_ns + r3r_stk_ns + r3r_rev_ns) * ms_per_s;
		const double resid_ms = (double)r3r_loco_ns * ms_per_s - sub_ms;
		const double nl_ms = R3RGetTrainsMs() * fps - (double)r3r_loco_ns * ms_per_s;
		fprintf(f, "PERF-TICK loco=%.1f/s %.0fms/s | coupleH=%.1f/s %.0fms/s | plat=%.1f/s %.0fms/s | "
				"resv=%.1f/s %.0fms/s | edgeGate=%.1f/s %.0fms/s | "
				"ctrl=%.1f/s %.0fms/s | coll=%.1f/s %.0fms/s | spd=%.1f/s %.0fms/s | "
				"vp=%.1f/s %.0fms/s | dump=%.0fms/s | mov=%.1f/s | "
				"ord=%.1f/s %.0fms/s | prv=%.1f/s %.0fms/s | load=%.1f/s %.0fms/s | dep=%.1f/s %.0fms/s | "
				"dec=%.1f/s %.0fms/s | wpr=%.1f/s %.0fms/s | stk=%.1f/s %.0fms/s | rev=%.1f/s %.0fms/s | "
				"sub=%.0fms/s resid=%.0fms/s | nl=%.0fms/s glTrains=%.2fms\n",
				(double)r3r_loco_calls * per_s, (double)r3r_loco_ns * ms_per_s,
				(double)r3r_ch_calls * per_s, (double)r3r_ch_ns * ms_per_s,
				(double)r3r_plat_calls * per_s, (double)r3r_plat_ns * ms_per_s,
				(double)r3r_resv_calls * per_s, (double)r3r_resv_ns * ms_per_s,
				(double)r3r_edge_calls * per_s, (double)r3r_edge_ns * ms_per_s,
				(double)r3r_ctrl_calls * per_s, (double)r3r_ctrl_ns * ms_per_s,
				(double)r3r_coll_calls * per_s, (double)r3r_coll_ns * ms_per_s,
				(double)r3r_spd_calls * per_s, (double)r3r_spd_ns * ms_per_s,
				(double)r3r_vp_calls * per_s, (double)r3r_vp_ns * ms_per_s,
				(double)r3r_dump_ns * ms_per_s,
				(double)r3r_mov_iters * per_s,
				(double)r3r_ord_calls * per_s, (double)r3r_ord_ns * ms_per_s,
				(double)r3r_prv_calls * per_s, (double)r3r_prv_ns * ms_per_s,
				(double)r3r_load_calls * per_s, (double)r3r_load_ns * ms_per_s,
				(double)r3r_dep_calls * per_s, (double)r3r_dep_ns * ms_per_s,
				(double)r3r_dec_calls * per_s, (double)r3r_dec_ns * ms_per_s,
				(double)r3r_wpr_calls * per_s, (double)r3r_wpr_ns * ms_per_s,
				(double)r3r_stk_calls * per_s, (double)r3r_stk_ns * ms_per_s,
				(double)r3r_rev_calls * per_s, (double)r3r_rev_ns * ms_per_s,
				sub_ms, resid_ms, nl_ms, R3RGetTrainsMs());
		fclose(f);
	}
	last_size = size;

	R3RDbgEdgeReset();
	p = R3RPerfCounters();
	r3r_loco_calls = r3r_loco_ns = 0;
	r3r_ch_calls = r3r_ch_ns = 0;
	r3r_plat_calls = r3r_plat_ns = 0;
	r3r_resv_calls = r3r_resv_ns = 0;
	r3r_edge_calls = r3r_edge_ns = 0;
	r3r_ctrl_calls = r3r_ctrl_ns = 0;
	r3r_coll_calls = r3r_coll_ns = 0;
	r3r_spd_calls = r3r_spd_ns = 0;
	r3r_vp_calls = r3r_vp_ns = 0;
	r3r_dump_ns = 0;
	r3r_mov_iters = 0;
	r3r_ord_calls = r3r_ord_ns = 0;
	r3r_prv_calls = r3r_prv_ns = 0;
	r3r_load_calls = r3r_load_ns = 0;
	r3r_dep_calls = r3r_dep_ns = 0;
	r3r_dec_calls = r3r_dec_ns = 0;
	r3r_wpr_calls = r3r_wpr_ns = 0;
	r3r_stk_calls = r3r_stk_ns = 0;
	r3r_rev_calls = r3r_rev_ns = 0;
}

/**
 * Get the vehicle where the decouple point is.
 * @param v %Train to decouple.
 * @return The first vehicle of the rear part, or nullptr on failure.
 */
static Train *GetDecoupleVehicle(Train *v)
{
	/* R3R: locate the DECOUPLE order. Normally (east-station path) the train
	 * is triggered while cur_real is still the travel order (GOTO_STATION /
	 * GOTO_DEPOT) and the DECOUPLE sits at cur_real+1. But after entering a
	 * depot, ProcessOrders advances cur_real ONTO the DECOUPLE order itself
	 * (real=5 here), so the gate fires with cur_real already == DECOUPLE.
	 * Accept either location so GetDecoupleVehicle never returns nullptr on
	 * the depot path (which made DecoupleTrain bail out every tick). */
	const Order *cur_order = v->GetOrder(v->cur_real_order_index);
	const Order *decouple_order = nullptr;
	if (cur_order != nullptr && cur_order->IsType(OT_DECOUPLE)) {
		decouple_order = cur_order;
	} else {
		VehicleOrderID next_real = v->cur_real_order_index + 1;
		if (v->GetNumOrders() > 0 && next_real >= v->GetNumOrders()) next_real = 0;
		decouple_order = v->GetOrder(next_real);
	}
	if (decouple_order == nullptr || !decouple_order->IsType(OT_DECOUPLE)) return nullptr;

	/* R3R: the decouple order's boundary selects where to split:
	 *  - auto: release the last coupled-on segment. Prefer the last segment
	 *    boundary so a 4+5 coupled train decouples back into its original 4 and
	 *    5 units.
	 *  - tail count (value N): release the last N coupled-on segments.
	 *  - head boundary (value n): split between segment n and n + 1, releasing
	 *    every segment behind segment n.
	 * The mode is taken from the order itself instead of being re-derived from
	 * the value, so an explicitly chosen boundary of 0 keeps its side (the two
	 * geometry helpers clamp 0 to 1, i.e. the nearest possible boundary).
	 * Fall back to the native per-vehicle heuristic only when the train has no
	 * coupled-on segments (an ordinary locomotive + wagons consist). */
	Train *seg = nullptr;
	switch (decouple_order->GetDecoupleBoundaryMode()) {
		case DecoupleBoundaryMode::TailSegments:
			seg = GetSegmentHeadFromRear(v, decouple_order->GetNumDecouple());
			break;

		case DecoupleBoundaryMode::HeadBoundary:
			seg = GetSegmentBoundaryFromHead(v, decouple_order->GetNumDecouple());
			break;

		case DecoupleBoundaryMode::Auto:
		case DecoupleBoundaryMode::End:
			seg = GetSegmentHeadFromRear(v, 1);
			break;
	}
	if (seg != nullptr) return seg;

	uint num_decouple = GetDecoupleVehicleAuto(v);
	Train *ret = v->GetNextVehicle();
	bool multihead_front = v->IsMultiheaded();
	for (uint i = 1; i < num_decouple && ret != nullptr && ret->GetNextVehicle() != nullptr; i++) {
		if (multihead_front) {
			if (ret->IsRearDualheaded()) multihead_front = false;
		} else {
			if (ret->IsMultiheaded()) multihead_front = true;
		}
		ret = ret->GetNextVehicle();
	}
	if (multihead_front) return nullptr;
	return ret;
}

/**
 * Try to decouple the rear part of the train, with rollback on failure.
 * @param v Front part (with engine).
 * @param u Rear part to decouple.
 * @return True on success.
 */
static bool TryTrainDecouple(Train *v, Train *u)
{
	TrainList original_src;
	MakeTrainBackup(original_src, v);
	Train *first_param = nullptr;
	/* Pre-set the decoupled head's type so NormaliseTrainHead's ConsistChanged assertion holds.
	 * DecoupleTrain will finalise the state (FrontEngine for engines, FreeWagon for wagons). */
	if (u->IsEngine()) {
		u->SetFrontEngine();
	} else {
		u->SetFreeWagon();
	}
	ArrangeTrains(&first_param, nullptr, &v, u, true);

	/* R3R: the automatic DECOUPLE order runs from the vehicle tick, where
	 * _current_company is not a valid company (no command context).
	 * CheckNewTrain -> GetFreeUnitNumber reads Company::Get(_current_company)
	 * and asserts (pool index out of range) whenever the decoupled rear head is
	 * an engine (e.g. an EMU/DMU set), which counts as a new train. Scope the
	 * check to the chain's actual owner, mirroring command-context behaviour. */
	const CompanyID old_company = _current_company;
	_current_company = v->owner;
	CommandCost ret = ValidateTrains(nullptr, u, v, v, true);
	_current_company = old_company;

	bool ok = !ret.Failed();
	u->ConsistChanged(CCF_ARRANGE);
	v->ConsistChanged(CCF_ARRANGE);
	if (!ok) {
		RestoreTrainBackup(original_src);
		v->ConsistChanged(CCF_ARRANGE);
		return false;
	}
	return true;
}

/**
 * R3R (KI-69): 在 R3R 改写订单 / 迁移链头身份之前，先把「正在装卸」这件事结清。
 *
 * CargoPayment 由 PrepareUnload() 建在【链头】上（economy.cpp:1524-1540，同时把该车
 * 压进 Station::loading_vehicles），释放它的唯一路径是 Vehicle::LeaveStation()
 * （vehicle.cpp:3588-3592），而它只会被两处触达：
 *   1. TrainController（train_cmd.cpp:3296-3303）—— 前端车已不在该站 / 已越过站台；
 *   2. Vehicle::HandleLoading（vehicle.cpp:3826-3831）—— 装卸完毕。
 * 其中 HandleLoading 只对【链头】生效（TrainLocoHandler 里 consist->HandleLoading()，
 * train_cmd.cpp:10267）。于是 R3R 只要在装卸途中把链头的 current_order 改写成非装卸
 * 类型，或把链拆开/合并让原链头不再是链头，payment 就再也没人结算：车上
 * cargo_payment 永久非空，loading_vehicles 里留下残条目，下一次
 * BeginLoading() -> PrepareUnload() 直接踩
 * assert(front_v->cargo_payment == nullptr)（economy.cpp:1535）。这正是
 * 「跑三个循环后突然崩溃」的现场（crash-20260916T175303Z.log：2 号机车在 39,33 站台
 * 装卸途中被解挂，CP 一直挂在车上，下个循环回站即炸）。
 *
 * 政策（允许型）：不禁止在站台解挂，只是「先把离站手续办完」——与列车自然装卸完毕
 * 离站完全等价。结算后 current_order 变成 OT_LEAVESTATION，下一 tick 由
 * TrainLocoHandler（train_cmd.cpp:10366-10369）正常 Free 并按新订单出发。
 *
 * 必须在调用方动 current_order 之前调用：LeaveStation() 自带
 * assert(current_order.IsAnyLoadingType())（vehicle.cpp:3590）。
 *
 * @param chain 待改写的链（沿其 Next() 遍历；在拆链【之后】调用时只覆盖该半条链）。
 * @param site  探针标签，便于在 R3R_debug.log 里定位是谁结的账。
 */
static void R3RSettleLoadingBeforeChainEdit(Train *chain, const char *site)
{
	if (chain == nullptr) return;

	/* 只有链头可能持有 payment（PrepareUnload 绑链头），且结算过程不改链序，
	 * 所以沿原链顺序走一遍即可。 */
	for (Train *v = chain; v != nullptr; v = v->Next()) {
		/* R3R (KI-133「幽灵进度条」): 站台上那个装卸进度条是 HandleLoading 给【链头】
		 * 建的贴图特效（economy.cpp SetFillingPercent(front, ...)），唯一摘除点是
		 * Vehicle::LeaveStation()/PreDestructor（vehicle.cpp HideFillingPercent）。
		 * 链头身份一旦被改写（挂车/解挂/换端正迁），特效 id 留在【旧链头】上，而旧
		 * 车头此时已埋进链中，永远不再自己离站 —— 进度条就悬在当初挂车的那座站上
		 * 不消失（玩家现场：车站头顶挂一个进度条，车开走了也不掉）。
		 * 这里对整条链无条件隐藏：本函数只在「链头身份即将改变」的现场被调用，此刻
		 * 链上任何进度条的位置锚点都已失效，纯贴图操作，不影响 payment/订单语义；
		 * 必须在下面的 payment 判定之前做，否则无 payment 的残留特效会漏掉。 */
		HideFillingPercent(&v->fill_percent_te_id);

		if (v->cargo_payment == nullptr) continue;

		if (v->current_order.IsAnyLoadingType()) {
			/* 完整走原生结算：删 payment、从 loading_vehicles 摘除、复位装卸标志、
			 * current_order 转 OT_LEAVESTATION。不能「继续装完」：调用方马上要给这辆车
			 * 换订单，而且链一旦拆开/合并，只管链头的 HandleLoading 再也不会跑。 */
			v->LeaveStation();
		} else {
			/* 兜底：订单已被改写，原生结算不可用（vehicle.cpp:3590 断言订单类型），
			 * 手工释放 payment 与 loading_vehicles 里的残留登记，避免下次
			 * PrepareUnload() 踩 economy.cpp:1535。 */
			Station *st = Station::GetIfValid(v->last_station_visited);
			if (st != nullptr) {
				for (auto it = st->loading_vehicles.begin(); it != st->loading_vehicles.end();) {
					if (*it == v) {
						it = st->loading_vehicles.erase(it);
					} else {
						++it;
					}
				}
			}
			delete v->cargo_payment;   // ~CargoPayment 会把 v->cargo_payment 置空
			v->vehicle_flags.Reset(VehicleFlag::LoadingFinished);
			v->vehicle_flags.Reset(VehicleFlag::CargoUnloading);
			v->load_unload_ticks = 0;
		}

		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "SETTLE-LOAD site=%s veh=%d co=%d tx=%d ty=%d\n",
					site, (int)v->index.base(), (int)v->current_order.GetType(),
					TileX(v->tile), TileY(v->tile));
			fclose(dbg);
		}
	}
}

/** Upper bound for R3R walks of a chain (diagnostics and respace). A legal train
 *  is far shorter than this; the cap only exists so a corrupted link/flag can
 *  never turn a walk into an endless loop (see KI-217). */
static const int R3R_CHAIN_WALK_LIMIT = 512;

/** Upper bound for the "push the half ahead forward" steps
 *  R3RRespaceChainAfterEdit() performs on a pair whose boxes overlap. A splice
 *  error is a pixel or two wide, so this only exists so a pathological chain can
 *  never march off along the track. */
static const int R3R_RESPACE_MAX_STRETCH = 16;

/**
 * R3R (KI-217, 2026-09-27): repair a chain whose DrivingBackwards flag is not
 * uniform.
 *
 * The flag is stored per vehicle (Vehicle::IsDrivingBackwards() reads
 * this->vehicle_flags), but every consumer treats it as a chain-wide property:
 * GetMovingFront()/GetMovingNext()/GetMovingPrev() pick First()/Last() and step
 * along Next()/Previous() depending on it. Upstream keeps it uniform by copying
 * the head's flag onto every vehicle in Train::ConsistChanged() (train_cmd.cpp,
 * "u->vehicle_flags.Set(VehicleFlag::DrivingBackwards, driving_backwards)").
 * R3R re-links two chains (couple / fold-fix merge / depot edit) without that
 * call, so a consist that arrived with the flag set keeps it on its own half of
 * the merged chain only.
 *
 * With the flag set on one half and clear on the other, every walk of the moving
 * chain ping-pongs across the seam forever. Observed 2026-09-27: idx 23 alone
 * carried DB among a DB-clear chain, so the walk alternated 26 <-> 23;
 * R3RRespaceChainAfterEdit() never terminated, its inner TrainController() found
 * no legal track to enter (TTB-PROBE fold-geom), that probe's dump loop ran 19M
 * times -- a 1.1 GB log -- and the game froze.
 *
 * Only an inconsistent chain is touched: a uniform chain (including one that
 * legitimately drives backwards, and a depot chain normalised by
 * R3RNormaliseDepotMergeDirection()) is left exactly as it is. A mixed chain is
 * pulled back onto "the head's end leads", i.e. DB clear -- the same state the
 * couple clean-up below forces on a locomotive that arrived while reversing.
 *
 * @param head Head of the chain to check.
 * @param tag  Probe label.
 * @return Number of vehicles whose flag was cleared (0 = chain was consistent).
 */
static int R3RNormaliseMixedChainDrivingBackwards(Train *head, const char *tag)
{
	if (head == nullptr) return 0;

	const bool head_db = head->vehicle_flags.Test(VehicleFlag::DrivingBackwards);
	bool mixed = false;
	int visited = 0;
	for (const Train *w = head; w != nullptr && visited < R3R_CHAIN_WALK_LIMIT; w = w->Next(), visited++) {
		if (w->vehicle_flags.Test(VehicleFlag::DrivingBackwards) != head_db) { mixed = true; break; }
	}
	if (!mixed) return 0;

	int cleared = 0;
	visited = 0;
	for (Train *w = head; w != nullptr && visited < R3R_CHAIN_WALK_LIMIT; w = w->Next(), visited++) {
		if (!w->vehicle_flags.Test(VehicleFlag::DrivingBackwards)) continue;

		/* Mirror ReverseTrainDirection()'s backup branch: the going-up/down bits
		 * are tied to the flag, so they are inverted together with it. */
		w->vehicle_flags.Reset(VehicleFlag::DrivingBackwards);
		if (HasBit(w->gv_flags, GVF_GOINGUP_BIT) || HasBit(w->gv_flags, GVF_GOINGDOWN_BIT)) {
			ToggleBit(w->gv_flags, GVF_GOINGDOWN_BIT);
			ToggleBit(w->gv_flags, GVF_GOINGUP_BIT);
		}
		UpdateStatusAfterSwap(w, false);
		cleared++;
	}

	/* Refresh the flags/speed caches for the now uniform chain, exactly like the
	 * couple clean-up does after clearing a stale driving-backwards state. */
	head->ConsistChanged(CCF_TRACK);

	R3RDbgWrite("[R3R] DB-NORMALISE %s head=%d headDB=%d cleared=%d n=%d\n",
			tag, (int)head->index.base(), (int)head_db, cleared, visited);
	return cleared;
}

/**
 * R3R (KI-134): pull a freshly edited chain geometrically tight.
 *
 * ArrangeTrains() only re-wires the chain links; every vehicle keeps the pixel
 * position it had before the splice (see the note in TryTrainCouple). A coupling
 * therefore preserves whatever distance the two trains happened to stop at —
 * anything from zero up to the acceptance tolerance of R3RSpliceFolded — and a
 * train is rigid: TrainController() advances every vehicle by exactly one pixel
 * per call, so an over-stretched pair is never closed by simply driving off.
 * The player sees that as "the locomotive and the wagons are no longer flush"
 * right after the inherited schedule (KI-128) starts the merged train moving.
 *
 * TrainController() is the same one-pixel stepper upstream uses in
 * AdvanceWagonsBeforeSwap()/AfterSwap(). Walking the chain from the head, the
 * tail of every over-stretched pair is pushed forward by (actual - nominal)
 * pixels, and — since 2026-09-27 (KI-214) — every pair whose boxes *overlap*
 * gets the half ahead of it pushed forward by (nominal - actual) instead:
 * TrainController(head, b) stops before b, so a and everything in front of it
 * advances while b and its tail stay put. That is the only way to widen a pair,
 * because the controller can only step forward. Directions, identities and
 * orders are never touched, and the nominal used here is the engine's own
 * CalcNextVehicleOffset() (odd lengths round up for the leading vehicle), not
 * the naive (la + lb) / 2 average that used to hide a 1px overlap. Articulated
 * parts, which legitimately share their parent's exact position, are never
 * touched.
 *
 * @param head Head of the chain to tighten (walked along GetMovingNext()).
 * @param tag  Probe label.
 * @return Number of pixels the chain was moved.
 */
static int R3RRespaceChainAfterEdit(Train *head, const char *tag)
{
	if (head == nullptr) return 0;

	/* R3R (KI-217): the walk below follows GetMovingNext(), i.e. it depends on
	 * DrivingBackwards being uniform along the chain. R3R re-links chains
	 * without ConsistChanged(), so repair a mixed flag here before stepping —
	 * otherwise the walk crosses the same seam back and forth forever. */
	R3RNormaliseMixedChainDrivingBackwards(head, tag);

	int moved = 0;
	int stretched = 0;
	int visited = 0;
	for (Train *a = head; a != nullptr && visited < R3R_CHAIN_WALK_LIMIT; a = a->GetMovingNext(), visited++) {
		Train *b = a->GetMovingNext();
		if (b == nullptr) break;

		/* Articulated parts legitimately share their parent's exact centre
		 * (distance 0); that is not an overlap and must never be "fixed". The
		 * same goes for pairs whose nominal offset is below one pixel: the
		 * engine's rounding is meaningless there. */
		if (a->IsArticulatedPart() || b->IsArticulatedPart()) continue;

		const int nominal = a->CalcNextVehicleOffset();
		if (nominal < 2) continue;

		/* A splice error is a couple of pixels wide at most; bound the loop so a
		 * pair that refuses to settle cannot spin here. */
		for (int guard = 0; guard < 64; guard++) {
			const int dist = std::max(std::abs((int)a->x_pos - (int)b->x_pos), std::abs((int)a->y_pos - (int)b->y_pos));
			if (dist == nominal) break;

			if (dist > nominal) {
				/* Too far apart: pull the tail of this pair forward. Only move
				 * when a one pixel step of b actually closes this pair: b may
				 * face the other way (artic parts, or a part that is still being
				 * turned around), in which case moving it would stretch the chain
				 * further instead. */
				const GetNewVehiclePosResult np = GetNewVehiclePos(b);
				const int next_dist = std::max(std::abs((int)a->x_pos - np.x), std::abs((int)a->y_pos - np.y));
				if (next_dist >= dist) break;

				if (!TrainController(b, nullptr, false)) break;
				moved++;
			} else {
				/* Too close: the two boxes overlap by (nominal - dist) pixels.
				 * TrainController() can only step forward, so b cannot be pushed
				 * back -- but the half *ahead* of the pair can be pulled further
				 * ahead instead. TrainController(head, b) walks from the head to
				 * b and stops there, so a (and everything in front of it) advances
				 * one pixel while b and its tail stay put: exactly the inverse of
				 * the tightening step above. */
				if (stretched >= R3R_RESPACE_MAX_STRETCH) break;

				const GetNewVehiclePosResult np = GetNewVehiclePos(a);
				const int next_dist = std::max(std::abs(np.x - (int)b->x_pos), std::abs(np.y - (int)b->y_pos));
				if (next_dist <= dist) break;

				if (!TrainController(head, b, false)) break;
				stretched++;
			}
		}
	}

	if (moved != 0 || stretched != 0 || visited >= R3R_CHAIN_WALK_LIMIT) {
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "[R3R] RESPACE-AFTER-EDIT %s head=%d px=%d stretch=%d visited=%d capped=%d\n",
					tag, (int)head->index.base(), moved, stretched, visited, (visited >= R3R_CHAIN_WALK_LIMIT) ? 1 : 0);
			fclose(dbg);
		}
	}
	return moved + stretched;
}

/**
 * R3R: does the train carry a real wagon behind its head? That is the native
 * "locomotive + wagons" shape the decouple fallback may still split; a train
 * made of nothing but engines has no wagon to release.
 * @param v Train to look at.
 * @return true if a real (non-engine) vehicle follows the head.
 */
static bool R3RHasWagonBehindEngine(const Train *v)
{
	for (const Train *t = v->GetNextVehicle(); t != nullptr; t = t->GetNextVehicle()) {
		if (!t->IsEngine()) return true;
	}
	return false;
}

/**
 * R3R (2026-09-23): step a train's real order index forward over the orders a
 * just-executed decouple has already fulfilled, so that the train comes to rest
 * on an order it can still execute. Two shapes are known to strand a train:
 *
 *  - the arrival order it is standing on (GOTO_STATION / GOTO_DEPOT whose
 *    destination is the tile under it) together with the DECOUPLE that belonged
 *    to it. Both were fulfilled by the decouple we just ran: stopping on the
 *    arrival re-fires the decouple every tick ("parked at this order's station"
 *    plus "next real order is a DECOUPLE" is still true, see the decouple gate
 *    in TrainLocoHandler) even though the train has nothing left to cut, and
 *    stopping on the DECOUPLE parks it on an order ProcessOrders answers false
 *    for (a decouple order only counts as "arrived" inside a depot) - the
 *    2026-09-23 report of veh=27 parked at 58,28 with real=6 on L-ORD 6 =
 *    DECOUPLE.
 *  - a bare DECOUPLE order: after the split the train is alone, so no boundary
 *    can ever be cut from it again. A train parked on that order keeps its
 *    platform tiles reserved only under its own vehicles, which is what made
 *    the next locomotive find no safe couple position at all (yapf probe
 *    FSCP fail=2nd / fail=notSeg, then CPL-SAFE-FAIL, same report).
 *
 * The walk is bounded by the order count so a pathological schedule (e.g. made
 * of nothing but DECOUPLE orders) cannot spin here.
 * @param v Train whose order index is stepped forward.
 * @return Number of orders stepped over (0 = the index was left untouched).
 */
static uint R3RSkipUnfireableDecoupleOrder(Train *v)
{
	if (v->GetNumOrders() == 0) return 0;
	uint stepped = 0;

	/* 1. The arrival order we are standing on and the DECOUPLE behind it. */
	const Order *cur = v->GetOrder(v->cur_real_order_index);
	if (cur != nullptr && (cur->IsType(OT_GOTO_STATION) || cur->IsType(OT_GOTO_DEPOT))) {
		const bool arrived = cur->IsType(OT_GOTO_STATION)
				? (IsTileType(v->tile, TileType::Station) && cur->GetDestination().ToStationID() == GetStationIndex(v->tile))
				: (IsRailDepotTile(v->tile) && cur->GetDestination().ToDepotID() == GetDepotIndex(v->tile));
		if (arrived) {
			VehicleOrderID next_real = v->cur_real_order_index + 1;
			if (next_real >= v->GetNumOrders()) next_real = 0;
			const Order *next = v->GetOrder(next_real);
			if (next != nullptr && next->IsType(OT_DECOUPLE)) {
				v->IncrementRealOrderIndex();
				stepped++;
			}
		}
	}

	/* 2. Never come to rest on a DECOUPLE order that cannot fire any more. */
	for (uint i = 0; i < v->GetNumOrders(); ++i) {
		const Order *o = v->GetOrder(v->cur_real_order_index);
		if (o == nullptr || !o->IsType(OT_DECOUPLE)) break;
		v->IncrementRealOrderIndex();
		stepped++;
	}

	return stepped;
}

/**
 * R3R (2026-09-27, 玩家口径拍板): advance one half of a just-split chain past the
 * orders the decouple itself has fulfilled. Both halves use this same rule; which
 * branch applies follows from WHICH SCHEDULE the half ended up driving, not from
 * whether it happens to be the front (v) or the rear (u) part, and not from any
 * assumption about which half carries the traction:
 *
 *  - the half which drives the schedule the consist was running (driving_orders,
 *    captured before the sync): the DECOUPLE that fired is an order of that very
 *    list, so the half resumes at its successor, decouple_idx + 1. A locomotive
 *    that keeps the borrowed list and a consist that owns it are treated alike --
 *    the owner is decided by r3r_priority, never by u/v.
 *  - the half which got its OWN parked schedule back (orders_backup): decouple_idx
 *    is meaningless there, because the DECOUPLE was never an order of that list.
 *    It only steps over the GOTO_COUPLE it was executing when it coupled -- that
 *    order has been fulfilled by the couple+decouple pair (the depot drag of a
 *    wagon, the locomotive picking its consist up), and re-running it would lock
 *    the index on the order forever.
 *
 * Both halves then run R3RSkipUnfireableDecoupleOrder(): whoever is alone cannot
 * fire a DECOUPLE any more, and resting on one is the known stranding shape.
 *
 * @param half The chain head of the half to advance.
 * @param driving_orders The schedule the merged consist was running.
 * @param decouple_idx Index of the DECOUPLE that fired (in driving_orders).
 */
static void R3RAdvanceAfterDecouple(Train *half, OrderList *driving_orders, VehicleOrderID decouple_idx)
{
	if (half->orders == nullptr) return;   /* Nothing to advance; see the no-table fallback in DecoupleTrain(). */

	if (half->orders == driving_orders) {
		const VehicleOrderID n = half->GetNumOrders();
		if (n > 0) {
			const VehicleOrderID next = (decouple_idx + 1) % n;
			half->cur_real_order_index = next;
			half->cur_implicit_order_index = next;
		}
	} else {
		const Order *parked_order = half->GetOrder(half->cur_real_order_index);
		if (parked_order != nullptr && parked_order->IsType(OT_GOTO_COUPLE)) {
			/* R3R (KI-215d, 2026-09-27): assign the landing index DIRECTLY instead of
			 * calling IncrementRealOrderIndex() -- that call is a guaranteed no-op here.
			 * At this moment current_order still holds the OT_GOTO_COUPLE that is being
			 * executed (it is only Free()d below, after this helper returned), and the
			 * R3R lock for GOTO_COUPLE locomotives makes every advance path return
			 * immediately: SkipToNextRealOrderIndex() (vehicle_base.h:1029) and
			 * IncrementImplicitOrderIndex() (:1053) both start with
			 * `if (type == Train && current_order.IsType(OT_GOTO_COUPLE)) return;`, and
			 * UpdateRealOrderIndex() carries the same guard (:1095). The index therefore
			 * stayed on that very order for ever -- observed in build\R3R_debug.log as
			 * `DECOUPLE-ADV dec_idx=19 v_real=20 u_real=4` on a 6 order parked list
			 * (U-ORD 4 type=16, 5 type=2), followed by a permanent
			 * CPL-GATE reject=pair-mismatch / COUPLE-FAIL / stuck=1 loop: the loco re-ran
			 * the already fulfilled GOTO_COUPLE instead of stepping onto 5 (GOTO_DEPOT).
			 * The front-half branch above always assigned the index directly, which is
			 * why only this half was affected. */
			const VehicleOrderID n = half->GetNumOrders();
			if (n > 0) {
				const VehicleOrderID next = (half->cur_real_order_index + 1) % n;
				half->cur_real_order_index = next;
				half->cur_implicit_order_index = next;
			}
		}
	}
	R3RSkipUnfireableDecoupleOrder(half);
	half->cur_timetable_order_index = half->cur_real_order_index;
	/* The index moved: repaint the order window (the replaced Increment call did that too). */
	InvalidateVehicleOrder(half, 0);
}

/* R3R (KI-205): defined next to Couple(); used by DecoupleTrain() as well. */
static uint R3RReleaseChainReservations(const Train *head, const char *why);

/**
 * Decouple the rear part of the train as specified by the DECOUPLE order.
 * @param v %Train to decouple.
 * @return The first vehicle of the decoupled rear part, or the original train if decoupling failed.
 */
static Train *DecoupleTrain(Train *v, bool allow_in_depot = false)
{
	/* R3R: allow a decouple order to be executed inside a depot. Native
	 * CanDecouple() forbids decoupling while InDepot(); R3R moves the
	 * round-2 decouple to a depot tile, so waive that one restriction here
	 * while keeping the other safety checks (chain length, next unit). */
	bool can_decouple = CanDecouple(v);
	if (!can_decouple && allow_in_depot && v->IsInDepot()) {
		if (CountVehiclesInChain(v) >= 2 && v->GetNextUnit() != nullptr) can_decouple = true;
	}
	if (!can_decouple) return v;
	Train *u = GetDecoupleVehicle(v);
	if (u == nullptr) return v;
	if (!TryTrainDecouple(v, u)) return v;

	/* R3R (KI-69)：拆分已经成功（此时才结账，避免 TryTrainDecouple 回滚后列车白跑一趟），
	 * 但订单/身份的改写还没开始 —— 在这里把装卸结算掉。链头 v 是拆前整链的前端车，
	 * payment 就挂在它身上；u 侧一并扫一遍是防御（正常不该有）。 */
	R3RSettleLoadingBeforeChainEdit(v, "decouple-front");
	R3RSettleLoadingBeforeChainEdit(u, "decouple-rear");

	if (u->IsEngine()) {
		u->ClearFreeWagon();   // R3R: drop any stale free-wagon flag so the front stays a (fake) engine
		u->SetFrontEngine();
		u->vehstatus.Reset(VehState::Stopped);
	} else {
		/* R3R: the wagon part becomes a car-only formation: a zero-power train
		 * front. Every OpenTTD subsystem then treats it as a normal train front
		 * (it can hold orders, appears in the vehicle list, etc.), but it has no
		 * power, so it simply waits until a locomotive couples onto it. */
		if (R3RCreateCarOnlyFormation(u) == nullptr) {
			u->SetFreeWagon();
		} else {
			u->SetFrontEngine();
			/* R3R: give the decoupled formation a unit number so it appears as a
			 * proper train in the list — but allocate it from the formation's OWN
			 * company pool (u->owner), NOT via GetFreeUnitNumber() which reads the
			 * global _current_company (invalid during vehicle ticks → crash). */
			/* R3R (第 148 轮续 / KI-237 站台复测，2026-09-28): 这一段可能带着一个
			 * 「停放/借出」的号 —— 车库拖动(DEPOT-PARK)与耦合借用交接会把失去链头身份的
			 * 段的号停进 unitnumber_backup，同时保持在号池里的占用（不 ReleaseID）。
			 * 那就必须先把号取回来，绝不能直接领新号：领了新号后 u->unitnumber != 0，
			 * 本函数末尾 NormaliseTrainHead(u) → R3RRestoreUnitNumber() 的
			 * 「unitnumber != 0 就提前返回」立刻成立 ⇒ 玩家看到解出来的车底换了号，
			 * 而它自己的号永久冻在 unitnumber_backup 里占着池位。
			 * （第 148 轮的站台复测日志里只有 UNIT-PARK 没有 UNIT-RESTORE，就是这个原因：
			 * 修复挂在了 NormaliseTrainHead 上，却被这里的"先领新号"抢了先。） */
			R3RRestoreUnitNumber(u);
			if (u->unitnumber == 0) {
				u->unitnumber = Company::Get(u->owner)->freeunits[VehicleType::Train].NextID();
				if (u->unitnumber != UINT16_MAX) Company::Get(u->owner)->freeunits[VehicleType::Train].UseID(u->unitnumber);
			}
		}
	}

	/* R3R: hand the consist's schedule over to the decoupled part, and restore
	 * the locomotive's OWN orders + order position + unit number (backed up by
	 * Couple) so the locomotive goes back to its pre-coupling schedule instead
	 * of ending up with an empty order list / number 1. This applies to both a
	 * wagon consist and a powered (engine) segment that was decoupled.
	 *
	 * R7 (restore guard mirrors Couple's hand-over guard): Couple writes
	 * orders_backup under "consist has orders" (u->orders != nullptr) and
	 * moves that schedule onto v. A scheduled locomotive coupling onto a
	 * schedule-less consist skips the hand-over entirely, so orders_backup
	 * stays null while v->orders still holds the locomotive's OWN schedule.
	 * Guarding this restore on v->orders != nullptr then robbed the
	 * locomotive: loco_orders was null, its own schedule was handed to the
	 * decoupled part u and the locomotive was left with no schedule. With
	 * v->orders_backup != nullptr the block runs exactly when a real hand-over
	 * happened — and because Couple writes the backup and the move together,
	 * u->orders = v->orders inside always picks up the moved consist schedule
	 * (never the locomotive's own). In the no-hand-over case the locomotive
	 * keeps its schedule and the schedule-less part waits on its own, exactly
	 * as it did before the couple. (第 148 轮续起，上面那段"领新号"之前先调
	 * R3RRestoreUnitNumber() 把本段停放/借出的号取回 —— 只有真的没有备份时才领新号；
	 * 本块里 v->unitnumber_backup 分支仍然把号在机车与车底之间换回来，两者互不冲突：
	 * 有 v 备份时说明 Couple 借过号，此时 u 没有备份可取、v->unitnumber 正是 u 的旧号。) */
	bool u_inherited_running = false;
	if (v->r3r_orders_borrowed) {
		/* R3R: remember which schedule index held the DECOUPLE we just ran, so the
		 * decoupled part resumes waiting at the NEXT WAIT_COUPLE (the one for the
		 * station it is physically sitting at), not the first one in the list.
		 * (KI-215c 修正了这个"记住的索引"到底是哪一条 —— 见下。) */
		/* R3R (KI-215c, 2026-09-26)：cur_real_order_index 此刻仍指向【刚跑完的那条】
		 * (列车停在它终点的 GOTO_STATION，正是这个位置让下一条 DECOUPLE 触发)，触发
		 * 解挂的 DECOUPLE 是它的【后继】。旧代码把"前一条"存进 decouple_idx，而下面
		 * "找后继 WAIT_COUPLE"的循环从 decouple_idx + 1 起步 ⇒ 第一次探测必然打在
		 * 刚执行的 DECOUPLE 自己身上(它不可能是 WAIT_COUPLE)，白跳一格；订单窗口
		 * 看上去从"到站行"一次跨两行落到等待点，玩家读成"解挂时列车多跳了一个命令"。
		 * 这里直接存 DECOUPLE 自身的索引，使循环起点 = D + 1。**落点不变**(两种读法都
		 * 得"D 之后第一个 WAIT_COUPLE"，因为 D+1 在两读法里都不是 WAIT_COUPLE)，只是
		 * 口径/命名与注释一致，后续任何以 decouple_idx 为"D 的索引"的用法不再差一。 */
		const VehicleOrderID fire_real = v->cur_real_order_index;
		VehicleOrderID decouple_idx = fire_real;
		{
			const VehicleOrderID n_fire = v->GetNumOrders();
			if (n_fire > 0) decouple_idx = (decouple_idx + 1) % n_fire;
		}

		/* R3R (KI-180,口径经玩家 2026-09-23 拍板确认): 解挂出来的两半【绝不共用
		 * 一张订单表】。旧代码把正在跑的排程【别名】给没有排程的解出方
		 * (u->orders = v->orders; u now owns it)，于是两条链指向同一张 OrderList
		 * 却没有登记成共享（IsOrderListShared() 仍是 false）：任一侧之后的编辑 /
		 * 清空 / 卖车 / DeleteVehicleOrders 都会直接改写或销毁另一侧正在用的表
		 * （KI-93 / KI-93b 那一族悬垂），而为一侧做的插入还会出现在另一侧的
		 * 订单窗口里（KI-178 现象②"那行变成了等待挂接"）。
		 *
		 * 现在的口径：解出方跑【自己的表】；自己没有排程 ⇒ 解挂后持空排程，
		 * 需要等待时用【自己表里】的 WAIT_COUPLE（见下面 u_inherited_running 的
		 * 就地插入）。因此这里只记录"解出方本来就没有自己的表"，不再别名。 */
		u_inherited_running = (u->orders == nullptr);
		/* R3R (route A, 2026-09-15): does u OWN the schedule the consist was just
		 * executing? Then the DECOUPLE that fired is an order of u's own list, so
		 * u must resume at the WAIT_COUPLE that FOLLOWS it -- exactly like the
		 * inherited case below -- instead of coming back at whatever stale index
		 * it happened to hold. Route A made "u keeps its own schedule" the normal
		 * case, which left the resume logic gated on u_inherited_running (now
		 * always false for a real consist) and therefore dead: the consist came
		 * back at index 0, so the next locomotive skipped its WAIT_COUPLE into the
		 * FIRST half of the cycle again -- "go to the station it is already parked
		 * at" was satisfied on the spot and the following DECOUPLE undid the
		 * couple on the very next tick (observed 2026-09-15: COUPLE-OK then
		 * DECOUPLE-FIRE at the same tile in test4.sav, after which the locomotive
		 * ran off to its own next order and left the consist waiting forever).
		 * Captured BEFORE R3RSyncDrivingOrders(v), which may hand v's borrowed
		 * pointer back and break the pointer comparison.
		 *
		 * R3R (KI-215, 2026-09-26 修): 需要的是"合并链正在跑的那张表"(v->orders)，
		 * 不是"两半本来共有的表"。旧判据 `u->orders == v->orders` 要求解出方【本来】
		 * 就持有那张表，于是"解出方是链中段"(它还保管着自己那份已停放的排程 —— 正是
		 * T8701 四段链现场)时判据必假：不给它找等待点，而上面的 sync 又把它的位置
		 * 留在命令所有者当初借用时【冻结】的旧索引上。现场：DECOUPLE-FIRE real=18，
		 * 随后 DECOUPLE-DONE u=0 real=15 —— 订单窗口可见地往回跳（第 19 行 → 第 16
		 * 行），且落在一条【已经执行过】的 DECOUPLE 上（ProcessOrders 永不为它收尾），
		 * 该车底就此僵死，直到有新机车来挂。R3R_debug.log 里每次解挂都是这个签名
		 * （u 停在 DECOUPLE 的索引上，从未停在 WAIT_COUPLE）。 */
		OrderList *const driving_orders = v->orders;
		/* Rebuild both sides' priorities, then let each chain head re-point at its
		 * own command owner: v gives the borrow back when it BECAME the owner
		 * (T8701 #2 restore), otherwise it keeps driving the owner's schedule
		 * (T8701 #1 inherit). */
		R3RRenumberPriorities(v);
		R3RRenumberPriorities(u);
		R3RSyncDrivingOrders(v, false);
		R3RSyncDrivingOrders(u, false);
		InvalidateVehicleOrder(v, 0);
		InvalidateVehicleOrder(u, 0);
		/* Keep the timetable index in sync with the restored order index. */
		v->cur_timetable_order_index = v->cur_real_order_index;
		/* R3R: the order the locomotive was executing when it coupled (e.g. its
		 * first order "go to depot & couple") has already been completed by the
		 * decouple — advance past it so it continues with the NEXT order instead
		 * of re-running the completed one (which the GOTO_COUPLE index lock would
		 * otherwise keep it stuck on forever).
		 *
		 * Only step over it when the restored position really IS that
		 * GOTO_COUPLE. A blind step also skips an order the decouple did NOT
		 * fulfil (a plain GOTO_STATION the locomotive was parked on) and can
		 * drop the index straight onto an unreachable DECOUPLE: a decouple order
		 * only counts as "arrived" inside a depot (see the decouple trigger in
		 * the vehicle order handler) and ProcessOrders returns false for it, so
		 * the train then sits on that order forever - the 2026-09-23 report of
		 * veh=27 parked at 58,28 with real=6 on L-ORD 6 = DECOUPLE was exactly
		 * that blind step landing on the DECOUPLE of the pair we had just run.
		 * The rest of that pair (the arrival order we are standing on, L-ORD 5 =
		 * GOTO_STATION, and the DECOUPLE behind it) is stepped over by the helper
		 * below, which also clears any DECOUPLE the restored index lands on: the
		 * locomotive is alone after the split, so a decouple order can never fire
		 * for it again, and a locomotive parked on one holds only the track under
		 * its own wheels, leaving the next locomotive without any safe couple
		 * position on that platform (FSCP fail=2nd / fail=notSeg). */
		/* R3R (2026-09-27): the same two-branch advance the rear half gets below.
		 * The locomotive half normally lands in the GOTO_COUPLE branch (it takes
		 * its own parked schedule back); a half which keeps driving the consist's
		 * own list lands on decouple_idx + 1 instead. */
		R3RAdvanceAfterDecouple(v, driving_orders, decouple_idx);
		R3RCheckTtSync(v, "decouple-v");
		/* R3R: clear the stale current order (the DECOUPLE that was just
		 * executed) so the next ProcessOrders tick loads whatever
		 * R3RAdvanceAfterDecouple() left the index on — the restored
		 * orders_backup position, or the order following the DECOUPLE —
		 * otherwise the train would sit idle forever (ProcessOrders returns
		 * false for DECOUPLE). */
		v->current_order.Free();
		v->SetDestTile(INVALID_TILE);
		/* R3R (KI-177): same suppression as at the couple submit point -- the
		 * cleared current order plus a continuing roll-out over the platform makes
		 * BeginLoading() record a phantom OT_IMPLICIT "(自动)" order. Cleared by
		 * the engine on the next real arrival (DeleteUnreachedImplicitOrders).
		 *
		 * R3R (2026-09-27): u gets it too. Since both halves now advance by the
		 * same rule, either of them can be the one that keeps running -- the rear
		 * half driving the consist's own list on from D+1 just as much as the
		 * front half rolling out towards its next order -- so "the half that
		 * keeps running" no longer identifies a single side. The old code relied
		 * on the released part resting on its WAIT_COUPLE and never re-entering
		 * loading, which is exactly the assumption this change removes. */
		SetBit(v->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS);
		SetBit(u->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS);
		/* R3R (2026-09-27, 玩家口径拍板): 解出的那半不再"搜 DECOUPLE 之后的第一个
		 * WAIT_COUPLE"钉住，而是与 v 走同一条推进规则（R3RAdvanceAfterDecouple）。
		 * 分支由【控制段归属】决定，不由 u/v 的先后决定，也不预设谁是动力方：
		 *  - 它正在跑原链跑的那张表（driving_orders）⇒ DECOUPLE 就是这张表的订单，
		 *    直接落到 D+1；
		 *  - 它拿回的是自己那张已停放的排程 ⇒ decouple_idx 在那张表里没有意义，
		 *    只跨过自己耦合时正在执行的那条 GOTO_COUPLE（车库拖动挂车那一类）。
		 *
		 * 玩家口径（2026-09-27）：WAIT_COUPLE 是在【耦合】那一刻被兑现的 —— 一张
		 * 能控制整条链的排程，别人来挂接它之后仍然由它控制，链跑的就是它排程的
		 * 下一步。所以等待点不必由代码去猜，落点由玩家自己写的订单顺序给出。
		 *
		 * R3R (KI-180, 玩家 2026-09-23 拍板): 两半各自只认【自己那张表】，推进只作用
		 * 于自己的索引，绝不改写另一条链的表。
		 *
		 * R3R (KI-180 现象②/KI-178): u 本来【没有】排程 ⇒ 解挂后持空排程：在【它自己
		 * 新建的表】里就地插一条 WAIT_COUPLE 作为等待点。这张表是 u 独有的，插入不再
		 * 会把另一条链（机车那一侧）订单窗口的行号整体下移一格 —— 那正是 KI-178
		 * 现象②"第二次点停车标后那行显示成了等待挂接"的成因。 */
		R3RAdvanceAfterDecouple(u, driving_orders, decouple_idx);
		VehicleOrderID wait_idx = INVALID_VEH_ORDER_ID;
		if (u_inherited_running && u->orders == nullptr) {
			/* u 连一张表都没有（R3RSyncDrivingOrders 之后仍是 nullptr，即解出链里
			 * 也没有别的段带排程）⇒ 新建它自己的表并就地插等待点。
			 * InsertOrder() 在 orders == nullptr 时会 OrderList::Create()，所以这里
			 * 不会碰到留下的那半的表。
			 *
			 * R3R (KI-186, 2026-09-24 崩溃): 下面走的是【底层】InsertOrder() 而不是
			 * 命令层的 CmdInsertOrder()，而 OrderList 池在 Debug 版被
			 * WITH_FULL_ASSERTS 的"先问再取"闸门保护 —— pool_func.hpp:124 的
			 * `dbg_assert(this->checked != 0)` 只认"刚刚调用过
			 * OrderList::CanAllocateItem()"这个预算位。命令层替我们问过了
			 * （order_cmd.cpp:1575），绕过命令层就必须自己问 —— 上游自己的写法就是
			 * vehicle.cpp:3544 那种 `orders == nullptr ? OrderList::CanAllocateItem()
			 * : ...` 紧挨着 :3548 的 InsertOrder()。不问的话，只要走到
			 * "解出的那半本来没有排程"这一支就必然断言崩溃
			 * （crash-20260924T094952Z.log: DecoupleTrain → InsertOrder:1596 →
			 * OrderList::Create → Pool::GetNew:124，现场 veh=33 @58,54）。
			 * 池真的满时退化成不给这张表：该部分就地停着，与"没有排程"的既有
			 * 语义一致，不拖垮整局。 */
			if (OrderList::CanAllocateItem()) {
				Order wc;
				wc.MakeWaitCouple();
				InsertOrder(u, std::move(wc), 0);
				wait_idx = 0;
			} else {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "DECOUPLE-NO-ORDERTABLE veh=%d (orderlist pool full)\n", (int)u->index.base());
					fclose(dbg);
				}
			}
		}
		if (wait_idx != INVALID_VEH_ORDER_ID) {
			u->cur_real_order_index = wait_idx;
			u->cur_implicit_order_index = wait_idx;   // sync implicit — otherwise the order window paints ▶ at the implicit index
			u->cur_timetable_order_index = wait_idx;
		}
		/* R3R (KI-215b, 2026-09-26): 两半的位置都定下来之后，把"实际跑到的位置"
		 * 推回各自的命令所有者 —— 借用期间它一直冻在借用开始时的索引上，而下一个
		 * 接手者恰恰是从它那里继承进度的（见 R3RPushProgressToOwner）。 */
		R3RPushProgressToOwner(v);
		R3RPushProgressToOwner(u);
		R3RCheckTtSync(u, "decouple-u");
		/* R3R: clear the stale current order (the DECOUPLE that was just
		 * executed) so the next ProcessOrders tick loads the WAIT_COUPLE
		 * order — otherwise the train would sit idle forever. */
		u->current_order.Free();
		u->SetDestTile(INVALID_TILE);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				/* R3R (KI-215c probe, 2026-09-26)：让"解挂时列车多跳了一个命令"的
				 * 报告可以直接判读 —— fire_real = 刚跑完的那条(列车停在其终点的站)，
				 * dec_idx = 真正触发解挂的 DECOUPLE，wait_idx = 解出方被钉住的
				 * WAIT_COUPLE。step = wait_idx - dec_idx(按 u 的表取模，-1 表示没钉)，
				 * 后随 4 行 JUMP-ORD 列出 dec_idx..dec_idx+3 上到底是什么类型，于是
				 * "跳了几格、跳过去的那条是什么"一次日志即可核对，不必再复跑。 */
				if (u->orders != nullptr) {
					const VehicleOrderID n_jump = u->GetNumOrders();
					const int jump_step = (wait_idx == INVALID_VEH_ORDER_ID || n_jump == 0)
							? -1
							: (int)((wait_idx + n_jump - decouple_idx) % n_jump);
					fprintf(dbg, "DECOUPLE-JUMP fire_real=%d dec_idx=%d wait_idx=%d step=%d v=%d u=%d n_u=%d\n",
						(int)fire_real, (int)decouple_idx, (int)wait_idx, jump_step,
						(int)v->index.base(), (int)u->index.base(), (int)n_jump);
					for (VehicleOrderID k = 0; k < 4 && n_jump > 0; ++k) {
						const VehicleOrderID i = (decouple_idx + k) % n_jump;
						const Order *jo = u->GetOrder(i);
						fprintf(dbg, "  JUMP-ORD %d type=%d\n", (int)i, (int)(jo ? jo->GetType() : -1));
					}
				} else {
					fprintf(dbg, "DECOUPLE-JUMP fire_real=%d dec_idx=%d wait_idx=%d step=-1 v=%d u=%d n_u=0\n",
						(int)fire_real, (int)decouple_idx, (int)wait_idx,
						(int)v->index.base(), (int)u->index.base());
				}
				/* R3R (2026-09-27): 两半各自推进后的落点。新规则下 wait_idx 只在
				 * "解出方本来没有排程"那一支才有值（step=0）；正常情况下两半都由
				 * R3RAdvanceAfterDecouple() 落位，直接看 v_real / u_real。 */
				fprintf(dbg, "DECOUPLE-ADV dec_idx=%d v_real=%d u_real=%d\n",
					(int)decouple_idx, (int)v->cur_real_order_index, (int)u->cur_real_order_index);
				fprintf(dbg, "DECOUPLE-DONE u=%d co=%d real=%d tx=%d ty=%d x=%d y=%d\n",
					(int)u->index.base(), (int)R3RIsCarOnlyFormation(u),
					(int)u->cur_real_order_index, (int)TileX(u->tile), (int)TileY(u->tile), (int)u->x_pos, (int)u->y_pos);
				if (u->orders != nullptr) {
					for (VehicleOrderID i = 0; i < u->GetNumOrders(); ++i) {
						const Order *o = u->GetOrder(i);
						fprintf(dbg, "  U-ORD %d type=%d\n", (int)i, (int)(o ? o->GetType() : -1));
					}
				}
				fclose(dbg);
			}
		}
		/* Restore the unit numbers: the decoupled part keeps its own number (the
		 * locomotive currently holds it), the locomotive gets its own back. */
		if (v->unitnumber_backup != 0) {
			/* R3R (KI-147): the car-only formation above just took a FRESH unit
			 * number out of the pool (u->unitnumber == 0 after Couple took the
			 * consist's number away). That fresh number is thrown away here by
			 * the restore below, so hand it straight back to the pool -- every
			 * decouple of a scheduled consist used to leak one unit id this way,
			 * which is what made train numbers climb over a long game. */
			if (u->unitnumber != 0 && u->unitnumber != v->unitnumber) {
				Company::Get(u->owner)->freeunits[VehicleType::Train].ReleaseID(u->unitnumber);
			}
			u->unitnumber = v->unitnumber;
			/* u now holds that number itself; the copy parked by Couple (used by
			 * the depot-split path) is stale and must not stay behind as a second
			 * reference to the same pool entry. */
			u->unitnumber_backup = 0;
			v->unitnumber = v->unitnumber_backup;
			v->unitnumber_backup = 0;
		}
	}
	/* R3R（2026-09-22）：解出的两半各自把自己链上的「列车分组」与「挂接分组」收敛到
	 * 实际控制段的分组，见 R3RNormaliseChainGroups()。num_vehicle 以「前端列」为单位
	 * 簿记：留下的 v 原本已把这一列计入它旧分组，先摘出、归一化后再按新分组计入；
	 * 解出的 u 在此之前不是独立前端列（只是原链的一段），故只需计入。
	 * 旧代码无条件 SetTrainGroupID(u, DEFAULT_GROUP) 会把 u 自带的段分组抹掉。 */
	GroupStatistics::CountVehicle(v, -1);
	R3RNormaliseChainGroups(v);
	GroupStatistics::CountVehicle(v, 1);
	R3RNormaliseChainGroups(u);
	GroupStatistics::CountVehicle(u, 1);
	NormaliseTrainHead(u);
	NormaliseTrainHead(v);
	/* R3R: the released part's head was just promoted to a front engine (the
	 * engine branch's SetFrontEngine() above, or R3RCreateCarOnlyFormation() for
	 * a pure-wagon part) -- and that happened *after* this tick's vehicle tick
	 * caches were built, so the new front is missing from
	 * _tick_train_front_cache. That cache is only rebuilt when it has been
	 * invalidated, therefore without this call the released part would never be
	 * Tick()ed again: TrainLocoHandler() never runs, its WAIT_COUPLE order is
	 * never loaded into current_order, and every coupling locomotive is rejected
	 * by R3RCanCoupleNow with reject=target-not-wait for ever -- the depot
	 * "couple again after a decouple" then never happens. Mirrors the same fix
	 * at the identity relocation in TryTrainCouple(). */
	InvalidateVehicleTickCaches();
	InvalidateWindowClassesData(WindowClass::TrainList, 0);

	/* R3R (KI-62): the decoupled part keeps its segment identity. A segment is a
	 * persistent object -- what the depot "upgrade to segment" command and the
	 * couple path create, and what the depot list shows as "第 k/N 段" -- not a
	 * boundary marker of whichever coupled chain it happens to sit in. Dropping
	 * the marker here turned a freshly decoupled segment into a loose wagon chain:
	 * it showed up as "散链" in the depot list and could no longer be a coupling
	 * destination at all (see R3RIsCoupleTarget). Markers further down the chain
	 * (when several segments were decoupled at once) were already kept; the head's
	 * marker is now kept as well, so every released segment stays a segment.
	 * GetSegmentHeadFromRear() skips a marker sitting on the chain head, so a
	 * stand-alone segment is never mistaken for one of its own coupled-on
	 * segments. */
	if (R3RIsCarOnlyFormation(u)) u->SetSegmentFront();

	/* R3R (KI-205): the split is committed, so neither half will ever drive the
	 * route the merged chain had booked before it arrived here -- part of that
	 * route is still on the map as the same "ghost reservation" the couple path
	 * leaves behind: the released part parks, the locomotive leaves by another
	 * route, and nothing ever walks over those bits again. Both halves have just
	 * been re-identified, so at this instant every plain-rail bit still answering
	 * to them is a leftover. Drop it before the waiting part books its own
	 * (protective) reservation below; the locomotive books its route on the next
	 * pathfind. */
	R3RReleaseChainReservations(v, "decouple");
	R3RReleaseChainReservations(u, "decouple");

	/* R3R: the waiting consist must hold its path reservation (occupy the track)
	 * so the signalling system treats it as an obstacle: other trains must not
	 * run through it, and the coupling locomotive can pathfind to its reserved
	 * track. Decoupling split the chain — the consist's own reservation is gone.
	 * Clear any stale reservation first so the re-reservation is accurate. */
	u->ClearReservationUnderConsist();
	u->ReserveTrackUnderConsist();

	/* R3R: after the split both newly-independent parts must refresh their
	 * images once, otherwise they keep sprites/caches that still reflect the
	 * combined train (e.g. the freed head/tail connection point and consist
	 * look). MarkDirty() repaints the whole chain of each part — mirrors the
	 * couple success path and the depot drag-split code. */
	v->MarkDirty();
	u->MarkDirty();

	/* R3R (KI-150/153): the split changed which vehicles belong to the depot and
	 * handed the released part a fresh front identity/number. If this ran inside
	 * a depot, regenerate the depot window(s) at once so the new part shows up
	 * and any stale row (absorbed head with number 0, outdated sprite) is gone. */
	R3RInvalidateDepotWindowsForChain(v);
	R3RInvalidateDepotWindowsForChain(u);

	/* R3R (KI-182): neither part of a split is still on its way to couple onto
	 * the other, so the couple target lock -- if either side somehow still held
	 * one -- is dropped here. A part which does keep a GOTO_COUPLE order simply
	 * re-locks on the next tick (TrainLocoHandler). The priority stamp is reset
	 * with it: a split ends the couple run which was in progress. */
	R3RUnpairCoupleTargets(v);
	R3RUnpairCoupleTargets(u);
	_r3r_couple_enter_tick.erase(v->index.base());
	_r3r_couple_enter_tick.erase(u->index.base());

	return u;
}

/**
 * R3R (pxp-decouple): check that a consist fits into the station platform it
 * is standing on. Used by the couple pathfinder (CYapfDestinationTrainRailT)
 * to reject waiting consists that overhang the platform.
 * @param v Any vehicle of the consist.
 * @return \c true if the consist fits the platform (or is not on a station).
 */
bool TrainFitStation(const Train *v)
{
	TileIndex tile = v->tile;
	if (!IsRailStationTile(tile)) return true;

	const Station *st = Station::Get(GetStationIndex(tile));
	if (st == nullptr) return true;

	/* Count consecutive platform tiles of this station along the platform axis. */
	Axis axis = GetRailStationAxis(tile);
	uint platform_tiles = 0;
	TileIndex tmp = tile;
	while (IsRailStationTile(tmp) && GetStationIndex(tmp) == st->index) {
		platform_tiles++;
		tmp = (axis == Axis::X) ? TileAddXY(tmp, -1, 0) : TileAddXY(tmp, 0, -1);
	}
	tmp = (axis == Axis::X) ? TileAddXY(tile, 1, 0) : TileAddXY(tile, 0, 1);
	while (IsRailStationTile(tmp) && GetStationIndex(tmp) == st->index) {
		platform_tiles++;
		tmp = (axis == Axis::X) ? TileAddXY(tmp, 1, 0) : TileAddXY(tmp, 0, 1);
	}

	return v->gcache.cached_total_length <= platform_tiles * TILE_SIZE;
}

static bool CheckReverseTrain(const Train *consist);

/* R3R (debug): log whether every neighbouring pair of the merged chain is
 * physically adjacent (chain order == physical order). A "fold" shows up as a
 * pair whose centre distance is far above the nominal coupling distance. This
 * identifies the folding pair for the fold-geom crash. */
static int R3RCheckChainFold(Train *head, const char *tag)
{
	/* R3R perf probe (KI-14): folded once per fold check, i.e. up to 3x per
	 * couple attempt, i.e. once per tick while the fold-fix retry loop never
	 * converges (KI-06). The scan itself is cheap; the per-call
	 * fopen/fprintf/fclose of the whole chain is gone (edge-triggered below). */
	R3RPerfCounters &r3rp = R3RP();
	if (R3RPerfOn()) r3rp.fold_check++;
	R3RPerfTimer r3r_fold_timer(&r3rp.fold_check_ns);

	int worst_gap = 0;
	int nom_wa = 0;
	int nom_wb = 0;
	int nom_sel = 0;
	const Train *wa = nullptr;
	const Train *wb = nullptr;
	for (const Train *a = head; a != nullptr; a = a->Next()) {
		const Train *b = a->Next();
		if (b == nullptr) break;
		/* R3R (KI-214 probe fix, 2026-09-27): the authoritative nominal centre
		 * distance is Train::CalcNextVehicleOffset(), not (la + lb) / 2. For odd
		 * vehicle lengths the engine rounds the *next* vehicle up and the current
		 * one down (train.h), so the naive average is short by exactly 1px on the
		 * pairs that R3R's flip/merge leaves mis-sorted -- which is why this probe
		 * kept reporting worst_gap=0 while the player saw a 1px overlap. */
		const int la = a->gcache.cached_veh_length;
		const int lb = b->gcache.cached_veh_length;
		/* The walk uses Next(); nom0/nom1 are the two rounding hypotheses
		 * (0 = a leads the movement, 1 = b leads). */
		const int nom0 = la / 2 + (lb + 1) / 2;
		const int nom1 = lb / 2 + (la + 1) / 2;
		/* The vehicle leading the movement owns the "next" offset: a normally,
		 * b when the pair is driving backwards. */
		const Train *const self = a->IsDrivingBackwards() ? b : a;
		const int nom = self->CalcNextVehicleOffset();
		const int dx = std::abs(a->x_pos - b->x_pos);
		const int dy = std::abs(a->y_pos - b->y_pos);
		const int gap = std::abs(std::max(dx, dy) - nom);
		if (gap > worst_gap) {
			worst_gap = gap;
			wa = a;
			wb = b;
			nom_wa = nom0;
			nom_wb = nom1;
			nom_sel = nom;
		}
	}
	/* KI-14 (1): edge-triggered. The fold-fix retry loop (KI-06) calls this up
	 * to three times per tick and rebuilds the merged chain for every attempt,
	 * so key on the current head and the tag and only re-log when the outcome
	 * (worst gap / folding pair) actually changes. */
	const uint64_t fold_key = ((uint64_t)head->index.base() << 32) ^ R3RDbgTagHash(tag);
	if (!R3RDbgEdge(R3REDGE_FOLDCHK, fold_key, ((uint64_t)worst_gap << 1) | (wa != nullptr ? 1 : 0))) return worst_gap;

	R3RDbgWrite("FOLDCHK %s n=%d worst_gap=%d", tag, CountVehiclesInChain(head), worst_gap);
	if (wa != nullptr) {
		R3RDbgWrite(" A idx=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X db=%d B idx=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X exp=%d nom=%d nom0=%d nom1=%d dist=%d",
				(int)wa->index.base(), (int)wa->x_pos, (int)wa->y_pos,
				(int)TileX(wa->tile), (int)TileY(wa->tile), (int)wa->direction, (uint)wa->track,
				wa->IsDrivingBackwards() ? 1 : 0,
				(int)wb->index.base(), (int)wb->x_pos, (int)wb->y_pos,
				(int)TileX(wb->tile), (int)TileY(wb->tile), (int)wb->direction, (uint)wb->track,
				(int)(wa->gcache.cached_veh_length + wb->gcache.cached_veh_length) / 2,
				nom_sel, nom_wa, nom_wb,
				std::max(std::abs(wa->x_pos - wb->x_pos), std::abs(wa->y_pos - wb->y_pos)));
	}
	R3RDbgWrite("\n");
	return worst_gap;
}

/* R3R: detect a "direction fold" — a chain where some vehicle lies on the nose
 * side of its predecessor (a nose-to-nose / U-shaped splice). The FOLDCHK
 * centre-distance check alone cannot catch that shape: the two vehicles may
 * still be only a few px apart, and yet the merged chain is geometrically
 * discontinuous (the consist points the other way) and crashes TrainController
 * as soon as the train moves. In a healthy chain every vehicle sits on the
 * tail side of its predecessor, so the dot product of the successor offset
 * with the predecessor's facing must be negative. */
static bool R3RCheckChainFoldedDirection(const Train *head, const char *tag)
{
	static const int dir_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }; // DIR_N, DIR_NE, DIR_E, DIR_SE, DIR_S, DIR_SW, DIR_W, DIR_NW
	static const int dir_dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	const Train *wa = nullptr;
	const Train *wb = nullptr;
	int worst_dot = 0;
	for (const Train *a = head; a != nullptr; a = a->Next()) {
		const Train *b = a->Next();
		if (b == nullptr) break;
		/* R3R (P4, KI-06 fix 2026-09-15): skip articulated parent/child pairs.
		 * A part is hard-wired to its parent -- it renders at a fixed offset
		 * (4 px in the test4.sav 现场) and always carries the parent's
		 * direction -- so the pair reports a small POSITIVE dot although it is
		 * not a splice point at all. A logical flip (R3RFlipChainBySegments)
		 * reverses every vehicle's direction while the positions stay put, so
		 * the sign of EVERY pair's dot flips with it: a healthy -4 px pair
		 * turns into +4 and is then (wrongly) read as a fold. Measured on
		 * test4.sav: the repaired COUPLE-FLIP-V and COUPLE-FLIP-BOTH runs
		 * reported worst dot=4 on `A idx=0 (632,514) dir=7 <-> B idx=1
		 * (632,510) dir=7` -- the locomotive's own part pair -- and NO other
		 * positive pair anywhere on the chain. That one 4 px false positive
		 * was therefore the only thing keeping the KI-06 couple retry loop
		 * alive. Splice points stay checked: across a segment boundary b is
		 * the NEXT group's parent (a group head, not an artic group member),
		 * so the pair is still evaluated as before -- e.g. the真折叠 reports
		 * `COUPLE idx2 <-> idx3 dot=27` and `COUPLE-FLIP-U idx2 <-> idx6
		 * dot=17` both remain vetoes. */
		if (b->IsArticGroupMember()) continue;
		/* R3R (KI-201 附记 4, 2026-09-25 现场实测): 紧贴残差对豁免方向判据。
		 *
		 * 与 SpliceFolded(KI-06, 6714-6728) 的容忍度**完全同一把尺子**(8px):
		 * 拼接对 |dist-exp|<=8 是"chain still closing up"的过渡态 —— ArrangeTrains
		 * 只重链序、不重排位置,两列车停在哪就保留多少缝(5200-5207)。这种残差
		 * 对上的像素偏移含 tile 内吸附/取整,而 direction 是 8 向的**斜向值**
		 * (NE/SE/SW/NW),两者的主轴符号可以不一致,于是同一个 8px 缝被 SpliceFolded
		 * 判为"可接受"、却被本函数读成"折叠",候选被双重惩罚。
		 *
		 * 现场(2026-09-25 第 109 轮后复测, build\R3R_debug.log):
		 *   FOLDCHK COUPLE-FLIP-U n=30 worst_gap=8
		 *     A idx=5 x=936 y=372 tile=58,23 dir=3  B idx=6 x=936 y=382 tile=58,23 dir=3 exp=2 dist=10
		 * 5 与 6 同 tile(58,23)、无 x 偏移、纯 +y 10px:exp=2 ⇒ 缝=8px ≤ 8,
		 * 拼接对合法;但 a_dir=DIR_SE=(+1,+1) ⇒ dot=0*1+10*1=+10>0 ⇒ 被判折叠,
		 * 把唯一接近成功的候选1(翻 u 整链倒置后新链头 6 已贴到 v 尾 5,distance
		 * 从候选0 的 68px 降到 10px)否掉。真折叠不受影响:同一次现场候选0
		 * dist=68 exp=2(|66|>8,不豁免,dot=69)、候选2 dist=90 exp=2(dot=90)照旧
		 * 判折叠;KI-06 实测的真折叠 dot=17/27 同样伴随大位移。 */
		/* R3R (KI-201 附记 8, 2026-09-25 第 110 轮后复测 veh=33 穿模现场): 豁免必须额外
		 * 要求"两车 direction 已一致"。
		 *
		 * 附记 4 的现场(5↔6 同 dir=3、纯 +y 10px、exp=2 即 8px 缝)是**方向一致**的紧贴
		 * 残差对,豁免该生效;而 veh=33 现场是
		 *   FOLDCHK COUPLE n=21 worst_gap=7
		 *     A idx=35 x=936 y=1022 tile=58,63 dir=7  B idx=6 x=936 y=1013 tile=58,63 dir=3 exp=2 dist=9
		 * 链序 33(y=1015)→34(1019)→35(1022) 一路向南,6 却回到 y=1013,且 dir 相反(7 vs 3):
		 * 这是"鼻对鼻命中后按链尾接链头硬拼"造成的**交错**,不是缝。旧豁免只按 |dist-exp|=7<=8
		 * 放过它 → 候选0(直拼)被接受 → 紧随其后的 SEAM-FLIP(7307 行)再把 6 的方向掰成 7,
		 * 方向判据也一并通过,于是留下"方向一致但位置倒退"的病链;列车启动后 idx=6..15 十来节
		 * 车被压在同一格(58,61 y=976),逐个 TTB-PROBE fold-geom(chosen_track == TRACK_BIT_NONE)
		 * 原地卡死 —— 玩家看到的穿模。收窄后该对不再豁免 ⇒ dot=+9 判折叠 ⇒ 候选0 被否,
		 * 改走翻 u(整链倒置)候选,由候选自身把车底链序摆正,而不是靠改方向掩盖几何。
		 * 健康链不受影响:同列相邻对 direction 恒相同(artic 成员在上方已跳过、IsDrivingBackwards
		 * 只翻移动方向不翻 direction),故正常对仍享豁免,附记 4 的方向一致场景也不会被误否。 */
		const int pair_expected = (a->gcache.cached_veh_length + b->gcache.cached_veh_length) / 2;
		const int pair_dist = std::max(std::abs(a->x_pos - b->x_pos), std::abs(a->y_pos - b->y_pos));
		if (std::abs(pair_dist - pair_expected) <= 8 && a->direction == b->direction) continue;
		/* R3R (KI-173,第 98 轮修正):判据的"前方"必须用**移动方向**——既不是裸
		 * direction,也不是 Flipped 补偿后的渲染朝向。
		 *  - Flipped 口径是错的(第 98 轮首版即此错,已回退):逻辑翻 = direction 反
		 *    + Flip(Flipped),若按 Flipped 反推"净朝向",翻过的链会整链反号。现场
		 *    实据 FOLDCHK-DIR COUPLE-FLIP-V dot=5 A idx=27 dir=1 B idx=6 dir=5:
		 *    同一对翻前是健康的 -5,翻后被本口径读成折叠的 +5,把本该通过的候选
		 *    (只翻 v) 否掉。
		 *  - 裸 direction 也不对:机车倒车接近车底时 db=1(现场 27/28/29 全 db=1),
		 *    链序相对 direction 是反的,相对实际移动方向 ReverseDir(direction) 才是
		 *    正常的;裸 direction 会把这条健康链逐对读成 +4(gap 检查看不出)。 */
		const Direction a_dir = a->IsDrivingBackwards() ? ReverseDir(a->direction) : a->direction;
		int dot = (b->x_pos - a->x_pos) * dir_dx[(int)a_dir & 7] + (b->y_pos - a->y_pos) * dir_dy[(int)a_dir & 7];
		if (dot > worst_dot) {
			worst_dot = dot;
			wa = a;
			wb = b;
		}
	}
	if (worst_dot > 0) {
		/* KI-14 (1): edge-triggered -- same per-tick retry loop as FOLDCHK. */
		const uint64_t dir_key = ((uint64_t)head->index.base() << 32) ^ R3RDbgTagHash(tag);
		if (R3RDbgEdge(R3REDGE_FOLDCHK_DIR, dir_key, ((uint64_t)worst_dot << 1) | (wa != nullptr ? 1 : 0))) {
			R3RDbgWrite("FOLDCHK-DIR %s dot=%d", tag, worst_dot);
			if (wa != nullptr) {
				R3RDbgWrite(" A idx=%d x=%d y=%d dir=%d trk=0x%X B idx=%d x=%d y=%d dir=%d trk=0x%X",
						(int)wa->index.base(), (int)wa->x_pos, (int)wa->y_pos, (int)wa->direction, (uint)wa->track,
						(int)wb->index.base(), (int)wb->x_pos, (int)wb->y_pos, (int)wb->direction, (uint)wb->track);
			}
			R3RDbgWrite("\n");
		}
		return true;
	}
	return false;
}

/**
 * R3R: 逻辑翻转的"方向改写"钩子 —— 就地反转链上每节车的 direction,并同步 Flip
 * VehicleRailFlag::Flipped,用后者抵偿前者来保住渲染(净图像方向 =
 * R(direction) × Flipped 不变,见 Train::GetImage())。
 * Flipped 的语义("this vehicle is drawn reversed relative to the chain")见
 * R3RNormaliseDepotMergeDirection() 处的注释 —— 它在此处必须保留,理由见下方
 * 第 138 轮记录。链序交给 RestoreTrainBackup 恢复(它只重建 SetNext,不碰
 * direction/flags),★/组角色由 R3RUndoLogicalFlip 单独还原。
 *
 * 【2026-09-27 第 138 轮:这次 Flip 曾被删掉又回退 —— 勿再删】KI-214 的"边界框
 * 偏移"一度被归因到这一句,理由是"同 dir/同 len 的两节因 flip 不同,在
 * Train::UpdateDeltaXY 里走了不同斜向分支,锚点差 6/3/1/0 个单位(len=2/5/7/8)"。
 * 该归因**已实测证伪**:bF(= flip_offs)本就是"让精灵与翻转后的图像对齐"而刻意
 * 留下的偏移量,差 6 是设计而非 bug。删掉这次 Flip 后,新日志(build\R3R_debug.log
 * 2156-2176,60,90 现场)整链变成 flip 全 0 / dir 全 7 / bF 全 0,1 —— 盒子确实
 * 自洽了,代价是玩家立刻报告的"图像会方向翻转":逻辑翻是**位置不动**的就地翻转,
 * 车在轨道上的实际朝向根本没变,取消抵偿等于把外观真的镜像过去。
 * ⇒ 外观必须保持不变;KI-214 的偏移另有原因,须按证据另找。
 *
 * 2026-09-18 曾试装 (a′)(本函数改空操作、连 direction 都不反)并已回退,原因见
 * 下方函数头注释与 R3R_KNOWN_ISSUES.md 的 KI-105 —— 那次证伪的是"什么都不改"。
 * 加上第 138 轮(删掉本 Flip)的失败,两次实验都已回退。
 */
/* R3R 2026-09-18 回退记录:(a′) 试验(把本函数改成空操作、不再碰 direction/Flipped)
 * 已实测证伪 —— 详见 R3R_KNOWN_ISSUES.md 的 KI-105。direction 的反转不是"纯账目":
 * 折叠判定 R3RCheckChainFoldedDirection(4975)按 a->direction 算点积、依赖"逻辑翻转
 * 把相邻对点积符号整体翻过来";TrainController 的 moving-front 也读它。取消反转后
 * 健康候选被判成折叠 ⇒ 逐候选回滚、机车一路穿模压进车底,直到缝上 dy 恰好=0 才勉强
 * COUPLE-OK(其 worst_gap=2、seam dist=0 ⇒ 一片重叠),随后 CRT-FOLD 立即反对、
 * 整列以尾端为"前"跑。故恢复旧实现。 */
static void R3RReverseChainDirections(Train *chain)
{
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		w->direction = ReverseDir(w->direction);
		/* R3R (KI-214 第 138 轮): 这次 Flip 曾被删除以"修"边界框偏移,已实测证伪并回退
		 * —— 删掉它会让整段外观真的翻转(位置不动 + direction 被反),理由见函数头。 */
		w->flags.Flip(VehicleRailFlag::Flipped);
	}
	/* R3R (KI-126): direction 与 Flipped 都会被 NewGRF 取数与图像缓存读走 ——
	 * 前者是 GRF 变量 0x48("reversed")、以及一切按 direction 取图的回调的输入；
	 * 后者决定 Train::GetImage() 里那次反向补偿。而任何「沿链数位置」的变量
	 * （0x40/0x41 position-in-consist、0x4D position-in-articulated-vehicle、
	 * 记忆 96042581 的 position-in-segment）都依赖刚被改写的链序与组角色位。
	 * 只改 direction/Flipped 而不失效缓存，GRF 侧与图像侧都会继续沿用翻转前的
	 * 取数结果 ⇒ 铰接组各节按旧位置选图，端头/中间节图像错乱。
	 * 注意 Train::ConsistChanged() 只做 InvalidateNewGRFCache()，**不清图像缓存**
	 * （cur_image_valid_dir / VCF_IMAGE_CURVATURE / VCF_REDRAW_ON_*），所以 R3R 的
	 * 逻辑翻必须自己补这两次失效。放在本函数（所有 R3R 逻辑方向改写的唯一出口）
	 * 里，顺带覆盖三条路径：R3RFlipChainBySegments 第 1 步、R3RUndoLogicalFlip 的
	 * 回滚、以及 Couple 里挂车拼缝反向那处直接调用。清缓存无副作用、可重复调用；
	 * 此处链序尚未重排（第 3 步才重链），但从本链头沿 Next() 仍能走到全部车辆，
	 * 故失效范围完整。 */
	chain->InvalidateNewGRFCacheOfChain();
	chain->InvalidateImageCacheOfChain();
	/* R3R (KI-214)：包围盒(bounds/coord)也必须跟着重算。Train::UpdateDeltaXY()
	 * 直接吃 direction 与 Flipped 两个量 —— 斜向分支的 flip_offs、直线分支的
	 * half_shorten = (VEHICLE_LENGTH - len + flipped) / 2 —— 而引擎只在【列车开动】
	 * 的那条路径里调它(见 TrainController 一带 12634 行的 v->UpdateDeltaXY())；
	 * 耦合/解挂现场两列车都是停着的，逻辑翻之后没有任何人重算，于是 bounds 留着
	 * 翻转前的补偿量：引擎据此算出来的重绘区与点击框(coord)与车体不一致，玩家
	 * 看到的就是"耦合后边界框错位"，短节(假铰接的 len=2 节)上尤其明显 —— 一个
	 * 单位(≈1px)的偏差在 2 单位长的车节上就是半个车身。位置本身没动，故不必再调
	 * UpdatePosition()(那是移动路径用来刷 tile 哈希的)；按引擎同款方式逐车重算并
	 * 标脏即可，headless 下 UpdateViewport 自行 early-return，不影响判据。 */
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		w->UpdateDeltaXY();
		if (w->IsDrawn()) w->Vehicle::UpdateViewport(true);
	}
}

/** R3R: 一条链上的段边界快照:★ 段首车与段右边界标记车(段尾)。 */
struct R3RSegBoundaries {
	std::vector<Train *> fronts; ///< 段首(★)车辆,按链序。
	std::vector<Train *> backs;  ///< 段右边界(SegmentBack)车辆,按链序。
	/**
	 * R3R (KI-187): 段头身份迁移记录(旧段头 → 新段头),按迁移发生顺序。
	 * 逻辑反转把 ★ 从段首搬到段内另一端时,段的"身份/责任数据"(排程、订单位置、
	 * 借用标志、段优先级)必须一起搬(见 R3RMoveSegmentOwner),否则持有排程的车会
	 * 掉出"段头集合",命令主人判定(R3RGetSegmentHeads → R3RGetLowestPriority)再也
	 * 找不到它。回滚(R3RUndoLogicalFlip)必须按本记录反向搬回 —— 一次被放弃的翻转
	 * 候选不能把排程留在"已不再是段头"的车厢上。
	 */
	std::vector<std::pair<Train *, Train *>> owner_moves;
};

/**
 * R3R: 快照链上所有段边界标记。逻辑反转(R3RFlipChainBySegments)会把 ★ 与段
 * 右边界标记一起迁移,回滚(R3RUndoLogicalFlip)时按本快照逐车原样还原。
 * @param chain 链头。
 * @return 段边界快照(fronts = ★ 段首,backs = 段右边界标记)。
 */
static R3RSegBoundaries R3RCaptureSegBoundaries(Train *chain)
{
	R3RSegBoundaries bounds;
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		if (w->IsSegmentFront()) bounds.fronts.push_back(w);
		if (w->IsSegmentBack()) bounds.backs.push_back(w);
	}
	return bounds;
}

/** R3R: 一辆车上的 de-articulated 组角色位快照条目(head/member 二选一或全无)。 */
struct R3RArticRoleState {
	Train *veh = nullptr;
	bool head = false;   // ArticGroupHead
	bool member = false; // ArticGroupMember
};

/**
 * R3R: 快照一条链上所有 de-articulated 组的角色位分布(仅录带角色位的车)。
 * 真 artic 组不带角色位,快照为空;空快照在撤销时等价于"全链清除角色位"。
 * KI-140 一度让逻辑反转不再改动角色位,但第 77 轮又把"段内彻底倒序 + 角色位迁移"
 * 恢复了(见 R3RFlipChainBySegments 第 4b 步),所以本快照重新变成回滚路径的
 * **必需**输入:必须按它把角色位精确还原成翻转前的分布。
 * @param chain 链头。
 * @return 按链序的角色位记录。
 */
static std::vector<R3RArticRoleState> R3RCaptureArticRoles(Train *chain)
{
	std::vector<R3RArticRoleState> roles;
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		bool head = w->flags.Test(VehicleRailFlag::ArticGroupHead);
		bool member = w->flags.Test(VehicleRailFlag::ArticGroupMember);
		if (head || member) roles.push_back({w, head, member});
	}
	return roles;
}

/**
 * R3R (KI-187): 把"段的身份/责任数据"从旧段头搬到新段头。
 *
 * 段的身份载体是段头(★ 车;链头段恒为链头):命令主人与段优先级都定义在
 * "段头集合"上(R3RGetSegmentHeads → R3RGetLowestPriority/R3RGetPriorityHead),
 * 而 route A 的排程借用制又把"谁是这份排程的主人"直接编码成"哪辆车持有
 * orders 指针 + r3r_priority 最小"。折叠修正(R3RFlipChainBySegments)只迁移 ★
 * 而不迁移这些数据,就会让持有排程的车掉出段头集合:
 *  - 挂车时按段头挑命令主人再也看不到真正持表的那辆车,排程交接被跳过;
 *  - 解挂时"解出方链头有没有表"(u->orders == nullptr ⇒ 当作无表方,新建一条
 *    只含 WAIT_COUPLE 的空表)随之判错。
 * 现场(KI-187):车底链头 6 持 25 条路线表,折叠修正把 ★ 从 6 迁到 23,排程留在 6
 * 身上 → 解挂出的 {23…6} 被判无表,合并列车此后只跑一条 WAIT_COUPLE 而死等。
 *
 * 迁移的是"责任",不是车的物理属性:可靠性/故障率/保养/引擎功率属于车辆与引擎
 * 本身,段内头尾互换不改变它们跟随哪辆车;车号、窗口、分组与前端列
 * (primary/FrontEngine 位)身份仍锚在原 primary 车上(假引擎 subtype 位不随 ★ 迁移,
 * 合并结束时由 Couple 销毁被并入车组的假引擎身份)。
 *
 * 全部字段用 swap:正常情况下新段头是空手接手(等价于搬运),异常情况下(段内两车
 * 都持表)也不会丢掉任何一张 OrderList;而且 swap 自反,回滚时对同一对车再调用
 * 一次即精确还原,不需要额外的值快照。
 *
 * @param from 旧段头(翻转前的段首车)。
 * @param to   新段头(翻转后的段首车)。
 */
static void R3RMoveSegmentOwner(Train *from, Train *to)
{
	if (from == nullptr || to == nullptr || from == to) return;

	/* 只有"旧段头确实带着责任"时才值得落一行,并且走边沿闸门(KI-14 口径):
	 * 挂车死循环下每 tick 都会翻转+回滚,直写会刷屏。 */
	const bool from_carries = from->orders != nullptr || from->orders_backup != nullptr ||
			from->r3r_orders_borrowed || from->r3r_priority != to->r3r_priority;
	const uint64_t owner_payload = ((uint64_t)from->r3r_priority << 40) ^
			((uint64_t)(from->orders != nullptr ? from->orders->GetNumOrders() : 0) << 8) ^
			(uint64_t)(from->orders_backup != nullptr ? 1 : 0) ^
			(uint64_t)(from->r3r_orders_borrowed ? 2 : 0);

	/* 排程所有权(route A 借用制:主人保留自己的指针,链头只借用)。 */
	std::swap(to->orders, from->orders);
	std::swap(to->orders_backup, from->orders_backup);
	std::swap(to->orders_backup_real_index, from->orders_backup_real_index);
	std::swap(to->orders_backup_implicit_index, from->orders_backup_implicit_index);

	/* 在这份排程里的位置(当前/隐式/时刻表)。 */
	std::swap(to->cur_real_order_index, from->cur_real_order_index);
	std::swap(to->cur_implicit_order_index, from->cur_implicit_order_index);
	std::swap(to->cur_timetable_order_index, from->cur_timetable_order_index);

	/* "链头借用中"标志与段优先级都属于段身份,跟着段头走。 */
	std::swap(to->r3r_orders_borrowed, from->r3r_orders_borrowed);
	std::swap(to->r3r_priority, from->r3r_priority);

	if (from_carries && R3RDbgEdge(R3REDGE_OWNERMOVE, from->index.base(), owner_payload)) {
		R3RDbgWrite("OWNER-MOVE from=%d to=%d orders=%d prio=%u borrowed=%d\n",
				(int)from->index.base(), (int)to->index.base(),
				(int)(to->orders != nullptr ? to->orders->GetNumOrders() : 0),
				(unsigned)to->r3r_priority, (int)to->r3r_orders_borrowed);
	}
}

/**
 * R3R: 撤销一次 R3RFlipChainBySegments。调用方必须先 RestoreTrainBackup
 * 恢复链序(本函数假定 chain 已是原链头),这里把方向翻回、段边界标记(★ 段首
 * 与 SegmentBack 段右边界)还原为翻转前的分布,并把 de-articulated 组的角色位
 * 也按快照还原。
 * 角色位还原是**必须的**:逻辑反转已重新引入角色位改写(第 4b 步把每个打散组
 * 的组头角色位迁到反转后的新组首),回滚若不还原,角色位就会留在翻转后的分布上。
 * 真 artic 组不带角色位,快照为空,还原等价于"全链清除角色位",对它无操作;
 * 打散组则被精确还原成翻转前的分布。
 * 这条还原路径绝不能省 —— 角色位一旦与
 * 链序错配(例如链头残留 ArticGroupMember 且 Previous() 为空),NewGRF 变量
 * 0x4D("articulated 组内位置")等沿 Previous() 回走的代码会对空指针调用虚函数,
 * 机车刚碰上车厢即卡死崩溃。
 * @param chain      已恢复链序的原链头。
 * @param old_bounds 翻转前收集的段边界快照(R3RCaptureSegBoundaries 的结果)。
 * @param old_roles  翻转前收集的组角色位(R3RCaptureArticRoles 的结果)。
 */
static void R3RUndoLogicalFlip(Train *chain, const R3RSegBoundaries &old_bounds,
                               const std::vector<R3RArticRoleState> &old_roles)
{
	assert(chain != nullptr && chain->First() == chain);

	R3RReverseChainDirections(chain);

	/* 第 1b 步在每节车上翻转过的 SegmentFlipped 一并翻回(自反标记,翻两次即还原)。 */
	for (Train *w = chain; w != nullptr; w = w->Next()) w->FlipSegmentFlipped();

	/* 先全清反转期间新标/迁移的段边界标记,再按快照把 ★ 与段右边界逐车还原。 */
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		w->ClearSegmentFront();
		w->ClearSegmentBack();
	}
	for (Train *w : old_bounds.fronts) w->SetSegmentFront();
	for (Train *w : old_bounds.backs) w->SetSegmentBack();

	/* 角色位先全清(去掉翻转期间迁移/新设的位),再按快照逐车原样还原。 */
	for (Train *w = chain; w != nullptr; w = w->Next()) {
		w->ClearArticGroupHead();
		w->ClearArticGroupMember();
	}
	for (const R3RArticRoleState &r : old_roles) {
		if (r.head) r.veh->SetArticGroupHead();
		if (r.member) r.veh->SetArticGroupMember();
	}

	/* 段头身份(排程/订单位置/借用标志/段优先级)按记录反向搬回:R3RMoveSegmentOwner
	 * 内部全是 swap(自反),对同一对车再搬一次即精确还原。取逆序处理,保证同一辆车
	 * 在同一次翻转里被连续搬迁过两次时也能逐对回退。 */
	for (auto it = old_bounds.owner_moves.rbegin(); it != old_bounds.owner_moves.rend(); ++it) {
		R3RMoveSegmentOwner(it->second, it->first);
	}
}

/**
 * R3R (TEMPORARY DIAGNOSTIC): chain census + stranded-vehicle watchdog.
 *
 * Called once every 256 state ticks from StateGameLoop(). It prints one line per
 * train chain (head identity, length, role flags and the per-vehicle tile /
 * direction / flags) plus an explicit anomaly line whenever a chain head is
 * neither a single front engine nor a set of free wagons -- which is what a
 * wagon left behind by a chain edit looks like. Read-only, changes nothing.
 */
void R3RStrandCensus()
{
	static uint32_t r3r_census_seq = 0;
	r3r_census_seq++;

	FILE *dbg = R3RFopenDbg("a");
	if (dbg == nullptr) return;

	fprintf(dbg, "=== CENSUS seq=%u ===\n", (unsigned)r3r_census_seq);
	for (const Vehicle *v : Vehicle::Iterate()) {
		if (v->type != VehicleType::Train) continue;
		if (v->Previous() != nullptr) continue;

		uint n = 0, nfe = 0, nfw = 0, nseg = 0;
		const Owner head_owner = v->owner;
		bool multi_owner = false;
		for (const Vehicle *w = v; w != nullptr; w = w->Next()) {
			const Train *wt = Train::From(w);
			n++;
			if (wt->IsFrontEngine()) nfe++;
			if (wt->IsFreeWagon()) nfw++;
			if (wt->IsSegmentFront()) nseg++;
			if (w->owner != head_owner) multi_owner = true;
		}

		fprintf(dbg, "CENSUS-CHAIN head=%d own=%d n=%u FE=%u FW=%u SEGF=%u multiown=%d ord=%d tile=%d,%d dir=%d xy=%d,%d:",
				(int)v->index.base(), (int)head_owner.base(), n, nfe, nfw, nseg,
				(int)multi_owner, (int)(v->orders != nullptr), TileX(v->tile), TileY(v->tile),
				(int)v->direction, v->x_pos, v->y_pos);
		for (const Vehicle *w = v; w != nullptr; w = w->Next()) {
			const Train *wt = Train::From(w);
			fprintf(dbg, " %d[ow%d t%d,%d d%d m%d %s %s %s %s]",
					(int)w->index.base(), (int)w->owner.base(), TileX(w->tile), TileY(w->tile),
					(int)w->direction, (int)wt->GetMovingDirection(),
					wt->IsFrontEngine() ? "FE" : "--",
					wt->IsFreeWagon() ? "FW" : "--",
					wt->IsSegmentFront() ? "SF" : "--",
					(w == v && wt->IsStoppedInDepot()) ? "DP" : "--");
		}
		fprintf(dbg, "\n");

		if (nfe != 1) {
			fprintf(dbg, "CENSUS-ANOMALY head=%d n=%u FE=%u FW=%u SEGF=%u (chain head is not exactly one front engine)\n",
					(int)v->index.base(), n, nfe, nfw, nseg);
		}
	}
	/* R3R (TEMPORARY DIAGNOSTIC): couple-gate probe. Prints the couple group
	 * table plus, for every ordered pair of chain heads, the verdict of the
	 * couple gate and the depot GOTO_COUPLE preconditions, so a coupling which
	 * never happens can be attributed to a specific condition. Read-only. */
	if (r3r_census_seq <= 8) {
		for (const CoupleGroup *cg : CoupleGroup::Iterate()) {
			fprintf(dbg, "CGPROBE-GROUP id=%d own=%d flags=%02X open=%d name=%s\n",
					(int)cg->index.base(), (int)cg->owner.base(), (unsigned)cg->flags,
					(int)cg->AllowsOthers(), cg->name.c_str());
		}
		std::vector<const Train *> heads;
		for (const Vehicle *v : Vehicle::Iterate()) {
			if (v->type != VehicleType::Train || v->Previous() != nullptr) continue;
			heads.push_back(Train::From(v));
		}
		for (const Train *a : heads) {
			for (const Train *b : heads) {
				if (a == b) continue;
				const bool a_target_depot = a->current_order.IsType(OT_GOTO_COUPLE) &&
						a->current_order.GetCoupleIsDepot() && IsRailDepotTile(a->tile) &&
						a->current_order.GetDestination().ToDepotID() == GetDepotIndex(a->tile);
				const Order *ra = (a->orders != nullptr) ? a->GetOrder(a->cur_real_order_index) : nullptr;
				const Order *rb = (b->orders != nullptr) ? b->GetOrder(b->cur_real_order_index) : nullptr;
				fprintf(dbg, "CGPROBE-PAIR a=%d b=%d ownA=%d ownB=%d gA=%llX gB=%llX allowed=%d"
						" aStopped=%d aSpd=%u aCo=%d aReal=%d aTargetDepot=%d"
						" bCo=%d bReal=%d bSegF=%d bDepot=%d\n",
						(int)a->index.base(), (int)b->index.base(), (int)a->owner.base(), (int)b->owner.base(),
						(unsigned long long)R3RGetCoupleGroupsOfSegment(a),
						(unsigned long long)R3RGetCoupleGroupsOfSegment(b),
						(int)R3RCoupleAllowed(a, b),
						(int)a->vehstatus.Test(VehState::Stopped), (unsigned)a->cur_speed,
						(int)a->current_order.GetType(), (int)(ra != nullptr ? ra->GetType() : -1),
						(int)a_target_depot,
						(int)b->current_order.GetType(), (int)(rb != nullptr ? rb->GetType() : -1),
						(int)b->IsSegmentFront(), (int)b->IsStoppedInDepot());
			}
		}
	}

	fprintf(dbg, "=== CENSUS-END ===\n");
	fclose(dbg);
}

/** Debug dump of a train chain structure for hang diagnosis. */
static void R3RDumpChainDbg(const Train *head, const char *tag)
{
	/* R3R perf probe (KI-14). */
	R3RPerfCounters &r3rp = R3RP();
	if (R3RPerfOn()) r3rp.dump_chain++;
	R3RPerfTimer r3r_dump_timer(&r3rp.dump_chain_ns);

	FILE *dbg = R3RFopenDbg("a");
	if (dbg == nullptr) return;
	if (head == nullptr) {
		fprintf(dbg, "DUMP %s head=NULL\n", tag);
		fclose(dbg);
		return;
	}
	fprintf(dbg, "DUMP %s head=%d", tag, (int)head->index.base());
	int i = 0;
	for (const Train *w = head; w != nullptr; w = w->Next()) {
		fprintf(dbg, " [%d p=%d n=%d f=%d l=%d]",
			(int)w->index.base(),
			w->Previous() != nullptr ? (int)w->Previous()->index.base() : -1,
			w->Next() != nullptr ? (int)w->Next()->index.base() : -1,
			w->First() != nullptr ? (int)w->First()->index.base() : -1,
			w->Last() != nullptr ? (int)w->Last()->index.base() : -1);
		if (++i > 60) break;
	}
	fprintf(dbg, "%s\n", i > 60 ? " CYCLE-OVERFLOW" : " END");
	fclose(dbg);
}

/** R3R debug: dump per-vehicle identity (subtype bits + group-role flags) of a
 *  fold-fix participant chain. Distinguishes real articulated parts (subtype
 *  artic bit) from de-articulated groups (ArticGroupHead/Member railflags,
 *  artic bit cleared by the segment upgrade). */
static void R3RDumpCoupleIdentity(const Train *head, const char *tag)
{
	/* R3R perf probe (KI-14). */
	R3RPerfCounters &r3rp = R3RP();
	if (R3RPerfOn()) r3rp.dump_ident++;
	R3RPerfTimer r3r_ident_timer(&r3rp.dump_ident_ns);

	FILE *dbg = R3RFopenDbg("a");
	if (dbg == nullptr) return;
	fprintf(dbg, "IDENT %s head=%d\n", tag, head != nullptr ? (int)head->index.base() : -1);
	if (head != nullptr) {
		int i = 0;
		for (const Train *w = head; w != nullptr; w = w->Next()) {
			fprintf(dbg, "  %s veh=%d p=%d n=%d subtype=0x%02x(front=%d eng=%d wagon=%d freeW=%d artic=%d) AH=%d AM=%d SF=%d dir=%d flip=%d db=%d x=%d y=%d engType=%d\n",
				tag,
				(int)w->index.base(),
				w->Previous() != nullptr ? (int)w->Previous()->index.base() : -1,
				w->Next() != nullptr ? (int)w->Next()->index.base() : -1,
				w->subtype,
				HasBit(w->subtype, GVSF_FRONT) ? 1 : 0,
				HasBit(w->subtype, GVSF_ENGINE) ? 1 : 0,
				HasBit(w->subtype, GVSF_WAGON) ? 1 : 0,
				HasBit(w->subtype, GVSF_FREE_WAGON) ? 1 : 0,
				HasBit(w->subtype, GVSF_ARTICULATED_PART) ? 1 : 0,
				w->flags.Test(VehicleRailFlag::ArticGroupHead) ? 1 : 0,
				w->flags.Test(VehicleRailFlag::ArticGroupMember) ? 1 : 0,
				w->IsSegmentFront() ? 1 : 0,
				(int)w->direction,
				w->flags.Test(VehicleRailFlag::Flipped) ? 1 : 0,
				w->IsDrivingBackwards() ? 1 : 0,
				(int)w->x_pos, (int)w->y_pos, (int)w->engine_type.base());
			if (++i > 60) break;
		}
	}
	fprintf(dbg, "IDENT-END %s\n", tag);
	fclose(dbg);
}

/** R3R: 以遍历为准重建链上所有车辆的 first/last 缓存。
 *  背景: RestoreTrainBackup 用 SetNext 重链时,若原链被逻辑反转(段序
 *  旋转过),SetNext 的 last 更新沿 this->first 遍历,可能把链尾车辆的
 *  last 缓存写到不相关车辆上;此 helper 不依赖任何缓存,纯遍历恢复。 */
static void R3RRefreshChainCaches(Train *head)
{
	if (head == nullptr) return;
	Train *tail = head;
	for (Train *w = head; w != nullptr; w = w->Next()) tail = w;
	for (Train *w = head; w != nullptr; w = w->Next()) {
		w->SetFirst(head);
		w->SetLast(tail);
	}
}

/** R3R: 纯遍历求出链的真链尾,不信任 first/last 缓存。
 *  回滚路径 RestoreTrainBackup 后 last 缓存可能陈旧(把合并链的尾巴写到
 *  链头上),直接当 dst 传给 InsertInConsist 会因 dst 落在 src 链内而
 *  在 SetNext 的缓存维护遍历里死循环。用遍历永远得到真实链尾。 */
static Train *R3RTrainTail(Train *head)
{
	if (head == nullptr) return nullptr;
	Train *tail = head;
	while (tail->Next() != nullptr) tail = tail->Next();
	return tail;
}

/* R3R (KI-140 第 77 轮修正): 这里原有一个 R3RReassignArticGroupRoles() —— 逻辑反转
 * 后把每个打散组的 ArticGroupHead 角色位归一到"反转后的组首"。KI-140 曾随第 2 步
 * 改走角色层分块判据把它删除;第 77 轮按玩家口径("段内彻底倒序")把分块判据改回
 * subtype 位,于是"组内链序也会翻转"重新成立,角色位迁移必须回来。重新实现不再
 * 用"链上连续带角色位的车"识别组(那会把相邻两个打散组误合并),改为按重链前记录
 * 好的组序列迁移,实现见 R3RFlipChainBySegments 第 2b / 4b 步。 */

/**
 * R3R (裁决修订 2026-09-05 晚,推翻当日 q-0 的"整链倒置"): "段级逻辑反转"
 * 采用用户 97996690/31054337 语义 —— 段序保持、逐段各自反转。等待链
 * (consist 或自由车厢链)就地镜像:位置不动、每节 direction 反、**段与段的
 * 先后次序(运营次序)不颠倒**,只对每段内部做头尾互换;段内倒序是**彻底的**,
 * 只有真 artic 组(父车 + 其后连续真 artic part,由 subtype 位判定)作为原子块
 * 整体参与倒序、块内顺序保持不变;打散组(ArticGroupHead + 其后 ArticGroupMember)
 * 的每一节各成一块,故组内链序也真正翻转,角色位由第 4b 步迁到新组首。
 * 名次镜像:段内倒序会改变每节车在段内/组内的名次,若不管,GRF 变量 0x40/0x41
 * (position / length in consist)与 0x4D(position in articulated vehicle)都会按
 * 新名次取数、端头与中间节的选图会整体错位。故第 1b 步在每节车上翻转
 * SegmentFlipped,由 newgrf_engine.cpp 把这两个变量的前后名次对调回翻转前的值
 * (真 artic 组内部顺序没变,0x4D 按"是否带打散组角色位"区分、不参与镜像)。
 * 段标记(★)迁移:旧链按 ★ 划分为若干连续组(含无 ★ 的链头引擎段/consist
 * 组),每组段内反转后其"新段首"(= 旧组尾所在铰接块之首车)打 ★,旧段首
 * 的 ★ 清除。打 ★ 的条件沿用耦合规则:旧段首本身带 ★,或该组含引擎(真引擎
 * 或 consist 假引擎头)。纯自由车厢组(无引擎、无 ★)不产生新的段标记。
 * 段序(whole_chain_invert,2026-09-25 用户拍板 A): 分两种模式,段内倒序、
 * ★ 迁移、角色位迁移、身份迁移逻辑完全共用,只有"段的遍历方向"不同 ——
 *   - 段序保持(false,默认): 从第一段迭代到最后一段。新总链头 = 第一段(原链头段)
 *     反转后的新段首,链头仍留在原链头段内。
 *   - 整链倒置(true): 从最后一段迭代到第一段。新总链头 = 原链尾段反转后的新段首
 *     = 原链尾所在铰接块之首车,即"整链头尾对调"。
 * 为什么必须区分:**位置不动的就地反转下,"整链头尾对调"与"段序颠倒"是同一件事**,
 * 不能只要前者不要后者。设链 = [S1 S2],物理相邻对是 S1 尾↔S2 头;段序保持得到的
 * 链序是 [rev(S1) rev(S2)],其唯一的新跨段相邻对变成 S1 的**远端**(原段首)接 S2 的
 * **远端**(原段尾),跨距 = 两段长度之和(日志实测 78px 伪链),而整链倒置得到的
 * [rev(S2) rev(S1)] 才是位置单调的合法链序。单段链两者恒等。
 * 注意折叠探测器(距离 + direction 点积)只看物理量,折叠修正的成功姿势以
 * FOLDCHK / FOLDCHK-DIR 实测为准。
 * 回滚需 RestoreTrainBackup 恢复链序后再调 R3RUndoLogicalFlip。
 * R3R (KI-187) 段头身份迁移:第 4 步把 ★ 从旧段首搬到新段首时,旧段首身上的
 * 段级责任数据(排程、订单位置、借用标志、段优先级)由 R3RMoveSegmentOwner
 * 一并搬到新段首;迁移对追加记入 owner_moves_out(若给出),供 R3RUndoLogicalFlip
 * 回滚时反向搬回。车辆物理属性(可靠性/故障率/引擎功率/NewGRF 车辆 ID)属于**物理
 * 车辆**,翻转只是改链序、不换车,故刻意不随 ★ 迁移 —— 这点与本迁移不冲突:
 * "身份继承"继承的是段/链的责任数据,不是把两台机车的物理参数对调。
 * 整链链头被搬走时(head != 原链头),调用方(TryTrainCouple)另调
 * R3RRelocateFrontIdentity 把**前端列车身份**(车号/车名/current_order/利润/
 * 服役间隔/时刻表标志/窗口)搬到新链头,与本函数的段级迁移合成完整的一次身份继承;
 * 段的群组掩码(couple_groups)不需要迁移 —— 它的读取端按整段取并集(Q3),
 * 掩码留在段内任何一车上都能被读到。
 * @param chain           当前链头(必须 chain->First() == chain)。
 * @param owner_moves_out 可选的段头身份迁移记录出参(通常直接传调用方的
 *                        R3RSegBoundaries 快照,回滚时交给 R3RUndoLogicalFlip)。
 * @param whole_chain_invert 段序是否随整链倒置一起颠倒(见上):false = 段序保持,
 *                        true = 整链倒置。
 * @return 反转后的新链头(段序保持: 原链头段的旧段尾所在铰接块之首车;整链倒置:
 *         原链尾段的旧段尾所在铰接块之首车 = 原链尾;单段链两者相同)。
 */
static Train *R3RFlipChainBySegments(Train *chain, R3RSegBoundaries *owner_moves_out = nullptr,
                                     bool whole_chain_invert = false)
{
	assert(chain != nullptr && chain->First() == chain);

	/* 本次翻转的身份迁移记录从干净状态开始:同一份 R3RSegBoundaries 会被
	 * "翻转 → 回滚 → 再翻转"复用(候选 1 折叠失败回滚后落候选 3,两次都用
	 * u_old_bounds),不清空就会把同一对(旧段头,新段头)记录两次,回滚时反向搬两次
	 * ⇒ 排程又被搬到已经还回去的那辆车上。 */
	if (owner_moves_out != nullptr) owner_moves_out->owner_moves.clear();

	/* 1. 每节车(含 artic parts)direction 反,位置不动。 */
	R3RReverseChainDirections(chain);

	/* 1b. 段内倒序会把每节车在段内(以及所在打散组内)的名次前后对调 —— 在每节车上
	 * 翻转 SegmentFlipped,让 NewGRF 变量 0x40/0x41(position / length in consist)
	 * 与 0x4D(position in articulated vehicle)继续按"翻转前的名次"取数,GRF 选图
	 * 不变。本标记自反:回滚路径 R3RUndoLogicalFlip 再翻一次即还原。 */
	for (Train *w = chain; w != nullptr; w = w->Next()) w->FlipSegmentFlipped();

	/* 2. 按铰接块收集旧段:块 = 真 artic 组(父车 + 其后连续真 artic part),
	 *    段边界 = ★ 真车。
	 * 判据必须走 subtype 位的 IsArticulatedPart(),不能走角色层的
	 * IsArticGroupMember():打散组的每一节都挂着 ArticGroupMember 角色位,用角色层
	 * 判据会把整个打散组当成一个原子块 ⇒ 段内倒序时组内链序不翻,而"段内彻底
	 * 倒序"才是用户要的语义(6 节打散组 1..6 → 6..1)。打散组内的逐节倒序由第 4b
	 * 步的角色位迁移负责,GRF 侧看到的名次由 SegmentFlipped 的取数镜像保住。 */
	struct R3RSegGroup {
		std::vector<TrainList> blocks; // 按原链序的铰接块(真 artic 组为原子块,其余单节)
		bool has_engine = false;       // 组内任一车为引擎(真/假,含 consist 头)
	};
	std::vector<R3RSegGroup> segs;
	segs.emplace_back(); // 第一段 = 链头引擎段/consist 组(可能无 ★)
	R3RSegGroup *cur = &segs.back();
	for (Train *w = chain; w != nullptr;) {
		if (w != chain && w->IsSegmentFront()) {
			segs.emplace_back();
			cur = &segs.back();
		}
		/* 收集一个铰接块:真 artic 组(父车 + 其后连续真 artic part)整体作为一个
		 * 原子块,块内顺序在反转前后恒不变;其余车辆 —— 含打散组的每一节 —— 各成
		 * 一块,于是段内倒序时打散组内部的链序也真正翻转。 */
		TrainList block;
		Train *p = w;
		while (p != nullptr && (p == w || p->IsArticulatedPart())) {
			block.push_back(p);
			p = p->Next();
		}
		for (const Train *q : block) {
			if (q->IsEngine()) cur->has_engine = true;
		}
		cur->blocks.push_back(std::move(block));
		w = p;
	}

	/* 2b. 记录打散组的车辆序列(组头 + 其后连续组员位车),供第 4b 步做角色位迁移。
	 * 必须在重链之前、按原链序收集:倒序后组内顺序翻转,再按"链上连续带角色位的
	 * 车"去识别组,会把相邻两个打散组误合并成一个(KI-140 删掉的旧实现即此毛病)。 */
	std::vector<std::vector<Train *>> deartic_groups;
	for (Train *w = chain; w != nullptr;) {
		if (w->flags.Test(VehicleRailFlag::ArticGroupHead)) {
			std::vector<Train *> g;
			g.push_back(w);
			Train *p = w->Next();
			while (p != nullptr && p->flags.Test(VehicleRailFlag::ArticGroupMember)) {
				g.push_back(p);
				p = p->Next();
			}
			deartic_groups.push_back(std::move(g));
			w = p;
		} else {
			w = w->Next();
		}
	}

	/* 3. 段内 artic 块倒序重链;段的遍历方向由 whole_chain_invert 决定:
	 * 段序保持 = 从第一段到最后一段(段间顺序不颠倒);整链倒置 = 从最后一段到
	 * 第一段(段序也随之颠倒 ⇒ 等价于整链逐车倒置,artic 块整块不动)。
	 * SetNext 双向维护 next/previous/first/last,会自动切断重链过程中遇到的
	 * 陈旧链接,无需先手工断链。 */
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "FLIP-STEP2-DONE nseg=%d whole=%d\n", (int)segs.size(), whole_chain_invert ? 1 : 0);
			fclose(dbg);
		}
	}
	Train *prev = nullptr;
	Train *new_head = nullptr;
	for (size_t si = 0; si < segs.size(); si++) {
		R3RSegGroup &s = whole_chain_invert ? segs[segs.size() - 1 - si] : segs[si];
		for (auto it = s.blocks.rbegin(); it != s.blocks.rend(); ++it) { // 段内倒序
			for (Train *q : *it) {
				if (prev != nullptr) {
					prev->SetNext(q);
				} else {
					/* 新链头 = 遍历到的第一段反转后的首车:断开它与原前驱的连接。 */
					new_head = q;
					if (q->Previous() != nullptr) q->Previous()->SetNext(nullptr);
				}
				prev = q;
			}
		}
	}
	/* 防御性清掉可能残留的陈旧 next。 */
	prev->SetNext(nullptr);
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) { fprintf(dbg, "FLIP-STEP3-DONE\n"); fclose(dbg); }
	}

	/* 4. 段标记迁移:段内反转后每组的新段首 = 旧组尾块之首车。 */
	for (R3RSegGroup &s : segs) {
		if (s.blocks.empty()) continue;
		Train *old_front = s.blocks.front().front();
		Train *new_front = s.blocks.back().front();
		bool old_head_is_segment = old_front->IsSegmentFront();
		if (old_head_is_segment) old_front->ClearSegmentFront();
		/* 旧段首是段(带 ★)或该组含引擎(真/假引擎,含 consist 假引擎头)时,
		 * 反转后该组仍是可独立成段的组 → 新段首打 ★。
		 * 整链倒置时链头段的身份不再由"链头"隐含(它已被搬到链尾),所以原链头段
		 * 即使无 ★ 且无引擎也必须补 ★,否则它会并进前一段、段数 nseg 丢失。 */
		const bool keeps_segment = old_head_is_segment || s.has_engine
				|| (whole_chain_invert && &s == &segs.front());
		if (keeps_segment) new_front->SetSegmentFront();
		/* R3R: 段右边界(SegmentBack)随段首一起迁移。组内反转后新段尾 = 旧段首块
		 * 的尾车,而旧段尾(带标记时)现在落进了新段首块内部:先把它上面的标记
		 * 摘掉,再在新段尾补上。必须按"块首/块尾"而不是单一车辆处理,artic 块
		 * (父车 + parts)才正确 —— 全为单辆块时 blocks.front().back() == old_front、
		 * blocks.back().back() == new_front,行为与旧实现完全一致。
		 * 这样"段 = ★ 起、右边界标记止"在反转后依然成立,depot 拖动不会把段后
		 * 拖挂进来的车辆算作段内容。 */
		Train *old_tail = s.blocks.back().back();
		Train *new_tail = s.blocks.front().back();
		if (old_tail->IsSegmentBack()) {
			old_tail->ClearSegmentBack();
			new_tail->SetSegmentBack();
		}

		/* R3R (KI-187): 段头迁移必须连"责任"一起搬。段的身份载体是段头车(命令
		 * 主人与段优先级都定义在段头集合上),而排程借用制把"谁是排程主人"直接
		 * 编码成"哪辆车持有 orders 指针 + r3r_priority 最小"。只搬 ★ 不搬这些
		 * 数据,持有排程的车就会掉出段头集合:挂车时按段头挑命令主人看不到它
		 * (排程交接被跳过),解挂时"解出方链头有没有表"也随之判错(把有表的一半
		 * 当成无表方,新建 WAIT_COUPLE 空表 ⇒ 合并列车死等)。迁移对记录在
		 * owner_moves_out 里,回滚(R3RUndoLogicalFlip)按它反向搬回。 */
		if (old_front != new_front) {
			R3RMoveSegmentOwner(old_front, new_front);
			if (owner_moves_out != nullptr) owner_moves_out->owner_moves.emplace_back(old_front, new_front);
		}
	}

	/* 4b. 打散组角色位迁移:段内倒序把每组内部的链序也翻了过来,新组首 = 原组尾
	 * (第 2b 步记录里该组的最后一节)。先清掉整链的角色位,再按记录的组序列重新
	 * 标注:原组尾升为 ArticGroupHead,其余(含原组头)降为 ArticGroupMember。
	 * 必须按第 2b 步记录好的组序列迁移,不能用"链上连续带角色位的车"去识别组 ——
	 * 段内倒序会把相邻两个打散组整体前后对调,按连续性识别会把两组误并成一个
	 * (KI-140 删掉的 R3RReassignArticGroupRoles 即此缺陷)。
	 * 真 artic 组不带角色位、组内顺序也从不改变,不受本步影响。
	 * 注意:迁移后新组首在组内的 0x4D 名次会变成 0,所以必须配合第 1b 步的
	 * SegmentFlipped 取数镜像(见 newgrf_engine.cpp),GRF 看到的名次才保持不变。 */
	for (Train *w = new_head; w != nullptr; w = w->Next()) {
		w->ClearArticGroupHead();
		w->ClearArticGroupMember();
	}
	for (const std::vector<Train *> &g : deartic_groups) {
		if (g.empty()) continue;
		g.back()->SetArticGroupHead();
		for (size_t i = 0; i + 1 < g.size(); i++) g[i]->SetArticGroupMember();
	}

	/* R3R (KI-126): 链序在第 3 步重排过,且第 1b 步翻转了 SegmentFlipped(0x40/0x41
	 * 与 0x4D 的取数基准随之改变),这里补一次 NewGRF 缓存失效,保证 GRF 侧按"新链序
	 * + 镜像后的名次"重新取数,而不是沿用翻链前的缓存值。 */
	new_head->InvalidateNewGRFCacheOfChain();

	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) { fprintf(dbg, "FLIP-DONE head=%d nseg=%d whole=%d\n", (int)new_head->index.base(), (int)segs.size(), whole_chain_invert ? 1 : 0); fclose(dbg); }
	}
	return new_head;
}

/**
 * R3R 换端重排身份迁移(用户决策 2026-09-07 / 规范 31054337):逻辑翻 v 可能让
 * 合并链的链头从原机车对象 from 移走、to 成为新链头(真引擎多节列车翻 v 后
 * 新链头 = 原链尾引擎)。列车 primary 身份(orders / 订单位置 / 车号 /
 * current_order / profit / 服役间隔 / 窗口)只挂在链头上,须整体迁往 to,
 * from 则降为链内普通引擎。调用时机:TryTrainCouple 成功提交前。
 * @param from 原链头(仍为 FrontEngine 的旧机车对象)。
 * @param to   新链头(from 所在链逻辑翻转后的链头)。
 * @param owner_moved_by_flip R3R (KI-187): 折叠修正(R3RFlipChainBySegments 第 4 步
 *              的 R3RMoveSegmentOwner)是否已经把 from 的**段级责任数据**(排程 /
 *              订单位置 / 借用标志 / 段优先级)搬到 to。为 true 时本函数不得再搬这
 *              批数据 —— 再搬一次等于把排程搬回已降为链内普通车的 from,正好复现
 *              KI-187;此时只补齐"前端列车身份"(车号 / current_order / 利润 /
 *              服役间隔 / 窗口 / 列车名)。
 */
static void R3RRelocateFrontIdentity(Train *from, Train *to, bool owner_moved_by_flip = false)
{
	assert(from->IsFrontEngine());
	assert(to != nullptr && to->First() == to);

	/* R3R (KI-69)：身份要从 from 迁到 to，from 若还挂着装卸结算单（cargo_payment）
	 * 就成了永远没人结的孤儿账 —— 在拷贝身份/清 from 的 front 位之前先结清。
	 * 真机车 v 只在执行 GOTO_COUPLE 时才会走到这里（不是装卸态），此调用是防御；
	 * 一旦将来有新的调用点落在装卸途中，这里能兜住 economy.cpp:1535 的断言。 */
	R3RSettleLoadingBeforeChainEdit(from, "relocate-front");

	/* 随列车身份迁移的窗口。 */
	CloseWindowById(WindowClass::VehicleView, from->index);
	CloseWindowById(WindowClass::VehicleOrders, from->index);
	CloseWindowById(WindowClass::VehicleRefit, from->index);
	CloseWindowById(WindowClass::VehicleDetails, from->index);
	CloseWindowById(WindowClass::VehicleTimetable, from->index);
	CloseWindowById(WindowClass::ScheduledDispatchSlots, from->index);
	CloseWindowById(WindowClass::VehicleOrderImportErrors, from->index);
	DeleteNewGRFInspectWindow(GrfSpecFeature::Trains, from->index.base());
	SetWindowDirty(WindowClass::Company, from->owner);

	/* R3R (KI-187): 先把 to 手上已有的段级责任数据整块快照下来。下面
	 * CopyVehicleConfigAndStatistics 内部会调 BaseConsist::CopyConsistPropertiesFrom
	 * (base_consist.cpp:34-36),把 cur_real_order_index / cur_implicit_order_index /
	 * cur_timetable_order_index 一并从 from 复制到 to。当折叠修正刚把 from 的排程与
	 * 订单位置搬到 to 时(owner_moved_by_flip),这份拷贝会把刚搬过来的正确位置覆盖成
	 * from 侧的值(from 在搬迁后拿到的正是 to 的旧值,通常 0)——
	 * 2026-09-16 现场(R3R_debug.log)的"挂车后 v->orders_backup_real_index 记成 0、
	 * 解挂还原后机车从 index 0 起 IncrementRealOrderIndex 跳过自己的 GOTO_COUPLE
	 * 直接跑去 waypoint、不再回库与段耦合(ADVANCE real_before=0)"就是这条路径。
	 * 故先快照,拷完再原样放回。 */
	OrderList *const kept_orders = to->orders;
	OrderList *const kept_orders_backup = to->orders_backup;
	const VehicleOrderID kept_backup_real = to->orders_backup_real_index;
	const VehicleOrderID kept_backup_implicit = to->orders_backup_implicit_index;
	const VehicleOrderID kept_real = to->cur_real_order_index;
	const VehicleOrderID kept_implicit = to->cur_implicit_order_index;
	const VehicleOrderID kept_timetable = to->cur_timetable_order_index;
	const bool kept_borrowed = to->r3r_orders_borrowed;
	const uint16_t kept_priority = to->r3r_priority;

	/* R3R (第 144 轮 / 需求贰): 名字属于段头车自己。下面的
	 * CopyVehicleConfigAndStatistics() 会走 CopyConsistPropertiesFrom() 把 from 的
	 * 名字写进 to（base_consist.cpp 的 this->name = src->name），把 to（控制段段头）
	 * 自己的列车名覆盖掉；先快照、拷完还原，于是合并链对外显示的是控制段的名字，
	 * 机车自己的名字留在 from 上（from 随后由 R3RParkTrainName() 停进备份）。 */
	const TinyString kept_to_name = to->name;

	/* 车号 / current_order / dest_tile / profit / 服役间隔 / timetable 标志:
	 * 复用官方"新车头取代旧头"的复制逻辑(顺带把 from 的车号清零)。
	 * 订单号位置的顺序约束见上面的 kept_* 快照:拷贝会覆盖订单位置,必须在拷完还原。 */
	to->CopyVehicleConfigAndStatistics(from);

	/* R3R (需求贰): 还原 to（控制段段头）自己的名字，见上面的快照说明。 */
	to->name = kept_to_name;

	/* R3R (KI-147 rev.3): unitnumber_backup 是"挂车时借出并暂存的车号",属于列车身份,
	 * 必须随身份一起搬到新链头。不搬的话它会滞留在 from 上,而 from 随后被清掉 front
	 * 位、降为链内普通车 —— PreDestructor 的备份回收只在 FrontEngine 上执行,这条池
	 * 记录就永久泄漏(表现为买新车时编号只增不减)。to 自带的旧备份(若有且不同于
	 * 新活号)先归还池,避免被覆盖时又漏一个。 */
	if (to->unitnumber_backup != 0 && to->unitnumber_backup != to->unitnumber &&
			!to->R3RUnitNumberOwnedByOther()) {
		Company::Get(to->owner)->freeunits[VehicleType::Train].ReleaseID(to->unitnumber_backup);
	}
	to->unitnumber_backup = from->unitnumber_backup;
	from->unitnumber_backup = 0;

	/* 把刚才快照下来的段级责任数据原样放回(上面的拷贝可能已覆盖订单位置)。 */
	to->orders = kept_orders;
	to->orders_backup = kept_orders_backup;
	to->orders_backup_real_index = kept_backup_real;
	to->orders_backup_implicit_index = kept_backup_implicit;
	to->cur_real_order_index = kept_real;
	to->cur_implicit_order_index = kept_implicit;
	to->cur_timetable_order_index = kept_timetable;
	to->r3r_orders_borrowed = kept_borrowed;
	to->r3r_priority = kept_priority;

	/* orders 与订单位置。机车 orders 独有、不与他人共享,Couple 的 orders 交接
	 * 同为指针直搬(见 Couple ~7178);若未来出现共享 orders 需先退出共享链。
	 *
	 * R3R (KI-187): 折叠修正已经把 from 的段级责任数据(排程 / 订单位置 / 借用标志 /
	 * 段优先级)搬到 to 时(head != v 的换端重排路径必然如此 —— R3RFlipChainBySegments
	 * 第 4 步对第一段调用 R3RMoveSegmentOwner(from, to)),**绝不能**在这里再搬一次:
	 * 再搬等于把排程搬回已经降为链内普通车的 from,链头持空表,正好复现 KI-187
	 * (挂车时按段头挑命令主人看不到持表车,排程交接被跳过;解挂时又把有表的一半
	 * 当成无表方,新建 WAIT_COUPLE 空表 ⇒ 合并列车死等)。
	 * 只有在翻转没有搬迁过身份时(直接拼接路径,或未来新增的其它调用点)才在这里搬:
	 * 用 swap 而非"搬走",在"to 空手接手"的正常情形与直搬等价,异常情形(两边都有表)
	 * 也不丢表。 */
	if (!owner_moved_by_flip) {
		std::swap(to->orders, from->orders);
		std::swap(to->orders_backup, from->orders_backup);
		std::swap(to->orders_backup_real_index, from->orders_backup_real_index);
		std::swap(to->orders_backup_implicit_index, from->orders_backup_implicit_index);
		std::swap(to->cur_real_order_index, from->cur_real_order_index);
		std::swap(to->cur_implicit_order_index, from->cur_implicit_order_index);
		std::swap(to->cur_timetable_order_index, from->cur_timetable_order_index);

		/* R3R (KI-187): 借用标志与段优先级同属链头/段头身份,必须一起迁移。漏掉它们
		 * 会让新链头自认为"不欠任何人排程"(R3RSyncDrivingOrders 里 owner == chain
		 * 分支会把借来的表当成自己的,解挂时又把整份排程当 backup 还回去)。 */
		std::swap(to->r3r_orders_borrowed, from->r3r_orders_borrowed);
		std::swap(to->r3r_priority, from->r3r_priority);
	}

	/* 降级为链内普通引擎的 from 不再持有列车级订单位置。 */
	from->cur_real_order_index = 0;
	from->cur_implicit_order_index = 0;
	from->cur_timetable_order_index = INVALID_VEH_ORDER_ID;

	/* 预置新链头 subtype(FrontEngine / FreeWagon)并清掉链内其余车辆(含旧链头
	 * from)的 front 位 —— 与 DecoupleTrain 3690-3696 的预置同款,Couple 尾
	 * NormaliseTrainHead(v) 的 ConsistChanged assert 才能通过。 */
	NormaliseSubtypes(to);

	/* 非链头引擎不再持有的列车级数据。 */
	from->dispatch_records.clear();
	/* R3R (第 144 轮 / 需求贰): 名字不再直接丢弃 —— 停进 name_backup，等 from 重新
	 * 成为链头时由 R3RRestoreTrainName() 取回（与 DEPOT-PARK 同款）。 */
	R3RParkTrainName(from);

	/* R3R (第 144 轮): 保证"停放排程"与借用标志成对 —— 身份迁移把 from 的
	 * orders_backup 搬到 to 之后，标志必须跟着，否则 to 会带着"有停放排程但标志为假"
	 * 的矛盾状态，被 R3RSyncChainAfterDepotEdit() 的兜底块当成垃圾清掉（玩家报的
	 * "车库内拖动耦合之后解耦，后半条链排程变空"）。 */
	if (to->orders_backup != nullptr && !to->r3r_orders_borrowed) to->r3r_orders_borrowed = true;
}

/**
 * Physically couple the front train onto the waiting chain.
 * @param v Front train (with engine, executing GOTO_COUPLE). 成功返回时若折叠
 *          修正逻辑翻 v 使链头离开原机车对象,v 被更新为合并链的新链头对象
 *         (身份已迁往新链头),调用方(Couple)一律从 v 读取列车身份。
 * @param u Waiting chain (free wagons) to couple onto.
 * @param[out] merged_first 成功时被置为"拼进合并链的车组部分的链头对象":
 *             (普通拼接路径下恒等于 u;逻辑翻转路径下 —— 翻 u 为整链倒置,
 *              = 原链尾;翻 v 仍段序保持,= 原链头段反转后的新段首;单段链两者相同)。
 *             调用方(Couple)用它作为方向统一循环与段标记 ★ 的起点。
 * @return True on success.
 */
static bool TryTrainCouple(Train *&v, Train *u, Train *&merged_first)
{
	/* R3R perf probe (KI-14): a converging couple calls this once; a fold-fix
	 * retry loop calls it every tick (KI-06). */
	R3RPerfCounters &r3rp = R3RP();
	if (R3RPerfOn()) r3rp.try_couple++;
	R3RPerfTimer r3r_couple_timer(&r3rp.try_couple_ns);

	/* R3R: refuse invalid input. Both chains must be intact (each argument must
	 * be its own chain head); otherwise ArrangeTrains would merge corrupted
	 * chains and later chain traversal (NormaliseSubtypes) would hit the
	 * "u->First() == this" assertion. */
	if (v == nullptr || u == nullptr) return false;
	if (v->First() != v || u->First() != u) return false;
	if (v->First() == u->First()) return false; // Never couple a chain onto itself.

	TrainList original_src;
	TrainList original_dst;
	MakeTrainBackup(original_src, v);
	MakeTrainBackup(original_dst, u);

	Train *u_head = u;
	Train *v_last = R3RTrainTail(v);
	ArrangeTrains(&v, v_last, &u_head, u, true);

	/* R3R: refuse to keep a "folded" merged chain — a shape that makes
	 * TrainController crash as soon as the train moves. That shape is a
	 * DIRECTION fold: some vehicle sits on the NOSE side of its predecessor (a
	 * nose-to-nose / U-shaped splice), so the merged chain is geometrically
	 * discontinuous. R3RCheckChainFoldedDirection() is the test for it.
	 *
	 * A centre distance larger (or smaller) than the nominal coupling distance
	 * is NOT a usable verdict, for two independent reasons:
	 *
	 *  (a) Articulated parts render at the parent's exact x/y, so a legitimate
	 *      pair reports dist=0 against a nominal exp of up to 5 (see the
	 *      FOLDCHK lines: exp=5 dist=0, exp=4 dist=0). Any "neighbour closer
	 *      than nominal == interpenetration" test therefore fires on every
	 *      articulated train.
	 *  (b) KI-06, fixed 2026-09-14: ArrangeTrains() only re-wires the chain
	 *      links; it does not recompute the vehicle positions. A pair that has
	 *      just been joined logically therefore keeps the spacing it had before
	 *      the splice until the train moves and TrainController spaces the chain
	 *      out again. The old `R3RCheckChainFold(head, tag) > 8` test treated
	 *      that transient as a fold, so whenever the waiting consist still sat a
	 *      tile away every candidate (COUPLE / FLIP-U / FLIP-V / FLIP-BOTH) was
	 *      rejected, rolled back and retried on the very next tick — the endless
	 *      couple loop (机车锁死 + FOLDCHK 刷屏).
	 *
	 * The distance scan is still run because its FOLDCHK line is our diagnostic,
	 * but note the two tests are no longer short-circuited: the old
	 * `gap > 8 || dir` form hid every FOLDCHK-DIR line behind a large residual
	 * gap. Only the direction verdict rejects a chain now. */
	auto ChainFolded = [](Train *head, const char *tag, int *out_gap = nullptr, bool *out_dir = nullptr) -> bool {
		const int worst_gap = R3RCheckChainFold(head, tag);
		if (out_gap != nullptr) *out_gap = worst_gap;
		const bool dir_fold = R3RCheckChainFoldedDirection(head, tag);
		if (out_dir != nullptr) *out_dir = dir_fold;
		if (dir_fold) return true;
		if (worst_gap > 8) {
			R3RDbgWrite("FOLDCHK-ACCEPT %s worst_gap=%d residual gap (chain still closing up), not a fold\n", tag, worst_gap);
		}
		return false;
	};

	/* R3R (KI-06 收尾, 2026-09-15): the SPLICE PAIR -- the one pair the merge
	 * itself creates, (v_last, u_head) -- must be physically adjacent.
	 *
	 * This is what the old whole-chain `worst_gap > 8` test was trying to say,
	 * minus its two false positives: it is evaluated on the splice point only,
	 * never on the chain's interior, so neither the articulated pairs of (a) nor
	 * the pre-splice spacing of parts that were merely re-linked in (b) can fire
	 * it. It is what separates a correct flip from a wrong one. test4.sav
	 * 2026-09-15, locomotive nose against the consist's rear (all dir=3, loco
	 * 506..514 with its nose at 514, consist 515..533 with its nose at 533):
	 *
	 *   COUPLE-FLIP-V    splice idx0(632,514) <-> idx3(632,533) exp=2 dist=19
	 *   COUPLE-FLIP-BOTH splice idx0(632,514) <-> idx8(632,515) exp=2 dist=1
	 *
	 * Accepting the 19px splice (the flip-V candidate) produced a chain whose
	 * order jumps from the locomotive's tail straight to the consist's NOSE and
	 * then walks the consist's body backwards -- i.e. the consist is traversed
	 * through itself. Every *local* pair of that chain is still directionally
	 * negative (the flipped locomotive faces away and 533 is "behind" it), so
	 * the direction test cannot see it, and TrainController ended up with six
	 * cars stacked on one tile (log: idx3..idx8 all at (632,496)/tile 39,31,
	 * prog=0, spd=0) while the locomotive ran off alone. The flip-BOTH splice is
	 * the only self-consistent one (1px) and is exactly the one the 2026-09-15
	 * P1 comment describes ("原机车鼻顶车组尾的相邻点恰好变成机车链尾顶车组链头").
	 * Rejecting on the splice gap therefore routes this case to candidate 3.
	 * 8px is the same tolerance the old whole-chain test used. */
	auto SpliceFolded = [](const Train *splice_prev, const char *tag) -> bool {
		if (splice_prev == nullptr) return false;
		const Train *splice_head = splice_prev->Next();
		/* Nothing spliced (no successor), or the successor is not really linked
		 * back to us. */
		if (splice_head == nullptr || splice_head->Previous() != splice_prev) return false;
		const int expected = (splice_prev->gcache.cached_veh_length + splice_head->gcache.cached_veh_length) / 2;
		const int dist = std::max(std::abs(splice_prev->x_pos - splice_head->x_pos), std::abs(splice_prev->y_pos - splice_head->y_pos));
		if (std::abs(dist - expected) <= 8) return false;
		R3RDbgWrite("SPLICE-GAP-REJECT %s prev=%d x=%d y=%d head=%d x=%d y=%d exp=%d dist=%d\n",
				tag, (int)splice_prev->index.base(), (int)splice_prev->x_pos, (int)splice_prev->y_pos,
				(int)splice_head->index.base(), (int)splice_head->x_pos, (int)splice_head->y_pos,
				expected, dist);
		return true;
	};
	/* R3R (2026-09-27, KI-214): 拼接端点一致性 —— 候选1 固定把翻好的 u 接到 v 的
	 * 链尾,但若 v 的**链头(鼻子)**才是朝 u 的那端,就拼错了端:现场 26@387↔23@395
	 * dist=8/exp=2 被 8px 容差放行,而真正相邻的 24@393↔23@395 只差 2px,提交后
	 * 链序 24,25,26,23,… 在 26→23 折返 → 包围盒/接缝交错。否决它,落到候选3(双翻,
	 * v 的链尾正好翻到鼻子那侧)。单节/退化链与正常端点恒放行。 */
	auto WrongSpliceEnd = [](const Train *chain, const Train *splice_prev, const char *tag) -> bool {
		if (chain == nullptr || splice_prev == nullptr) return false;
		const Train *splice_head = splice_prev->Next();
		if (splice_head == nullptr || splice_head->Previous() != splice_prev) return false;
		const Train *other_end = chain->First();
		if (other_end == splice_prev || other_end == splice_head) return false;
		const int expected = (splice_prev->gcache.cached_veh_length + splice_head->gcache.cached_veh_length) / 2;
		const int d_used = std::max(std::abs(splice_prev->x_pos - splice_head->x_pos), std::abs(splice_prev->y_pos - splice_head->y_pos));
		const int d_other = std::max(std::abs(other_end->x_pos - splice_head->x_pos), std::abs(other_end->y_pos - splice_head->y_pos));
		if (d_other <= expected + 4 && d_used > d_other + 4) {
			R3RDbgWrite("SPLICE-WRONG-END %s used=%d x=%d d=%d other=%d x=%d d=%d head=%d x=%d exp=%d\n",
					tag, (int)splice_prev->index.base(), (int)splice_prev->x_pos, d_used,
					(int)other_end->index.base(), (int)other_end->x_pos, d_other,
					(int)splice_head->index.base(), (int)splice_head->x_pos, expected);
			return true;
		}
		return false;
	};

	bool u_flipped = false;
	bool v_flipped = false;
	R3RSegBoundaries u_old_bounds;
	R3RSegBoundaries v_old_bounds;
	std::vector<R3RArticRoleState> u_old_roles;
	std::vector<R3RArticRoleState> v_old_roles;
	Train *u_merged_head = u;
	/* 合并链当前链头对象。普通拼接/只翻 u 时恒等于 v;逻辑翻 v 使链头离开
	 * v 对象时更新为翻 v 后的新链头(Arrangement/折叠检查/最终检查一律用它)。 */
	Train *head = v;
	int direct_gap = -1;
	bool direct_dir_fold = false;
	bool direct_folded = ChainFolded(v, "COUPLE", &direct_gap, &direct_dir_fold);
	const bool direct_splice = SpliceFolded(v_last, "COUPLE");

	/* R3R (KI-174): 几何复核 —— 用位置证据驳回方向判据的误判。
	 *
	 * R3RCheckChainFoldedDirection 只读 direction 点积,是一条启发式:当机车
	 * 正在倒车贴近(IsDrivingBackwards)时,它的"实际前进端"与 direction 的指向
	 * 可以完全相反,点积于是给出"折叠"的假阳性(现场: 倒车机车尾对车头,dot=2)。
	 *
	 * 该判据本身已在 KI-173 按 db 口径修正,这里再加一道彼此独立的兜底:如果
	 * 直接拼接(候选 0)在几何上已经严丝合缝(worst_gap<=2,每个内部接缝都紧贴),
	 * 那么方向判据的"折叠"结论几乎不可能成立 —— 两段车确实已经头尾对位。此时
	 * 启动翻转有害无益:翻转是"段序保持、逐段各自反转",它把段尾当新接点,能把
	 * 一条 1px 的紧贴链掰成 91px 的断开链(现场日志 FOLDCHK COUPLE-FLIP-U
	 * worst_gap=91),四个候选于是全部被否,回滚后下一 tick 重来,机车锁死。
	 *
	 * 因此:紧贴的直接拼接直接采纳,跳过方向判据。阈值取得很紧(2px),只有
	 * "物理上已经完全对位"才会触发;test4.sav 那种真正需要翻转的场景(接缝
	 * 在 19px 级别)不受影响。 */
	if (direct_folded && direct_dir_fold && !direct_splice && direct_gap >= 0 && direct_gap <= 2) {
		R3RDbgWrite("FOLDCHK-DIR-OVERRIDE worst_gap=%d tight direct splice, fold verdict ignored\n", direct_gap);
		direct_folded = false;
	}

	if (direct_folded || direct_splice) {
		/* R3R: the first splice attempt folded — the end of the consist that
		 * the locomotive's rear was spliced onto (the consist's chain head)
		 * physically lies on the far side of the locomotive, i.e. the consist
		 * is parked facing the wrong way for a tail-to-nose splice. A waiting
		 * consist is stationary, so flip it in place and retry the splice onto
		 * its other end: a consist standing on a straight platform only has
		 * two ends, so one of the two attempts must place its nose next to
		 * the locomotive's rear.
		 *
		 * The waiting chain is reversed by R3RFlipChainBySegments (段级逻辑
		 * 反转): vehicle positions stay put, every vehicle's direction is
		 * reversed and each segment is inverted on its own (segment-internal
		 * head/tail swap), with segment markers (★) relocated onto each
		 * segment's new head.
		 * Segment ORDER (用户拍板 A,2026-09-25 修订 —— 推翻 2026-09-05 晚的
		 * "段序保持",至少对翻 u 这一路): with positions fixed, in-place
		 * inversion can put the chain head at the old chain TAIL only by
		 * reversing the segment order as well — "段序保持"版本的新链头留在
		 * 原链头段内,多段链上恰好接不到物理相邻的那一对(实测 78px 伪链,
		 * 见 KI-201 附记 2)。So: candidate 1 (flip u) passes
		 * whole_chain_invert = true (段序随整链颠倒 ⇒ 新链头 = 原链尾),
		 * while candidate 2/3 (flip v) keeps the segment order preserved as
		 * before (its head must stay an engine, checked below). Both modes
		 * share the direction / ★ / role / owner-migration code; only the
		 * segment iteration direction differs. Rollback needs only RestoreTrainBackup
		 * (chain pointers) plus R3RUndoLogicalFlip (directions + segment
		 * markers) — no physical mirroring back and forth. The locomotive
		 * chain v is also flipped logically (用户修订 2026-09-07): a logical
		 * flip of v only keeps the engine at the chain head when v is a
		 * single block (pure engine, or engine + its articulated parts);
		 * otherwise (engine towing plain wagons) the flip is rolled back and
		 * the candidate is dropped — v is never mirrored geometrically. */
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
		R3RRefreshChainCaches(v);
		R3RRefreshChainCaches(u);
		u_old_bounds = R3RCaptureSegBoundaries(u);
		u_old_roles = R3RCaptureArticRoles(u);
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) { fprintf(dbg, "FOLD-CATCHUP START\n"); fclose(dbg); }
		}
		/* R3R identity probe: dump both participants' per-vehicle identity so the
		 * fold-fix scenario can be classified (real artic bits vs de-articulated
		 * group roles). */
		R3RDumpCoupleIdentity(v, "FOLD-V");
		R3RDumpCoupleIdentity(u, "FOLD-U");

		/* R3R (用户方案 2026-09-05:"谁反就翻谁"追平;2026-09-07 修订:v 一律
		 * 逻辑翻、不物理 SWAP): A1 直接拼接折叠后不再按僵化的"翻 u → 双翻"
		 * 顺序走,而是逐候选判定:哪条链仍处于反(错误)向就只翻哪条 ——
		 * 只有 u 反 → 候选1 只逻辑翻 u;只有 v 反 → 候选2 只逻辑翻 v
		 * (旧序列从不单独翻 v,是 (v反,u正)/(v正,u反) 死锁的根因);
		 * 两条都反 → 候选3 双翻(逻辑翻 v + 逻辑翻 u)。
		 * 每个候选翻转后重拼做 ChainFolded 检查:通过即保留该翻转跳出;
		 * 失败则必须把该候选自己的翻转完整撤销(链序 RestoreTrainBackup +
		 * 方向/★ R3RUndoLogicalFlip)再试下一候选,候选状态不叠加。
		 * 逻辑翻 v 只有当翻转后链头仍是引擎时才有意义:单 artic 块(纯机车或
		 * 机车+artic parts)翻后链头仍留在原机车;多节真引擎链翻后新链头 =
		 * 原链尾引擎 —— 同样接受,primary 身份在成功提交点随 head != v 迁往
		 * 新链头(R3RRelocateFrontIdentity,换端重排 31054337)。v 拖着普通
		 * 车厢时翻后链头是车厢、机车失去链头,该候选直接失败回滚(不做几何
		 * 镜像)。全候选折叠 → 回滚到原状 return false(下 tick 重试,与旧
		 * 行为一致)。 */

		/* 候选1:只逻辑翻 u(u 反)。 */
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) { fprintf(dbg, "A2-FLIP-U\n"); fclose(dbg); }
		}
		/* 用户拍板 A(2026-09-25): 翻 u 走**整链倒置**(段序随整链颠倒)——
		 * 位置不动的就地反转下,"整链头尾对调"与"段序颠倒"是同一件事,段序保持
		 * 版本只能把链头搬到原链头段内(多段链时正好接不上物理相邻对,见函数头
		 * 注释与 KI-201 附记 2)。翻 v 一路仍为段序保持(候选2/3 见下)。 */
		Train *u_flip_head = R3RFlipChainBySegments(u, &u_old_bounds, true);
		u_flipped = true;
		u_merged_head = u_flip_head;
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) { fprintf(dbg, "A2-FLIP-DONE merged_head=%d\n", (int)u_flip_head->index.base()); fclose(dbg); }
		}

		u_head = u_flip_head;
		v_last = R3RTrainTail(v);
		R3RDumpChainDbg(v, "A2-ARR-BEFORE-V");
		R3RDumpChainDbg(u_flip_head, "A2-ARR-BEFORE-U");
		ArrangeTrains(&v, v_last, &u_head, u_flip_head, true);
		if (ChainFolded(v, "COUPLE-FLIP-U") || SpliceFolded(v_last, "COUPLE-FLIP-U") ||
				WrongSpliceEnd(v, v_last, "COUPLE-FLIP-U")) {
			/* 候选1 失败:撤销只翻 u(u 回原样后进候选2)。 */
			RestoreTrainBackup(original_src);
			RestoreTrainBackup(original_dst);
			R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
			u_flipped = false;
			R3RRefreshChainCaches(v);
			R3RRefreshChainCaches(u);
			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) { fprintf(dbg, "A2-ROLLBACK-DONE\n"); fclose(dbg); }
			}

			/* 候选2:只逻辑翻 v(u 保持原样)。机车停在车底头端但尾巴背向
			 * 车底(单侧朝向错误)时,单翻 v 即可让机车尾贴上 u_head。 */
			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) { fprintf(dbg, "A3-FLIP-V-ONLY\n"); fclose(dbg); }
			}
			v_old_bounds = R3RCaptureSegBoundaries(v);
			v_old_roles = R3RCaptureArticRoles(v);
			Train *v_flip_head = R3RFlipChainBySegments(v, &v_old_bounds);
			v_flipped = true;
			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) { fprintf(dbg, "A3-FLIPV-DONE head=%d\n", (int)v_flip_head->index.base()); fclose(dbg); }
			}
			/* R3R (P1, KI-06 fix 2026-09-15): 候选3(双翻)改为"可达且被实测判定"。
			 * 逻辑翻 v 使链头离开原机车对象时,仅当新链头仍是引擎(真引擎列车翻后
			 * 新链头 = 原链尾引擎,primary 身份随成功提交迁往新链头)才接受;v 拖着
			 * 普通车厢(翻后链头是车厢、机车失去链头)时候选2 不可行。旧代码在此处
			 * 直接 return false —— 但候选3 同样要逻辑翻 v,"候选3 是否也不可行"因此
			 * 成了一个从未被测量的假定(现场日志里 COUPLE-FLIP-BOTH 的判定行只可能
			 * 来自候选2 几何失败那条路径)。现在改为:结构不可行同样回滚候选2,但
			 * 流程继续落到候选3,由候选3 自己的同一个守卫给出结论 —— 放弃的原因
			 * 变成实测,而不是假定。 */
			bool try_candidate3 = false;
			if (v_flip_head != v && !v_flip_head->IsEngine()) {
				RestoreTrainBackup(original_src);
				RestoreTrainBackup(original_dst);
				R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
				v_flipped = false;
				R3RRefreshChainCaches(v);
				R3RRefreshChainCaches(u);
				v->ConsistChanged(CCF_ARRANGE);
				u->ConsistChanged(CCF_ARRANGE);
				R3RDbgWrite("FOLDCHK-SKIP COUPLE-FLIP-V v_head_not_engine head=%d -> fall through to candidate 3\n", (int)v_flip_head->index.base());
				try_candidate3 = true;
			} else {
				/* 接受翻 v:v 链真头可能已离开 v 对象(多节真引擎),把合并链当前
				 * 链头更新为 v_flip_head(v_flip_head == v 的单块场景无副作用)。 */
				head = v_flip_head;
				u_merged_head = u;
				u_head = u;
				v_last = R3RTrainTail(v);
				R3RDumpChainDbg(v, "A3-ARR-BEFORE-V");
				R3RDumpChainDbg(u, "A3-ARR-BEFORE-U");
				ArrangeTrains(&head, v_last, &u_head, u, true);
				if (ChainFolded(head, "COUPLE-FLIP-V") || SpliceFolded(v_last, "COUPLE-FLIP-V")) {
					/* 候选2 失败:撤销只翻 v(逻辑翻回原状)。 */
					RestoreTrainBackup(original_src);
					RestoreTrainBackup(original_dst);
					R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
					v_flipped = false;
					R3RRefreshChainCaches(v);
					R3RRefreshChainCaches(u);
					{
						FILE *dbg = R3RFopenDbg("a");
						if (dbg != nullptr) { fprintf(dbg, "A3-ROLLBACK-DONE\n"); fclose(dbg); }
					}
					try_candidate3 = true;
				}
			}

			if (try_candidate3) {
				/* 候选3:双翻 —— 机车与车底都处于错误朝向。机车逻辑反转 +
				 * 车组逻辑反转后两链同向,原"机车鼻顶车组尾"的相邻点恰好
				 * 变成"机车链尾顶车组链头",一次即可拼成。 */
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) { fprintf(dbg, "A3-BOTH-FLIPV\n"); fclose(dbg); }
				}
				v_old_bounds = R3RCaptureSegBoundaries(v);
				head = R3RFlipChainBySegments(v, &v_old_bounds);
				v_flipped = true;
				/* 与候选1 一致: 翻 u 用整链倒置(用户拍板 A,2026-09-25)。 */
				u_flip_head = R3RFlipChainBySegments(u, &u_old_bounds, true);
				u_flipped = true;
				u_merged_head = u_flip_head;
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) { fprintf(dbg, "A3-BOTH-FLIP-DONE merged_head=%d\n", (int)u_flip_head->index.base()); fclose(dbg); }
				}
				if (head != v && !head->IsEngine()) {
					/* 候选3 的结构守卫(与候选2 同一判据):双翻同样让链头离开原机车
					 * 对象,不可接受 —— 完整回滚两条链并放弃本次折叠修正(下 tick
					 * 重试)。走到这里说明"候选3 也不可行"是本 tick 实测结论。 */
					RestoreTrainBackup(original_src);
					RestoreTrainBackup(original_dst);
					R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
					R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
					v_flipped = false;
					u_flipped = false;
					R3RRefreshChainCaches(v);
					R3RRefreshChainCaches(u);
					v->ConsistChanged(CCF_ARRANGE);
					u->ConsistChanged(CCF_ARRANGE);
					R3RDbgWrite("FOLDCHK-SKIP COUPLE-FLIP-BOTH v_head_not_engine head=%d -> give up this tick\n", (int)head->index.base());
					return false;
				}

				u_head = u_flip_head;
				v_last = R3RTrainTail(v);
				R3RDumpChainDbg(v, "A3-ARR-BEFORE-V");
				R3RDumpChainDbg(u_flip_head, "A3-ARR-BEFORE-U");
				ArrangeTrains(&head, v_last, &u_head, u_flip_head, true);
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) { fprintf(dbg, "A3-ARRANGE-DONE\n"); fclose(dbg); }
				}
				if (ChainFolded(head, "COUPLE-FLIP-BOTH")) {
					/* 彻底失败:把两条链都翻回原状再报错(下次触发会重头试)。 */
					RestoreTrainBackup(original_src);
					RestoreTrainBackup(original_dst);
					R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
					R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
					v_flipped = false;
					u_flipped = false;
					R3RRefreshChainCaches(v);
					R3RRefreshChainCaches(u);
					v->ConsistChanged(CCF_ARRANGE);
					u->ConsistChanged(CCF_ARRANGE);
					return false;
				}
				/* R3R (2026-09-15): candidate 3 is the LAST candidate, so a
				 * non-adjacent splice here is accepted instead of rolled back.
				 * Rolling back would leave the state unchanged and retry on the
				 * next tick for ever -- exactly the KI-06 couple loop this fold
				 * machinery exists to end. The DIRECTION verdict above is still a
				 * hard veto (a genuine fold must not be committed); only the
				 * splice-gap preference is relaxed here, and it says so in the
				 * log so a broken merged chain is traceable. */
				if (SpliceFolded(v_last, "COUPLE-FLIP-BOTH")) {
					R3RDbgWrite("SPLICE-GAP-LAST-RESORT COUPLE-FLIP-BOTH accepted anyway (no candidate has an adjacent splice)\n");
				}
			}
		}
	}

	bool ok = CheckTrainAttachment(head).Succeeded();
	if (!ok) {
		RestoreTrainBackup(original_src);
		RestoreTrainBackup(original_dst);
		if (v_flipped) R3RUndoLogicalFlip(v, v_old_bounds, v_old_roles);
		if (u_flipped) R3RUndoLogicalFlip(u, u_old_bounds, u_old_roles);
		R3RRefreshChainCaches(v);
		R3RRefreshChainCaches(u);
		v->ConsistChanged(CCF_ARRANGE);
		u->ConsistChanged(CCF_ARRANGE);
		return false;
	}
	/* R3R 换端重排:逻辑翻 v 后合并链头可能不再是原机车对象 v(head != v,
	 * 仅发生在"多节真引擎 v 翻 v 后新链头 = 原链尾引擎"且成功拼合的路径)。
	 * 把列车 primary 身份整体迁往新链头,并把 v 更新为新链头对象 —— Couple
	 * 从 TryTrainCouple 报告的新链头读取全部身份(orders 交接 / group / 车号 /
	 * NormaliseTrainHead),原机车对象降为链内普通引擎。 */
	if (head != v) {
		/* owner_moved_by_flip = true:合并链新头 head 正是折叠修正第一段反转后的新段头,
		 * from(=v)的段级责任数据(排程 / 订单位置 / 借用标志 / 段优先级)已由
		 * R3RFlipChainBySegments 第 4 步的 R3RMoveSegmentOwner 搬到 head,这里只补
		 * 前端列车身份,不能再搬这批数据(见 R3RRelocateFrontIdentity 注释)。 */
		R3RRelocateFrontIdentity(v, head, true);
		v = head;

		/* 新链头(原链尾引擎)在本次 tick 建立车辆 tick 缓存时只是链内普通引擎,
		 * 不在 _tick_train_front_cache 里;旧链头对象(原机车 v)反而还在缓存中,
		 * 但它已被 NormaliseSubtypes 清掉 FrontEngine 位。若不重建缓存,合并后
		 * 真正的前端永远不会作为链头被 Tick,列车会从此静止(调度全停)。强制
		 * 重建使新链头从下一 tick 起被正常驱动;本 tick 内旧链头对象经
		 * Train::Tick / TrainLocoHandler 的 IsFrontEngine 保护安全退出。 */
		InvalidateVehicleTickCaches();
	}
	/* The merged-on group is headed by u itself on the plain splice, and by
	 * the logically reversed former tail after a fold-fix flip. Callers use
	 * merged_first as the chain anchor for the seam-direction test (R3R 2026-09-19,
	 * R3R_couple_direction_rule_memo.md) and the segment-marker ★ placement. */
	merged_first = u_merged_head;
	return true;
}

/* R3R: edge-triggered throttle for the "unable to couple" advice news.
 * A GOTO_COUPLE locomotive that reaches a consist it cannot couple onto (e.g. a
 * NewGRF "can attach wagon" rejection) is parked and retries every tick; without
 * throttling Couple() would push one advice news per tick. Record locomotives for
 * which the news has already been shown during the current failure episode; clear
 * the entry as soon as the locomotive leaves that episode (moves off, succeeds,
 * or drops the GOTO_COUPLE order), so a NEW failure episode may report again.
 * Deliberately a plain bool rather than a per-vehicle counter or cooldown:
 * a persistent (permanently rejected) failure must only annoy the player once.
 * Static only for this translation unit; a stale entry for a deleted VehicleID
 * merely suppresses one future report and is cleared as soon as that (or any)
 * locomotive with the same index moves again. */
static btree::btree_map<VehicleID, bool> _r3r_couple_fail_news_shown;

/** R3R (KI-199): chains whose coupling just completed and which therefore still owe
 *  themselves one real path reservation (see Couple() and TrainLocoHandler()). Only the
 *  head of the merged consist is remembered and the entry is consumed on the next
 *  TrainLocoHandler() run of that consist: the hand-over clears current_order, so the
 *  inherited travel order (and with it dest_tile) only exists once the ordinary
 *  ProcessOrders() of that tick has run. A leftover entry for a train that is never
 *  ticked again is harmless -- nothing is emitted unless the same index shows up. */
static btree::btree_set<VehicleID> _r3r_couple_autoreserve;

/** R3R (KI-206): platform waiters whose stale "route out of the station" reservation has
 *  already been released. A formation that parks on a platform as a waiter (car-only
 *  formation or a primary train sitting on a WAIT_COUPLE order) still carries the track
 *  reservation that brought it in (or that its schedule pointed at): it starts on the
 *  platform and runs out of the station in the direction the formation is facing. This
 *  waiter will never drive it -- it is picked up and pulled away by another locomotive,
 *  usually in the opposite direction -- so the bits are dead weight from the moment the
 *  consist stands still. Worse, they only live as long as the chain answers for them:
 *  once a coupling locomotive merges the chains, GetTrainForReservation() cannot follow
 *  them back to the merged train any more, they turn ownerless (stray), and from then on
 *  nothing in the game can ever clear them: the reservation preview keeps drawing a route
 *  out of the platform that no train will ever use. (The couple/decouple commit points do
 *  run R3RReleaseChainReservations(), but by then the damage is done for any bit whose
 *  ownership broke in the merge.) So release it here instead, on the first tick the
 *  consist stands still as a waiter, while the chain still answers for it. Keyed by
 *  VehicleID; an entry only lives while the consist is a parked waiter. */
static btree::btree_set<VehicleID> _r3r_waiter_purged;

/* R3R (KI-208): consists for which the "parked waiter must not book a route"
 * refusal has already been logged in the current waiting episode, so the
 * evidence is written once per episode instead of every tick. Cleared next to
 * _r3r_waiter_purged, i.e. as soon as the consist stops being a parked
 * platform waiter. */
static btree::btree_set<VehicleID> _r3r_waiter_nobook_logged;

/* R3R (裁决 2026-09-19 / R3R_couple_direction_rule_memo.md): Couple() turns a
 * 90-degree seam into an accident, which needs TrainCrashed(). That one is defined
 * far below (next to CheckTrainCollision), so forward-declare it here. */
static uint TrainCrashed(Train *v);

/**
 * R3R (2026-09-22): normalise the heading of a chain that was merged inside a rail
 * depot -- the end nearer the depot exit leads.
 *
 * Every train standing in a depot is parked on the same depot axis, so both the
 * per-vehicle heading and the leading end follow from the depot geometry alone:
 *   - the leading end (= GetMovingFront()) is whichever chain end is closer to the
 *     depot exit door (the player-facing rule: "the end nearest the door leads");
 *   - every vehicle then faces along the depot axis, flipped when the tail leads --
 *     which is exactly what Train::ConsistChanged(CCF_ARRANGE) already does for
 *     chains standing on a depot tile (see its DepotDirection branch).
 *
 * VehicleRailFlag::Flipped is deliberately left untouched. It means "this vehicle
 * is drawn reversed relative to the chain" (multiheaded rear engine, GRF
 * reverse-on-build, the player's per-wagon flip button), never "which way the depot
 * faces"; R3RReverseChainDirections flips it as render compensation, which is why
 * the seam-direction test in Couple() is skipped for depot merges.
 *
 * @param v Head of the freshly merged chain (the train identity holder).
 */
static void R3RNormaliseDepotMergeDirection(Train *v)
{
	/* Only touch a chain that stands entirely on the depot tile (depot stands are
	 * compressed onto one tile, so a long train still qualifies). A chain with the
	 * tail still outside would get its on-track heading overwritten on plain track,
	 * which is exactly the illegal (track, direction) pair KI-114 is about. */
	if (v == nullptr || !IsRailDepotTile(v->tile) || !IsWholeTrainInsideDepot(v)) return;

	/* Depot exit axis and the depot tile centre as the projection origin. The
	 * offset grows towards the exit door (and keeps growing outside the depot,
	 * because a chain may stick out of the door). */
	const TileIndexDiffC axis = TileIndexDiffCByDiagDir(GetRailDepotDirection(v->tile));
	const int origin_x = TileX(v->tile) * TILE_SIZE + TILE_SIZE / 2;
	const int origin_y = TileY(v->tile) * TILE_SIZE + TILE_SIZE / 2;

	auto exit_offset = [&](const Train *w) {
		return (w->x_pos - origin_x) * axis.x + (w->y_pos - origin_y) * axis.y;
	};

	/* With DrivingBackwards set the moving front is Last(), otherwise it is the
	 * head. Let the end nearer the door win. */
	const bool tail_leads = exit_offset(v->Last()) > exit_offset(v);
	v->vehicle_flags.Set(VehicleFlag::DrivingBackwards, tail_leads);
	v->ConsistChanged(CCF_ARRANGE);

	FILE *dbg = R3RFopenDbg("a");
	if (dbg != nullptr) {
		fprintf(dbg, "[R3R] DEPOT-MERGE-DIR head=%d tail=%d tail_leads=%d head_off=%d tail_off=%d\n",
				(int)v->index.base(), (int)v->Last()->index.base(), (int)tail_leads,
				(int)exit_offset(v), (int)exit_offset(v->Last()));
		fclose(dbg);
	}
}

/**
 * R3R (KI-205): drop every tile reservation that still answers to @p head.
 *
 * FreeTrainTrackReservation() cannot do this job: it walks *forward* from the
 * consist along the reserved path and gives up at the first tile that carries
 * no reservation. Right after a couple that walk dies on the spot, because the
 * loco has just rolled over the tiles in front of it (their bits were consumed
 * as it drove), while the *far* end of its old path -- the part beyond the
 * station it stopped in -- is still booked. Those tiles are not "stray": ask the
 * engine and GetTrainForReservation() follows the reservation, jumps across the
 * platform and still names the train. That is exactly why they are a "ghost" --
 * nothing will ever consume them, because the merged train leaves the station by
 * another route, and the reservation preview keeps drawing them for ever.
 *
 * So do not walk: ask per tile and drop everything the merged chain still owns.
 * Safe at the commit point of a chain edit (couple / decouple), where the
 * affected halves have just been re-identified: at that instant every bit on the
 * map that answers to them is a leftover of the route they will not drive, and
 * the next pathfind books the route they really will.
 *
 * Kept away from depots, level crossings, tunnels and bridges: there a reserved
 * bit is not an individual leftover but the protection of the vehicle on/in
 * them, and none of them can be attributed to a chain that has just been
 * re-identified. The same goes for any tile a train is physically standing on,
 * whatever the tile type.
 *
 * Station tiles -- platforms *and* waypoints -- are the one exception and are
 * released as a whole tile as well (KI-207c, 2026-09-25). TryReserveRailTrack()
 * books them as a single tile bit for every station that carries rail, so a
 * path reservation whose far end is a waypoint books the waypoint itself. All
 * the engine's release paths then ask IsRailStationTile() -- which excludes
 * waypoints by definition -- or walk plain rail only, so that one bit is
 * exactly what survives a sweep: the player watched the train reserve towards
 * the waypoint, saw those bits dropped at once, and was left with nothing but
 * the waypoint tile (log: SET-STN tile=60,16 in phase choose-track by veh 29,
 * then "RESV-GHOST couple" freed 58,18 / 59,17 / 60,17 / 59,18 -- the same
 * route, two tiles short). The identical "no vehicle on the tile" and "still
 * answers to this chain" guards apply, so only our own leftover is touched.
 *
 * @param head Head of the chain whose leftover reservations should go.
 * @param why  Reason tag written to the log.
 * @return Number of released reservations (track bits and whole tiles).
 */
static uint R3RReleaseChainReservations(const Train *head, const char *why)
{
	if (head == nullptr) return 0;

	Train *const chain_head = head->First();
	if (chain_head == nullptr) return 0;

	uint freed = 0;

	/* Two passes, and the separation is the whole point (KI-207).
	 *
	 * GetTrainForReservation() answers by *following the reserved path* from the
	 * queried tile back to the train standing at its end. Freeing a bit while
	 * scanning therefore cuts the path: every bit behind the cut -- i.e. the far
	 * end of the route, exactly the part that is not under the train -- stops
	 * resolving to anybody and is skipped by the rest of the scan. The result is
	 * the leftover the player keeps seeing: one or two isolated bits at the end of
	 * the old route ("60,16", "57,49") that no release path can ever attribute
	 * again, so nothing removes them and the preview draws them for ever.
	 *
	 * Pass 1 asks the engine about every bit while the map is still untouched and
	 * only remembers what it finds; pass 2 then frees that snapshot. */
	struct R3RResvBit { TileIndex tile; Track track; bool whole_tile; };
	std::vector<R3RResvBit> doomed;

	/* KI-209 (2026-09-25): the stations this chain still physically stands on.
	 * The engine books the *whole platform* when a train enters a station
	 * (SetRailStationPlatformReservation), so a consist standing on one tile of
	 * a platform answers for every other tile of that same platform -- those
	 * bits are the protection of the train standing right there, not a leftover
	 * of ours. Sweeping them stripped the platform preview off trains that were
	 * simply standing in the station (player report: "you removed my ordinary
	 * train's whole-platform reservation"; log: RESV-GHOST couple veh=27
	 * tile=24,9/25,9/26,9 kind=station and RESV-GHOST decouple veh=0
	 * tile=31,9/32,9/33,9 kind=station). A platform or waypoint the chain has
	 * left behind still counts as a leftover and is released as before. */
	btree::btree_set<StationID> chain_stations;
	for (const Train *w = chain_head; w != nullptr; w = w->Next()) {
		if (IsTileType(w->tile, TileType::Station)) chain_stations.insert(GetStationIndex(w->tile));
	}

	/* KI-211: walk the tiles and bits this chain booked *itself*, forward from the
	 * chain head along its own reservation (R3RCollectReservedPathTiles).
	 *
	 * This replaces the backward owner lookup used before (KI-210), which asked the
	 * map "who does this bit belong to" and therefore crossed onto a neighbouring
	 * train's booking whenever the two touch -- which they do at every platform
	 * edge. Field case: veh 24 decouples from veh 6 and stands one tile outside the
	 * platform with a live route to waypoint 38,10 (34,9 .. 38,10), while veh 6
	 * waits on the platform tile 33,9 holding the whole-platform booking. The
	 * backward walk from 24's route met 6 on 33,9, so 6's waiter purge attributed
	 * the live route to itself and freed it: the reservation the player sees flash
	 * for an instant, on a route that was perfectly legitimate.
	 *
	 * Booking is a forward relation, so the purge now reads it forward: a bit that
	 * is not on this chain's own path can never be this chain's leftover.
	 *
	 * KI-211c: neither half of that is sufficient on its own -- see the two gate
	 * comments below. The own path is collected with the walk fenced at the first
	 * train (pbs.cpp), which keeps the neighbour's booking out of it, and every
	 * candidate still has to answer the nearest-train owner question, because our
	 * own walk legitimately continues onto a booking joined to our own front tile.
	 * Both gates have to pass before a bit is freed. */
	struct R3ROwnPath {
		btree::btree_set<TileIndex> tiles;   ///< tiles the chain's own path covers (station whole-tile test)
		btree::btree_set<uint64_t> bits;     ///< (tile << 3) | track, for the plain-rail test
	} own_path;
	R3RCollectReservedPathTiles(chain_head, [](TileIndex t, Trackdir td, void *ctx) {
		R3ROwnPath *p = static_cast<R3ROwnPath *>(ctx);
		p->tiles.insert(t);
		p->bits.insert((static_cast<uint64_t>(t.base()) << 3) | static_cast<uint64_t>(TrackdirToTrack(td)));
	}, &own_path);

	for (TileIndex t(0); t < Map::Size(); t++) {
		/* KI-207c: a station tile (platform or waypoint) is reserved as one
		 * whole-tile bit, so it is attributed as a whole tile here. */
		if (IsTileType(t, TileType::Station)) {
			if (!HasStationRail(t) || !HasStationReservation(t)) continue;
			/* Never unreserve the tile a vehicle is standing on: that bit is what
			 * keeps other trains from pathing into it. */
			if (GetFirstVehicleOnTile(t, VehicleType::Train) != nullptr) continue;

			/* KI-211: only a tile on this chain's own booked path can be its
			 * leftover (KI-210 asked the map for an owner here and freed live
			 * routes of neighbouring trains, see above). */
			if (own_path.tiles.find(t) == own_path.tiles.end()) continue;

			/* KI-211b: a platform/waypoint carries one whole-tile bit and no
			 * per-track information at all (station_map.h, SetRailStationReservation),
			 * so (tile, track) precision is impossible here -- unlike on plain rail,
			 * where the release below matches the exact track and therefore cannot
			 * touch the other diagonal of a crossing. The compensation is to ask the
			 * engine who owns the bit and refuse when it is another live train.
			 * nullptr means the bit resolves to nobody: an orphan, which is exactly
			 * what this sweep exists to remove.
			 *
			 * KI-211c: the owner question must be the *nearest train* one
			 * (GetR3RWholeTileReservationOwnerNearby), not the far-end one
			 * (GetTrainForWholeTileReservation). A fresh booking joined to a stale
			 * one is a single continuous run of reserved bits, so the far-end walk
			 * sweeps past the locomotive that booked the fresh part and answers with
			 * the parked consist standing at the other end -- which is us, so the
			 * gate below passed and we freed the neighbour's live route. Field log
			 * (waiter purge of veh 6): `RESV-WALK start=38,10 how=direct end=27,9
			 * owner=veh6 head=veh6` followed by `RESV-GHOST waiter veh=6 tile=34,9 /
			 * 35,9 / 36,9 / 37,9 / 37,10 / 38,10`, and veh 24 re-booked 38,10 on the
			 * next tick: the blink the player reported. The nearest train along the
			 * run is the locomotive that booked it, so that walk refuses instead. */
			const Train *whole_owner = GetR3RWholeTileReservationOwnerNearby(t);
			if (whole_owner != nullptr && whole_owner->First() != chain_head) continue;

			/* KI-209: the chain still stands on this very station -> keep the
			 * platform protection. */
			if (chain_stations.find(GetStationIndex(t)) != chain_stations.end()) continue;

			doomed.push_back({t, INVALID_TRACK, true});
			continue;
		}

		if (GetTileType(t) != TileType::Railway || !IsPlainRailTile(t)) continue;
		/* Never unreserve the tile a vehicle is standing on: that bit is what
		 * keeps other trains from pathing into it. */
		if (GetFirstVehicleOnTile(t, VehicleType::Train) != nullptr) continue;
		const TrackBits bits = GetRailReservationTrackBits(t);
		if (bits == TRACK_BIT_NONE) continue;

		for (Track track = TRACK_BEGIN; track < TRACK_END; track++) {
			if (!HasBit(bits, track)) continue;

			/* KI-211: match (tile, track) and not the tile alone: on a crossing
			 * (the 35,8/35,9 example from the field) another train legitimately
			 * uses the *other* track of a tile this chain also passes through. */
			if (own_path.bits.find((static_cast<uint64_t>(t.base()) << 3) | static_cast<uint64_t>(track)) == own_path.bits.end()) continue;

			/* KI-211c: membership in our own path is necessary but not
			 * sufficient, for the same reason as on the platform branch above:
			 * our own walk leaves our front tile and continues onto whatever
			 * booking is joined to it, so a neighbour's fresh route reads as our
			 * own path. The nearest train along the run is the one that booked
			 * it; refuse when that is somebody else. */
			const Train *rail_owner = GetR3RReservationOwnerNearby(t, track);
			if (rail_owner != nullptr && rail_owner->First() != chain_head) continue;

			doomed.push_back({t, track, false});
		}
	}

	if (!doomed.empty()) {
		R3RResvPhaseGuard phase_guard("release-ghost", R3RResvActorID(chain_head));
		for (const R3RResvBit &rec : doomed) {
			/* Nothing runs between the passes, but validate anyway: the map must
			 * never be touched through a stale record. */
			if (rec.whole_tile) {
				if (!IsTileType(rec.tile, TileType::Station) || !HasStationRail(rec.tile)) continue;
				if (!HasStationReservation(rec.tile)) continue;
				if (GetFirstVehicleOnTile(rec.tile, VehicleType::Train) != nullptr) continue;

				SetRailStationReservation(rec.tile, false);
				freed++;
				R3RDbgWrite("RESV-GHOST %s veh=%d tile=%d,%d track=whole kind=%s\n",
						why, (int)chain_head->index.base(), (int)TileX(rec.tile), (int)TileY(rec.tile),
						IsRailWaypointTile(rec.tile) ? "waypoint" : "station");
				continue;
			}

			if (GetTileType(rec.tile) != TileType::Railway || !IsPlainRailTile(rec.tile)) continue;
			if (!HasBit(GetRailReservationTrackBits(rec.tile), rec.track)) continue;

			UnreserveRailTrack(rec.tile, rec.track);
			freed++;
			R3RDbgWrite("RESV-GHOST %s veh=%d tile=%d,%d track=%u\n",
					why, (int)chain_head->index.base(), (int)TileX(rec.tile), (int)TileY(rec.tile), (uint)rec.track);
		}
	}

	/* Always log the summary when there was anything to look at: "cand" and
	 * "freed" must be equal, and the pair is what tells a future round whether a
	 * path was still being cut. */
	if (!doomed.empty()) {
		R3RDbgWrite("RESV-GHOST-SUMMARY %s veh=%d cand=%u freed=%u\n",
				why, (int)chain_head->index.base(), (unsigned)doomed.size(), freed);
	}
	return freed;
}

/**
 * Couple the train onto the waiting consist.
 * @param v Front train (with engine).
 * @param u Waiting consist to couple onto.
 * @param train_u_reversed Whether the consist is facing the opposite direction.
 */
static void Couple(Train *v, Train *u)
{
	/* R3R: remember the original coupling locomotive. A fold-fix logical flip of a
	 * multi-engine chain may migrate the train identity (and thus v) onto a new
	 * head; the news-throttle entry must be keyed on the locomotive that actually
	 * tried (and failed) the coupling. */
	const VehicleID couple_loco_id = v->index;
	/* R3R（2026-09-23 玩家口径）：「临时挂接分组」的销毁点。
	 *
	 * 「临时挂接分组」是订单属性：机车执行一条 GOTO_COUPLE 命令时，如果这条命令带着
	 * 某个真挂接分组的名字，那么它在本命令执行期间临时"同时属于"该分组
	 * （R3RCoupleAllowed() 把该分组的 bit 并进参与挂接那个段的有效掩码）。挂接一旦
	 * 成功，命令就会被下面 IncrementImplicitOrderIndex()/ProcessOrders() 推进掉，
	 * 这个临时身份随之消失 —— 这里先把本次生效的分组记下来，在成功提交点上打
	 * CGRP-FAKE-DESTROY 日志，便于现场核对：临时分组只存在于"前去挂接"执行期间，
	 * 不属于合并后的链。 */
	const CoupleGroupID coupler_used_fake_group = R3RGetTempCoupleGroup(v);
	/* R3R: neither the locomotive (v) nor the consist (u) is reversed here.
	 * The locomotive drives towards the consist nose-first, and the consist
	 * keeps its heading; both may couple from either end (nose-to-tail or
	 * nose-to-nose) — the merged chain simply follows the locomotive's
	 * direction, and any later reversal is handled by realistic reverse. */
	v->IncrementImplicitOrderIndex();
	ProcessOrders(v);

	/* R3R (KI-204): remember the *live* schedule position of the waiting consist.
	 * Only a chain head is ticked, so its order index is the real progress of the
	 * schedule it is running; the schedule's nominal owner (couple_owner below, a
	 * mere segment head of the merged chain) is not ticked and may sit many steps
	 * behind. Inheriting the owner's index after the merge thus resumed the merged
	 * train BEFORE the WAIT_COUPLE the waiting consist had already reached, so that
	 * banked order got replayed and the train parked on the very same WAIT_COUPLE
	 * again -- the "the long schedule never steps past the wait order" symptom.
	 * Sample the waiter's position here, while it is still its own chain head. */
	const VehicleOrderID waiter_real_index = u->cur_real_order_index;
	const VehicleOrderID waiter_implicit_index = u->cur_implicit_order_index;

	Train *merged_first = nullptr;
	/* R3R (KI-187): 这里**不再**采集两侧段头列表。折叠修正的 ★ 迁移会把段的
	 * 身份/责任数据(排程、借用标志、段优先级)连同段头搬到段内另一辆车
	 * (R3RMoveSegmentOwner),合并后链上真正的段头与合并前可以完全不同;按合并前
	 * 的列表挑"命令主人",会挑中一辆已经交空排程的旧段头车,于是排程交接被整块
	 * 跳过。两侧列表改在 TryTrainCouple 之后按合并链的实际段头重采(见下方
	 * passive_segs_now / active_segs_now)。 */
	if (!TryTrainCouple(v, u, merged_first)) {
		if (v->owner == _local_company) {
			/* R3R: edge-triggered report. A stationary GOTO_COUPLE locomotive that
			 * cannot couple (e.g. NewGRF "can attach wagon" refusal) retries every
			 * tick; show the news only once per continuous failure episode. The
			 * entry is cleared when the locomotive moves/succeeds/drops the order,
			 * so a fresh episode can warn again. */
			if (_r3r_couple_fail_news_shown.find(couple_loco_id) == _r3r_couple_fail_news_shown.end()) {
				_r3r_couple_fail_news_shown[couple_loco_id] = true;
				AddVehicleAdviceNewsItem(AdviceType::TrainStuck, GetEncodedString(STR_NEWS_ORDER_COUPLE_FAILED, couple_loco_id, u->index), couple_loco_id);
			}
		}
		return;
	}

	/* R3R (KI-182): the coupling succeeded, so the pair flag has served its
	 * purpose and both sides drop it -- otherwise the freshly coupled consist
	 * would stay locked against every other locomotive for ever. The active
	 * side is cleared through the *original* locomotive's id: a fold-fix flip
	 * may have migrated the identity (and thus the object `v` points at) onto a
	 * new head, while the recorded id still resolves to the merged chain. */
	R3RUnpairCoupleTargets(Train::GetIfValid(couple_loco_id));
	R3RUnpairCoupleTargets(u);
	_r3r_pair_scan_tick.erase(couple_loco_id.base());
	_r3r_couple_enter_tick.erase(couple_loco_id.base());

	/* R3R (KI-188, 玩家规则 2026-09-24): the consist we just coupled onto is
	 * consumed and must stop advertising itself as "waiting to be coupled onto" in
	 * this very step.
	 *
	 * The schedule hand-over below transplants the consist's order list onto the
	 * new head, but the consist's own segment fronts keep their current_order
	 * pointing at the WAIT_COUPLE which has just been fulfilled: only the true
	 * chain head is ticked (see _tick_train_front_cache), and after the merge the
	 * consist is no longer one, so that marker would stay for ever. While it stays,
	 * R3RIsCoupleTarget() keeps reporting the merged train as an idle consist, so
	 * other locomotives lock onto it, their couple pathfinder aims at it and never
	 * finds a valid destination, and their GOTO_COUPLE never completes -- the
	 * "无头苍蝇" scene of KI-188.
	 *
	 * The merged head itself is deliberately skipped: its own stale order is dropped
	 * by the hand-over below (v->current_order.Free()). */
	{
		Train *const wait_clear_head = Train::From(v->First());
		for (Train *w = (wait_clear_head != nullptr) ? wait_clear_head->Next() : nullptr;
				w != nullptr; w = w->Next()) {
			R3RClearStaleWaitMarker(w);
		}
	}

	/* R3R: after a successful merge the train still holds the loco's stale lookahead.
	 * During the whole GOTO_COUPLE run the couple pathfinder starts from the loco
	 * itself (yapf_rail.cpp ChooseRailTrack), so FillTrainReservationLookAhead is
	 * never called and the lookahead keeps pointing at the pre-couple reservation end
	 * (often the depot the loco came out of, i.e. BEHIND the loco). With realistic
	 * braking FollowTrainReservation() would start the first post-merge pathfind from
	 * that stale end tile, reversing the train (REVERSEDIR) and stranding it. Drop
	 * the lookahead so the next pathfind starts from the loco itself. */
	v->lookahead.reset();
	u->lookahead.reset();

	/* R3R (KI-205): the merge is committed and the chain will never drive the
	 * route it booked before it arrived, yet part of that route is still reserved
	 * on the map -- the "ghost reservation" that keeps following the train around
	 * and never goes away (the player sees it as a stale reservation preview, and
	 * it survives as long as the save does, because nothing ever walks over it
	 * again). Right here the chain has no lookahead, so every bit still answering
	 * to it is a leftover and safe to drop; the next pathfind books the route the
	 * train will really take. */
	R3RReleaseChainReservations(v, "couple");

	/* R3R (裁决 2026-09-19 / R3R_couple_direction_rule_memo.md): 挂车「端面方向判据」。
	 *
	 * 旧实现(KI-114)把链头朝向 v->direction 无条件覆盖到 merged_first 起的每一节。
	 * 链跨多块轨道时，承载不了该朝向的车被写成 (track, direction) 非法组合
	 * (TRACK_Y 只允许 SE/NW 却被写 N)，只有车再动起来才由 VehicleEnterTileCoordinates
	 * 自愈 —— 这正是 KI-107 / KI-108 崩溃窗口的状态来源。
	 *
	 * 新判据只看拼缝「端面两节」：a = merged_first->Previous()(主动方链尾端面)、
	 * b = merged_first(拼入段端面)。direction 是 0..7 的环，
	 *   circ = min(|a->direction - b->direction|, 8 - |a->direction - b->direction|) ∈ [0,4]
	 *     circ <= 1  同向(含相邻轨道 45° 差)：链已自洽，一个字节都不改。
	 *     circ == 2  直角错位(90°)：现实里没有直角挂车，判为事故，按撞毁处理。
	 *     circ >= 3  反向(nose-to-nose / tail-to-tail)：对拼入段逐节 ReverseDir 并同步
	 *                Flip(Flipped) 作图像补偿(净渲染 = 原朝向，一个像素不动)，即
	 *                R3RReverseChainDirections。逐节反转恒合轨(轨道对 180° 对称)，
	 *                且把 circ 3/4 映射成 circ 1/0，拼缝自洽。
	 *
	 * 不依赖「整列同向」：moving-*(GetMovingFront / GetMovingNext / GetMovingPrev /
	 * GetMovingBack，vehicle_base.h)只读 DrivingBackwards 标志，与逐节 direction 无关；
	 * ReverseTrainSwapVeh 是成对 swap(不推链尾朝向)。旧注释「必须整列同向」的两条论据
	 * 在本代码树里都不成立。
	 *
	 * 起点用 merged_first 而非 u：折叠修正逻辑翻过 u 之后，拼入段的几何头是反转后的
	 * 原链尾，从 u 起只会碰到那一节。普通拼接 merged_first == u，两条路径都覆盖。 */
	if (merged_first != nullptr) {
		/* R3R (2026-09-22): 车库内合链不走拼缝局部反转 —— 库轴是唯一真值来源，整链
		 * 朝向在下面的 R3RNormaliseDepotMergeDirection() 里按库轴统一。若在这里先做
		 * R3RReverseChainDirections，它会 Flip(Flipped) 作渲染补偿，而随后库内按库轴
		 * 重写 direction 时这份补偿会让被翻转的那半截整体渲染反向。库内每节车的
		 * direction 只可能是库轴或其反向，也不会出现 90° 拼缝。 */
		Train *const seam_prev = IsWholeTrainInsideDepot(v) ? nullptr : merged_first->Previous();
		if (seam_prev != nullptr) {
			const int seam_delta = static_cast<int>(merged_first->direction) - static_cast<int>(seam_prev->direction);
			const int seam_abs = (seam_delta < 0) ? -seam_delta : seam_delta;
			const int seam_circ = (seam_abs > 4) ? 8 - seam_abs : seam_abs;
			if (seam_circ == 2) {
				/* 直角(90°)拼缝 = 事故。两车在这之前已完成物理拼接(ArrangeTrains)，这里立即
				 * 按撞毁处理，不再走下面的排程交接：车已毁，orders 交接没有意义。 */
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "[R3R] COUPLE-SEAM-CRASH circ=2 a=%d dirA=%d b=%d dirB=%d tile=%u\n",
							(int)seam_prev->index.base(), (int)seam_prev->direction,
							(int)merged_first->index.base(), (int)merged_first->direction,
							(unsigned)v->tile.base());
					fclose(dbg);
				}
				if (!v->vehstatus.Test(VehState::Crashed)) {
					const uint num_victims = TrainCrashed(v);
					if (v->owner == _local_company) {
						AddTileNewsItem(GetEncodedString(STR_NEWS_TRAIN_CRASH, num_victims), NewsType::Accident, v->tile);
					}
				}
				return;
			}
			if (seam_circ >= 3) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "[R3R] COUPLE-SEAM-FLIP circ=%d a=%d dirA=%d b=%d dirB=%d\n",
							seam_circ, (int)seam_prev->index.base(), (int)seam_prev->direction,
							(int)merged_first->index.base(), (int)merged_first->direction);
					fclose(dbg);
				}
				R3RReverseChainDirections(merged_first);
			}
		}
		for (Train *w = merged_first; w != nullptr; w = w->Next()) w->UpdateViewport(false, false);
	}

	/* R3R (2026-09-22): 车库内合链 —— 由靠近库门的那一端带路。
	 * 库里各股道平行，没有"谁更靠外"的偏序，但每个车到库门的距离可以由库轴
	 * 唯一确定，所以合链后直接把 DrivingBackwards 摆到"靠门端领车"，并按库轴
	 * 统一全链朝向。必须早于 R3RRespaceChainAfterEdit()：后者沿
	 * GetMovingNext()（读 DrivingBackwards）把链接缝推紧，DB 未摆正会把车往
	 * 错误方向推。 */
	if (IsRailDepotTile(v->tile)) R3RNormaliseDepotMergeDirection(v);

	/* R3R (KI-134): the splice only re-linked the chain, so the seam pair — and on a
	 * fold-fix merge the whole re-linked part — still sits at the distance the two
	 * trains stopped at. Close that up now, while both halves are standing still and
	 * the directions of the seam have just been made consistent, instead of leaving a
	 * permanent visible gap between the locomotive and the wagons. Runs after the
	 * seam-direction fix so every vehicle faces the train's direction (that is what
	 * makes a forward step of a wagon move it towards the head). */
	R3RRespaceChainAfterEdit(v->First(), "couple");

	/* R3R (KI-207): second sweep of the same release, now that the merged chain has
	 * its final orientation and its final occupancy.
	 *
	 * The sweep above (before the seam-direction fix) can only see what the two
	 * chains had booked on the way here. But everything from that point down to
	 * R3RRespaceChainAfterEdit() changes *which end of the chain leads* -- the
	 * seam-direction fix reverses the merged-on part, the depot normaliser puts
	 * the door-side end in front, the respace slides the vehicles along the track.
	 * A reservation is bound to the leading end: the bits booked from the old
	 * leading end stay on the map, still answer to the chain (so no release path
	 * can call them ownerless), but the train will now drive away from them, and
	 * FreeTrainTrackReservation() -- which walks forward from the *new* moving
	 * front -- can never reach them either. That is the exact "owned but dead"
	 * shape of the ghost the player keeps seeing, and the only place it can be
	 * caught is after the geometry has settled, which is here. */
	R3RReleaseChainReservations(v, "couple-settled");

	/* The consist's schedule belongs to the wagon part: hand the orders over to the
	 * train. The locomotive's OWN orders are backed up on the locomotive itself
	 * (orders_backup) so a later decouple can restore them.
	 *
	 * R3R (identity hand-over invariant, R6): TryTrainCouple returns with the
	 * merged train head in v. A fold-fix logical flip of u migrates the consist's
	 * segment identity TOGETHER with its ★ marker: R3RFlipChainBySegments step 4
	 * calls R3RMoveSegmentOwner, which moves the segment front's orders pointer,
	 * order position, borrow flag and segment priority onto that segment's NEW
	 * front (KI-187). merged_first is exactly that new front — the merged-on
	 * group's geometric head — so it anchors the seam-direction test above and
	 * the ★ placement below; the handed-over schedule lives there as well and is
	 * read through `couple_owner` (the merged chain's lowest-priority segment),
	 * never through u. A fold-fix logical
	 * flip of v (multi-engine loco chain) may move the merged head off the
	 * original locomotive object: TryTrainCouple then migrates the train
	 * identity to the flipped chain's NEW head and updates v to point at it,
	 * so v is always the merged head holding the train identity here. */
	/* R3R (KI-128): settle any in-progress loading before this coupling rewrites
	 * the head identity and transplants the order list.
	 *
	 * The station-loading state describes the OLD train and cannot survive the
	 * merge:
	 *   - the CargoPayment is bound to the pre-merge head (PrepareUnload), and
	 *     after the merge only the new head's HandleLoading could ever release it,
	 *     so it would leak (the KI-69 helper handles this, including the
	 *     Station::loading_vehicles registration and the loading indicator);
	 *   - the fill-percent text effect keeps the loading bar parked at the
	 *     platform (the "ghost progress bar", KI-133);
	 *   - the platform-alignment flags (NotYetInPlatform / BeyondPlatformEnd /
	 *     AdvanceInPlatform) describe the old consist. A surviving
	 *     AdvanceInPlatform makes the very next station stop re-enter
	 *     OT_LOADING_ADVANCE immediately (Vehicle::HandleLoading ->
	 *     AdvanceLoadingInStation), which caps the speed at
	 *     through_load_speed_limit -- the "coupled train creeps out of the
	 *     station" symptom (KI-128).
	 *
	 * LeaveStation() only clears BeyondPlatformEnd / NotYetInPlatform
	 * (vehicle.cpp), so AdvanceInPlatform needs the explicit reset below; it is
	 * cleared across the whole merged chain because any car may carry it. */
	R3RSettleLoadingBeforeChainEdit(v, "couple");
	for (Train *w = v; w != nullptr; w = w->Next()) {
		w->flags.Reset(VehicleRailFlag::AdvanceInPlatform);
	}

	/* R3R (route A): merge the priorities first so the command owner is known
	 * (passive list first, then the active locomotive).
	 * R3R (KI-187): 被动/主动段列表按**合并后**的实际段头重采。被动侧 = 被并入的
	 * 车组部分(merged_first 起至链尾),主动侧 = 合并链上其余段头(集合差);折叠
	 * 修正搬迁段头身份后,只有重采才能指向真正持有排程的那辆车。 */
	const std::vector<Train *> passive_segs_now = R3RGetSegmentHeads(merged_first);
	std::vector<Train *> active_segs_now = R3RGetSegmentHeads(v);
	active_segs_now.erase(std::remove_if(active_segs_now.begin(), active_segs_now.end(),
			[&passive_segs_now](Train *s) {
				return std::find(passive_segs_now.begin(), passive_segs_now.end(), s) != passive_segs_now.end();
			}), active_segs_now.end());

	R3RMergePriorities(passive_segs_now, active_segs_now);
	R3RRenumberPriorities(v);
	Train *couple_owner = R3RGetLowestPriority(passive_segs_now);
	if (couple_owner == nullptr || couple_owner->orders == nullptr) couple_owner = merged_first;
	if (couple_owner == nullptr || couple_owner->orders == nullptr) couple_owner = u;

	/* R3R (KI-132 probe): the schedule hand-over below moves the command owner's
	 * order list onto the merged head. When the command owner belongs to another
	 * company, one company's schedule ends up on another company's train and its
	 * permission check (which only looks at the head's company) lets that company
	 * edit the foreign schedule. The hand-over itself is route A behaviour and is
	 * left untouched pending the owner's decision; this line only makes the
	 * cross-company case visible so the report can be tied to a real coupling.
	 * Read-only, one line per cross-company coupling. */
	if (couple_owner->owner != v->owner) {
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "COUPLE-XCOMPANY head=%d head_own=%d owner=%d owner_own=%d owner_ords=%d head_ords=%d borrows=%d\n",
					(int)v->index.base(), (int)v->owner.base(),
					(int)couple_owner->index.base(), (int)couple_owner->owner.base(),
					(int)couple_owner->GetNumOrders(), (int)v->GetNumOrders(),
					(int)(couple_owner->orders != nullptr));
			fclose(dbg);
		}
	}

	/* R3R (KI-187): 交接判据必须问"命令主人有没有表",而不是"挂车前的车组链头
	 * (u) 有没有表" —— 折叠修正可能已把 u 的段身份(含排程)搬到段内另一辆车,
	 * u 自身交空后这个判据会整块跳过交接,把车底的整份路线丢成孤儿。 */
	if (couple_owner->orders != nullptr) {
		/* R3R (route A): the merged head BORROWS the command owner's schedule
		 * instead of stealing it -- the owner keeps its own pointer (no
		 * "u->orders = nullptr"), so a later decouple can hand each part back
		 * its own schedule without a backup stack. */
		/* R3R (KI-215)：自己的排程只在【第一次】借用时寄存。一辆车在两次解挂之间
		 * 连挂第二次时本来就在借用状态（v->orders 是上一个主人的表，
		 * orders_backup 里才是自己那张），此时照旧寄存就会用别人的表覆盖掉自己的
		 * 排程 —— 之后解挂还原出来的是一张【不属于自己的表】：链头会停在那张表里
		 * 它从未拥有过的 WAIT_COUPLE 上再也不动（现场 2026-09-26 veh30：自己那份
		 * 3 条订单在第二次耦合被覆盖成他车排程，解挂后 LOCO real=0 正好落在那张表
		 * 的 OT_WAIT_COUPLE 上）。与 R3RSyncDrivingOrders 同一条规则：只有第一次
		 * 切换才寄存。连续两次耦合本身是允许的，第二次只是把驱动表换成新的主人
		 * 表，自己那张继续留在 orders_backup 里等解挂。
		 * R3R: preserve the locomotive's own order position so a later decouple
		 * can restore them. */
		if (!v->r3r_orders_borrowed) {
			v->orders_backup = v->orders;
			v->orders_backup_real_index = v->cur_real_order_index;
			v->orders_backup_implicit_index = v->cur_implicit_order_index;
		} else {
			/* KI-215 判据：第二次及以后的耦合必须出现这一行（第二次耦合不再
			 * 覆盖寄存），且 parked_real 是本车自己那张表里的位置。 */
			R3RDbgWrite("ORD-XFER keep veh=%d owner=%d cur_real=%d parked_real=%d\n",
					(int)v->index.base(), (int)couple_owner->index.base(),
					(int)v->cur_real_order_index, (int)v->orders_backup_real_index);
		}
		v->orders = couple_owner->orders;
		v->r3r_orders_borrowed = true;
		/* R3R: inherit the consist's unit number so the coupled
		 * train keeps the consist's identity (e.g. stays "Train 1"). The unit
		 * number backup is taken only when a number is actually inherited:
		 * DecoupleTrain swaps numbers only when v->unitnumber_backup != 0, so
		 * an unconditional backup would later hand the locomotive's OWN number
		 * to the decoupled part and give that same number back to v (duplicate
		 * unit numbers) whenever the consist carried no number of its own. */
		/* R3R (KI-147): a train that couples twice without a decouple in between
		 * still parks the number of its FIRST couple in unitnumber_backup. That
		 * parked number is a live pool reservation nobody will ever point at
		 * again once the backup is overwritten below -- release it, otherwise
		 * every re-couple leaks one more unit id (same leak that makes train
		 * numbers creep upwards). */
		if (v->unitnumber_backup != 0 && v->unitnumber_backup != v->unitnumber &&
				!v->R3RUnitNumberOwnedByOther()) {
			Company::Get(v->owner)->freeunits[VehicleType::Train].ReleaseID(v->unitnumber_backup);
		}
		v->unitnumber_backup = 0;
		if (u->unitnumber != 0) {
			/* R3R: the consist which lends its number keeps a copy of it, so a
			 * depot drag which splits it off again can take that number back
			 * instead of being handed a new one from the pool. */
			if (u->unitnumber_backup == 0) u->unitnumber_backup = u->unitnumber;
			v->unitnumber_backup = v->unitnumber;
			v->unitnumber = u->unitnumber;
			u->unitnumber = 0;
		}
		/* Inherit the consist's schedule position from the vehicle that was
		 * waiting to be coupled. The WAIT_COUPLE order currently being fulfilled
		 * is finished, so we continue from the order after it (skip below).
		 * Starting from the top of the schedule would replay earlier
		 * WAIT_COUPLE/DECOUPLE pairs and trigger decouples at the wrong stations.
		 * R3R (KI-204): take that position from the waiter's CHAIN HEAD (sampled in
		 * Couple() before the merge), not from couple_owner. Only a chain head is
		 * ticked, so its index is the table's real progress; couple_owner is just
		 * the segment that happens to hold the order-list pointer and its index is
		 * frozen at whatever it was the last time it was a chain head. Reading the
		 * owner's index stepped the merged train BACK one order -- right onto the
		 * WAIT_COUPLE the waiter had already banked -- so the waiter's next-to-last
		 * order was replayed and the train then parked on that WAIT_COUPLE for good
		 * ("the long schedule never steps past the wait order"). */
		v->cur_real_order_index = waiter_real_index;
		v->cur_implicit_order_index = waiter_implicit_index;
		if (v->GetNumOrders() == 0 || (uint)waiter_real_index >= v->GetNumOrders()) {
			/* The waiter's index does not fit the borrowed table (both sides were
			 * running different order lists); keep the old behaviour. */
			v->cur_real_order_index = couple_owner->cur_real_order_index;
			v->cur_implicit_order_index = couple_owner->cur_implicit_order_index;
		}
		v->DeleteUnreachedImplicitOrders();
		InvalidateVehicleOrder(v, 0);
		/* Drop the stale GOTO_COUPLE current order: ProcessOrders keeps it forever
		 * (GOTO_COUPLE never advances), which would leave the coupled train stuck.
		 * With a cleared current order the next ProcessOrders tick advances into
		 * the consist's schedule at the inherited position. */
		v->current_order.Free();
		v->SetDestTile(INVALID_TILE);

		/* R3R (KI-177): the hand-over just cleared the current order while the
		 * train is still standing at the rear half of the platform and has to
		 * roll on to its own (mid-platform) stop marker. On that stretch
		 * BeginLoading() takes its "we weren't scheduled to stop here" branch and
		 * inserts an OT_IMPLICIT "(自动)" order -- into v->orders, which at this
		 * moment IS the borrowed table of the consist, so the spurious order ends
		 * up in the consist's schedule and travels with it after a decouple.
		 * Suppress implicit order bookkeeping until the train reaches its next
		 * real order; the engine clears the bit itself in
		 * DeleteUnreachedImplicitOrders(), which runs on that next real arrival
		 * (a scheduled station, waypoint or depot), so normal mid-route recording
		 * resumes immediately afterwards. Must come AFTER the
		 * DeleteUnreachedImplicitOrders() call above, which clears the bit. */
		SetBit(v->GetGroundVehicleFlags(), GVF_SUPPRESS_IMPLICIT_ORDERS);

		v->UpdateRealOrderIndex();
		if (v->GetNumOrders() > 0 && v->GetOrder(v->cur_real_order_index)->IsType(OT_WAIT_COUPLE)) {
			v->IncrementRealOrderIndex();
			v->UpdateRealOrderIndex();
		}
		R3RCheckTtSync(v, "couple-waitcouple");
		/* Keep the timetable index in sync (IncrementRealOrderIndex does not
		 * update it; a mismatch crashes UpdateVehicleTimetable on station leave:
		 * "real_timetable_order == real_current_order"). */
		v->cur_timetable_order_index = v->cur_real_order_index;
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				const Order *co = (v->GetNumOrders() > 0) ? v->GetOrder(v->cur_real_order_index) : nullptr;
				fprintf(dbg, "COUPLE-OK loco=%d rear=%d consist=%d co=%d real=%d type=%d tx=%d ty=%d x=%d y=%d\n",
					(int)v->index.base(), (int)(v->Last()->index.base()),
					(int)u->index.base(), (int)R3RIsCarOnlyFormation(u),
					(int)v->cur_real_order_index, (int)(co ? co->GetType() : -1),
					(int)TileX(v->tile), (int)TileY(v->tile), (int)v->x_pos, (int)v->y_pos);
				for (const Train *w = v; w != nullptr; w = w->Next()) {
					/* R3R 1px probe: gap = actual centre distance - nominal
					 * (len_a + len_b)/2. Negative => the two boxes overlap. */
					const Train *nxt = w->Next();
					/* R3R (KI-214 probe fix, 2026-09-27): nominal is the engine's
					 * CalcNextVehicleOffset() (for odd lengths the leading vehicle
					 * rounds up), not the (la + lb) / 2 average -- the average
					 * reported gap=0 even for a real 1px overlap. nom0/nom1 print
					 * both rounding hypotheses for the record. */
					int gap = 0;
					int nom = 0;
					int nom0 = 0;
					int nom1 = 0;
					if (nxt != nullptr) {
						const int la = w->gcache.cached_veh_length;
						const int lb = nxt->gcache.cached_veh_length;
						nom0 = la / 2 + (lb + 1) / 2;
						nom1 = lb / 2 + (la + 1) / 2;
						const Train *const self = w->IsDrivingBackwards() ? nxt : w;
						nom = self->CalcNextVehicleOffset();
						gap = std::max(std::abs((int)w->x_pos - (int)nxt->x_pos), std::abs((int)w->y_pos - (int)nxt->y_pos)) - nom;
					}
					/* R3R (KI-214 probe, 2026-09-26)：玩家反复报的"图像偏移"无法只靠
					 * x/y/gap 判定 —— 那些只证明【物理间隔】在容差内(1px 重合都可能
					 * 参数合格)，完全不代表【画出来的盒子】和车一致。这里补上
					 * Train::UpdateDeltaXY() 由 direction/Flipped 推出的渲染包围盒
					 * (origin/extent/offset) 与它据以推导的那对方向量，于是"盒子不再
					 * 对齐车身"可以直接读出来。与本节其余 dump 同一频度(每次耦合一次)，
					 * 不经 R3RDbgEdge，不会被 128 帧窗口抑制。 */
					fprintf(dbg, "  CPL idx=%d x=%d y=%d tile=%d,%d dir=%d flip=%d db=%d trk=0x%X len=%d gap=%d nom=%d nom0=%d nom1=%d bO=%d,%d bE=%d,%d bF=%d,%d\n",
							(int)w->index.base(), (int)w->x_pos, (int)w->y_pos,
							(int)TileX(w->tile), (int)TileY(w->tile), (int)w->direction,
							w->flags.Test(VehicleRailFlag::Flipped) ? 1 : 0,
							w->IsDrivingBackwards() ? 1 : 0,
							(uint)w->track,
							(int)w->gcache.cached_veh_length, gap, nom, nom0, nom1,
							(int)w->bounds.origin.x, (int)w->bounds.origin.y,
							(int)w->bounds.extent.x, (int)w->bounds.extent.y,
							(int)w->bounds.offset.x, (int)w->bounds.offset.y);
				}
				fclose(dbg);
			}
		}
	}

	/* R3R (KI-128 probe): snapshot the order list right after the hand-over so a
	 * report of "a mystery order appeared after coupling / the train crawls out
	 * of the station" can be traced to the exact inherited order. One header line
	 * plus one line per order, per coupling -- not per tick. */
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "ORD-AFTER-COUPLE head=%d n=%d real=%d impl=%d tt=%d co=%d dest=%u borrowed=%d owner=%d u_has_orders=%d\n",
					(int)v->index.base(), (int)v->GetNumOrders(),
					(int)v->cur_real_order_index, (int)v->cur_implicit_order_index,
					(int)v->cur_timetable_order_index, (int)v->current_order.GetType(),
					(unsigned)v->dest_tile.base(), (int)v->r3r_orders_borrowed,
					(int)couple_owner->index.base(), (int)(u->orders != nullptr));
			for (uint i = 0; i < v->GetNumOrders(); i++) {
				const Order *o = v->GetOrder(i);
				if (o == nullptr) continue;
				fprintf(dbg, "  ORD idx=%u type=%d dest=%u\n",
						(uint)i, (int)o->GetType(), (unsigned)o->GetDestination().value);
			}
			fclose(dbg);
		}
	}

	/* R3R (KI-179): grant the merged train the one-shot "independent reservation"
	 * ability. The two halves were reserved separately before the coupling, so the
	 * joined train can end up with its front just past a signal it never reserved;
	 * this lets it make one reservation which ignores that signal, instead of being
	 * pushed into the force-proceed / permanently-stuck state. It is used up by the
	 * first successful reservation and kept while no reservation can be made (the
	 * train then takes the ordinary "waiting for free track" punishment). */
	v->SetForceReserveOnce();
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "R3R-RES-ONCE grant head=%d force_proceed=%d tile=%d,%d\n",
					(int)v->index.base(), (int)v->force_proceed, (int)TileX(v->tile), (int)TileY(v->tile));
			fclose(dbg);
		}
	}

	/* R3R (KI-199): "couple, then reserve". The hand-over right above discarded the
	 * current order and left loading the new one to the *next* ProcessOrders() run --
	 * but none of the places that ask for a path reservation (leaving a station,
	 * leaving a depot, the stuck retry, a tunnel/bridge exit) fires for a train that has
	 * just been joined and is standing still, so the merged consist never asked at all
	 * (player report 2026-09-24: "coupling succeeds and nothing is ever reserved").
	 * Queue one request here; TrainLocoHandler() issues it on the next tick, once
	 * ProcessOrders() has put the inherited travel order (and with it dest_tile) in
	 * place. */
	if (v->IsFrontEngine()) _r3r_couple_autoreserve.insert(v->index);

	/* R3R (KI-131 probe): report the merged chain's aggregate attributes and, per
	 * segment head, the values that segment reports. Train::ConsistChanged()
	 * stores the *consist-wide* totals in the gcache of every vehicle, so any
	 * code (or UI panel / GRF) which sums gcache over the segment heads of a
	 * multi-segment chain reports exactly n_segments times the real value -- the
	 * "every attribute is three times too big" report for a three-segment chain.
	 * The per-segment lines make that visible: when all of them carry the same
	 * power/weight/length as the head, the totals are chain-wide and summing them
	 * multiplies. Read-only, one header plus one line per segment, per coupling. */
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			uint n = 0, nseg = 0, nfe = 0;
			for (const Train *w = v; w != nullptr; w = w->Next()) {
				n++;
				if (w->IsSegmentFront()) nseg++;
				if (w->IsFrontEngine()) nfe++;
			}
			fprintf(dbg, "CHAIN-ATTRS head=%d n=%u FE=%u SEG=%u pow=%lld wt=%lld len=%d spd=%d\n",
					(int)v->index.base(), n, nfe, nseg,
					(long long)v->gcache.cached_power, (long long)v->gcache.cached_weight,
					(int)v->gcache.cached_total_length, (int)v->GetDisplayMaxSpeed());
			for (const Train *w = v; w != nullptr; w = w->Next()) {
				if (w != v && !w->IsSegmentFront()) continue;
				fprintf(dbg, "  SEG idx=%d pow=%lld wt=%lld len=%d spd=%d\n",
						(int)w->index.base(), (long long)w->gcache.cached_power,
						(long long)w->gcache.cached_weight,
						(int)w->gcache.cached_total_length, (int)w->GetDisplayMaxSpeed());
			}
			fclose(dbg);
		}
	}

	/* The formation front (zero-power locomotive identity) becomes a normal wagon again.
	 * R3R: remember that the coupled-on chain carried a formation identity BEFORE the
	 * fake engine is destroyed, so a pure wagon group can still be marked as a
	 * decouplable segment below. */
	bool u_was_caronly = R3RIsCarOnlyFormation(u);
	if (R3RIsCarOnlyFormation(u)) R3RDestroyCarOnlyFormation(u);

	/* We are now part of another train, remove all independent identity. */
	CloseWindowById(WindowClass::VehicleView, u->index);
	CloseWindowById(WindowClass::VehicleOrders, u->index);
	CloseWindowById(WindowClass::VehicleRefit, u->index);
	CloseWindowById(WindowClass::VehicleDetails, u->index);
	CloseWindowById(WindowClass::VehicleTimetable, u->index);
	SetWindowDirty(WindowClass::Company, _current_company);

	/* R3R（2026-09-22 玩家口径）：这里不再把被并入的段清零到 DEFAULT_GROUP —— 合并完成后
	 * 整链的「列车分组」与「挂接分组」统一到实际控制段（排程归属段）的分组，见本函数
	 * 下面 SetSegmentFront 之后的 R3RNormaliseChainGroups(v)。u 作为独立前端列的
	 * num_vehicle 仍要在下面摘掉（GroupStatistics::CountVehicle(u, -1)），
	 * 而且必须早于分组被改写。 */
	/* R3R (KI-147 rev.3): u 的车号只有在上面 orders 交接真正跑过、且 u->unitnumber
	 * 非 0 时才会被 v 接管(v->unitnumber = u->unitnumber; u->unitnumber = 0)。若没跑
	 * (u->orders == nullptr,即被挂上的链自身没有排程;或排程挂在同链别的段上,
	 * couple_owner != u),这里就是把一个还占着池位的车号直接抹掉 —— 每挂一次泄漏
	 * 一个号。u 紧接着要被剥掉 front 位,之后它的 PreDestructor 再也回收不了它。 */
	if (u->unitnumber != 0 && u->unitnumber != v->unitnumber) {
		/* R3R (第 144 轮 / 需求壹): 按玩家口径改成"冻结"——号停进
		 * unitnumber_backup 并**保持占用池位**，其它列车无法占用它，直到 u 这个段
		 * 被销毁时才由 PreDestructor 回收（那时 u 是该号唯一所有者，见
		 * Vehicle::R3RUnitNumberOwnedByOther()）。旧代码在这里 ReleaseID 把号直接
		 * 还回公司池，等于把编号让给了别的列车。 */
		if (u->unitnumber_backup == 0) u->unitnumber_backup = u->unitnumber;
		R3RDbgWrite("UNIT-FREEZE id=%u head_id=%u\n", u->unitnumber, v->unitnumber);
	}
	u->unitnumber = 0;
	GroupStatistics::CountVehicle(u, -1);

	v->profit_this_year += u->profit_this_year;
	v->profit_last_year += u->profit_last_year;
	u->profit_last_year = 0;
	u->profit_this_year = 0;

	u->ClearFreeWagon();
	u->ClearFrontEngine();
	u->ClearFrontWagon();

	NormaliseTrainHead(v);

	/* R3R: remember the coupled-on chain as a decouplable segment by marking
	 * its front vehicle. On plain splices merged_first == u (u kept its
	 * position as the first vehicle of the merged-on part); after a fold-fix
	 * logical flip merged_first is the reversed former tail, which is where
	 * the merged-on group now starts inside the train. Every chain that was
	 * coupled on is marked, whether it carried a real engine or was a
	 * car-only formation (its fake front was turned back into a wagon
	 * above): a trailing wagon group must remain identifiable by its
	 * SegmentFront so a later decouple/depot drag can split it off as a whole.
	 * TrainHasEngine is tested from merged_first so it scans the whole
	 * merged-on group (after a logical flip the fake engine head of the
	 * formation sits at the far end of the group). */
	if (u_was_caronly || TrainHasEngine(merged_first)) {
		merged_first->SetSegmentFront();
		/* R3R: 同时标出这个新段的右边界(合并进来的那一段的最后一辆车,止于下一个
		 * 段边界或链尾)。没有右边界标记的段是开放段,depot 里拖挂到它后面的车链
		 * 会被误当成段内容一起拖动。 */
		Train *seg_tail = merged_first;
		while (seg_tail->Next() != nullptr && !seg_tail->Next()->IsSegmentFront()) seg_tail = seg_tail->Next();
		seg_tail->SetSegmentBack();
	}

	/* R3R（2026-09-22 玩家口径）：合并完成后整链的「列车分组」与「挂接分组」收敛到
	 * 实际控制段（排程归属段）的分组。num_vehicle 以「前端列」为单位簿记：v 这一列
	 * 原本计入它旧分组，先摘出、归一化后再按新分组计入；被并入的 u 的旧分组已在
	 * 上面摘掉（若 v 旧分组 == 控制段分组，此处 -1/+1 相互抵消，只刷新统计）。
	 * 必须放在 SetSegmentFront/SetSegmentBack 之后：链内段边界此时才定型。 */
	GroupStatistics::CountVehicle(v, -1);
	R3RNormaliseChainGroups(v);
	GroupStatistics::CountVehicle(v, 1);

	/* R3R（2026-09-23 玩家口径）：「临时挂接分组」在挂接成功的这一刻销毁。
	 * 它只是"前去挂接"命令执行期间借来的身份，真正的合并已经完成、命令也已推进掉
	 * （IncrementImplicitOrderIndex/ProcessOrders 在上方），所以这里打点日志供现场
	 * 核对：临时分组不进入合并后的链，整链的挂接分组由上一步 R3RNormaliseChainGroups()
	 * 统一到控制段的真分组。 */
	if (coupler_used_fake_group != INVALID_COUPLE_GROUP) {
		const char *fake_name = R3RGetCoupleGroupName(coupler_used_fake_group);
		R3RDbgWrite("CGRP-FAKE-DESTROY head=%d loco=%d group=%d name=%s mask=0x%llx\n",
				(int)v->index.base(), (int)couple_loco_id.base(), (int)coupler_used_fake_group.base(),
				fake_name != nullptr ? fake_name : "-",
				(unsigned long long)R3RGetCoupleGroupsOfSegment(v));
	}

	InvalidateWindowClassesData(WindowClass::TrainList, 0);

	/* R3R (KI-150/153): an in-depot couple absorbed a chain into this one, so the
	 * absorbed head (its number is now 0 after the hand-over) must vanish from an
	 * open depot window right away, and the merged chain must be re-drawn with
	 * its new identity. Without this the depot list kept the stale "0" row and an
	 * unrefreshed sprite until an unrelated redraw happened to regenerate it. */
	R3RInvalidateDepotWindowsForChain(v);

	/* R3R: a locomotive that reached the coupling point driving backwards (it
	 * backed up nose-first to the consist) carries VehicleFlag::DrivingBackwards
	 * into the merged chain, making GetMovingFront() the far (wagon) end even
	 * though the direction-unify loop above set the whole chain to the
	 * locomotive's heading. Departure would then start from the wagon end and
	 * the train slides the wrong way until an automatic REVERSEDIR corrects it
	 * (observed: CRT-FOLD + CheckReverseTrain found=0 + stranding at the end of
	 * line). Undo the stale backing-up state here so the locomotive end leads.
	 * This mirrors ReverseTrainDirection's backup branch, but runs at the
	 * coupling point where the current order has just been freed: the full
	 * reversal machinery (path reservation etc.) must not fire yet, the next
	 * ProcessOrders tick drives departure with the corrected head. It touches
	 * no physical vehicle order, positions, directions, or images. */
	/* R3R (2026-09-22): 库内不做这步清 DB —— 车库里的带路端由出口几何决定
	 * （见 R3RNormaliseDepotMergeDirection），这里若把 DB 一律清零，就会把刚摆好的
	 * "靠门端领车"又推翻。非库场景保持原行为：由机车端领车。 */
	/* R3R DEBUG probe: report whether the coupled-on chain carried the stale
	 * backing-up state and whether a no-cab speed flag was lingering. */
	if (v->vehicle_flags.Test(VehicleFlag::DrivingBackwards) && !IsRailDepotTile(v->tile)) {
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "DB-CLEAR head=%d staleNocab=%d last=%d lastLead=%d\n",
						(int)v->index.base(),
						(int)((v->tcache.cached_tflags & TCF_NO_DRIVING_CAB) ? 1 : 0),
						(int)v->Last()->index.base(), (int)v->Last()->CanLeadTrain());
				fclose(dbg);
			}
		}
		for (Train *u = v; u != nullptr; u = u->Next()) {
			u->vehicle_flags.Reset(VehicleFlag::DrivingBackwards);

			/* Invert going up/down, as ReverseTrainDirection does. */
			if (HasBit(u->gv_flags, GVF_GOINGUP_BIT) || HasBit(u->gv_flags, GVF_GOINGDOWN_BIT)) {
				ToggleBit(u->gv_flags, GVF_GOINGDOWN_BIT);
				ToggleBit(u->gv_flags, GVF_GOINGUP_BIT);
			}
			UpdateStatusAfterSwap(u, false);
		}

		/* The no-cab speed flag was computed while DrivingBackwards was still
		 * set; recompute the cached train flags now that the chain drives
		 * forwards, mirroring the ConsistChanged(CCF_TRACK) that
		 * ReverseTrainDirection issues after flipping DrivingBackwards.
		 * Without this the 32 km/h no-driving-cab limit lingers on a
		 * forward-driving train until the next ConsistChanged. */
		v->ConsistChanged(CCF_TRACK);
	}

	if (CheckReverseTrain(v)) v->flags.Set(VehicleRailFlag::Reversing);
	else v->flags.Reset(VehicleRailFlag::Reversing);

	/* R3R (KI-148): the merge above dropped the realistic-braking lookahead
	 * (v->lookahead.reset()), which leaves the coupled train in exactly the state
	 * a train is in after forcing past a signal: Train::GetCurrentMaxSpeedInfoInternal()
	 * sees lookahead == nullptr while UsingRealisticBraking() and caps the train at
	 * an advisory 30 km/h. That cap only disappears when FillTrainReservationLookAhead()
	 * is called again, which happens from the signal/crossing checks -- i.e. not
	 * before the train reaches the NEXT signal. Rebuild the lookahead here instead,
	 * from the merged train's own moving front, so a freshly coupled train
	 * accelerates normally again. Rebuilding it (rather than keeping the stale
	 * pre-couple one) preserves the original intent: the follow-reservation walk
	 * starts at the loco itself instead of at the tile the loco came from. */
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		v->lookahead.reset();
		FillTrainReservationLookAhead(v);
	}

	/* R3R: force an immediate visual refresh of the whole merged chain, so the
	 * consist (which just became ordinary wagons again) repaints right away
	 * instead of keeping its old consist-front look until the next redraw. */
	v->MarkDirty();
}

/**
 * Does the coupler's GOTO_COUPLE order allow this candidate at all?
 *
 * R3R (KI-165): a GOTO_COUPLE order stores the station (or depot) the consist
 * waits at -- that is what "go to and couple" picks in the order window and what
 * the order list prints. A locomotive whose order says "couple at station 0"
 * must therefore not grab a waiting consist parked at station 2 (observed
 * 2026-09-22: veh=0 held exactly that order and coupled at station 2).
 *
 * The rule itself is enforced inside R3RCoupleAllowed() (couple_group.cpp), so
 * that the couple pathfinder's destination test, the back-walk safety test and
 * this gate all judge candidates identically. This local copy mirrors it and is
 * used for one thing only: choosing between the labels reject=dest-mismatch and
 * reject=group-mismatch in the probe line. Keep it in sync with that function.
 *
 * @param coupler The locomotive executing the GOTO_COUPLE order.
 * @param target  The candidate consist (its chain head).
 * @return True when the order permits this candidate, or the rule does not apply.
 */
static bool R3RCoupleTargetAtOrderStation(const Train *coupler, const Train *target)
{
	const Order &order = coupler->current_order;
	if (!order.IsType(OT_GOTO_COUPLE) || order.GetCoupleIsDepot()) return true;
	if (!IsRailStationTile(target->tile)) return true;
	return GetStationIndex(target->tile) == order.GetDestination().ToStationID();
}

/**
 * R3R (KI-60, couple gating): the hard conditions a coupling attempt must meet.
 *
 * Reported by the player after two illegitimate couplings in a depot: a
 * locomotive coupled onto a *stopped* waiting consist, and onto a same-group
 * consist that was not waiting for a coupling at all. A coupling is only
 * legitimate when all of the following hold:
 *
 *  1. the active side (the locomotive) is really executing GOTO_COUPLE;
 *  2. the passive side holds a WAIT_COUPLE order -- "car-only formation" is NOT
 *     a substitute any more (a segment that is not waiting must be left alone,
 *     even if it sits in the same couple group);
 *  3. neither side is stopped by the player (VehState::Stopped) -- both must be
 *     started, so a consist the player parked/stopped is never grabbed;
 *  4. the couple-group whitelist permits it, and -- since KI-165 -- the candidate
 *     stands where the coupler's GOTO_COUPLE order points (R3RCoupleAllowed,
 *     which the pathfinder and the back-walk safety test consult as well).
 *
 * Every target resolution in TrainCoupleHandler() goes through here, so an
 * illegitimate candidate is treated exactly like "no waiting consist at all":
 * the scan keeps looking for another candidate, and a locomotive without one
 * simply stays parked with its GOTO_COUPLE order (the ordinary COUPLE-FAIL
 * path) instead of coupling.
 *
 * @param coupler The locomotive executing the GOTO_COUPLE order.
 * @param target  The candidate consist (its chain head).
 * @param site    Name of the resolution path, used for the probe line.
 * @param why     Optional out-parameter: the rejection reason.
 * @return True when the two may couple.
 */
static bool R3RCanCoupleNow(const Train *coupler, const Train *target, const char *site, const char **why = nullptr)
{
	if (why != nullptr) *why = nullptr;
	if (coupler == nullptr || target == nullptr) return false;

	const char *reason = nullptr;
	if (!coupler->current_order.IsType(OT_GOTO_COUPLE)) {
		reason = "active-not-goto";
	} else if (!target->IsSegmentFront()) {
		reason = "target-not-segment";
	} else if (!target->current_order.IsType(OT_WAIT_COUPLE)) {
		reason = "target-not-wait";
	} else if (coupler->vehstatus.Test(VehState::Stopped)) {
		reason = "active-stopped";
	} else if (target->vehstatus.Test(VehState::Stopped)) {
		reason = "target-stopped";
	} else if (!R3RCouplePairMatches(coupler, target)) {
		/* R3R (KI-182): the pair flag is the outer gate -- without a matching
		 * "已有耦合目标" lock on both sides nothing may couple, whatever the
		 * groups and the destination say. */
		reason = "pair-mismatch";
	} else if (!R3RCoupleAllowedIgnoringPair(coupler, target)) {
		/* R3R (KI-165): the permit refuses two different things now -- the
		 * couple-group whitelist and a candidate waiting at a station the order
		 * does not name. Whether the coupling happens is decided by
		 * R3RCoupleAllowed() alone; this test only picks the label, so the two
		 * rejections stay distinguishable in the log. */
		reason = R3RCoupleTargetAtOrderStation(coupler, target) ? "group-mismatch" : "dest-mismatch";
	} else {
		return true;
	}

	if (why != nullptr) *why = reason;
	/* KI-14: edge-triggered -- this gate is re-evaluated every tick while a
	 * locomotive sits next to a candidate it is not allowed to couple onto. */
	const uint64_t g_key = ((uint64_t)coupler->index.base() << 32) ^ (uint64_t)target->index.base();
	const uint64_t g_payload = R3RDbgTagHash(reason) ^
			(R3RDbgTagHash(site != nullptr ? site : "-") << 24);
	if (R3RDbgEdge(R3REDGE_COUPLEGATE, g_key, g_payload)) {
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "[R3R] CPL-GATE reject=%s site=%s act=%d tgt=%d aOrd=%d tOrd=%d aStop=%d tStop=%d grp=%d\n",
					reason, site != nullptr ? site : "-",
					(int)coupler->index.base(), (int)target->index.base(),
					(int)coupler->current_order.GetType(), (int)target->current_order.GetType(),
					(int)coupler->vehstatus.Test(VehState::Stopped),
					(int)target->vehstatus.Test(VehState::Stopped),
					(int)R3RCoupleAllowed(coupler, target));
			fclose(dbg);
		}
	}
	return false;
}

/**
 * R3R (KI-182): keep the couple target lock -- "已有耦合目标" -- of a locomotive
 * which is executing a GOTO_COUPLE order up to date.
 *
 * The player's rule: from the moment the "go to and couple" order starts
 * running the locomotive must already know which consist it is going to couple
 * onto, and both sides mark that lock. Afterwards the couple pathfinder only
 * ever resolves that one consist -- R3RCoupleAllowed() requires the matching
 * pair, so no other waiting segment is even a candidate -- and the path found
 * and the coupling finally executed necessarily agree.
 *
 * This function is what establishes and maintains the lock. While the order
 * runs the chain holds exactly one waiting segment; if that segment stops
 * qualifying (it was coupled away, its order advanced, it entered a depot) the
 * lock is dropped and a new one is taken, so a locomotive never ends up locked
 * onto something it can no longer reach. Called once per tick from
 * TrainLocoHandler(); the flags are NOSAVE, and this is also what re-locks a
 * chain after a savegame round trip (where the two-sided check simply fails
 * once and the next scan re-pairs).
 *
 * @param v Any vehicle of the chain which runs the order.
 */
static void R3REnsureCouplePair(Train *v)
{
	if (v == nullptr) return;
	Train *coupler = Train::From(v->First());
	if (coupler == nullptr) return;

	/* Not executing a GOTO_COUPLE order any more: whatever lock this chain
	 * still holds is stale, and it must not keep a waiting consist blocked for
	 * every other locomotive. The priority stamp goes with it -- otherwise a
	 * chain which re-enters the order much later would still claim to have
	 * started first. */
	if (!coupler->current_order.IsType(OT_GOTO_COUPLE)) {
		R3RUnpairCoupleTargets(coupler);
		_r3r_pair_scan_tick.erase(coupler->index.base());
		_r3r_couple_enter_tick.erase(coupler->index.base());
		return;
	}

	/* R3R (KI-182, player rule 2026-09-24): stamp the tick this chain entered the
	 * order. Every later contention for a waiting consist is settled by this
	 * stamp, so "whoever started the couple first" wins. */
	if (R3RFindCoupleEnterStamp(coupler->index.base(), coupler) == nullptr) {
		_r3r_couple_enter_tick[coupler->index.base()] = R3RCoupleEnterStamp{ coupler, _tick_counter };
	}

	/* Still locked onto a consist which still qualifies: nothing to do. This is
	 * the steady state, so it must stay cheap -- no pool walk at all. */
	Train *partner = R3RGetCouplePairPartner(coupler);
	/* R3R (KI-188, 玩家规则 2026-09-24): the steady state only holds while the
	 * partner is a *different* chain -- a lock whose two halves ended up in one
	 * chain (a merge which did not drop the flags) is no lock at all. */
	if (partner != nullptr && partner->First() != coupler->First() && R3RIsCoupleTarget(partner) &&
			!partner->vehstatus.Test(VehState::Stopped) &&
			R3RCoupleAllowedIgnoringPair(coupler, partner)) {
		_r3r_pair_scan_tick.erase(coupler->index.base());
		return;
	}
	if (partner != nullptr) R3RUnpairCoupleTargets(coupler);

	/* (Re)select the nearest waiting consist which satisfies every ordinary
	 * condition and which no other locomotive has locked yet. A locomotive which
	 * repeatedly finds nothing retries a few ticks later instead of walking the
	 * whole vehicle pool every tick; the first attempt (the normal order start,
	 * and the in-depot couple) is never delayed. */
	static const uint64_t R3R_PAIR_RESCAN_TICKS = 8;
	auto scan_it = _r3r_pair_scan_tick.find(coupler->index.base());
	if (scan_it != _r3r_pair_scan_tick.end() && _tick_counter - scan_it->second < R3R_PAIR_RESCAN_TICKS) return;

	Train *best = nullptr;
	uint best_dist = 0;
	for (Train *t : Train::Iterate()) {
		if (t->index == coupler->index) continue;
		/* R3R (KI-188, 玩家规则 2026-09-24): only ever look for a *different* chain,
		 * and only at whole trains (the head of its own chain). A vehicle which is
		 * merely part of a chain can neither be coupled onto nor serve as a pair
		 * counterpart -- R3RPairCoupleTargets() normalises through First() and
		 * refuses a common head -- so it must never be picked as a candidate. That
		 * is exactly what locked a locomotive onto a chain whose middle segment
		 * still carried a WAIT_COUPLE marker, and produced the misleading
		 * "CPL-PAIR act=30 tgt=0 dist=0" lines. Should such a stale marker still
		 * exist (an old save, or a path which merges chains without going through
		 * Couple()), heal it here instead of advertising it for ever. */
		if (t->First() != t || t->First() == coupler->First()) {
			R3RClearStaleWaitMarker(t);
			continue;
		}
		if (!R3RIsCoupleTarget(t)) continue;
		if (t->vehstatus.Test(VehState::Stopped)) continue;
		/* Somebody else already locked this consist: first come, first served.
		 * "First" is the tick the order was entered, not the tick this scan ran,
		 * so a locomotive which is already on its way to a consist is only
		 * displaced by one which started its couple earlier. */
		Train *locker = R3RGetCouplePairPartner(t);
		if (locker != nullptr) {
			if (!R3RCouplePairOutranks(coupler, locker)) continue;
			const uint64_t s_key = ((uint64_t)coupler->index.base() << 32) ^ (uint64_t)t->index.base();
			if (R3RDbgEdge(R3REDGE_CPLPAIRSTEAL, s_key, (uint64_t)locker->index.base())) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "[R3R] CPL-PAIR-STEAL act=%d tgt=%d from=%d myEnter=%llu hisEnter=%llu\n",
							(int)coupler->index.base(), (int)t->index.base(), (int)locker->index.base(),
							(unsigned long long)R3RCoupleEnterTick(coupler),
							(unsigned long long)R3RCoupleEnterTick(locker));
					fclose(dbg);
				}
			}
		}
		if (!R3RCoupleAllowedIgnoringPair(coupler, t)) continue;

		const uint dist = DistanceManhattan(coupler->tile, t->tile);
		if (best == nullptr || dist < best_dist) {
			best = t;
			best_dist = dist;
		}
	}

	if (best == nullptr) {
		_r3r_pair_scan_tick[coupler->index.base()] = _tick_counter;
		return;
	}
	if (!R3RPairCoupleTargets(coupler, best)) {
		/* R3R (KI-188): the pairing layer refused the candidate (it normalises both
		 * sides to their chain heads and refuses a common head). Do not report a lock
		 * which was never taken -- that lie is what made the KI-188 log read as a
		 * self-lock -- and retry after the ordinary rescan delay. */
		_r3r_pair_scan_tick[coupler->index.base()] = _tick_counter;
		return;
	}
	_r3r_pair_scan_tick.erase(coupler->index.base());

	const uint64_t g_key = ((uint64_t)coupler->index.base() << 32) ^ (uint64_t)best->index.base();
	if (R3RDbgEdge(R3REDGE_CPLPAIR, g_key, (uint64_t)best_dist)) {
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "[R3R] CPL-PAIR act=%d tgt=%d dist=%u actTile=%d,%d tgtTile=%d,%d\n",
					(int)coupler->index.base(), (int)best->index.base(), best_dist,
					TileX(coupler->tile), TileY(coupler->tile),
					TileX(best->tile), TileY(best->tile));
			fclose(dbg);
		}
	}
}

/**
 * Get the position of the consist to couple onto, if the train has reached it.
 * @param v %Train executing a GOTO_COUPLE order.
 * @param reverse Whether the consist is facing the opposite direction.
 * @return The consist to couple onto, or nullptr if not in coupling position.
 */
static Train *GetCouplePosition(Train *v, bool &reverse)
{
	Vehicle *other_vehicle = nullptr;
	FollowTrainReservation(v, &other_vehicle);

	if (other_vehicle == nullptr) return nullptr;
	if (other_vehicle->First()->index == v->index) return nullptr;
	Train *u = Train::From(other_vehicle)->First();
	/* R3R (KI-60): the couple gate decides whether this candidate may be coupled
	 * onto at all -- active side in GOTO_COUPLE, passive side holding a
	 * WAIT_COUPLE order, both started, same couple group. A rejected candidate is
	 * reported as "no waiting consist here" (no geometric hit), so the locomotive
	 * keeps its GOTO_COUPLE order and retries, or looks for another candidate. */
	if (!R3RCanCoupleNow(v, u, "geo")) return nullptr;

	/* R3R (KI-106 fix 3): measure from the end of the loco chain that actually
	 * leads, and against the end of the waiting consist that is closest to it.
	 *
	 * A loco driving backwards (db=1) leads with its chain TAIL -- GetMovingFront()
	 * returns Last() in that state (vehicle_base.h). The old code measured from the
	 * chain head v regardless, so while reversing the nominal touch point lay a
	 * whole consist length away from the leading end and NO movement frame ever
	 * satisfied diff == need. The loco then kept rolling until
	 * CheckTrainCollision's overlap fallback (need - 1) fired and froze the merge
	 * with a constant 1 px overlap: the exact KI-106 symptom, on the reversing
	 * path fix 1 did not cover.
	 *
	 * Which end of u faces the loco follows from the geometry too: a reversing
	 * loco approaches u's HEAD even though u is still "same direction", so the old
	 * direction-only reverse flag (which forces u->Last()) picked the far end as
	 * well.
	 *
	 * Choosing the nearest end is a strict generalisation, not a behaviour change
	 * for the already working case: for a forward loco the near end of u is still
	 * u->Last() whenever the direction difference is Same/45 deg, so those
	 * couplings keep hitting the exact pixel as before. */
	Train *v_near = v->GetMovingFront();
	const auto end_dist = [](const Vehicle *a, const Vehicle *b) {
		return std::max(std::abs(a->x_pos - b->x_pos), std::abs(a->y_pos - b->y_pos));
	};
	const bool z_is_tail = end_dist(v_near, u->Last()) < end_dist(v_near, u);
	Train *z = z_is_tail ? u->Last() : u;
	reverse = z_is_tail;

	int x_diff = std::abs(v_near->x_pos - z->x_pos);
	int y_diff = std::abs(v_near->y_pos - z->y_pos);

	int diff = std::max(x_diff, y_diff);

	uint8_t v_length = v_near->gcache.cached_veh_length;
	uint8_t u_length = z->gcache.cached_veh_length;

	if (diff == ((v_length + 1) / 2 + (u_length + 1) / 2)) {
		/* R3R 1px probe: this is the *exact* end-to-end path -- the loco stopped
		 * on the nominal pixel, so the coupled chain has no pixel overlap. Logged
		 * once per (loco, consist, lengths) so it does not flood the file. */
		const uint64_t g_key = ((uint64_t)v->index.base() << 32) ^ (uint64_t)u->index.base();
		const uint64_t g_payload = ((uint64_t)v_length << 32) ^ (uint64_t)u_length ^ ((uint64_t)(reverse ? 1u : 0u) << 16);
		if (R3RDbgEdge(R3REDGE_CPLGEO, g_key, g_payload)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "[R3R] CPL-GEO site=%s v=%d vn=%d vlen=%d u=%d z=%d ulen=%d rev=%d dx=%d dy=%d diff=%d need=%d spd=%d db=%d\n",
						"geo", (int)v->index.base(), (int)v_near->index.base(), (int)v_length,
						(int)u->index.base(), (int)z->index.base(),
						(int)u_length, (int)reverse, x_diff, y_diff, diff,
						(int)((v_length + 1) / 2 + (u_length + 1) / 2),
						(int)v->cur_speed, (int)v->IsDrivingBackwards());
				fclose(dbg);
			}
		}
		return u;
	}

	return nullptr;
}

/* R3R (KI-193, revised in round 112, 2026-09-24): "a GOTO_COUPLE locomotive whose
 * destination holds no waiting consist WAITS IN PLACE".
 *
 * Player rule (2026-09-24, supersedes the earlier "treat the order as COMPLETED"
 * decision of the same day): the order must NOT be advanced. Skipping a GOTO_COUPLE
 * order silently gives up the rendezvous, which is the whole point of the order --
 * the loco has to be there when the consist finally shows up. A loco that has arrived
 * at its destination without a candidate therefore stops and waits, exactly like it
 * waits for a consist that is still shunting towards the platform.
 *
 * Two things still have to be handled while it waits:
 *  - the map-wide candidate search (GetCouplePosition() + the 3x3 touch scan + the
 *    depot scan, plus TryTrainCouple) must not run on every tick, otherwise the parked
 *    loco burns the train tick and keeps drifting along the platform. Round 109
 *    evidence: veh=24 stood at 21,9 (order type=16 pointing at 25,9) with 27 CPL-ENTRY
 *    attempts, 7 COUPLE-FAILs and ZERO CPL-BEST/CPL-RESERVE, while veh=30 wandered
 *    60,30 -> 58,36 -> 59,17 -> 58,38 hunting a schedule point that had already been
 *    taken away. The wait gate (right after the rolling check in TrainCoupleHandler)
 *    therefore lets the search through only once every R3R_COUPLE_DEST_IDLE_LIMIT
 *    ticks and returns immediately on all other ticks;
 *  - the stale pair lock (R3RUnpairCoupleTargets()) is still dropped on each search
 *    pass, so a legitimate target that shows up later is not blocked by one that was
 *    taken away by somebody else in the meantime.
 *
 * COUPLE-DEST-EMPTY is therefore no longer an "order completed" marker but the
 * heartbeat of a loco waiting in place; it is emitted at most once per wait window
 * (i.e. once per R3R_COUPLE_DEST_IDLE_LIMIT ticks). */
static const uint32_t R3R_COUPLE_DEST_IDLE_LIMIT = 500; ///< ~15 s at normal game speed.

/** R3R (KI-193): per-locomotive counter of ticks spent "at the GOTO_COUPLE
 *  destination with nothing to couple onto". Keyed by VehicleID; a stale entry for a
 *  deleted vehicle only wastes a few bytes and is dropped as soon as a locomotive with
 *  the same index passes through this handler. */
static btree::btree_map<VehicleID, uint32_t> _r3r_couple_dest_idle;

/** R3R (KI-193 fallback, round 114): per-locomotive episode of "a qualifying coupling
 *  candidate is standing right here, yet the merge never commits". Only used for a loco
 *  that has arrived at its GOTO_COUPLE destination, where a coupling attempt happens at
 *  most once per parked-wait window (R3R_COUPLE_DEST_IDLE_LIMIT ticks). Keyed by
 *  VehicleID; a stale entry for a deleted vehicle only wastes a few bytes and is dropped
 *  as soon as a locomotive with the same index passes through this handler. */
struct R3RCoupleCommitFailEpisode {
	VehicleID candidate = VehicleID::Invalid(); ///< Candidate the failed attempts belong to.
	uint32_t failures = 0;                      ///< Failed Couple() attempts of this episode.
};
static btree::btree_map<VehicleID, R3RCoupleCommitFailEpisode> _r3r_couple_commit_fail;

/** R3R (KI-193 fallback, round 114): how many failed coupling attempts -- each one a
 *  parked-wait window apart, i.e. R3R_COUPLE_DEST_IDLE_LIMIT ticks at minimum -- are
 *  tolerated before the loco gives up the rendezvous and skips the order. Four windows
 *  are about a minute of game time at normal speed: long enough that an ordinary
 *  coupling is never abandoned, short enough to break a fold-correction deadlock or a
 *  persistently vetoed attach instead of retrying for ever. */
static const uint32_t R3R_COUPLE_COMMIT_FAIL_LIMIT = 4;

/**
 * R3R (KI-193): is this locomotive standing on the destination of its running
 * GOTO_COUPLE order?
 *
 * Stations are matched by STATION id, not by tile: the order stores the platform's
 * reference tile while a platform is several tiles long and the consist parks anywhere
 * along it, so an exact tile comparison leaves the loco permanently "one tile short"
 * (see the DEPOT-ARR tileEqDest=0 probe on the very same log).
 * @param v %Train executing the order.
 * @return True if the head of the consist has reached the order destination.
 */
static bool R3RCoupleOrderDestinationReached(const Train *v)
{
	const Order &order = v->current_order;
	if (!order.IsType(OT_GOTO_COUPLE)) return false;
	const Train *front = v->GetMovingFront();
	const TileIndex tile = (front != nullptr) ? front->tile : v->tile;
	if (order.GetCoupleIsDepot()) {
		return IsRailDepotTile(tile) && GetDepotIndex(tile) == order.GetDestination().ToDepotID();
	}
	return HasStationTileRail(tile) && GetStationIndex(tile) == order.GetDestination().ToStationID();
}

/**
 * Handle a GOTO_COUPLE order: try to couple onto the waiting consist.
 * @param v %Train executing the order.
 * @return True if a coupling attempt was made.
 */
static bool TrainCoupleHandler(Train *v)
{
	R3RScopeTimer r3r_ch_timer(&r3r_ch_ns, &r3r_ch_calls);
	if (!v->current_order.IsType(OT_GOTO_COUPLE)) {
		/* R3R: no longer executing a GOTO_COUPLE order (player skipped it / the
		 * schedule moved on) — any couple-failure news throttle episode is over;
		 * allow a fresh failure to report again. */
		_r3r_couple_fail_news_shown.erase(v->index);
		/* R3R (KI-193): the GOTO_COUPLE order is gone (coupled / skipped), so any
		 * "parked at the destination without a candidate" episode is over. */
		_r3r_couple_dest_idle.erase(v->index);
		/* R3R (KI-193 fallback): likewise for the "a candidate is there but the merge
		 * never commits" episode. */
		_r3r_couple_commit_fail.erase(v->index);
		return false;
	}
	/* R3R (KI-106 fix 1): the exact-geometry path must also run while the loco is
	 * ROLLING. The movement loop in TrainLocoHandler advances exactly one map
	 * pixel per iteration and runs the couple check *after* moving, so
	 * diff = max(|dx|,|dy|) passes through `need` on some frame; skipping that
	 * frame used to let CheckTrainCollision's min_diff = need - 1 take over one
	 * pixel later and freeze the loco with a constant 1 px overlap.
	 * The early-out is kept for the depot branch only: it has NO distance
	 * criterion (it couples onto any gate-approved consist on the tile), so it
	 * must not run while rolling. */
	const bool r3r_rolling = v->cur_speed != 0;
	if (r3r_rolling) {
		/* R3R: the locomotive is moving again, so a previous continuous failure
		 * episode (if any) has ended; clear the throttle so a later stationary
		 * attempt that fails again can report once more. */
		_r3r_couple_fail_news_shown.erase(v->index);
		if (v->track == TRACK_BIT_DEPOT) return false;
	}

	/* R3R (KI-193 revised, round 112): parked-wait gate. A standstill loco that already
	 * sits on the destination of its GOTO_COUPLE order waits there; only one tick per
	 * R3R_COUPLE_DEST_IDLE_LIMIT is allowed to run the candidate search below (and to
	 * reach the wait heartbeat in the u == nullptr branch). Every other tick drops out
	 * right here -- that is what stops a waiting loco from scanning the whole map, and
	 * from drifting along the platform, on every single tick. */
	if (!r3r_rolling && R3RCoupleOrderDestinationReached(v)) {
		uint32_t &idle = _r3r_couple_dest_idle[v->index];
		if (++idle < R3R_COUPLE_DEST_IDLE_LIMIT) return false;
		idle = 0;
	}

	bool reverse = false;
	Train *u = nullptr;
	if (v->track == TRACK_BIT_DEPOT) {
		/* In a depot: scan the depot tile AND the entrance track tile for a waiting
		 * consist (free wagon or independent consist). The consist often waits just
		 * outside the depot mouth on the approach track. */
		TileIndex tiles[2] = { v->tile, v->tile + TileOffsByDiagDir(GetRailDepotDirection(v->tile)) };
		for (TileIndex tile : tiles) {
			Vehicle *first = GetFirstVehicleOnTile(tile, VehicleType::Train);
			for (Train *w = first != nullptr ? Train::From(first) : nullptr; w != nullptr; w = w->HashTileNext()) {
				if (w->First()->index == v->index) continue; // Skip self.
				/* R3R (D6-①): the old "same owner" filter is gone. A candidate of
				 * another company is admitted -- or refused -- by the couple gate
				 * below (R3RCanCoupleNow -> R3RCoupleAllowed), which demands a
				 * shared couple group with the cross-company opt-in switched on.
				 * Anything else is treated exactly like "no waiting consist on this
				 * tile", so the scan keeps looking and nothing starts to spin. */
				/* R3R: a target here (as everywhere else) must be an actual
				 * waiting consist. The depot branch and the open-track branch are
				 * aligned on the same "waiting to be coupled" semantics -- see
				 * R3RCanCoupleNow: WAIT_COUPLE order held, started (not stopped by
				 * the player), same couple group. A candidate that fails the gate
				 * is not a target, so the scan keeps looking; a locomotive without
				 * any qualifying candidate simply stays parked and retries (the
				 * ordinary COUPLE-FAIL path). */
				if (R3RCanCoupleNow(v, w->First(), "depot")) {
					u = w->First();
					break;
				}
			}
			if (u != nullptr) break;
		}
	} else {
		u = GetCouplePosition(v, reverse);
		/* R3R (KI-106 fix 1): the 9-tile touch/overlap scan below has only a loose
		 * upper bound (<= half-length sum), so running it while rolling would
		 * couple at an arbitrary distance short of the exact pixel -- the exact
		 * opposite of the fix. It therefore stays a standstill-only fallback. */
		if (u == nullptr && !r3r_rolling) {
			/* R3R: also couple when the loco is touching/overlapping the waiting
			 * consist, or parked at the exact end-to-end distance that
			 * CheckTrainCollision's 8px hash gate misses. GetCouplePosition only
			 * fires at the precise distance, and a stationary loco parked at/near
			 * the consist (visual bounding boxes overlapping, but logical centre
			 * distance >8px) would otherwise never couple. Scan the loco's tile
			 * and its 8 neighbours for a waiting consist within coupling range
			 * (centre distance <= half-length sum, covering both overlap and the
			 * exact对接点). Only reached for GOTO_COUPLE locos, so ordinary trains
			 * are unaffected. */
			uint8_t v_length = v->gcache.cached_veh_length;
			for (int8_t dx = -1; dx <= 1; dx++) {
				for (int8_t dy = -1; dy <= 1; dy++) {
					TileIndex t = TileAddWrap(v->tile, dx, dy);
					for (Vehicle *fv = GetFirstVehicleOnTile(t, VehicleType::Train); fv != nullptr;
							fv = Train::From(fv)->HashTileNext()) {
						Train *w = Train::From(fv);
						if (w->First()->index == v->index) continue; // Skip self.
						/* R3R (D6-①): no owner filter -- see the depot branch above;
						 * the couple gate decides, and a refused candidate is just
						 * "nothing here". */
						/* R3R (KI-60): same gate as the geometry and depot paths. */
						if (!R3RCanCoupleNow(v, w->First(), "scan")) continue;
						Train *z = w->First();
						int x_diff = abs(v->x_pos - z->x_pos);
						int y_diff = abs(v->y_pos - z->y_pos);
						if (std::max(x_diff, y_diff) <= (v_length + 1) / 2 + (z->gcache.cached_veh_length + 1) / 2) {
							u = z;
							break;
						}
					}
					if (u != nullptr) break;
				}
				if (u != nullptr) break;
			}
		}
	}
	if (u == nullptr) {
		if (r3r_rolling) {
			/* R3R (KI-106 fix 1): for a rolling loco "not on the exact pixel yet" is
			 * the normal state of almost every movement step -- not a failure, and
			 * not worth the edge-probe hash either. */
			return false;
		}
		/* KI-14 (2): edge-triggered. This branch is reached on every tick (and on
		 * every movement step, see the loop at the end of TrainLocoHandler) while
		 * a GOTO_COUPLE loco has no reachable target, so the two raw
		 * fopen/fprintf/fclose blocks below used to run continuously and dominate
		 * the train tick. Compute the failure signature first and only touch the
		 * log when it changes (a stuck loco logs once per perf window). */
		const uint64_t cf_key = R3RDbgTagHash("COUPLE-FAIL") | ((uint64_t)v->index.base() << 32);
		const uint64_t cf_payload = ((uint64_t)v->tile.base() << 32) ^
				((uint64_t)v->current_order.GetType() << 48);
		if (R3RDbgEdge(R3REDGE_COUPLEFAIL, cf_key, cf_payload)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "COUPLE-FAIL loco=%d order=%d tx=%d ty=%d x=%d y=%d\n",
					(int)v->index.base(), (int)v->current_order.GetType(),
					(int)TileX(v->tile), (int)TileY(v->tile), (int)v->x_pos, (int)v->y_pos);
				if (v->orders != nullptr) {
					for (VehicleOrderID i = 0; i < v->GetNumOrders(); ++i) {
						const Order *o = v->GetOrder(i);
						fprintf(dbg, "  LOCO-ORD %d type=%d\n", (int)i, (int)(o ? o->GetType() : -1));
					}
				}
				fclose(dbg);
			}
		}

		/* R3R (KI-193 revised, round 112): standstill on the GOTO_COUPLE destination with
		 * nothing to couple onto -- WAIT IN PLACE. The order is deliberately NOT advanced
		 * (player rule: "no consist to couple onto => wait where you are"); skipping it
		 * would silently give up the rendezvous. This branch is only reached once per wait
		 * window (see the parked-wait gate above), so it carries the heartbeat and the
		 * stale pair-lock cleanup. */
		if (R3RCoupleOrderDestinationReached(v)) {
			_r3r_couple_fail_news_shown.erase(v->index);
			/* The pair lock may point at a consist that is not there any more; keeping it
			 * would block the next, legitimate coupling target that shows up later. */
			R3RUnpairCoupleTargets(v);

			const VehicleOrderID r3r_num_orders = v->GetNumOrders();
			const VehicleOrderID r3r_next = (r3r_num_orders > 0)
					? (VehicleOrderID)((v->cur_real_order_index + 1) % r3r_num_orders)
					: INVALID_VEH_ORDER_ID;

			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "COUPLE-DEST-EMPTY loco=%d tile=%d,%d real=%d num=%d next=%d (waiting in place, order kept)\n",
						(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
						(int)v->cur_real_order_index, (int)r3r_num_orders, (int)r3r_next);
				fclose(dbg);
			}
			return false;
		} else {
			_r3r_couple_dest_idle.erase(v->index);
		}
		return false;
	}

	/* R6: only couple onto a consist that reaches the minimum programmable score.
	 * TEMPORARILY DISABLED (2026-08-12): the built-in ruleset gives a consist
	 * (1 km/h zero-power locomotive) a score of -20, which is always below the
	 * minimum of 0, so coupling was always rejected. Re-enable once the scoring
	 * rules are reworked (e.g. exempt consists from speed matching). */
	//if (EvaluateCoupleScore(u) < _couple_min_score) {
	//	/* Stop behind the candidate and wait; do not couple onto a low-scoring consist. */
	//	v->cur_speed = 0;
	//	return false;
	//}

	Couple(v, u);
	if (r3r_rolling) {
		/* R3R (KI-106 fix 1) probe: the couple was triggered from the movement loop
		 * (spd != 0) instead of from standstill. merged tells whether the merge
		 * actually committed -- a fold-fix rollback leaves the two chains apart and
		 * the loco is stopped on the exact pixel, retrying from standstill. */
		const uint64_t gm_key = ((uint64_t)v->index.base() << 32) ^ (uint64_t)u->index.base();
		const uint64_t gm_payload = (uint64_t)v->tile.base();
		if (R3RDbgEdge(R3REDGE_CPLGEOCOMMIT, gm_key, gm_payload)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "[R3R] CPL-GEO-DONE v=%d u=%d merged=%d spd=%d x=%d y=%d\n",
						(int)v->index.base(), (int)u->index.base(),
						(int)(u->First() == v->First()),
						(int)v->cur_speed, (int)v->x_pos, (int)v->y_pos);
				fclose(dbg);
			}
		}
	}

	/* R3R (KI-193 fallback, round 114): a rendezvous which cannot be committed.
	 * Reaching this point means a qualifying waiting consist was found right here
	 * (R3RCanCoupleNow() passed) and Couple() ran -- this is NOT the "no consist wants
	 * to couple with me" case, which waits in place for ever (see the u == nullptr
	 * branch). It is "the partner is standing there, but the merge does not happen":
	 * a fold-correction rollback, a NewGRF "can attach wagon" veto, an articulation
	 * deadlock, ... Retrying that for ever leaves the loco parked on the platform and
	 * freezes the whole schedule behind it, so after R3R_COUPLE_COMMIT_FAIL_LIMIT
	 * failed attempts give up the rendezvous and advance the order, exactly as if the
	 * player had skipped it in the orders window. That is a safety valve for a broken
	 * coupling, not the normal way of giving up a rendezvous -- an ordinary coupling
	 * never gets near the limit.
	 * Counted only for a stationary loco standing ON its destination: a loco that is
	 * still shunting towards it has not had its rendezvous yet and must keep going. */
	if (u->First() == v->First()) {
		/* Committed -- the episode (if any) is over. */
		_r3r_couple_commit_fail.erase(v->index);
	} else if (!r3r_rolling && R3RCoupleOrderDestinationReached(v)) {
		R3RCoupleCommitFailEpisode &ep = _r3r_couple_commit_fail[v->index];
		if (ep.candidate != u->index) {
			/* A different partner is a different rendezvous: start counting afresh. */
			ep.candidate = u->index;
			ep.failures = 0;
		}
		++ep.failures;
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "COUPLE-COMMIT-FAIL loco=%d cand=%d tile=%d,%d real=%d tries=%u\n",
						(int)v->index.base(), (int)u->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
						(int)v->cur_real_order_index, (unsigned)ep.failures);
				fclose(dbg);
			}
		}

		if (ep.failures >= R3R_COUPLE_COMMIT_FAIL_LIMIT) {
			_r3r_couple_commit_fail.erase(v->index);
			/* Drop the pair lock first: it must not block the next rendezvous. */
			R3RUnpairCoupleTargets(v);

			const VehicleOrderID r3r_num_orders = v->GetNumOrders();
			const VehicleOrderID r3r_next = (r3r_num_orders >= 2)
					? (VehicleOrderID)((v->cur_real_order_index + 1) % r3r_num_orders)
					: INVALID_VEH_ORDER_ID;

			{
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "COUPLE-SKIP-COMMIT-FAIL loco=%d cand=%d tile=%d,%d real=%d num=%d next=%d\n",
							(int)v->index.base(), (int)u->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
							(int)v->cur_real_order_index, (int)r3r_num_orders, (int)r3r_next);
					fclose(dbg);
				}
			}

			if (r3r_next != INVALID_VEH_ORDER_ID) {
				/* Same bookkeeping as CmdSkipToOrder(): leave any loading/waiting state,
				 * reset the beyond-platform-end marks, select the next order and discard
				 * the stale current order so the ordinary ProcessOrders() of the next
				 * tick loads the newly selected one. */
				if (v->current_order.IsAnyLoadingType()) v->LeaveStation();
				if (v->current_order.IsType(OT_WAITING)) v->HandleWaiting(true);
				for (Train *w = v; w != nullptr; w = w->Next()) w->flags.Reset(VehicleRailFlag::BeyondPlatformEnd);

				v->cur_implicit_order_index = v->cur_real_order_index = r3r_next;
				v->UpdateRealOrderIndex();
				v->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
				v->current_order.Free();
				v->SetDestTile(INVALID_TILE);
				v->ResetDepotUnbunching();
				InvalidateVehicleOrder(v, 0);
				v->StopSeparation();
			}
			return false;
		}
	}
	return true;
}

/**
 * Try to find a depot nearby.
 * @param v %Train that wants a depot.
 * @param max_distance Maximal search distance.
 * @return Information where the closest train depot is located.
 * @pre The given vehicle must not be crashed!
 */
static FindDepotData FindClosestTrainDepot(const Train *v, int max_distance)
{
	assert(!v->vehstatus.Test(VehState::Crashed));

	if (v->lookahead != nullptr && !ValidateLookAhead(v)) return FindDepotData();

	return YapfTrainFindNearestDepot(v, max_distance);
}

ClosestDepot Train::FindClosestDepot() const
{
	FindDepotData tfdd = FindClosestTrainDepot(this, 0);
	if (tfdd.best_length == UINT_MAX) return ClosestDepot();

	return ClosestDepot(tfdd.tile, GetDepotIndex(tfdd.tile), tfdd.reverse);
}

void Train::PlayLeaveStationSound(bool force) const
{
	static const SoundFx sfx[] = {
		SND_04_DEPARTURE_STEAM,
		SND_0A_DEPARTURE_TRAIN,
		SND_0A_DEPARTURE_TRAIN,
		SND_47_DEPARTURE_MONORAIL,
		SND_41_DEPARTURE_MAGLEV
	};

	if (PlayVehicleSound(this, VSE_START, force)) return;

	SndPlayVehicleFx(sfx[to_underlying(RailVehInfo(this->engine_type)->engclass)], this);
}

/**
 * Check if the train is on the last reserved tile and try to extend the path then.
 * @param moving_front MOving front of train that needs its path extended.
 */
static void CheckNextTrainTile(Train *moving_front)
{
	/* Don't do any look-ahead if path_backoff_interval is 255. */
	if (_settings_game.pf.path_backoff_interval == 255) return;

	/* Exit if we are inside a depot. */
	if (moving_front->track == TRACK_BIT_DEPOT) return;

	Train *consist = moving_front->First();

	/* Exit if we are currently in a waiting order */
	if (consist->current_order.IsType(OT_WAITING)) return;

	/* Exit if we are on a station tile and are going to stop. */
	if (HasStationTileRail(moving_front->tile) && consist->current_order.ShouldStopAtStation(consist, GetStationIndex(moving_front->tile), IsRailWaypoint(moving_front->tile))) return;

	switch (consist->current_order.GetType()) {
		/* Exit if we reached our destination depot. */
		case OT_GOTO_DEPOT:
			if (moving_front->tile == consist->dest_tile) return;
			break;

		case OT_GOTO_WAYPOINT:
			/* If we reached our waypoint, make sure we see that. */
			if (IsRailWaypointTile(moving_front->tile) && GetStationIndex(moving_front->tile) == consist->current_order.GetDestination()) ProcessOrders(consist);
			break;

		case OT_NOTHING:
		case OT_LEAVESTATION:
		case OT_LOADING:
			/* Exit if the current order doesn't have a destination, but the train has orders. */
			if (consist->GetNumOrders() > 0) return;
			break;

		default:
			break;
	}

	Trackdir td = moving_front->GetVehicleTrackdir();

	/* On a tile with a red non-pbs signal, don't look ahead. */
	if (HasBlockSignalOnTrackdir(moving_front->tile, td) && GetSignalStateByTrackdir(moving_front->tile, td) == SignalState::Red) return;

	CFollowTrackRail ft(consist);
	if (!ft.Follow(moving_front->tile, td)) return;

	if (!HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(ft.new_td_bits))) {
		/* Next tile is not reserved. */
		if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
			Trackdir td = FindFirstTrackdir(ft.new_td_bits);
			if (HasPbsSignalOnTrackdir(ft.new_tile, td) && !IsNoEntrySignal(ft.new_tile, TrackdirToTrack(td))) {
				/* If the next tile is a PBS signal, try to make a reservation. */
				TrackBits tracks = TrackdirBitsToTrackBits(ft.new_td_bits);
				if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td, _settings_game.pf.forbid_90_deg)) {
					tracks &= ~TrackCrossesTracks(TrackdirToTrack(ft.old_td));
				}
				ChooseTrainTrack(consist, ft.new_tile, ft.exitdir, tracks, CTTF_NONE);
			}
		}
	} else if (consist->lookahead != nullptr && consist->lookahead->reservation_end_tile == ft.new_tile && IsTileType(ft.new_tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationEntrance(ft.new_tile) &&
			consist->lookahead->reservation_end_trackdir == FindFirstTrackdir(ft.new_td_bits)) {
		/* If the lookahead ends at the next tile which is a signalled tunnel/bridge entrance, try to make a reservation. */
		TryLongReserveChooseTrainTrackFromReservationEnd(consist);
	}
}

/**
 * Will the train stay in the depot the next tick?
 * @param v %Train to check.
 * @return True if it stays in the depot, false otherwise.
 */
static bool CheckTrainStayInDepot(Train *v)
{
	/* R3R: a GOTO_COUPLE locomotive stays inside its target depot ONLY when it
	 * has actually reached that depot (GOTO_COUPLE has no arrival handling so
	 * without this it would drive right back out). A locomotive that is merely
	 * sitting in its HOME depot while holding a GOTO_COUPLE order for ANOTHER
	 * depot must still leave and pathfind there.
	 * IMPORTANT: only call GetDepotIndex(v->tile) when v->tile is actually a
	 * depot tile — it asserts otherwise, which crashes when CheckTrainStayInDepot
	 * is called for a consist that decoupled on a station platform. */
	if (v->current_order.IsType(OT_GOTO_COUPLE) && v->current_order.GetCoupleIsDepot() &&
			IsRailDepotTile(v->tile) &&
			v->current_order.GetDestination().ToDepotID() == GetDepotIndex(v->tile)) return true;

	/* bail out if not all wagons are in the same depot or not in a depot at all */
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		if (u->track != TRACK_BIT_DEPOT || u->tile != v->tile) return false;
	}

	/* if the train got no power, then keep it in the depot
	 * (R3R: except a car-only formation — its front keeps the real wagon's
	 * engine_type with 0 power; without this exemption it would be auto-stopped
	 * and could never be coupled onto. This is the "1 hp minimum" equivalent.) */
	if (v->gcache.cached_power == 0 && !R3RIsCarOnlyFormation(v)) {
		v->vehstatus.Set(VehState::Stopped);
		SetWindowDirty(WindowClass::VehicleDepot, v->tile.base());
		return true;
	}

	if (v->current_order.IsWaitTimetabled()) {
		v->HandleWaiting(false, true);
	}
	if (v->current_order.IsType(OT_WAITING)) {
		return true;
	}

	/* R3R: a WAIT_COUPLE order parks the train in place (it declares "wait here
	 * to be coupled"), like a normal wait order. Without this the consist would
	 * keep creeping along (it has a minimal power to avoid the auto-stop check). */
	if (v->current_order.IsType(OT_WAIT_COUPLE)) {
		return true;
	}

	/* Check if we should wait here for unbunching. */
	if (v->IsWaitingForUnbunching()) return true;

	if (v->reverse_distance > 0) {
		v->reverse_distance--;
		if (v->reverse_distance == 0) SetWindowWidgetDirty(WindowClass::VehicleView, v->index, WID_VV_START_STOP);
		return true;
	}

	SigSegState seg_state;
	bool exit_blocked = false;

	if (v->force_proceed == TFP_NONE) {
		/* force proceed was not pressed */
		if (++v->wait_counter < 37) {
			return true;
		}

		v->wait_counter = 0;

		seg_state = _settings_game.pf.reserve_paths ? SigSegState::Path : UpdateSignalsOnSegment(v->tile, DiagDirection::Invalid, v->owner);
		if (seg_state == SigSegState::Full || HasDepotReservation(v->tile)) {
			/* Full and no PBS signal in block or depot reserved, can't exit. */
			exit_blocked = true;
		}
	} else {
		seg_state = _settings_game.pf.reserve_paths ? SigSegState::Path : UpdateSignalsOnSegment(v->tile, DiagDirection::Invalid, v->owner);
	}

	/* We are leaving a depot, but have to go to the exact same one; re-enter. */
	if (v->current_order.IsType(OT_GOTO_DEPOT) && v->tile == v->dest_tile) {
		if (exit_blocked) return true;
		/* Service when depot has no reservation. */
		if (!HasDepotReservation(v->tile)) VehicleEnterDepot(v);
		return true;
	}

	if (_settings_game.vehicle.drive_through_train_depot) {
		const TileIndex depot_tile = v->tile;
		const DiagDirection depot_dir = GetRailDepotDirection(depot_tile);
		const DiagDirection behind_depot_dir = ReverseDiagDir(depot_dir);
		const int depot_z = GetTileMaxZ(depot_tile);
		const TileIndexDiffC tile_diff = TileIndexDiffCByDiagDir(behind_depot_dir);

		TileIndex behind_depot_tile = depot_tile;
		uint skipped = 0;

		while (true) {
			TileIndex tile = AddTileIndexDiffCWrap(behind_depot_tile, tile_diff);
			if (tile == INVALID_TILE) break;
			if (!IsRailDepotTile(tile)) break;
			DiagDirection dir = GetRailDepotDirection(tile);
			if (dir != depot_dir && dir != behind_depot_dir) break;
			if (!v->compatible_railtypes.Test(GetRailType(tile))) break;
			if (GetTileMaxZ(tile) != depot_z) break;
			behind_depot_tile = tile;
			skipped++;
		}

		if (skipped > 0 && GetRailDepotDirection(behind_depot_tile) == behind_depot_dir &&
				YapfTrainCheckDepotReverse(v, depot_tile, behind_depot_tile)) {
			Direction direction = DiagDirToDir(behind_depot_dir);
			int x = TileX(behind_depot_tile) * TILE_SIZE | _vehicle_initial_x_fract[behind_depot_dir];
			int y = TileY(behind_depot_tile) * TILE_SIZE | _vehicle_initial_y_fract[behind_depot_dir];
			if (v->gcache.cached_total_length < skipped * TILE_SIZE) {
				int delta = (skipped * TILE_SIZE) - v->gcache.cached_total_length;
				int speed = std::max(1, v->GetCurrentMaxSpeed());
				v->reverse_distance = (1 + (((192 * 3 / 2) * delta) / speed));
				SetWindowWidgetDirty(WindowClass::VehicleView, v->index, WID_VV_START_STOP);
			}

			/* If preserving direction, flip driving end. */
			const bool preserve_direction = _settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None || v->Last()->CanLeadTrain();
			const bool driving_backwards = preserve_direction && !v->vehicle_flags.Test(VehicleFlag::DrivingBackwards);
			if (driving_backwards) direction = ReverseDir(direction);

			for (Train *u = v; u != nullptr; u = u->Next()) {
				u->tile = behind_depot_tile;
				u->direction = direction;
				u->x_pos = x;
				u->y_pos = y;
				u->vehicle_flags.Set(VehicleFlag::DrivingBackwards, driving_backwards);

				u->UpdatePosition();
				u->Vehicle::UpdateViewport(false);
			}

			if (preserve_direction) {
				v->flags.Flip(VehicleRailFlag::Reversed);
			} else {
				v->flags.Reset(VehicleRailFlag::Reversed);
			}
			v->ConsistChanged(CCF_TRACK);

			InvalidateWindowData(WindowClass::VehicleDepot, depot_tile.base());
			InvalidateWindowData(WindowClass::VehicleDepot, behind_depot_tile.base());
			return true;
		}
	}

	if (exit_blocked) return true;

	/* Only leave when we can reserve a path to our destination. */
	if (seg_state == SigSegState::Path && !TryPathReserve(v) && v->force_proceed == TFP_NONE) {
		/* No path and no force proceed. */
		MarkTrainAsStuck(v);
		return true;
	}

	SetDepotReservation(v->tile, true);
	if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(v->tile, VMDF_NOT_MAP_MODE);

	VehicleServiceInDepot(v);
	v->LeaveUnbunchingDepot();
	DirtyVehicleListWindowForVehicle(v);
	v->PlayLeaveStationSound();

	Train *moving_front = v->GetMovingFront();
	moving_front->track = AxisToTrackBits(DiagDirToAxis(DirToDiagDir(moving_front->direction)));

	moving_front->vehstatus.Reset(VehState::Hidden);
	moving_front->UpdateIsDrawn();
	v->cur_speed = 0;

	moving_front->UpdateViewport(true, true);
	moving_front->UpdatePosition();
	UpdateSignalsOnSegment(v->tile, DiagDirection::Invalid, v->owner);
	v->UpdateAcceleration();
	InvalidateWindowData(WindowClass::VehicleDepot, v->tile.base());

	return false;
}

static int GetAndClearLastBridgeEntranceSetSignalIndex(TileIndex bridge_entrance)
{
	uint16_t m = _m[bridge_entrance].m2;
	if (m & BRIDGE_M2_SIGNAL_STATE_EXT_FLAG) {
		auto it = _long_bridge_signal_sim_map.find(bridge_entrance);
		if (it != _long_bridge_signal_sim_map.end()) {
			LongBridgeSignalStorage &lbss = it->second;
			uint slot = (uint)lbss.signal_red_bits.size();
			while (slot > 0) {
				slot--;
				uint64_t &slot_bits = lbss.signal_red_bits[slot];
				if (slot_bits) {
					uint8_t i = FindLastBit(slot_bits);
					ClrBit(slot_bits, i);
					return 1 + BRIDGE_M2_SIGNAL_STATE_COUNT + (64 * slot) + i;
				}
			}
		}
	}
	uint16_t m_masked = GB(m & (~BRIDGE_M2_SIGNAL_STATE_EXT_FLAG), BRIDGE_M2_SIGNAL_STATE_OFFSET, BRIDGE_M2_SIGNAL_STATE_FIELD_SIZE);
	if (m_masked) {
		uint8_t i = FindLastBit(m_masked);
		ClrBit(_m[bridge_entrance].m2, BRIDGE_M2_SIGNAL_STATE_OFFSET + i);
		return 1 + i;
	}

	return 0;
}

static void UpdateTunnelBridgeEntranceSignalAspect(TileIndex tile)
{
	Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(tile);
	uint8_t aspect = GetForwardAspectFollowingTrackAndIncrement(tile, trackdir);
	uint8_t old_aspect = GetTunnelBridgeEntranceSignalAspect(tile);
	if (aspect != old_aspect) {
		SetTunnelBridgeEntranceSignalAspect(tile, aspect);
		MarkTunnelBridgeSignalDirty(tile, false);
		PropagateAspectChange(tile, trackdir, aspect);
	}
}

static void SetTunnelBridgeEntranceSignalGreen(TileIndex tile)
{
	if (GetTunnelBridgeEntranceSignalState(tile) == SignalState::Red) {
		SetTunnelBridgeEntranceSignalState(tile, SignalState::Green);
		MarkTunnelBridgeSignalDirty(tile, false);
		if (_extra_aspects > 0) {
			SetTunnelBridgeEntranceSignalAspect(tile, 0);
			UpdateAspectDeferred(tile, GetTunnelBridgeEntranceTrackdir(tile));
		}
	} else if (_extra_aspects > 0) {
		UpdateTunnelBridgeEntranceSignalAspect(tile);
	}
}

static void UpdateEntranceAspectFromMiddleSignalChange(TileIndex entrance, int signal_number)
{
	if (signal_number < _extra_aspects && GetTunnelBridgeEntranceSignalState(entrance) == SignalState::Green) {
		UpdateTunnelBridgeEntranceSignalAspect(entrance);
	}
}

static void UpdateAspectFromBridgeMiddleSignalChange(TileIndex entrance, TileIndexDiff diff, int signal_number)
{
	UpdateEntranceAspectFromMiddleSignalChange(entrance, signal_number);
	if (signal_number > 0) {
		for (int i = std::max<int>(0, signal_number - _extra_aspects); i < signal_number; i++) {
			MarkSingleBridgeSignalDirty(entrance + (diff * (i + 1)), entrance);
		}
	}
}

static void HandleLastTunnelBridgeSignals(TileIndex tile, TileIndex end, DiagDirection dir, bool free)
{
	if (IsBridge(end) && _m[end].m2 != 0 && IsTunnelBridgeSignalSimulationEntrance(end)) {
		/* Clearing last bridge signal. */
		int signal_offset = GetAndClearLastBridgeEntranceSetSignalIndex(end);
		if (signal_offset) {
			TileIndexDiff diff = TileOffsByDiagDir(dir) * GetTunnelBridgeSignalSimulationSpacing(tile);
			TileIndex last_signal_tile = end + (diff * signal_offset);
			MarkSingleBridgeSignalDirty(last_signal_tile, end);
			if (_extra_aspects > 0) UpdateAspectFromBridgeMiddleSignalChange(end, diff, signal_offset - 1);
		}
		MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	}
	if (free) {
	/* Open up the wormhole and clear m2. */
		if (IsBridge(end)) {
			bool redraw = false;
			if (IsTunnelBridgeSignalSimulationEntrance(tile)) {
				redraw |= SetAllBridgeEntranceSimulatedSignalsGreen(tile);
			}
			if (IsTunnelBridgeSignalSimulationEntrance(end)) {
				redraw |= SetAllBridgeEntranceSimulatedSignalsGreen(end);
			}
			if (redraw) MarkBridgeDirty(tile, end, GetTunnelBridgeDirection(tile), GetBridgeHeight(tile), VMDF_NOT_MAP_MODE);
		}

		if (IsTunnelBridgeSignalSimulationEntrance(end)) SetTunnelBridgeEntranceSignalGreen(end);
		if (IsTunnelBridgeSignalSimulationEntrance(tile)) SetTunnelBridgeEntranceSignalGreen(tile);
	} else if (IsTunnel(end) && _extra_aspects > 0 && IsTunnelBridgeSignalSimulationEntrance(end)) {
		uint signal_count = GetTunnelBridgeLength(tile, end) / GetTunnelBridgeSignalSimulationSpacing(end);
		if (signal_count > 0) UpdateEntranceAspectFromMiddleSignalChange(end, signal_count - 1);
	}
}

static void UnreserveBridgeTunnelTile(TileIndex tile)
{
	UnreserveAcrossRailTunnelBridge(tile);
	if (IsTunnelBridgeSignalSimulationExit(tile) && IsTunnelBridgeEffectivelyPBS(tile)) {
		if (IsTunnelBridgePBS(tile)) {
			SetTunnelBridgeExitSignalState(tile, SignalState::Red);
			if (_extra_aspects > 0) PropagateAspectChange(tile, GetTunnelBridgeExitTrackdir(tile), 0);
		} else {
			UpdateSignalsOnSegment(tile, DiagDirection::Invalid, GetTileOwner(tile));
		}
	}
}

/**
 * Clear the reservation of \a tile that was just left by a wagon on \a track_dir.
 * @param v %Train owning the reservation.
 * @param tile Tile with reservation to clear.
 * @param track_dir Track direction to clear.
 * @param tunbridge_clear_unsignaled_other_end Whether to clear the far end of unsignalled tunnels/bridges.
 */
static void ClearPathReservation(const Train *v, TileIndex tile, Trackdir track_dir, bool tunbridge_clear_unsignaled_other_end = false)
{
	/* R3R KI-108: GetVehicleTrackdir() can hand us INVALID_TRACKDIR (crashed vehicle,
	 * or the direction/track mismatch state left behind by chain edits). Both
	 * TrackdirToExitdir() and TrackdirToTrack() assert on that, so recover an
	 * equivalent trackdir from the vehicle's own track bits instead of crashing. */
	if (unlikely(track_dir == INVALID_TRACKDIR)) {
		const TrackBits tbits = v->track & TRACK_BIT_MASK;
		if (tbits == TRACK_BIT_NONE) return;
		track_dir = TrackToTrackdir(FindFirstTrack(tbits));
		if (R3RDbgEdge(R3REDGE_VEHTD, (uint64_t)v->index.base(), ((uint64_t)v->tile.base() << 32) | ((uint64_t)(uint)v->track << 16) | (uint64_t)(uint)track_dir)) {
			R3RDbgWrite("VEHTD-CLEARRES veh=%d tile=%d,%d trk=0x%X td=%d\n",
					(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
					(uint)v->track, (int)track_dir);
		}
	}

	if (IsTileType(tile, TileType::TunnelBridge)) {
		if (IsTrackAcrossTunnelBridge(tile, TrackdirToTrack(track_dir))) {
			UnreserveBridgeTunnelTile(tile);

			if (IsTunnelBridgeWithSignalSimulation(tile)) {
				/* Are we just leaving a tunnel/bridge? */
				if (TrackdirExitsTunnelBridge(tile, track_dir)) {
					TileIndex end = GetOtherTunnelBridgeEnd(tile);
					bool free = TunnelBridgeIsFree(tile, end, v, TBIFM_ACROSS_ONLY).Succeeded();
					HandleLastTunnelBridgeSignals(tile, end, ReverseDiagDir(GetTunnelBridgeDirection(tile)), free);
				}
			} else if (tunbridge_clear_unsignaled_other_end) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				UnreserveAcrossRailTunnelBridge(end);
				if (_settings_client.gui.show_track_reservation) {
					MarkTileDirtyByTile(end, VMDF_NOT_MAP_MODE);
				}
			}

			if (_settings_client.gui.show_track_reservation || IsTunnelBridgeSignalSimulationBidirectional(tile)) {
				MarkBridgeOrTunnelDirtyOnReservationChange(tile, VMDF_NOT_MAP_MODE);
			}
		} else {
			UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
			if (_settings_client.gui.show_track_reservation) {
				MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
			}
		}
	} else if (IsRailStationTile(tile)) {
		DiagDirection dir = TrackdirToExitdir(track_dir);
		TileIndex new_tile = TileAddByDiagDir(tile, dir);
		/* If the new tile is not a further tile of the same station, we
		 * clear the reservation for the whole platform. */
		if (!IsCompatibleTrainStationTile(new_tile, tile)) {
			SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), false);
		}
	} else {
		/* Any other tile */
		UnreserveRailTrack(tile, TrackdirToTrack(track_dir));
	}
}

/**
 * Free the reserved path in front of a vehicle.
 * @param consist %Train owning the reserved path.
 * @param origin %Tile to start clearing (if #INVALID_TILE, use the current tile of \a v).
 * @param orig_td Track direction (if #INVALID_TRACKDIR, use the track direction of \a v).
 */
void FreeTrainTrackReservation(Train *consist, TileIndex origin, Trackdir orig_td)
{
	assert(consist->IsFrontEngine());

	if (origin == INVALID_TILE) consist->lookahead.reset();

	const Train *moving_front = consist->GetMovingFront();
	TileIndex moving_tile = moving_front->tile;

	bool free_origin_tunnel_bridge = false;

	if (origin == INVALID_TILE && (moving_front->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(moving_tile)) {
		TileIndex other_end = GetOtherTunnelBridgeEnd(moving_tile);
		Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(moving_tile));
		DiagDirection axial_dir = DirToDiagDirAlongAxis(consist->GetMovingDirection(), axis);
		TileIndex exit = moving_tile;
		TileIndex entrance = other_end;
		if (axial_dir == GetTunnelBridgeDirection(moving_tile)) std::swap(exit, entrance);
		if (GetTrainClosestToTunnelBridgeEnd(exit, entrance) == consist) {
			origin = exit;
			TrackBits tracks = GetAcrossTunnelBridgeTrackBits(origin);
			orig_td = ReverseTrackdir(TrackExitdirToTrackdir(FindFirstTrack(tracks), GetTunnelBridgeDirection(origin)));
			free_origin_tunnel_bridge = true;
		} else {
			return;
		}
	}

	TileIndex tile = origin != INVALID_TILE ? origin : moving_tile;
	Trackdir  td = orig_td != INVALID_TRACKDIR ? orig_td : moving_front->GetVehicleTrackdir();
	bool      free_tile = tile != moving_tile || !(IsRailStationTile(moving_tile) || IsTileType(moving_tile, TileType::TunnelBridge));
	StationID station_id = IsRailStationTile(moving_tile) ? GetStationIndex(moving_tile) : StationID::Invalid();

	/* Can't be holding a reservation if we enter a depot. */
	if (IsRailDepotTile(tile) && TrackdirToExitdir(td) != GetRailDepotDirection(tile)) return;
	if (moving_front->track == TRACK_BIT_DEPOT) {
		/* Front engine is in a depot. We enter if some part is not in the depot. */
		for (const Train *u = consist; u != nullptr; u = u->Next()) {
			if (u->track != TRACK_BIT_DEPOT || u->tile != consist->tile) return;
		}
	}
	/* Don't free reservation if it's not ours. */
	if (TracksOverlap(GetReservedTrackbits(tile) | TrackToTrackBits(TrackdirToTrack(td)))) return;

	/* Do not attempt to unreserve out of a signalled tunnel/bridge entrance, as this would unreserve the reservations of another train coming in */
	if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirExitsTunnelBridge(tile, td) && IsTunnelBridgeSignalSimulationEntranceOnly(tile)) return;

	if (free_origin_tunnel_bridge) {
		if (!HasReservedTracks(tile, TrackToTrackBits(TrackdirToTrack(td)))) return;
		UnreserveRailTrack(tile, TrackdirToTrack(td));
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && !IsTunnelBridgePBS(tile)) {
			UpdateSignalsOnSegment(tile, DiagDirection::Invalid, GetTileOwner(tile));
		}
	}

	CFollowTrackRail ft(consist, consist->GetIndirectCompatibleRailTypes());
	while (ft.Follow(tile, td)) {
		tile = ft.new_tile;
		TrackdirBits bits = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(tile));
		td = RemoveFirstTrackdir(&bits);
		dbg_assert(bits == TRACKDIR_BIT_NONE);

		if (!IsValidTrackdir(td)) break;

		bool update_signal = false;

		if (IsTileType(tile, TileType::Railway)) {
			if (HasSignalOnTrackdir(tile, td) && !IsPbsSignal(GetSignalType(tile, TrackdirToTrack(td)))) {
				/* Conventional signal along trackdir: remove reservation and stop. */
				UnreserveRailTrack(tile, TrackdirToTrack(td));
				break;
			}
			if (HasPbsSignalOnTrackdir(tile, td)) {
				if (GetSignalStateByTrackdir(tile, td) == SignalState::Red || IsNoEntrySignal(tile, TrackdirToTrack(td))) {
					/* Red PBS signal? Can't be our reservation, would be green then. */
					break;
				} else {
					/* Turn the signal back to red. */
					if (GetSignalType(tile, TrackdirToTrack(td)) == SignalType::Block) {
						update_signal = true;
					} else {
						SetSignalStateByTrackdir(tile, td, SignalState::Red);
					}
					MarkSingleSignalDirty(tile, td);
				}
			} else if (HasSignalOnTrackdir(tile, ReverseTrackdir(td)) && IsOnewaySignal(tile, TrackdirToTrack(td))) {
				break;
			}
		} else if (IsTunnelBridgeWithSignalSimulation(tile) && TrackdirExitsTunnelBridge(tile, td)) {
			TileIndex end = GetOtherTunnelBridgeEnd(tile);
			bool free = TunnelBridgeIsFree(tile, end, consist, TBIFM_ACROSS_ONLY).Succeeded();
			if (!free) break;
		} else if (IsTunnelBridgeWithSignalSimulation(tile) && IsTunnelBridgeSignalSimulationExitOnly(tile) && TrackdirEntersTunnelBridge(tile, td)) {
			break;
		}

		/* Don't free first station/bridge/tunnel if we are on it. */
		if (free_tile || (!(ft.is_station && GetStationIndex(ft.new_tile) == station_id) && !ft.is_tunnel && !ft.is_bridge)) ClearPathReservation(consist, tile, td);
		if (update_signal) {
			AddSideToSignalBuffer(tile, TrackdirToExitdir(td), GetTileOwner(tile));
			UpdateSignalsInBuffer();
		}

		free_tile = true;
	}
}

/**
 * Perform pathfinding for a train.
 *
 * @param v The train
 * @param tile The tile the train is about to enter
 * @param enterdir Diagonal direction the train is coming from
 * @param tracks Usable tracks on the new tile
 * @param[out] path_found Whether a path has been found or not.
 * @param do_track_reservation Path reservation is requested
 * @param[out] dest State and destination of the requested path
 * @param[out] final_dest Final tile of the best path found
 * @return The best track the train should follow
 */
static Track DoTrainPathfind(const Train *v, TileIndex tile, DiagDirection enterdir, TrackBits tracks, bool &path_found, bool do_track_reservation, PBSTileInfo *dest, TileIndex *final_dest)
{
	if (final_dest != nullptr) *final_dest = INVALID_TILE;
	return YapfTrainChooseTrack(v, tile, enterdir, tracks, path_found, do_track_reservation, dest, final_dest);
}

/**
 * Find the track to take when going to couple with another train.
 * @param v The train.
 * @param do_track_reservation Whether to reserve the path.
 * @return The track to take, or #INVALID_TRACK if no path was found.
 */
static Track DoTrainCouplePathfind(const Train *v, bool do_track_reservation)
{
	return YapfTrainCoupleTrack(v, !do_track_reservation);
}

/**
 * Extend a train path as far as possible. Stops on encountering a safe tile,
 * another reservation or a track choice.
 * @param v The train.
 * @param origin The tile from which the reservation have to be extended
 * @param new_tracks [out] Tracks to choose from when encountering a choice
 * @param enterdir [out] The direction from which the choice tile is to be entered
 * @param temporary_slot_state The temporary slot to use (will be activated/deactivated as necessary if it isn't already)
 * @return INVALID_TILE indicates that the reservation failed.
 */
static PBSTileInfo ExtendTrainReservation(const Train *v, const PBSTileInfo &origin, TrackBits *new_tracks, DiagDirection *enterdir, TraceRestrictSlotTemporaryState &temporary_slot_state)
{
	CFollowTrackRail ft(v);

	TileIndex tile = origin.tile;
	Trackdir  cur_td = origin.trackdir;
	while (ft.Follow(tile, cur_td)) {
		if (KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
			/* Possible signal tile. */
			if (HasOnewaySignalBlockingTrackdir(ft.new_tile, FindFirstTrackdir(ft.new_td_bits))) break;
		}

		if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td, _settings_game.pf.forbid_90_deg)) {
			ft.new_td_bits &= ~TrackdirCrossesTrackdirs(ft.old_td);
			if (ft.new_td_bits == TRACKDIR_BIT_NONE) break;
		}

		/* Station, depot or waypoint are a possible target. */
		bool target_seen = ft.is_station || (IsTileType(ft.new_tile, TileType::Railway) && !IsPlainRail(ft.new_tile));
		if (target_seen || KillFirstBit(ft.new_td_bits) != TRACKDIR_BIT_NONE) {
			/* Choice found or possible target encountered.
			 * On finding a possible target, we need to stop and let the pathfinder handle the
			 * remaining path. This is because we don't know if this target is in one of our
			 * orders, so we might cause pathfinding to fail later on if we find a choice.
			 * This failure would cause a bogus call to TryReserveSafePath which might reserve
			 * a wrong path not leading to our next destination. */
			if (HasReservedTracks(ft.new_tile, TrackdirBitsToTrackBits(TrackdirReachesTrackdirs(ft.old_td)))) break;

			/* If we did skip some tiles, backtrack to the first skipped tile so the pathfinder
			 * actually starts its search at the first unreserved tile. */
			if (ft.tiles_skipped != 0 && !IsTileType(ft.new_tile, TileType::TunnelBridge)) ft.new_tile -= TileOffsByDiagDir(ft.exitdir) * ft.tiles_skipped;


			/* Choice found, path valid but not okay. Save info about the choice tile as well. */
			if (new_tracks != nullptr) *new_tracks = TrackdirBitsToTrackBits(ft.new_td_bits);
			if (enterdir != nullptr) *enterdir = ft.exitdir;
			return PBSTileInfo(ft.new_tile, ft.old_td, false);
		}

		tile = ft.new_tile;
		cur_td = FindFirstTrackdir(ft.new_td_bits);

		if (IsSafeWaitingPosition(v, tile, cur_td, true, _settings_game.pf.forbid_90_deg)) {
			PBSWaitingPositionRestrictedSignalState restricted_signal_state;
			bool wp_free = IsWaitingPositionFree(v, tile, cur_td, _settings_game.pf.forbid_90_deg, &restricted_signal_state);
			if (!(wp_free && TryReserveRailTrackdir(v, tile, cur_td))) break;
			/* Safe position is all good, path valid and okay. */
			restricted_signal_state.TraceRestrictExecuteResEndSlot(v);
			return PBSTileInfo(tile, cur_td, true);
		}

		if (IsTileType(tile, TileType::Railway) && HasSignals(tile) && IsRestrictedSignal(tile) && HasSignalOnTrack(tile, TrackdirToTrack(cur_td))) {
			const bool front_side = HasSignalOnTrackdir(tile, cur_td);

			TraceRestrictProgramActionsUsedFlags au_flags = TRPAUF_SLOT_ACQUIRE;
			if (front_side) {
				/* Passing through a signal from the front side */
				au_flags |= TRPAUF_WAIT_AT_PBS;
			}

			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(cur_td));
			if (prog != nullptr && prog->actions_used_flags & au_flags) {
				TraceRestrictProgramInput input(tile, cur_td, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
				if (prog->actions_used_flags & TRPAUF_SLOT_ACQUIRE) {
					input.permitted_slot_operations = TRPISP_ACQUIRE_TEMP_STATE;

					if (!temporary_slot_state.IsActive()) {
						/* The temporary slot state needs to be be pushed because permission to use it is granted by TRPISP_ACQUIRE_TEMP_STATE */
						temporary_slot_state.PushToChangeStack();
					}
				}

				TraceRestrictProgramResult out;
				prog->Execute(v, input, out);
				if (front_side && (out.flags & TRPRF_WAIT_AT_PBS)) {
					/* Wait at PBS is set, take this as waiting at the start signal, handle as a reservation failure */
					break;
				}
			}
		}

		if (!TryReserveRailTrackdir(v, tile, cur_td)) break;
	}

	if (ft.err == CFollowTrackRail::EC_OWNER || ft.err == CFollowTrackRail::EC_NO_WAY) {
		/* End of line, path valid and okay. */
		return PBSTileInfo(ft.old_tile, ft.old_td, true);
	}

	/* Sorry, can't reserve path, back out. */
	tile = origin.tile;
	cur_td = origin.trackdir;
	TileIndex stopped = ft.old_tile;
	Trackdir  stopped_td = ft.old_td;
	while (tile != stopped || cur_td != stopped_td) {
		if (!ft.Follow(tile, cur_td)) break;

		if (ft.tiles_skipped == 0 && Rail90DegTurnDisallowedTilesFromTrackdir(ft.old_tile, ft.new_tile, ft.old_td, _settings_game.pf.forbid_90_deg)) {
			ft.new_td_bits &= ~TrackdirCrossesTrackdirs(ft.old_td);
			dbg_assert(ft.new_td_bits != TRACKDIR_BIT_NONE);
		}
		dbg_assert(KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE);

		tile = ft.new_tile;
		cur_td = FindFirstTrackdir(ft.new_td_bits);

		UnreserveRailTrackdir(tile, cur_td);
	}

	if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(v->index);

	/* Path invalid. */
	return PBSTileInfo();
}

/**
 * Try to reserve any path to a safe tile, ignoring the vehicle's destination.
 * Safe tiles are tiles in front of a signal, depots and station tiles at end of line.
 *
 * @param v The vehicle.
 * @param tile The tile the search should start from.
 * @param td The trackdir the search should start from.
 * @param override_railtype Whether all physically compatible railtypes should be followed.
 * @return True if a path to a safe stopping tile could be reserved.
 */
static bool TryReserveSafeTrack(const Train *v, TileIndex tile, Trackdir td, bool override_railtype)
{
	return YapfTrainFindNearestSafeTile(v, tile, td, override_railtype);
}

const Order *_choose_train_track_saved_current_order = nullptr;

/** This class will save the current order of a vehicle and restore it on destruction. */
class VehicleOrderSaver {
private:
	Train          *v;
	Order          old_order;
	TileIndex      old_dest_tile;
	StationID      old_last_station_visited;
	VehicleOrderID old_index;
	VehicleOrderID old_impl_index;
	VehicleOrderID old_tt_index;
	bool           suppress_implicit_orders;
	bool           clear_saved_order_ptr;
	bool           restored;

public:
	VehicleOrderSaver(Train *_v) :
		v(_v),
		old_order(_v->current_order),
		old_dest_tile(_v->dest_tile),
		old_last_station_visited(_v->last_station_visited),
		old_index(_v->cur_real_order_index),
		old_impl_index(_v->cur_implicit_order_index),
		old_tt_index(_v->cur_timetable_order_index),
		suppress_implicit_orders(HasBit(_v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS)),
		restored(false)
	{
		if (_choose_train_track_saved_current_order == nullptr) {
#if defined(__GNUC__) && (__GNUC__ >= 12)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif /* __GNUC__ */
			_choose_train_track_saved_current_order = &(this->old_order);
#if defined(__GNUC__) && (__GNUC__ >= 12)
#pragma GCC diagnostic pop
#endif /* __GNUC__ */

			this->clear_saved_order_ptr = true;
		} else {
			this->clear_saved_order_ptr = false;
		}
	}

	/**
	 * Restore the saved order to the vehicle.
	 */
	void Restore()
	{
		this->v->current_order = std::move(this->old_order);
		this->v->dest_tile = this->old_dest_tile;
		this->v->last_station_visited = this->old_last_station_visited;
		this->v->cur_real_order_index = this->old_index;
		this->v->cur_implicit_order_index = this->old_impl_index;
		this->v->cur_timetable_order_index = this->old_tt_index;
		AssignBit(this->v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS, suppress_implicit_orders);
		if (this->clear_saved_order_ptr) _choose_train_track_saved_current_order = nullptr;
		this->restored = true;
	}

	/**
	 * Restore the saved order to the vehicle, if Restore() has not already been called.
	 */
	~VehicleOrderSaver()
	{
		if (!this->restored) this->Restore();
	}

	/**
	 * Set the current vehicle order to the next order in the order list.
	 * @param skip_first Shall the first (i.e. active) order be skipped?
	 * @return True if a suitable next order could be found.
	 */
	bool SwitchToNextOrder(bool skip_first)
	{
		if (this->v->GetNumOrders() == 0) return false;

		if (skip_first) ++this->v->cur_real_order_index;

		int depth = 0;

		do {
			/* Wrap around. */
			if (this->v->cur_real_order_index >= this->v->GetNumOrders()) this->v->cur_real_order_index = 0;

			Order *order = this->v->GetOrder(this->v->cur_real_order_index);
			dbg_assert(order != nullptr);

			switch (order->GetType()) {
				case OT_GOTO_DEPOT:
					/* Skip service in depot orders when the train doesn't need service. */
					if ((order->GetDepotOrderType().Test(OrderDepotTypeFlag::Service)) && !this->v->NeedsServicing()) break;
					[[fallthrough]];
				case OT_GOTO_STATION:
				case OT_GOTO_WAYPOINT:
				case OT_GOTO_COUPLE:
					/* R3R: GOTO_COUPLE is a travel order. It must be selectable
					 * when advancing, e.g. after a waypoint/station order is
					 * completed. Without this case it fell into the default
					 * branch and was skipped, so the locomotive jumped straight
					 * to the order after the GOTO_COUPLE (e.g. GOTO_DEPOT),
					 * failed to find a path and got stuck permanently. */
					this->v->current_order = *order;
					return UpdateOrderDest(this->v, order, 0, true);
				case OT_CONDITIONAL: {
					VehicleOrderID next = ProcessConditionalOrder(order, this->v, PCO_DRY_RUN);
					if (next != INVALID_VEH_ORDER_ID) {
						depth++;
						this->v->cur_real_order_index = next;
						/* Don't increment next, so no break here. */
						continue;
					}
					break;
				}
				default:
					break;
			}
			/* Don't increment inside the while because otherwise conditional
			 * orders can lead to an infinite loop. */
			++this->v->cur_real_order_index;
			depth++;
		} while (this->v->cur_real_order_index != this->old_index && depth < this->v->GetNumOrders());

		return false;
	}

	void AdvanceOrdersFromVehiclePosition(ChooseTrainTrackLookAheadState &state)
	{
		/* If the current tile is the destination of the current order and
		 * a reservation was requested, advance to the next order.
		 * Don't advance on a depot order as depots are always safe end points
		 * for a path and no look-ahead is necessary. This also avoids a
		 * problem with depot orders not part of the order list when the
		 * order list itself is empty.
		 * R3R: same for GOTO_COUPLE - a coupling consist must keep the
		 * GOTO_COUPLE order until the coupling actually completes (order
		 * advancement is handled by the R3R coupling logic). Advancing the
		 * look-ahead here (the locomotive has reached the platform entrance
		 * tile which is the GOTO_COUPLE destination) would make the
		 * pathfinder target the order after the GOTO_COUPLE instead of the
		 * waiting consist. */
		Train *v = this->v;
		if (v->current_order.IsType(OT_LEAVESTATION)) {
			this->SwitchToNextOrder(false);
			return;
		}
		if (v->current_order.IsAnyLoadingType()) {
			SetBit(state.flags, CTTLASF_STOP_FOUND);
			this->SwitchToNextOrder(true);
			return;
		}
		if (v->current_order.IsType(OT_GOTO_DEPOT) || v->current_order.IsType(OT_GOTO_COUPLE)) return;

		Train *moving_front = v->GetMovingFront();
		if (v->current_order.IsBaseStationOrder() ?
				HasStationTileRail(moving_front->tile) && v->current_order.GetDestination() == GetStationIndex(moving_front->tile) :
				moving_front->tile == v->dest_tile) {
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && v->current_order.IsBaseStationOrder()) {
				if (v->current_order.ShouldStopAtStation(v, v->current_order.GetDestination().ToStationID(), v->current_order.IsType(OT_GOTO_WAYPOINT))) {
					SetBit(state.flags, CTTLASF_STOP_FOUND);
					v->last_station_visited = v->current_order.GetDestination().ToStationID();
				}
			}
			if (v->current_order.IsType(OT_WAITING)) SetBit(state.flags, CTTLASF_STOP_FOUND);
			this->SwitchToNextOrder(true);
			return;
		}
	}

	void AdvanceOrdersFromLookahead(ChooseTrainTrackLookAheadState &state)
	{
		TrainReservationLookAhead *lookahead = this->v->lookahead.get();
		if (lookahead == nullptr) return;

		for (size_t i = state.order_items_start; i < lookahead->items.size(); i++) {
			const TrainReservationLookAheadItem &item = lookahead->items[i];
			switch (item.type) {
				case TRLIT_STATION: {
					const StationID st = static_cast<StationID>(item.data_id);
					if (this->v->current_order.IsBaseStationOrder()) {
						/* we've already seen this station in the lookahead, advance current order */
						if (this->v->current_order.ShouldStopAtStation(this->v, st, Waypoint::GetIfValid(st) != nullptr)) {
							SetBit(state.flags, CTTLASF_STOP_FOUND);
							this->v->last_station_visited = st;
						} else if (this->v->current_order.IsType(OT_GOTO_WAYPOINT) && this->v->current_order.GetDestination() == st && this->v->current_order.GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) {
							if (!HasBit(state.flags, CTTLASF_REVERSE_FOUND)) {
								SetBit(state.flags, CTTLASF_REVERSE_FOUND);
								state.reverse_dest = st;
								if (this->v->current_order.IsWaitTimetabled()) {
									this->v->last_station_visited = st;
									SetBit(state.flags, CTTLASF_STOP_FOUND);
								}
							}
						}
						if (this->v->current_order.GetDestination() == st) {
							this->SwitchToNextOrder(true);
						}
					}
					break;
				}

				default:
					break;
			}
		}
		state.order_items_start = (uint)lookahead->items.size();
	}
};

static bool IsReservationLookAheadLongEnough(const Train *v, const ChooseTrainTrackLookAheadState &lookahead_state)
{
	if (!v->UsingRealisticBraking() || v->lookahead == nullptr) return true;

	if (v->current_order.IsAnyLoadingType() || v->current_order.IsType(OT_WAITING)) return true;

	if (HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND) || v->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) return true;

	if (v->reverse_distance >= 1) {
		if (v->lookahead->reservation_end_position >= v->lookahead->current_position + v->reverse_distance - 1) return true;
	}

	if (v->lookahead->lookahead_end_position <= v->lookahead->reservation_end_position && _settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON &&
			v->lookahead->reservation_end_position > v->lookahead->current_position + 24) {
		return true;
	}

	TrainDecelerationStats stats(v, v->lookahead->cached_zpos);

	bool found_signal = false;
	int signal_speed = 0;
	int signal_position = 0;
	int signal_z = 0;
	bool signal_limited_lookahead_check = false;

	for (const TrainReservationLookAheadItem &item : v->lookahead->items) {
		if (item.type == TRLIT_REVERSE) {
			if (v->lookahead->reservation_end_position >= item.start + v->gcache.cached_total_length) return true;
		}
		if (item.type == TRLIT_STATION && HasBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND) && lookahead_state.reverse_dest == item.data_id) {
			if (v->lookahead->reservation_end_position >= item.start + v->gcache.cached_total_length) return true;
		}

		if (found_signal) {
			if (item.type == TRLIT_TRACK_SPEED || item.type == TRLIT_SPEED_RESTRICTION || item.type == TRLIT_CURVE_SPEED) {
				if (item.data_id > 0) LimitSpeedFromLookAhead(signal_speed, stats, signal_position, item.start, item.data_id, item.z_pos - stats.z_pos);
			}
		} else if (item.type == TRLIT_SIGNAL && item.start > v->lookahead->current_position + 24) {
			signal_speed = std::min<int>(item.data_id > 0 ? item.data_id : UINT16_MAX, v->vcache.cached_max_speed);
			signal_position = item.start;
			signal_z = item.z_pos;
			found_signal = true;
		}

		if (item.type == TRLIT_SIGNAL && _settings_game.vehicle.realistic_braking_aspect_limited == TRBALM_ON && item.start <= v->lookahead->current_position + 24) {
			if (HasBit(item.data_aux, TRSLAI_NO_ASPECT_INC) || HasBit(item.data_aux, TRSLAI_NEXT_ONLY) || HasBit(item.data_aux, TRSLAI_COMBINED_SHUNT)) {
				signal_limited_lookahead_check = true;
			}
		}
	}

	if (signal_limited_lookahead_check) {
		/* Do not unnecessarily extend the reservation when passing a signal within the reservation which could not display an aspect
		 * beyond the current end of the reservation, e.g. banner repeaters and shunt signals */
		if (AdvanceTrainReservationLookaheadEnd(v, v->lookahead->current_position + 24) <= v->lookahead->reservation_end_position &&
				v->lookahead->reservation_end_position > v->lookahead->current_position + 24) {
			return true;
		}
	}

	if (found_signal) {
		int delta_z = v->lookahead->reservation_end_z - signal_z;
		delta_z += (delta_z >> 2); // Slightly overestimate slope changes to compensate for non-uniform descents
		int64_t distance = GetRealisticBrakingDistanceForSpeed(stats, signal_speed, 0, delta_z);
		if (signal_position + distance <= v->lookahead->reservation_end_position) return true;
	}

	return false;
}

static bool LookaheadWithinCurrentTunnelBridge(const Train *t)
{
	return t->lookahead->current_position >= t->lookahead->reservation_end_position - ((int)TILE_SIZE * t->lookahead->tunnel_bridge_reserved_tiles) && !t->lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree);
}

static bool HasLongReservePbsSignalOnTrackdir(Train *consist, TileIndex tile, Trackdir trackdir, bool default_value, uint16_t lookahead_state_flags)
{
	if (HasPbsSignalOnTrackdir(tile, trackdir)) {
		if (IsNoEntrySignal(tile, TrackdirToTrack(trackdir))) return false;
		if (IsRestrictedSignal(tile)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(trackdir));
			if (prog != nullptr && prog->actions_used_flags & TRPAUF_LONG_RESERVE) {
				TraceRestrictProgramResult out;
				if (default_value) out.flags |= TRPRF_LONG_RESERVE;
				TraceRestrictProgramInput input(tile, trackdir, &VehiclePosTraceRestrictPreviousSignalCallback, nullptr);
				if (HasBit(lookahead_state_flags, CTTLASF_STOP_FOUND)) input.input_flags.Set(TraceRestrictProgramInputFlag::PassedStop);
				prog->Execute(consist, input, out);
				return (out.flags & TRPRF_LONG_RESERVE);
			}
		}
		return default_value;
	}

	return false;
}

static TileIndex CheckLongReservePbsTunnelBridgeOnTrackdir(Train *v, TileIndex tile, Trackdir trackdir, bool restricted_only = false)
{
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTunnelBridgeSignalSimulationEntranceTile(tile) && TrackdirEntersTunnelBridge(tile, trackdir)) {
		TileIndex end = GetOtherTunnelBridgeEnd(tile);
		if (restricted_only && !IsTunnelBridgeRestrictedSignal(end)) return INVALID_TILE;
		int raw_free_tiles;
		if (v->lookahead != nullptr && v->lookahead->reservation_end_tile == tile && v->lookahead->reservation_end_trackdir == trackdir) {
			if (v->lookahead->flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) {
				raw_free_tiles = INT_MAX;
			} else {
				raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridgeWithStartOffset(tile, end, v->lookahead->tunnel_bridge_reserved_tiles + 1);
				ApplyAvailableFreeTunnelBridgeTiles(v->lookahead.get(), raw_free_tiles, tile, end);
				FlushDeferredDetermineCombineNormalShuntMode(v);
				SetTrainReservationLookaheadEnd(v);
			}
		} else {
			raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(tile, end, tile);
		}
		if (!HasAcrossTunnelBridgeReservation(end) && raw_free_tiles == INT_MAX) {
			return end;
		}
	}
	return INVALID_TILE;
}

bool _long_reserve_disabled = false;

static void TryLongReserveChooseTrainTrack(Train *v, TileIndex tile, Trackdir td, bool force_res, ChooseTrainTrackLookAheadState lookahead_state)
{
	if (_long_reserve_disabled) return;

	const bool long_enough = IsReservationLookAheadLongEnough(v, lookahead_state);

	// We reserved up to a unoccupied signalled tunnel/bridge, reserve past it as well. recursion
	TileIndex exit_tile = CheckLongReservePbsTunnelBridgeOnTrackdir(v, tile, td, long_enough);
	if (exit_tile != INVALID_TILE) {
		CFollowTrackRail ft(v);
		Trackdir exit_td = GetTunnelBridgeExitTrackdir(exit_tile);
		if (ft.Follow(exit_tile, exit_td)) {
			const TrackBits reserved_bits = GetReservedTrackbits(ft.new_tile);
			if ((ft.new_td_bits & TrackBitsToTrackdirBits(reserved_bits)) == TRACKDIR_BIT_NONE) {
				/* next tile is not reserved */

				bool long_reserve = !long_enough;
				if (IsTunnelBridgeRestrictedSignal(exit_tile)) {
					/* Test for TRPRF_LONG_RESERVE in a separate execution from TRPRF_WAIT_AT_PBS/slot operations.
					 * This is to avoid prematurely acquiring slots on the exit signal before we try to make an exit reservation.
					 */
					const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(exit_tile, TrackdirToTrack(exit_td));
					if (prog != nullptr && (prog->actions_used_flags & TRPAUF_LONG_RESERVE)) {
						TraceRestrictProgramResult out;
						if (long_reserve) out.flags |= TRPRF_LONG_RESERVE;
						TraceRestrictProgramInput input(exit_tile, exit_td, nullptr, nullptr);
						if (HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND)) input.input_flags.Set(TraceRestrictProgramInputFlag::PassedStop);
						prog->Execute(v, input, out);
						long_reserve = (out.flags & TRPRF_LONG_RESERVE);
					}
					if (!long_reserve) return;
					if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE | TRPAUF_REVERSE_AT)) {
						TraceRestrictProgramResult out;
						TraceRestrictProgramInput input(exit_tile, exit_td, nullptr, nullptr);
						input.permitted_slot_operations = TRPISP_ACQUIRE;
						prog->Execute(v, input, out);
						if (out.flags & (TRPRF_WAIT_AT_PBS | TRPRF_REVERSE_AT)) {
							return;
						}
					}
				}
				if (!long_reserve) return;

				const SignalState orig_exit_state = GetTunnelBridgeExitSignalState(exit_tile);

				/* reserve exit to make contiguous reservation */
				if (IsBridge(exit_tile)) {
					TryReserveRailBridgeHead(exit_tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(exit_tile)));
				} else {
					SetTunnelReservation(exit_tile, true);
				}
				if (orig_exit_state == SignalState::Red && _extra_aspects > 0) {
					SetTunnelBridgeExitSignalAspect(exit_tile, 0);
					UpdateAspectDeferredWithVehicleTunnelBridgeExit(v, exit_tile, GetTunnelBridgeExitTrackdir(exit_tile));
				}
				SetTunnelBridgeExitSignalState(exit_tile, SignalState::Green);

				ChooseTrainTrack(v, ft.new_tile, ft.exitdir, TrackdirBitsToTrackBits(ft.new_td_bits), CTTF_NO_LOOKAHEAD_VALIDATE | (force_res ? CTTF_FORCE_RES : CTTF_NONE), lookahead_state);
				FlushDeferredDetermineCombineNormalShuntMode(v);

				if (reserved_bits == GetReservedTrackbits(ft.new_tile)) {
					/* next tile is still not reserved, so unreserve exit and restore signal state */
					if (IsBridge(exit_tile)) {
						UnreserveRailBridgeHeadTrack(exit_tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(exit_tile)));
					} else {
						SetTunnelReservation(exit_tile, false);
					}
					SetTunnelBridgeExitSignalState(exit_tile, orig_exit_state);
				} else {
					if (orig_exit_state == SignalState::Green && _extra_aspects > 0) {
						SetTunnelBridgeExitSignalAspect(exit_tile, 0);
						UpdateAspectDeferred(exit_tile, GetTunnelBridgeExitTrackdir(exit_tile));
					}
					MarkTileDirtyByTile(exit_tile, VMDF_NOT_MAP_MODE);
				}
			}
		}
		return;
	}

	CFollowTrackRail ft(v);
	if (ft.Follow(tile, td) && HasLongReservePbsSignalOnTrackdir(v, ft.new_tile, FindFirstTrackdir(ft.new_td_bits), !long_enough, lookahead_state.flags)) {
		/* We reserved up to a LR signal, reserve past it as well. recursion */
		ChooseTrainTrack(v, ft.new_tile, ft.exitdir, TrackdirBitsToTrackBits(ft.new_td_bits), CTTF_NO_LOOKAHEAD_VALIDATE | (force_res ? CTTF_FORCE_RES : CTTF_NONE), lookahead_state);
	}
}

static void TryLongReserveChooseTrainTrackFromReservationEnd(Train *v, bool no_reserve_vehicle_tile)
{
	ClearLookAheadIfInvalid(v);

	PBSTileInfo origin = FollowTrainReservation(v, nullptr, FollowTrainReservationFlag::OkayUnused);
	if (IsRailDepotTile(origin.tile)) return;

	ChooseTrainTrackLookAheadState lookahead_state;
	if (no_reserve_vehicle_tile) SetBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE);
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		VehicleOrderSaver orders(v);
		orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
		orders.AdvanceOrdersFromLookahead(lookahead_state);

		/* Note that this must be called before the VehicleOrderSaver destructor, above */
		TryLongReserveChooseTrainTrack(v, origin.tile, origin.trackdir, true, lookahead_state);
	} else {
		TryLongReserveChooseTrainTrack(v, origin.tile, origin.trackdir, true, lookahead_state);
	}
}

/**
 * Choose a track and reserve if necessary
 *
 * @param consist The vehicle
 * @param tile The tile from which to start
 * @param enterdir
 * @param tracks
 * @param flags ChooseTrainTrackFlags flags
 * @return The track the train should take and the result flags
 */
static ChooseTrainTrackResult ChooseTrainTrack(Train *consist, const TileIndex tile, const DiagDirection enterdir, TrackBits tracks, ChooseTrainTrackFlags flags, ChooseTrainTrackLookAheadState lookahead_state)
{
	/* R3R (KI-207): the lookahead path reservation is booked from here (the three
	 * TryReserveRailTrack(moving_front->tile, ...) calls below), so name the phase
	 * and the train for the reservation-origin trace. */
	R3RResvPhaseGuard r3r_phase("choose-track", R3RResvActorID(consist));
	Track best_track = INVALID_TRACK;
	bool do_track_reservation = _settings_game.pf.reserve_paths || (flags & CTTF_FORCE_RES);
	Trackdir changed_signal = INVALID_TRACKDIR;
	TileIndex final_dest = INVALID_TILE;

	dbg_assert((tracks & ~TRACK_BIT_MASK) == 0);

	ChooseTrainTrackResultFlags result_flags = CTTRF_NONE;

	/* Don't use tracks here as the setting to forbid 90 deg turns might have been switched between reservation and now. */
	TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir));
	/* Do we have a suitable reserved track? */
	if (res_tracks != TRACK_BIT_NONE) return { FindFirstTrack(res_tracks), result_flags };

	bool mark_stuck = (flags & CTTF_MARK_STUCK);

	/* Quick return in case only one possible track is available */
	if (KillFirstBit(tracks) == TRACK_BIT_NONE) {
		Track track = FindFirstTrack(tracks);
		/* We need to check for signals only here, as a junction tile can't have signals. */
		if (track != INVALID_TRACK && HasPbsSignalOnTrackdir(tile, TrackEnterdirToTrackdir(track, enterdir)) && !IsNoEntrySignal(tile, track)) {
			if (IsRestrictedSignal(tile) && consist->force_proceed != TFP_SIGNAL) {
				const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, track);
				if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE | TRPAUF_TRAIN_NOT_STUCK | TRPAUF_REVERSE_AT)) {
					TraceRestrictProgramResult out;
					TraceRestrictProgramInput input(tile, TrackEnterdirToTrackdir(track, enterdir), nullptr, nullptr);
					input.permitted_slot_operations = TRPISP_ACQUIRE;
					prog->Execute(consist, input, out);
					if (out.flags & TRPRF_TRAIN_NOT_STUCK && !(consist->track & TRACK_BIT_WORMHOLE) && !(consist->track == TRACK_BIT_DEPOT)) {
						consist->wait_counter = 0;
					}
					if (out.flags & TRPRF_REVERSE_AT) {
						result_flags |= CTTRF_REVERSE_AT_SIGNAL;
					}
					if (out.flags & (TRPRF_WAIT_AT_PBS | TRPRF_REVERSE_AT)) {
						if (mark_stuck) MarkTrainAsStuck(consist, true);
						return { track, result_flags };
					}
				}
			}
			consist->flags.Reset(VehicleRailFlag::WaitingRestriction);

			do_track_reservation = true;
			changed_signal = TrackEnterdirToTrackdir(track, enterdir);
			SetSignalStateByTrackdir(tile, changed_signal, SignalState::Green);
			if (_extra_aspects > 0) {
				SetSignalAspect(tile, track, 0);
				UpdateAspectDeferredWithVehicleRail(consist, tile, changed_signal);
			}
		} else if (!do_track_reservation) {
			return { track, result_flags };
		}
		best_track = track;
	}

	if ((flags & CTTF_NON_LOOKAHEAD) && consist->lookahead != nullptr) {
		/* We have reached a diverging junction with no reservation, yet we have a lookahead state.
		 * Clear the lookahead state. */
		consist->lookahead.reset();
	}

	if (!(flags & CTTF_NO_LOOKAHEAD_VALIDATE)) {
		ClearLookAheadIfInvalid(consist);
	}

	/* The temporary slot state only needs to be pushed to the stack (i.e. activated) on first use */
	TraceRestrictSlotTemporaryState temporary_slot_state;

	/* All exit paths except success should revert the temporary slot state if required */
	auto slot_state_guard = scope_guard([&]() {
		if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(consist->index);
	});

	PBSTileInfo   origin = FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::OkayUnused);
	PBSTileInfo   res_dest(tile, INVALID_TRACKDIR, false);
	DiagDirection dest_enterdir = enterdir;
	if (do_track_reservation) {
		res_dest = ExtendTrainReservation(consist, origin, &tracks, &dest_enterdir, temporary_slot_state);
		if (res_dest.tile == INVALID_TILE) {
			/* Reservation failed? */
			if (mark_stuck) MarkTrainAsStuck(consist);
			if (changed_signal != INVALID_TRACKDIR) SetSignalStateByTrackdir(tile, changed_signal, SignalState::Red);
			return { FindFirstTrack(tracks), result_flags };
		}
		if (res_dest.okay) {
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(consist);
			bool long_reserve = (CheckLongReservePbsTunnelBridgeOnTrackdir(consist, res_dest.tile, res_dest.trackdir) != INVALID_TILE);
			if (!long_reserve) {
				CFollowTrackRail ft(consist);
				if (ft.Follow(res_dest.tile, res_dest.trackdir)) {
					Trackdir  new_td = FindFirstTrackdir(ft.new_td_bits);
					long_reserve = HasLongReservePbsSignalOnTrackdir(consist, ft.new_tile, new_td, _settings_game.vehicle.train_braking_model == TBM_REALISTIC, lookahead_state.flags);
				}
			}

			if (!long_reserve) {
				/* Got a valid reservation that ends at a safe target, quick exit. */
				result_flags |= CTTRF_RESERVATION_MADE;
				if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
				if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) {
					const Train *moving_front = consist->GetMovingFront();
					TryReserveRailTrack(moving_front->tile, TrackdirToTrack(moving_front->GetVehicleTrackdir()));
				}
				if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(consist);
				return { best_track, result_flags };
			}
		}

		/* Check if the train needs service here, so it has a chance to always find a depot.
		 * Also check if the current order is a service order so we don't reserve a path to
		 * the destination but instead to the next one if service isn't needed. */
		CheckIfTrainNeedsService(consist);
		if (consist->current_order.IsType(OT_DUMMY) || consist->current_order.IsType(OT_CONDITIONAL) || consist->current_order.IsType(OT_GOTO_DEPOT) ||
				consist->current_order.IsSlotCounterOrder() || consist->current_order.IsType(OT_LABEL)) {
			ProcessOrders(consist);
		}
	}

	/* Save the current train order. The destructor will restore the old order on function exit. */
	VehicleOrderSaver orders(consist);

	if (lookahead_state.order_items_start == 0) {
		orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
	}
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) orders.AdvanceOrdersFromLookahead(lookahead_state);

	/* When going to couple with another train, use the couple pathfinder to
	 * follow the waiting train's reservation. This lets the locomotive reserve
	 * up to the consist's own reservation (which the normal pathfinder treats
	 * as an obstacle and fails on). The couple destination
	 * (CYapfDestinationTrainRailT) only accepts a consist whose current order is
	 * WAIT_COUPLE and which satisfies the GOTO_COUPLE order requirements, so the
	 * locomotive does not wander to an arbitrary consist. */
	if (consist->current_order.IsType(OT_GOTO_COUPLE)) {
		/* R3R: always reserve the couple path - the loco needs a reservation
		 * to move, and it must extend right up to the consist. Without this the
		 * loco only reserves on force_res ticks and then moves without one. */
		Track path_found = DoTrainCouplePathfind(consist, true);
		/* R3R: the couple pathfinder starts from the loco itself and always
		 * returns a valid departure direction, so use it regardless of where
		 * ExtendTrainReservation ended up (res_dest.tile != tile when the
		 * consist platform reservation is reached first). Without this the
		 * loco never moves and never shows a reservation. */
		if (path_found != INVALID_TRACK) {
			best_track = path_found;
		}
		if (path_found == INVALID_TRACK) {
			if (mark_stuck) MarkTrainAsStuck(consist);
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
			return { FindFirstTrack(tracks), result_flags };
		}
		result_flags |= CTTRF_RESERVATION_MADE;
	}

	if (res_dest.tile != INVALID_TILE && !res_dest.okay && !consist->current_order.IsType(OT_GOTO_COUPLE)) {
		/* Pathfinders are able to tell that route was only 'guessed'.
		 * R3R: a GOTO_COUPLE locomotive's route is chosen and reserved by the
		 * couple pathfinder (CPL) above. The normal pathfinder treats the
		 * waiting consist as an obstacle and fails with "no route", which
		 * calls HandlePathfindingResult(false) and leaves the loco stuck. */
		bool      path_found = true;
		TileIndex new_tile = res_dest.tile;

		Track next_track = DoTrainPathfind(consist, new_tile, dest_enterdir, tracks, path_found, do_track_reservation, &res_dest, &final_dest);
		DEBUG_UPDATESTATECHECKSUM("ChooseTrainTrack: consist: {}, path_found: {}, next_track: {}", consist->index, path_found, next_track);
		UpdateStateChecksum((((uint64_t) consist->index.base()) << 32) | (path_found << 16) | next_track);
		if (new_tile == tile && HasTrack(DiagdirReachesTracks(enterdir), next_track)) best_track = next_track;
		consist->HandlePathfindingResult(path_found);
	}

	/* No track reservation requested -> finished. */
	if (!do_track_reservation) return { best_track, result_flags };

	/* A path was found, but could not be reserved. */
	if (res_dest.tile != INVALID_TILE && !res_dest.okay) {
		/* R3R: a GOTO_COUPLE loco's couple pathfinder already reserved the
		 * path up to the consist. ExtendTrainReservation stops at the
		 * already-reserved consist platform (res_dest.okay == false), and
		 * freeing the reservation here would wipe the couple reservation,
		 * leaving the loco without one. Keep it and return. */
		if (consist->current_order.IsType(OT_GOTO_COUPLE)) return { best_track, result_flags };
		if (mark_stuck) MarkTrainAsStuck(consist);
		FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
		return { best_track, result_flags };
	}

	/* No possible reservation target found, we are probably lost. */
	if (res_dest.tile == INVALID_TILE) {
		/* Try to find any safe destination. */
		PBSTileInfo path_end = FollowTrainReservation(consist, nullptr, FollowTrainReservationFlag::OkayUnused);
		if (TryReserveSafeTrack(consist, path_end.tile, path_end.trackdir, false)) {
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(consist);
			TrackBits res = GetReservedTrackbits(tile) & DiagdirReachesTracks(enterdir);
			best_track = FindFirstTrack(res);
			if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) {
				const Train *moving_front = consist->GetMovingFront();
				TryReserveRailTrack(moving_front->tile, TrackdirToTrack(moving_front->GetVehicleTrackdir()));
			}
			result_flags |= CTTRF_RESERVATION_MADE;
			if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);
			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(consist);
		} else {
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(consist);
		}
		return { best_track, result_flags };;
	}

	result_flags |= CTTRF_RESERVATION_MADE;

	auto check_destination_seen = [&](TileIndex tile) {
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC && consist->current_order.IsBaseStationOrder() &&
				HasStationTileRail(tile)) {
			if (consist->current_order.ShouldStopAtStation(consist, GetStationIndex(tile), IsRailWaypoint(tile))) {
				SetBit(lookahead_state.flags, CTTLASF_STOP_FOUND);
			} else if (consist->current_order.IsType(OT_GOTO_WAYPOINT) && consist->current_order.GetDestination() == GetStationIndex(tile) && consist->current_order.GetWaypointFlags().Test(OrderWaypointFlag::Reverse)) {
				if (!HasBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND)) {
					SetBit(lookahead_state.flags, CTTLASF_REVERSE_FOUND);
					lookahead_state.reverse_dest = GetStationIndex(tile);
				}
			}
		}
	};

	check_destination_seen(res_dest.tile);

	/* Reservation target found and free, check if it is safe. */
	while (!IsSafeWaitingPosition(consist, res_dest.tile, res_dest.trackdir, true, _settings_game.pf.forbid_90_deg)) {
		/* Extend reservation until we have found a safe position. */
		DiagDirection exitdir = TrackdirToExitdir(res_dest.trackdir);
		TileIndex     next_tile = TileAddByDiagDir(res_dest.tile, exitdir);
		TrackBits     reachable = TrackdirBitsToTrackBits(GetTileTrackdirBits(next_tile, TRANSPORT_RAIL, 0)) & DiagdirReachesTracks(exitdir);
		if (Rail90DegTurnDisallowedTilesFromDiagDir(res_dest.tile, next_tile, exitdir, _settings_game.pf.forbid_90_deg)) {
			reachable &= ~TrackCrossesTracks(TrackdirToTrack(res_dest.trackdir));
		}

		/* Get next order with destination. */
		if (orders.SwitchToNextOrder(true)) {
			PBSTileInfo cur_dest;
			bool path_found;
			DoTrainPathfind(consist, next_tile, exitdir, reachable, path_found, true, &cur_dest, nullptr);
			if (cur_dest.tile != INVALID_TILE) {
				res_dest = cur_dest;
				if (res_dest.okay) {
					check_destination_seen(res_dest.tile);
					continue;
				}
				/* Path found, but could not be reserved. */
				FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
				if (mark_stuck) MarkTrainAsStuck(consist);
				result_flags &= ~CTTRF_RESERVATION_MADE;
				changed_signal = INVALID_TRACKDIR;
				if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(consist->index);
				break;
			}
		}
		/* No order or no safe position found, try any position. */
		if (!TryReserveSafeTrack(consist, res_dest.tile, res_dest.trackdir, true)) {
			FreeTrainTrackReservation(consist, origin.tile, origin.trackdir);
			if (mark_stuck) MarkTrainAsStuck(consist);
			result_flags &= ~CTTRF_RESERVATION_MADE;
			changed_signal = INVALID_TRACKDIR;
			if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackRevertTemporaryChanges(consist->index);
		}
		break;
	}

	if (result_flags & CTTRF_RESERVATION_MADE) {
		if (temporary_slot_state.IsActive()) temporary_slot_state.PopFromChangeStackApplyTemporaryChanges(consist);
		if (consist->current_order.IsBaseStationOrder() && HasStationTileRail(res_dest.tile) && consist->current_order.GetDestination() == GetStationIndex(res_dest.tile)) {
			if (consist->current_order.ShouldStopAtStation(consist, consist->current_order.GetDestination().ToStationID(), consist->current_order.IsType(OT_GOTO_WAYPOINT))) {
				consist->last_station_visited = consist->current_order.GetDestination().ToStationID();
			}
			orders.SwitchToNextOrder(true);
		}
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(consist);
			if (consist->lookahead != nullptr) lookahead_state.order_items_start = (uint)consist->lookahead->items.size();
		}
		TryLongReserveChooseTrainTrack(consist, res_dest.tile, res_dest.trackdir, (flags & CTTF_FORCE_RES), lookahead_state);
	}

	if (!HasBit(lookahead_state.flags, CTTLASF_NO_RES_VEH_TILE)) {
		const Train *moving_front = consist->GetMovingFront();
		TryReserveRailTrack(moving_front->tile, TrackdirToTrack(moving_front->GetVehicleTrackdir()));
	}

	if (changed_signal != INVALID_TRACKDIR) MarkSingleSignalDirty(tile, changed_signal);

	orders.Restore();
	if (consist->current_order.IsType(OT_GOTO_DEPOT) &&
			(consist->current_order.GetDepotActionType() & ODATFB_NEAREST_DEPOT) &&
			final_dest != INVALID_TILE && IsRailDepotTile(final_dest)) {
		consist->current_order.SetDestination(GetDepotIndex(final_dest));
		consist->dest_tile = final_dest;
		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
	}

	return { best_track, result_flags };
}

/* R3R (KI-199 probe): one line per *distinct* reservation attempt, so "coupling never
 * reserves anything" can be told apart from "it tries and fails", and a failure can be
 * attributed to the order / destination the train was holding. The context (order type,
 * destination, speed, force-proceed) is part of the payload, so a train failing in
 * exactly the same state logs once instead of every tick. Attempts made while the
 * one-shot ability of KI-179 is pending ("a coupling just completed") are always
 * logged, because that is the window the player is looking at. lookahead= tells the two
 * very different cases apart: extending an existing reservation (lookahead!=null)
 * versus asking for a brand new one after the coupling invalidated both halves. */
static void R3RLogReserveAttempt(Train *consist, bool mark_as_stuck, bool first_tile_okay, TryPathReserveResultFlags res)
{
	if (!R3RDbgOn() || consist == nullptr) return;

	const bool one_shot = consist->HasForceReserveOnce();
	const uint64_t payload = (uint64_t)(uint8_t)res
			| ((uint64_t)(uint32_t)consist->current_order.GetType() << 8)
			| ((uint64_t)(uint32_t)consist->dest_tile.base() << 16)
			| ((uint64_t)(consist->cur_speed != 0 ? 1 : 0) << 48);
	if (!one_shot && !R3RDbgEdge(R3REDGE_TRP, (uint64_t)consist->index.base(), payload)) return;

	Train *mf = consist->GetMovingFront();
	FILE *dbg = R3RFopenDbg("a");
	if (dbg != nullptr) {
		fprintf(dbg, "TRP veh=%d mf=%d,%d real=%d type=%d dest=%u spd=%d fp=%d look=%d one=%d mstuck=%d fto=%d => ok=%d res=%d\n",
				(int)consist->index.base(), (int)TileX(mf->tile), (int)TileY(mf->tile),
				(int)consist->cur_real_order_index, (int)consist->current_order.GetType(),
				(unsigned)consist->dest_tile.base(), (int)consist->cur_speed, (int)consist->force_proceed,
				consist->lookahead != nullptr ? 1 : 0, one_shot ? 1 : 0,
				mark_as_stuck ? 1 : 0, first_tile_okay ? 1 : 0,
				(res & TPRRF_RESERVATION_OK) ? 1 : 0, (int)(uint8_t)res);
		fclose(dbg);
	}
}

static TryPathReserveResultFlags R3RTryPathReserveCore(Train *consist, bool mark_as_stuck, bool first_tile_okay);

/**
 * R3R (KI-199): thin wrapper so that *every* reservation attempt becomes observable;
 * the real work stays in R3RTryPathReserveCore() below, which keeps the single exit
 * point of the original function irrelevant for the probe.
 */
TryPathReserveResultFlags TryPathReserveWithResultFlags(Train *consist, bool mark_as_stuck, bool first_tile_okay)
{
	/* R3R (KI-208): a consist parked on a platform waiting to be coupled must
	 * never book a route in the first place -- refusing the booking is what
	 * stops the ghost reservation preview from being *painted* at all.
	 *
	 * Field report 2026-09-25: the player saw the reservation appear and vanish
	 * ("the ghost is still issued first, only then destroyed -- this really
	 * hurts the game feel"). The log showed exactly that chain for the waypoint
	 * 60,16: RESV-WATCH SET-STN tile=60,16 phase=choose-track actor=29 head=29
	 * (the *waiting* consist, head 29, parked on the platform), and 233 log
	 * lines later RESV-WATCH CLEAR-STN ... phase=release-ghost actor=30 head=30
	 * -- i.e. the couple of the approaching locomotive swept the route its
	 * partner had booked. The whole route out of the platform (58,18 / 59,18 /
	 * 59,17 / 60,17 plus the waypoint itself) was affected, so every
	 * "RESV-GHOST couple/decouple/waiter ... tile=..." line in the log is the
	 * same single booking being undone.
	 *
	 * The waiter purge (KI-206, TrainLocoHandler "waiter" branch) already
	 * documented the assumption "while parked the consist never books a new
	 * route" and therefore sweeps only once per waiting episode; the log
	 * proved that assumption wrong. Refusing the reservation here makes it
	 * true: nothing is booked, so there is nothing left to sweep and nothing
	 * can flicker.
	 *
	 * Scope: exactly the parked platform waiter the KI-206 purge targets --
	 * standing still, on a station tile, either an R3R car-only formation or a
	 * primary train whose current order is OT_WAIT_COUPLE (it waits for a
	 * coupler by definition, it never drives that route itself). Only the
	 * *route* reservation is refused: ReserveTrackUnderConsist() (the
	 * protection of the tiles it stands on, which the couple pathfinder needs)
	 * and TrainCoupleHandler() both keep working, and the depot/plain-track
	 * cases are untouched. As soon as the couple lands the merged chain has a
	 * different order (and the formation identity is gone), so the gate stops
	 * applying and the merged train reserves normally. */
	if (consist->cur_speed == 0 && IsTileType(consist->tile, TileType::Station) &&
			(R3RIsCarOnlyFormation(consist) ||
			(consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE)))) {
		R3RLogReserveAttempt(consist, mark_as_stuck, first_tile_okay, TPRRF_NONE);
		if (_r3r_waiter_nobook_logged.insert(consist->index).second) {
			R3RDbgWrite("RESV-NOBOOK veh=%d tile=%d,%d order=%d caronly=%d reason=parked-waiter\n",
					(int)consist->index.base(), (int)TileX(consist->tile), (int)TileY(consist->tile),
					(int)consist->current_order.GetType(), (int)R3RIsCarOnlyFormation(consist));
		}
		return TPRRF_NONE;
	}

	const TryPathReserveResultFlags r3r_res = R3RTryPathReserveCore(consist, mark_as_stuck, first_tile_okay);
	R3RLogReserveAttempt(consist, mark_as_stuck, first_tile_okay, r3r_res);
	return r3r_res;
}

/**
 * Try to reserve a path to a safe position.
 *
 * @param consist The vehicle
 * @param mark_as_stuck Should the train be marked as stuck on a failed reservation?
 * @param first_tile_okay True if no path should be reserved if the current tile is a safe position.
 * @return Result flags.
 */
static TryPathReserveResultFlags R3RTryPathReserveCore(Train *consist, bool mark_as_stuck, bool first_tile_okay)
{
	dbg_assert(consist->IsFrontEngine());

	ClearLookAheadIfInvalid(consist);

	/* R3R (KI-179): the one-shot "independent reservation" ability granted when a
	 * coupling completes (VehicleRailFlag::ForceReserveOnce). The two halves were
	 * reserved separately before they were joined, so the merged train can end up
	 * with its front just past a signal it never reserved; this lets it make one
	 * reservation which ignores a red signal instead of being pushed into the
	 * force-proceed state. The ability is used up by the first *successful*
	 * reservation (whether or not the bypass was needed) and is kept while every
	 * attempt fails -- the train then takes the ordinary "waiting for free track"
	 * punishment (MarkTrainAsStuck), which is the behaviour the player asked for. */
	struct R3ROneShotReserveGuard {
		Train *consist;
		const bool armed;
		const TrainForceProceeding saved_force_proceed;
		bool reserved = false;

		~R3ROneShotReserveGuard()
		{
			if (!this->armed) return;
			if (this->reserved) {
				/* R3R probe: the ability has been spent (recorded once per grant). */
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "R3R-RES-ONCE used head=%d force_proceed=%d tile=%d,%d\n",
							(int)this->consist->index.base(), (int)this->consist->force_proceed,
							(int)TileX(this->consist->tile), (int)TileY(this->consist->tile));
					fclose(dbg);
				}
				this->consist->ClearForceReserveOnce();
			} else {
				/* Nothing could be reserved: keep the ability for the next attempt. */
				this->consist->force_proceed = this->saved_force_proceed;
			}

			/* R3R (KI-219, 2026-09-27): the bypass is there to *make* one reservation,
			 * not to be carried around afterwards. Every PBS signal on a reserved
			 * path is turned green -- TryReserveRailTrackdir() does it for the
			 * reservation itself (pbs.cpp) and ChooseTrainTrack() does it at the
			 * entry of the chosen track (train_cmd.cpp:10534) -- so a train that
			 * holds its reservation does not need to ignore a red signal. The bypass
			 * is only spent by passing two *plain rail* signal tiles (train_cmd.cpp:
			 * 12616), which a route over platforms, waypoints and depots never does,
			 * so in the player's crash report it was still set two minutes later,
			 * survived the decouple that replaced the whole path and let the train
			 * run into an unreserved block. Restoring the pre-guard value here is a
			 * no-op when the player had already forced this train (TFP_STUCK), and
			 * cannot re-introduce the original KI-179 problem: there the ability
			 * only matters for a reservation that *cannot* be made without it, and
			 * that case is `reserved == false`, which keeps the bypass anyway. */
			if (this->reserved && this->saved_force_proceed == TFP_NONE && this->consist->force_proceed == TFP_SIGNAL) {
				this->consist->force_proceed = TFP_NONE;
				InvalidateWindowData(WindowClass::VehicleView, this->consist->index);
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					fprintf(dbg, "R3R-RES-ONCE clear head=%d tile=%d,%d\n",
							(int)this->consist->index.base(),
							(int)TileX(this->consist->tile), (int)TileY(this->consist->tile));
					fclose(dbg);
				}
			}
		}
	} r3r_one_shot{consist, consist->HasForceReserveOnce(), consist->force_proceed};
	if (r3r_one_shot.armed && consist->force_proceed == TFP_NONE) consist->force_proceed = TFP_SIGNAL;

	if (consist->lookahead != nullptr && consist->lookahead->flags.Test(TrainReservationLookAheadFlag::DepotEnd)) {
		/* R3R (KI-189): this branch reports "reservation OK" without reserving a
		 * single tile - the lookahead already covers up to the depot end, so there
		 * is nothing to reserve. Marking the one-shot ability as spent here burned
		 * it while the merged train was still standing in a depot (probe evidence:
		 * grant and "used" both at tile 1,11 with tileDepot=1, no reservation in
		 * between), so the train left the depot with no independent reservation
		 * left. Leave reserved=false: the ability now survives until a reservation
		 * is really made, which is what it is for. */
		/* R3R (KI-221, 2026-09-27): this branch reports a *successful* reservation
		 * (the train is allowed to drive on it, so TrainLocoHandler() treats it as
		 * "not stuck any more"), therefore it must resolve the stuck state exactly
		 * like the ordinary success path at the end of this function does (see the
		 * SetWindowWidgetDirty/Reset(VehicleRailFlag::Stuck) pair there). Without
		 * this a consist that was flagged stuck shortly before can never leave the
		 * stuck state again: the only retry path -- TrainLocoHandler()'s stuck
		 * block -- reaches this branch, gets TPRRF_RESERVATION_OK and so never
		 * reaches that Reset(), which means the handler keeps returning early on
		 * every tick which is not a path_backoff_interval tick.
		 * Probe evidence (build/R3R_debug.log), decoupled locomotive heading for
		 * depot 1,11: "TRP veh=26 mf=4,11 ... mstuck=1 fto=1 => ok=0" at the
		 * decouple hook, then forever "TRP veh=26 mf=4,11 ... spd=0 mstuck=0 fto=0
		 * => ok=1 res=1" while "DEPOT-ARR veh=26 ... stuck=1 ... destDepot=1
		 * destResv=1" -- the train never moved off tile 4,11, cp. the green PBS
		 * signal and the reserved depot the player saw. */
		if (consist->flags.Test(VehicleRailFlag::Stuck)) {
			consist->wait_counter = 0;
			SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		}
		consist->flags.Reset(VehicleRailFlag::Stuck);
		return TPRRF_RESERVATION_OK;
	}

	Train *moving_front = consist->GetMovingFront();

	/* We have to handle depots specially as the track follower won't look
	 * at the depot tile itself but starts from the next tile. If we are still
	 * inside the depot, a depot reservation can never be ours. */
	if (moving_front->track == TRACK_BIT_DEPOT) {
		if (HasDepotReservation(moving_front->tile)) {
			if (mark_as_stuck) MarkTrainAsStuck(consist);
			return TPRRF_NONE;
		} else {
			/* Depot not reserved, but the next tile might be. */
			TileIndex next_tile = TileAddByDiagDir(moving_front->tile, GetRailDepotDirection(moving_front->tile));
			if (HasReservedTracks(next_tile, DiagdirReachesTracks(GetRailDepotDirection(moving_front->tile)))) return TPRRF_NONE;
		}
	}

	if (IsTileType(moving_front->tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExitOnly(moving_front->tile) &&
			TrackdirEntersTunnelBridge(moving_front->tile, moving_front->GetVehicleTrackdir())) {
		/* prevent any attempt to reserve the wrong way onto a tunnel/bridge exit */
		return TPRRF_NONE;
	}
	if (IsTunnelBridgeWithSignalSimulation(moving_front->tile) && ((moving_front->track & TRACK_BIT_WORMHOLE) || TrackdirEntersTunnelBridge(moving_front->tile, moving_front->GetVehicleTrackdir()))) {
		DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(moving_front->tile);
		Axis axis = DiagDirToAxis(tunnel_bridge_dir);
		DiagDirection axial_dir = DirToDiagDirAlongAxis(moving_front->GetMovingDirection(), axis);
		if (axial_dir == tunnel_bridge_dir) {
			/* prevent use of the entrance tile for reservations when the train is already in the wormhole */

			if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
				/* Initialise a lookahead if there isn't one already */
				if (consist->lookahead == nullptr) FillTrainReservationLookAhead(consist);
				if (consist->lookahead != nullptr && !LookaheadWithinCurrentTunnelBridge(consist)) {
					/* Try to extend the reservation beyond the tunnel/bridge exit */
					TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
				}
			} else {
				TileIndex exit = GetOtherTunnelBridgeEnd(moving_front->tile);
				TileIndex v_pos = TileVirtXY(moving_front->x_pos, moving_front->y_pos);
				if (v_pos != exit) {
					v_pos += TileOffsByDiagDir(tunnel_bridge_dir);
				}
				if (v_pos == exit) {
					if (!CheckTrainStayInWormHolePathReserve(consist, moving_front, exit)) return TPRRF_NONE;
					/* R3R (KI-222, 2026-09-27): same family as KI-221 (the
					 * DepotEnd fast path above). This return reports a successful
					 * reservation too, so it must resolve the stuck state like the
					 * ordinary success paths do, otherwise a consist that was
					 * flagged stuck shortly before can never recover on a
					 * tunnel/bridge route: the only retry path -- TrainLocoHandler()'s
					 * stuck block -- comes back here, is told "not stuck any more"
					 * and therefore never reaches the Reset(VehicleRailFlag::Stuck)
					 * of the ordinary success path, so the handler keeps bailing out
					 * on every tick that is not a path_backoff_interval tick (and
					 * the exit tile stays reserved, leaving the train standing still
					 * behind a green signal, exactly as KI-221 described it).
					 * R3R (KI-179) note: r3r_one_shot.reserved is deliberately *not*
					 * set here -- the helper's true can also mean "the lookahead
					 * already covered the exit" instead of "just reserved
					 * something", so consuming the one-shot ability stays a separate
					 * decision (same reasoning as the DepotEnd branch above). */
					if (consist->flags.Test(VehicleRailFlag::Stuck)) {
						consist->wait_counter = 0;
						SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
					}
					consist->flags.Reset(VehicleRailFlag::Stuck);
					return TPRRF_RESERVATION_OK;
				}
			}
			return TPRRF_NONE;
		}
	}

	Vehicle *other_train = nullptr;
	PBSTileInfo origin = FollowTrainReservation(consist, &other_train);
	/* The path we are driving on is already blocked by some other train.
	 * This can only happen in certain situations when mixing path and
	 * block signals or when changing tracks and/or signals.
	 * Exit here as doing any further reservations will probably just
	 * make matters worse. */
	if (other_train != nullptr && other_train->index != consist->index) {
		if (mark_as_stuck) MarkTrainAsStuck(consist);
		return TPRRF_NONE;
	}
	/* If we have a reserved path and the path ends at a safe tile, we are finished already. */
	if (origin.okay && (moving_front->tile != origin.tile || first_tile_okay)) {
		/* Can't be stuck then. */
		if (consist->flags.Test(VehicleRailFlag::Stuck)) SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		consist->flags.Reset(VehicleRailFlag::Stuck);
		if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
			FillTrainReservationLookAhead(consist);
			TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
		}
		/* R3R (KI-179): the path is reserved, so the one-shot ability is used up. */
		r3r_one_shot.reserved = true;
		return TPRRF_RESERVATION_OK;
	}

	/* If we are in a depot, tentatively reserve the depot. */
	if (moving_front->track == TRACK_BIT_DEPOT && moving_front->tile == origin.tile) {
		SetDepotReservation(moving_front->tile, true);
		if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(moving_front->tile, VMDF_NOT_MAP_MODE);
	}

	DiagDirection exitdir = TrackdirToExitdir(origin.trackdir);
	TileIndex new_tile;
	if (IsTileType(origin.tile, TileType::TunnelBridge) && GetTunnelBridgeDirection(origin.tile) == exitdir) {
		new_tile = GetOtherTunnelBridgeEnd(origin.tile);
	} else {
		new_tile = TileAddByDiagDir(origin.tile, exitdir);
	}
	TrackBits reachable = TrackdirBitsToTrackBits(GetTileTrackdirBits(new_tile, TRANSPORT_RAIL, 0) & DiagdirReachesTrackdirs(exitdir));

	if (Rail90DegTurnDisallowedTilesFromDiagDir(origin.tile, new_tile, exitdir, _settings_game.pf.forbid_90_deg)) reachable &= ~TrackCrossesTracks(TrackdirToTrack(origin.trackdir));

	TryPathReserveResultFlags result_flags = TPRRF_NONE;
	if (reachable != TRACK_BIT_NONE) {
		ChooseTrainTrackResult result = ChooseTrainTrack(consist, new_tile, exitdir, reachable, CTTF_FORCE_RES | (mark_as_stuck ? CTTF_MARK_STUCK : CTTF_NONE));
		if (result.ctt_flags & CTTRF_RESERVATION_MADE) {
			result_flags |= TPRRF_RESERVATION_OK;
		} else if (result.ctt_flags & CTTRF_REVERSE_AT_SIGNAL) {
			result_flags |= TPRRF_REVERSE_AT_SIGNAL;
		}
	}

	if ((result_flags & TPRRF_RESERVATION_OK) == 0) {
		/* Free the depot reservation as well. */
		if (moving_front->track == TRACK_BIT_DEPOT && moving_front->tile == origin.tile) SetDepotReservation(moving_front->tile, false);
		return result_flags;
	}

	/* R3R (KI-179): a reservation was made -- the one-shot ability is used up, no
	 * matter whether it was actually needed or the signals allowed the path anyway. */
	r3r_one_shot.reserved = true;

	if (consist->flags.Test(VehicleRailFlag::Stuck)) {
		consist->wait_counter = 0;
		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
	}
	consist->flags.Reset(VehicleRailFlag::Stuck);
	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) FillTrainReservationLookAhead(consist);
	return result_flags;
}

/**
 * Can the train reverse?
 * @param consist The train to check.
 * @return \c true iff the train can be reversed.
 */
static bool CheckReverseTrain(const Train *consist)
{
	const Train *moving_front = consist->GetMovingFront();
	if (_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::EndOfLineOnly ||
			moving_front->track == TRACK_BIT_DEPOT) {
		return false;
	}

	dbg_assert(moving_front->track != TRACK_BIT_NONE);

	return YapfTrainCheckReverse(consist);
}

/**
 * Get the location of the next station to visit.
 * @param station Next station to visit.
 * @return Location of the new station.
 */
TileIndex Train::GetOrderStationLocation(StationID station)
{
	if (station == this->last_station_visited) this->last_station_visited = StationID::Invalid();

	const Station *st = Station::Get(station);
	if (!st->facilities.Test(StationFacility::Train)) {
		/* The destination station has no trainstation tiles. */
		this->IncrementRealOrderIndex();
		return {};
	}

	return st->xy;
}

/** Goods at the consist have changed, update the graphics, cargo, and acceleration. */
void Train::MarkDirty()
{
	Train *v = this;
	do {
		v->colourmap = PAL_NONE;
		v->InvalidateImageCache();
		v->UpdateViewport(true, false);
	} while ((v = v->Next()) != nullptr);

	/* need to update acceleration and cached values since the goods on the train changed. */
	this->CargoChanged();
	this->UpdateAcceleration();
}

/**
 * This function looks at the vehicle and updates its speed (cur_speed
 * and subspeed) variables. Furthermore, it returns the distance that
 * the train can drive this tick. #Vehicle::GetAdvanceDistance() determines
 * the distance to drive before moving a step on the map.
 * @return distance to drive.
 */
int Train::UpdateSpeed(MaxSpeedInfo max_speed_info)
{
	AccelStatus accel_status = this->GetAccelerationStatus();
	if (this->lookahead != nullptr && this->lookahead->flags.Test(TrainReservationLookAheadFlag::ApplyAdvisory) && this->cur_speed <= max_speed_info.strict_max_speed) {
		this->lookahead->flags.Reset(TrainReservationLookAheadFlag::ApplyAdvisory);
	}
	switch (_settings_game.vehicle.train_acceleration_model) {
		default: NOT_REACHED();
		case AM_ORIGINAL:
			return this->DoUpdateSpeed({ this->acceleration * (accel_status == AS_BRAKE ? -4 : 2), this->acceleration * -4 }, 0,
					max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed, this->UsingRealisticBraking());

		case AM_REALISTIC:
			return this->DoUpdateSpeed(this->GetAcceleration(), accel_status == AS_BRAKE ? 0 : 2,
					max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed, this->UsingRealisticBraking());
	}
}
/**
 * Handle all breakdown related stuff for a train consist.
 * @param v The front engine.
 */
static bool HandlePossibleBreakdowns(Train *v)
{
	dbg_assert(v->IsFrontEngine());
	for (Train *u = v; u != nullptr; u = u->Next()) {
		if (u->breakdown_ctr != 0 && (u->IsEngine() || u->IsMultiheaded())) {
			if (u->breakdown_ctr <= 2) {
				if (u->HandleBreakdown()) return true;
				/* We check the order of v (the first vehicle) instead of u here! */
			} else if (!v->current_order.IsType(OT_LOADING)) {
				u->breakdown_ctr--;
			}
		}
	}
	return false;
}

/**
 * Trains enters a station, send out a news item if it is the first train, and start loading.
 * @param consist Train that entered the station.
 * @param station Station visited.
 */
static void TrainEnterStation(Train *consist, StationID station)
{
	/* R3R: a waiting consist (WAIT_COUPLE) must NOT run the normal station
	 * arrival flow — it waits passively to be coupled onto. The normal flow
	 * would start loading and advance the current order, overwriting the
	 * WAIT_COUPLE state (observed: the consist then "executes" the first
	 * order of its schedule again). */
	if (consist->current_order.IsType(OT_WAIT_COUPLE)) return;

	consist->last_station_visited = station;

	BaseStation *bst = BaseStation::Get(station);

	if (Waypoint::IsExpected(bst)) {
		consist->DeleteUnreachedImplicitOrders();
		UpdateVehicleTimetable(consist, true);
		consist->last_station_visited = station;
		consist->force_proceed = TFP_NONE;
		SetWindowDirty(WindowClass::VehicleView, consist->index);
		consist->current_order.MakeWaiting();
		consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
		consist->cur_speed = 0;
		consist->UpdateTrainSpeedAdaptationLimit(0);
		return;
	}

	/* check if a train ever visited this station before */
	Station *st = Station::From(bst);
	if (!st->had_vehicle_of_type.Test(StationVehicleType::Train)) {
		st->had_vehicle_of_type.Set(StationVehicleType::Train);
		AddVehicleNewsItem(
			GetEncodedString(STR_NEWS_FIRST_TRAIN_ARRIVAL, st->index),
			consist->owner == _local_company ? NewsType::ArrivalCompany : NewsType::ArrivalOther,
			consist->index,
			st->index
		);
		AI::NewEvent(consist->owner, new ScriptEventStationFirstVehicle(st->index, consist->index));
		Game::NewEvent(new ScriptEventStationFirstVehicle(st->index, consist->index));
	}

	consist->force_proceed = TFP_NONE;
	InvalidateWindowData(WindowClass::VehicleView, consist->index);

	/* GOTO_COUPLE: do not stop at the platform edge, keep creeping forward
	 * until the waiting consist is reached and the coupling handler fires. */
	if (consist->current_order.IsType(OT_GOTO_COUPLE)) {
		/* DEBUG (R3R — remove): log the station arrival of a GOTO_COUPLE train. */
		{
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "SA-GOTO_COUPLE: veh=%d real_idx=%d implicit_idx=%d speed=%d\n",
						(int)consist->index.base(), (int)consist->cur_real_order_index, (int)consist->cur_implicit_order_index, (int)consist->cur_speed);
				fclose(dbg);
			}
		}
		if (consist->cur_speed > 20) consist->cur_speed = 20;
		consist->UpdateTrainSpeedAdaptationLimit(0);
		return;
	}


	/* R3R: 检修所（workshop）—— 只有「订单目的地明确指向某个检修所场」且本车确实
	 * 停在该场的 tile 上时才触发进厂检修（与车库检修同效）。只是路过、或者被引到
	 * 同一个车站的别的场时都不检修。必须赶在 BeginLoading() 把 current_order
	 * 改写成 OT_LOADING 之前读取目的地场。 */
	if (consist->current_order.IsType(OT_GOTO_STATION)) {
		const uint16_t dest_yard = consist->current_order.GetR3RYard();
		if (dest_yard != Station::R3R_YARD_NONE && st->R3RIsYardWorkshop(dest_yard)) {
			const TileIndex load_tile = consist->GetStationLoadingVehicle()->tile;
			if (IsRailStationTile(load_tile) && GetStationIndex(load_tile) == st->index &&
					st->R3RGetYardOfTile(load_tile) == dest_yard) {
				VehicleServiceInDepot(consist);
			}
		}
	}

	consist->BeginLoading();

	/* (R3R) DECOUPLE handling moved to TrainLocoHandler so it can trigger from
	 * any stopped state (station, depot, after WAIT_COUPLE), not only right
	 * after arriving at a station. */

	TileIndex station_tile = consist->GetStationLoadingVehicle()->tile;
	TriggerStationRandomisation(st, station_tile, StationRandomTrigger::VehicleArrives);
	TriggerStationAnimation(st, station_tile, StationAnimationTrigger::VehicleArrives);
}

/**
 * Check if the vehicle is compatible with the specified tile.
 * @param v The train to check.
 * @param tile The tile to check.
 * @param check_railtype Should we check the railtype for compatibility?
 * @return \c true iff the tile is compatible with the train.
 */
static inline bool CheckCompatibleRail(const Train *v, TileIndex tile, DiagDirection enterdir, bool check_railtype)
{
	return IsInfraTileUsageAllowed(VehicleType::Train, v->owner, tile) &&
			(!check_railtype || v->compatible_railtypes.Test(GetRailTypeByEntryDir(tile, enterdir)));
}

/** Data structure for storing engine speed changes of an acceleration type. */
struct AccelerationSlowdownParams {
	uint8_t small_turn; ///< Speed change due to a small turn.
	uint8_t large_turn; ///< Speed change due to a large turn.
	uint8_t z_up;       ///< Fraction to remove when moving up.
	uint8_t z_down;     ///< Fraction to add when moving down.
};

/** Speed update fractions for each acceleration type. */
static const AccelerationSlowdownParams _accel_slowdown[] = {
	/* normal accel */
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< normal
	{256 / 4, 256 / 2, 256 / 4, 2}, ///< monorail
	{0,       256 / 2, 256 / 4, 2}, ///< maglev
};

/**
 * Modify the speed of the vehicle due to a change in altitude.
 * @param consist %Train to update.
 * @param z_diff Z difference new - old.
 */
static inline void AffectSpeedByZChange(Train *consist, int z_diff)
{
	if (z_diff == 0 || _settings_game.vehicle.train_acceleration_model != AM_ORIGINAL) return;

	const AccelerationSlowdownParams *asp = &_accel_slowdown[static_cast<uint>(consist->GetAccelerationType())];

	if (z_diff > 0) {
		consist->cur_speed -= (consist->cur_speed * asp->z_up >> 8);
	} else {
		uint16_t spd = consist->cur_speed + asp->z_down;
		if (spd <= consist->gcache.cached_max_track_speed) consist->cur_speed = spd;
	}
}

enum TrainMovedChangeSignalEnum {
	CHANGED_NOTHING, ///< No special signals were changed
	CHANGED_NORMAL_TO_PBS_BLOCK, ///< A PBS block with a non-PBS signal facing us
	CHANGED_LR_PBS ///< A long reserve PBS signal
};

static TrainMovedChangeSignalEnum TrainMovedChangeSignal(Train *consist, TileIndex tile, DiagDirection dir, bool is_front)
{
	if (IsTileType(tile, TileType::Railway) &&
			GetRailTileType(tile) == RailTileType::Signals) {
		TrackdirBits tracks = TrackBitsToTrackdirBits(GetTrackBits(tile)) & DiagdirReachesTrackdirs(dir);
		Trackdir trackdir = FindFirstTrackdir(tracks);
		/* R3R KI-108: an inconsistent vehicle direction/track state can leave no
		 * trackdir reachable from \a dir; TrackdirToExitdir() would assert on it. */
		if (trackdir != INVALID_TRACKDIR && UpdateSignalsOnSegment(tile,  TrackdirToExitdir(trackdir), GetTileOwner(tile)) == SigSegState::Path && HasSignalOnTrackdir(tile, trackdir)) {
			/* A PBS block with a non-PBS signal facing us? */
			if (!IsPbsSignal(GetSignalType(tile, TrackdirToTrack(trackdir)))) return CHANGED_NORMAL_TO_PBS_BLOCK;

			if (is_front && HasLongReservePbsSignalOnTrackdir(consist, tile, trackdir, _settings_game.vehicle.train_braking_model == TBM_REALISTIC, 0)) return CHANGED_LR_PBS;
		}
	}
	if (IsTileType(tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExit(tile) && GetTunnelBridgeDirection(tile) == ReverseDiagDir(dir)) {
		if (UpdateSignalsOnSegment(tile, dir, GetTileOwner(tile)) == SigSegState::Path) {
			return CHANGED_NORMAL_TO_PBS_BLOCK;
		}
	}
	if (is_front && _settings_game.vehicle.train_braking_model == TBM_REALISTIC && IsTileType(tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationEntrance(tile)) {
		TrackdirBits tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(tile)) & DiagdirReachesTrackdirs(dir);
		Trackdir trackdir = FindFirstTrackdir(tracks);
		if (trackdir != INVALID_TRACKDIR && CheckLongReservePbsTunnelBridgeOnTrackdir(consist, tile, trackdir) != INVALID_TILE) return CHANGED_LR_PBS;
	}

	return CHANGED_NOTHING;
}

/**
 * R3R debug helper: log call sites that hand a non-single-track TrackBits
 * value to TrackBitsToTrack() (which asserts exactly one track). Catches
 * broken per-vehicle track state (e.g. folded chains after couple/reverse).
 */
static inline void R3RTrackBitsProbe(const char *where, TrackBits tb, int veh, TileIndex tile)
{
	if (tb != TRACK_BIT_NONE && KillFirstBit(tb & TRACK_BIT_MASK) == TRACK_BIT_NONE) return;
	/* KI-14 (1): edge-triggered -- a per-vehicle track state that stays broken
	 * would otherwise log once per tick per vehicle (seen as TTB-PROBE flood). */
	const uint64_t ttb_key = ((uint64_t)(uint32_t)veh << 32) | (uint64_t)tile.base();
	const uint64_t ttb_payload = ((uint64_t)(uint)tb << 32) | R3RDbgTagHash(where);
	if (R3RDbgEdge(R3REDGE_TTB, ttb_key, ttb_payload)) {
		R3RDbgWrite("TTB-PROBE %s FAIL tracks=0x%X veh=%d tile=%d,%d\n",
				where, (uint)tb, veh, (int)TileX(tile), (int)TileY(tile));
	}
}

/* --- R3R (KI-203): reservation bookkeeping for waiting consists -----------
 * ReserveTrackUnderConsist() only ever *adds* track reservations, and the only
 * matching release (ClearReservationUnderConsist()) walks the tiles the consist
 * occupies *at the moment it is called*. Whenever the consist has moved in the
 * meantime, or a single vehicle carries a stale tile for one tick (logical
 * flip, splice, depot drag), the tile that was really reserved is never visited
 * again and its reservation bits stay set forever: a black blob of reserved
 * track in the middle of empty line, which the signalling system then treats as
 * an obstacle. Player report 2026-09-25 -- "black reservation preview while the
 * train's next order is a plain GOTO_STATION, followed it to tile 60,16" -- and
 * the earlier "(60,51)-(60,54) mysteriously occupied" (KI-163) are the same
 * symptom, and R3R_debug.log holding no line for that tile at all is exactly
 * what a *stale* reservation looks like: the probe fired when it was made.
 * The table below records what we reserved per consist (chain head index), so
 * we can release precisely those bits and never somebody else's. */
struct R3RResvRec {
	TileIndex tile;
	Track track;
};

static std::unordered_map<uint32_t, std::vector<R3RResvRec>> _r3r_consist_resv;
static uint8_t _r3r_consist_resv_gc_clock = 0;

/** R3R: does this consist hold its platform reservation on purpose? (Same gate
 * as the platform-waiter call site in TrainLocoHandler.) */
static bool R3RIsReservationHolder(const Train *consist)
{
	return consist != nullptr && (R3RIsCarOnlyFormation(consist) ||
			(consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE)));
}

/**
 * R3R (KI-203b): release one booked reservation without tripping over tiles that
 * changed type since it was booked (track lifted, tile rebuilt into a station,
 * depot, tunnel bridge or level crossing).
 *
 * UnreserveTrack() is the low level plain-rail helper: it goes through
 * HasTrack() -> GetTrackBits(), which dbg_asserts IsPlainRailTile()
 * (rail_map.h:168). Calling it on a booked tile that has been rebuilt since is
 * what crashed the game -- the bookkeeping has to be defensive here, because it
 * by design looks at tiles the consist *left* long ago.
 *
 * Anything we cannot prove to be a plain rail tile which still carries our bit
 * is left untouched and only dropped from the books.
 */
static void R3RReleaseReservationSafe(uint32_t key, TileIndex tile, Track track, const char *why)
{
	/* KI-207c: a station platform or a waypoint does not carry per-track
	 * reservations but one whole-tile bit (station_map.h SetRailStationReservation),
	 * and ReserveTrackUnderConsist() books those tiles too -- a consist parked on a
	 * waypoint by the R3R platform waiter reserves exactly that tile. Until now the
	 * release refused everything that is not plain rail (HasTrack() -> rail_map.h:168
	 * asserts IsPlainRailTile), which made the bit permanent: no engine path ever
	 * clears a *waypoint* tile either, because every platform release site
	 * (SetRailStationPlatformReservation) is guarded by IsRailStationTile(), which
	 * by definition excludes waypoints. That is the ghost the player reported --
	 * only on the waypoint tile, one tile wide, gone once the waypoint is gone.
	 *
	 * The platform bit is shared between the tracks of that tile, so only release it
	 * when no train stands on the tile and the engine cannot attribute the
	 * reservation to anybody -- the same "nobody owns this path" criterion the audit
	 * uses. */
	if (IsValidTile(tile) && GetTileType(tile) == TileType::Station && HasStationRail(tile)) {
		const bool ours = HasStationReservation(tile) &&
				GetFirstVehicleOnTile(tile, VehicleType::Train) == nullptr &&
				GetTrainForWholeTileReservation(tile) == nullptr;
		if (ours) {
			const TrackBits rail_bits = GetStationReservationTrackBits(tile);
			if (rail_bits != TRACK_BIT_NONE) {
				R3RResvPhaseGuard phase_guard("gc-stn");
				UnreserveRailTrack(tile, FindFirstTrack(rail_bits));
			}
		}
		R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=stn kind=%s why=%s%s\n",
				key, (int)TileX(tile), (int)TileY(tile), IsRailWaypointTile(tile) ? "WAYPOINT" : "STATION",
				why, ours ? "" : "-skip");
		return;
	}

	const bool ours = IsValidTile(tile) && GetTileType(tile) == TileType::Railway &&
			IsPlainRailTile(tile) && HasTrack(tile, track) &&
			HasBit(GetRailReservationTrackBits(tile), track);
	if (ours) UnreserveTrack(tile, track);
	R3RDbgWrite("RESV-GC veh=%u tile=%d,%d track=0x%X why=%s%s\n",
			key, (int)TileX(tile), (int)TileY(tile), (uint)track, why, ours ? "" : "-skip");
}

/**
 * R3R (KI-203c/KI-203d): read-only audit of the reservations on the map.
 *
 * KI-203d (2026-09-25, after the player's track-removal experiment): do not
 * guess the owner from "nearest train distance" any more -- ask the engine the
 * very same question the track removal command asks. CmdRemoveRailTrack does
 *
 *     if (HasReservedTracks(tile, trackbit)) {
 *         v = GetTrainForReservation(tile, track);
 *         if (v != nullptr) { ...may refuse the removal... }
 *     }
 *     ... if (v != nullptr) FreeTrainTrackReservation(v);
 *
 * so GetTrainForReservation() returning nullptr IS "nobody owns this path, the
 * player can lift that piece of track" -- it follows the reserved path to both
 * ends and looks for the train standing there (pbs.cpp, "nullptr if the path is
 * stray").
 *
 * The distance heuristic misfired on exactly the reported tile 60,16: the path
 * was veh=30's own couple path reservation running 60,31 -> 60,16 -> 52,15
 * (REACH/RP probe), the owner sat 15 tiles away, and the old "best > 24" filter
 * therefore never printed it -- while in game the player could *not* remove that
 * track, i.e. the reservation was perfectly alive.
 *
 * Safe to call GetTrainForReservation() here: the entry tile is a plain rail
 * tile carrying our bit (the loop checks both), and FollowReservation itself
 * dispatches through GetReservedTrackbits()/HasStationReservation() for station
 * and tunnel bridge tiles, so it never trips rail_map.h's IsPlainRailTile()
 * assert -- unlike the unreserve path of KI-203b, which crashed on tiles whose
 * type had changed since the reservation was booked.
 */
static void R3RReservationAudit(const char *tag)
{
	/* KI-207: self-healing for bits that are already orphaned when an existing
	 * save is loaded -- the two-pass release (R3RReleaseChainReservations) stops
	 * new ghosts from being created, but cannot help the ones already in the file.
	 *
	 * A bit that resolves to nobody is, by the engine's own measure, an orphan
	 * (see the comment above). It can however *look* ownerless for a tick or two
	 * while a path is being rebuilt, so a bit must stay ownerless for
	 * R3R_RESV_REAP_AUDITS consecutive audits before it is released. Every release
	 * is logged, and the counter is dropped as soon as the bit resolves again, so
	 * a reaped tile either stays clean or comes straight back with its origin
	 * visible in the RESV-WATCH lines. */
	static const uint8_t R3R_RESV_REAP_AUDITS = 3;
	/* KI-207c: key slot for a whole-tile reservation (track slots 0..5 are real
	 * tracks, 6 is TRACK_END), so station/waypoint strays age in the same map. */
	static const uint32_t R3R_RESV_WHOLE_TILE_KEY = 7;
	static std::unordered_map<uint32_t, uint8_t> _r3r_resv_stray_age;

	auto bit_key = [](TileIndex t, uint32_t track) -> uint32_t {
		return (t.base() << 3) | track;
	};

	uint reserved = 0;
	uint owned = 0;
	uint stray = 0;
	uint reaped = 0;
	std::vector<uint32_t> strays_now;

	for (TileIndex t(0); t < Map::Size(); t++) {
		/* KI-207: the reap below only sees plain rail, the only place a single
		 * track bit can be booked on its own. A watched tile may however be a
		 * station platform, a depot or a crossing, whose reservation is a
		 * separate whole-tile bit -- report its state explicitly instead of
		 * silently skipping it, otherwise "watch" would be blind exactly where
		 * the ghost would not be reapable. */
		if (R3RIsWatchedResvTile(t) && (GetTileType(t) != TileType::Railway || !IsPlainRailTile(t))) {
			/* KI-207c: for a rail station platform / waypoint the owner *can* be
			 * asked now (GetTrainForWholeTileReservation() walks the reservation
			 * from every track of the tile instead of from one plain rail bit), so
			 * the watch line names the owner instead of only the raw state. Depot,
			 * crossing and bridge keep the raw report: their whole-tile bit means
			 * "a vehicle is in / on it" and there is no such walk for them. */
			const char *kind = (GetTileType(t) == TileType::Station && HasStationRail(t)) ?
					(IsRailWaypointTile(t) ? "WAYPOINT" : "STATION") : "OTHER";
			if (GetTileType(t) == TileType::Station && HasStationRail(t) && HasStationReservation(t)) {
				Train *w_owner = GetTrainForWholeTileReservation(t);
				if (w_owner != nullptr) {
					R3RDbgWrite("RESV-AUDIT-WATCH %s tile=%d,%d kind=%s reserved=1 owner=veh%u\n",
							tag, (int)TileX(t), (int)TileY(t), kind, w_owner->index.base());
				} else {
					R3RDbgWrite("RESV-AUDIT-WATCH %s tile=%d,%d kind=%s reserved=1 owner=none\n",
							tag, (int)TileX(t), (int)TileY(t), kind);
				}
			} else {
				R3RDbgWrite("RESV-AUDIT-WATCH %s tile=%d,%d kind=%s ttype=%u bits=0x%X\n",
						tag, (int)TileX(t), (int)TileY(t), kind, (uint)GetTileType(t), (uint)GetReservedTrackbits(t));
			}
		}

		/* KI-207c: station platform / waypoint whole-tile reservation. This is the
		 * only kind of ghost that the plain-rail reap below can never see, and it is
		 * exactly the kind the player reported ("only on the waypoint tile, one tile
		 * wide, gone after demolishing the waypoint"). */
		if (GetTileType(t) == TileType::Station && HasStationRail(t)) {
			if (!HasStationReservation(t)) continue;
			reserved++;
			Train *owner = GetTrainForWholeTileReservation(t);
			const uint32_t wkey = bit_key(t, R3R_RESV_WHOLE_TILE_KEY);
			if (owner == nullptr) {
				stray++;
				strays_now.push_back(wkey);
				const auto aged = _r3r_resv_stray_age.find(wkey);
				R3RDbgWrite("RESV-AUDIT-STRAY %s tile=%d,%d track=stn kind=%s age=%u\n",
						tag, (int)TileX(t), (int)TileY(t), IsRailWaypointTile(t) ? "WAYPOINT" : "STATION",
						(uint)(aged != _r3r_resv_stray_age.end() ? aged->second : 0));
			} else {
				owned++;
				_r3r_resv_stray_age.erase(wkey);
				if (R3RIsWatchedResvTile(t)) {
					R3RDbgWrite("RESV-AUDIT-OWNER %s tile=%d,%d track=stn kind=%s owner=veh%u\n",
							tag, (int)TileX(t), (int)TileY(t), IsRailWaypointTile(t) ? "WAYPOINT" : "STATION",
							owner->index.base());
				}
			}
			continue;
		}

		if (GetTileType(t) != TileType::Railway || !IsPlainRailTile(t)) continue;
		const TrackBits bits = GetRailReservationTrackBits(t);
		if (bits == TRACK_BIT_NONE) continue;
		reserved++;
		for (Track track = TRACK_BEGIN; track < TRACK_END; track++) {
			if (!HasBit(bits, track)) continue;
			Train *owner = GetTrainForReservation(t, track);
			const uint32_t bkey = bit_key(t, track);
			if (owner == nullptr) {
				stray++;
				strays_now.push_back(bkey);
				const auto aged = _r3r_resv_stray_age.find(bkey);
				R3RDbgWrite("RESV-AUDIT-STRAY %s tile=%d,%d track=%u bits=0x%X age=%u\n",
						tag, (int)TileX(t), (int)TileY(t), (uint)track, (uint)bits,
						(uint)(aged != _r3r_resv_stray_age.end() ? aged->second : 0));
			} else {
				owned++;
				_r3r_resv_stray_age.erase(bkey);
				/* One line per reserved bit per audit was the biggest remaining
				 * flood (and pure noise once the bit resolves). Keep it only for a
				 * watched tile, where the owner's identity is the whole point. */
				if (R3RIsWatchedResvTile(t)) {
					R3RDbgWrite("RESV-AUDIT-OWNER %s tile=%d,%d track=%u owner=veh%u bits=0x%X\n",
							tag, (int)TileX(t), (int)TileY(t), (uint)track, owner->index.base(), (uint)bits);
				}
			}
		}
	}

	/* Age the strays seen this pass, and release the ones that are old enough. */
	for (uint32_t key : strays_now) {
		uint8_t &age = _r3r_resv_stray_age[key];
		if (age < 255) age++;
		if (age < R3R_RESV_REAP_AUDITS) continue;

		const TileIndex tile(key >> 3);
		const uint32_t slot = key & 7;
		/* KI-207c: station platform / waypoint whole-tile bit. */
		if (slot == R3R_RESV_WHOLE_TILE_KEY) {
			if (GetTileType(tile) != TileType::Station || !HasStationRail(tile)) continue;
			if (GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr) continue;
			if (!HasStationReservation(tile)) continue;
			if (GetTrainForWholeTileReservation(tile) != nullptr) continue;
			const TrackBits rail_bits = GetStationReservationTrackBits(tile);
			if (rail_bits == TRACK_BIT_NONE) continue;

			{
				R3RResvPhaseGuard phase_guard("reap-stn");
				UnreserveRailTrack(tile, FindFirstTrack(rail_bits));
			}
			reaped++;
			R3RDbgWrite("RESV-REAP %s tile=%d,%d track=stn kind=%s age=%u\n",
					tag, (int)TileX(tile), (int)TileY(tile),
					IsRailWaypointTile(tile) ? "WAYPOINT" : "STATION", (uint)age);
			_r3r_resv_stray_age.erase(key);
			continue;
		}

		const Track track = static_cast<Track>(slot);
		if (GetTileType(tile) != TileType::Railway || !IsPlainRailTile(tile)) continue;
		/* Same two guards as the release path: never touch a tile a train stands
		 * on, never touch a bit that is not actually booked. */
		if (GetFirstVehicleOnTile(tile, VehicleType::Train) != nullptr) continue;
		if (!HasBit(GetRailReservationTrackBits(tile), track)) continue;

		{
			R3RResvPhaseGuard phase_guard("reap");
			UnreserveRailTrack(tile, track);
		}
		reaped++;
		R3RDbgWrite("RESV-REAP %s tile=%d,%d track=%u age=%u\n",
				tag, (int)TileX(tile), (int)TileY(tile), (uint)track, (uint)age);
		_r3r_resv_stray_age.erase(key);
	}

	/* Forget strays that resolved in the meantime: not a leak. */
	for (auto it = _r3r_resv_stray_age.begin(); it != _r3r_resv_stray_age.end(); ) {
		bool seen = false;
		for (uint32_t key : strays_now) {
			if (key == it->first) { seen = true; break; }
		}
		if (!seen) {
			it = _r3r_resv_stray_age.erase(it);
		} else {
			++it;
		}
	}

	R3RDbgWrite("RESV-AUDIT-SUMMARY %s reservedTiles=%u owned=%u strayTiles=%u reaped=%u pendingStray=%u\n",
			tag, reserved, owned, stray, reaped, (uint)_r3r_resv_stray_age.size());
}

/**
 * R3R (KI-203): release the bits booked for @p key that are not part of
 * @p fresh any more, then remember @p fresh as the new set.
 * @param key  consist identity (index of the vehicle that called in).
 * @param fresh the tracks that consist occupies and wants reserved right now.
 * @param why  reason tag for the log line.
 */
static void R3RReconcileConsistReservation(uint32_t key, const std::vector<R3RResvRec> &fresh, const char *why)
{
	auto it = _r3r_consist_resv.find(key);
	if (it != _r3r_consist_resv.end()) {
		for (const R3RResvRec &rec : it->second) {
			bool still_ours = false;
			for (const R3RResvRec &f : fresh) {
				if (f.tile == rec.tile && f.track == rec.track) { still_ours = true; break; }
			}
			if (!still_ours) {
				R3RReleaseReservationSafe(key, rec.tile, rec.track, why);
			}
		}
		if (fresh.empty()) {
			_r3r_consist_resv.erase(it);
		} else {
			it->second = fresh;
		}
	} else if (!fresh.empty()) {
		_r3r_consist_resv.emplace(key, fresh);
	}
}

/**
 * R3R (KI-203): drop bookkeeping entries whose consist is gone or has stopped
 * being a reservation holder (sold, decoupled, coupled on, or driving again).
 * Those are the ones that would never be reconciled by a later call, i.e. the
 * permanent leaks this table exists for.
 */
static void R3RGcConsistReservations()
{
	for (auto it = _r3r_consist_resv.begin(); it != _r3r_consist_resv.end(); ) {
		const Train *owner = Train::GetIfValid((size_t)it->first);
		if (R3RIsReservationHolder(owner)) { ++it; continue; }
		for (const R3RResvRec &rec : it->second) {
			R3RReleaseReservationSafe(it->first, rec.tile, rec.track, "holder-gone");
		}
		it = _r3r_consist_resv.erase(it);
	}
}

/** Tries to reserve track under whole train consist. */
void Train::ReserveTrackUnderConsist() const
{
	/* R3R (KI-207): name this entry point for the reservation-origin trace. */
	R3RResvPhaseGuard r3r_phase("consist", R3RResvActorID(this));
	R3RScopeTimer r3r_resv_timer(&r3r_resv_ns, &r3r_resv_calls);
	std::vector<R3RResvRec> r3r_fresh;
	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (u->track & TRACK_BIT_WORMHOLE) {
			if (IsRailCustomBridgeHeadTile(u->tile)) {
				/* reserve the first available track */
				TrackBits bits = GetAcrossTunnelBridgeTrackBits(u->tile);
				Track first_track = RemoveFirstTrack(&bits);
				dbg_assert(IsValidTrack(first_track));
				TryReserveRailTrack(u->tile, first_track);
				r3r_fresh.push_back(R3RResvRec{u->tile, first_track});
			} else {
				const Track tb = DiagDirToDiagTrack(GetTunnelBridgeDirection(u->tile));
				TryReserveRailTrack(u->tile, tb);
				r3r_fresh.push_back(R3RResvRec{u->tile, tb});
			}
		} else if (u->track != TRACK_BIT_DEPOT) {
			TrackBits bits = u->track;
			bool fallback = false;
			if (bits == TRACK_BIT_NONE) {
				/* R3R: an articulated part may not have its track bits synced
				 * after a decouple/couple split; fall back to the tile's actual
				 * rail bits so its platform track still gets reserved. */
				bits = GetTrackBits(u->tile);
				fallback = true;
				if (bits == TRACK_BIT_NONE) {
					/* KI-14 (1): edge-triggered. A consist parked on a platform
					 * re-runs this whole function every tick via the R3R
					 * platform-waiter gate; un-gated it produced 17118 of the
					 * 28838 log lines written in 38 s. Payload bit 32 marks the
					 * "no reservation bits at all" case. */
					const uint64_t rk = ((uint64_t)u->index.base() << 32) | (uint64_t)u->tile.base();
					if (R3RDbgEdge(R3REDGE_RESERVECONSIST, rk, 1ULL << 32)) {
						R3RDbgWrite("RESERVECONSIST veh=%d tile=%d,%d track=0x%X fb=1 RES_NONE skip\n",
							(int)u->index.base(), (int)TileX(u->tile), (int)TileY(u->tile), (uint)u->track);
					}
					continue;
				}
			}
			R3RTrackBitsProbe("reserve-consist", bits, (int)u->index.base(), u->tile);
			bool ok = TryReserveRailTrack(u->tile, TrackBitsToTrack(bits));
			/* R3R (KI-203): book only bits that are ours -- either we just took
			 * them, or they are already reserved and this vehicle stands on them
			 * (then they were taken by an earlier call of ours). Never book a bit
			 * that belongs to somebody else: the GC would release it. */
			if (ok || HasReservedTracks(u->tile, bits)) {
				r3r_fresh.push_back(R3RResvRec{u->tile, TrackBitsToTrack(bits)});
			}
			/* KI-14 (1): edge-triggered (see the skip branch above).
			 * KI-207: a reservation that simply succeeded is not news -- with `ok`
			 * in the payload every waiting consist re-logged its whole chain as the
			 * reservation was refreshed (979 lines in one session). Only the anomaly
			 * ("could not reserve" / "the bits are gone") reports a line now, and its
			 * payload is still the bits/fallback pair so a changing state is seen. */
			const uint64_t rk = ((uint64_t)u->index.base() << 32) | (uint64_t)u->tile.base();
			const bool res_now = HasReservedTracks(u->tile, bits);
			const bool anomaly = (!ok || !res_now);
			const uint64_t rp = ((uint64_t)bits << 8) | ((uint64_t)fallback << 2) | ((uint64_t)anomaly << 1);
			if (anomaly && R3RDbgEdge(R3REDGE_RESERVECONSIST, rk, rp)) {
				R3RDbgWrite("RESERVECONSIST veh=%d tile=%d,%d track=0x%X fb=%d bits=0x%X ok=%d resNow=%d\n",
					(int)u->index.base(), (int)TileX(u->tile), (int)TileY(u->tile), (uint)u->track,
					(int)fallback, (uint)bits, (int)ok, (int)res_now);
			}
		}
	}
	/* R3R (KI-203): release what we booked for tiles this consist no longer
	 * occupies, then refresh the bookkeeping. Cheap (the vector holds one entry
	 * per vehicle) and it runs on the platform-waiter path only, i.e. for the
	 * consists that actually hold reservations. */
	R3RReconcileConsistReservation(this->index.base(), r3r_fresh, "moved");
	if (++_r3r_consist_resv_gc_clock == 0) R3RGcConsistReservations();
}

/** R3R: clear any stale reservation under the consist before re-reserving. */
void Train::ClearReservationUnderConsist() const
{
	/* R3R (KI-207): name this entry point for the reservation-origin trace. */
	R3RResvPhaseGuard r3r_phase("clear-consist", R3RResvActorID(this));
	/* R3R (KI-203): release everything we booked for this consist, including
	 * tiles it has left since (walking only the current tiles cannot reach those
	 * -- that is how the stale reservations of KI-163 / the 60,16 report were
	 * left behind). */
	auto it = _r3r_consist_resv.find(this->index.base());
	if (it != _r3r_consist_resv.end()) {
		for (const R3RResvRec &rec : it->second) {
			R3RReleaseReservationSafe(this->index.base(), rec.tile, rec.track, "cleared");
		}
		_r3r_consist_resv.erase(it);
	}
	for (const Train *u = this; u != nullptr; u = u->Next()) {
		if (u->track == TRACK_BIT_NONE) continue;
		ClearPathReservation(u, u->tile, u->GetVehicleTrackdir(), true);
	}
}

/**
 * The train vehicle crashed!
 * Update its status and other parts around it.
 * @param flooded Crash was caused by flooding.
 * @return Number of people killed.
 */
uint Train::Crash(bool flooded)
{
	uint victims = 0;
	if (this->IsFrontEngine()) {
		victims += 2; // driver

		/* Remove the reserved path in front of the train if it is not stuck.
		 * Also clear all reserved tracks the train is currently on. */
		if (!this->flags.Test(VehicleRailFlag::Stuck)) FreeTrainTrackReservation(this);
		for (const Train *v = this; v != nullptr; v = v->Next()) {
			ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);
		}

		/* we may need to update crossing we were approaching,
		 * but must be updated after the train has been marked crashed */
		TileIndex crossing = TrainApproachingCrossingTile(this->GetMovingFront());
		if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

		/* Remove the loading indicators (if any) */
		HideFillingPercent(&this->fill_percent_te_id);
	}

	RegisterGameEvents(GEF_TRAIN_CRASH);

	victims += this->GroundVehicleBase::Crash(flooded);

	this->crash_anim_pos = flooded ? 4000 : 1; // max 4440, disappear pretty fast when flooded
	return victims;
}

/**
 * Marks train as crashed and creates an AI event.
 * Doesn't do anything if the train is crashed already.
 * @param v first vehicle of chain
 * @return number of victims (including 2 drivers; zero if train was already crashed)
 */
static uint TrainCrashed(Train *v)
{
	uint victims = 0;

	/* do not crash train twice */
	if (!v->vehstatus.Test(VehState::Crashed)) {
		victims = v->Crash();
		TileIndex tile = v->GetMovingFront()->tile;
		AI::NewEvent(v->owner, new ScriptEventVehicleCrashed(v->index, tile, ScriptEventVehicleCrashed::CRASH_TRAIN, victims, v->owner));
		Game::NewEvent(new ScriptEventVehicleCrashed(v->index, tile, ScriptEventVehicleCrashed::CRASH_TRAIN, victims, v->owner));
	}

	/* Try to re-reserve track under already crashed train too.
	 * Crash() clears the reservation! */
	v->ReserveTrackUnderConsist();

	return victims;
}

/* R3R (KI-220): order type at a real-order index, -1 when the index is unusable.
 * Probe only - never asserts on a desynced index (KI-220 shows exactly such a
 * desync: cur_real_order_index says WAIT_COUPLE while current_order is a service
 * GOTO_DEPOT). */
static int R3RDbgOrderTypeAt(const Train *t, uint idx)
{
	if (t->orders == nullptr || idx >= t->GetNumOrders()) return -1;
	const Order *o = t->GetOrder(idx);
	return (o != nullptr) ? (int)o->GetType() : -1;
}

/**
 * Collision test function.
 * @param v The %Train vehicle we may have collided with.
 * @param moving_front The %Train vehicle being examined.
 * @return Number of victims.
 */
static uint CheckTrainCollision(Train *v, Train *moving_front)
{
	/* not in depot */
	if (v->track == TRACK_BIT_DEPOT) {
		/* R3R: collision-as-couple inside the depot. Strict pairing: the moving
		 * train must be executing GOTO_COUPLE and the other chain's head must
		 * declare WAIT_COUPLE. Ordinary trains never carry these orders, so a
		 * normal rear-end collision inside a depot is still handled as before
		 * (no crash here; depot collisions are skipped anyway).
		 * Use First() (chain head = locomotive) as in the open-track branch.
		 * The actual coupling is now gated by a touch/overlap distance test
		 * (see the inner block) so a loco merely present on the depot track no
		 * longer couples with any WAIT_COUPLE consist regardless of separation;
		 * this mirrors the open-track distance gate at 5867, which the depot
		 * branch used to bypass because it returns before that check. */
		Train *loco = moving_front->First();
		if (loco->current_order.IsType(OT_GOTO_COUPLE) &&
				v->First()->current_order.IsType(OT_WAIT_COUPLE) &&
				loco != v->First() &&
				/* R3R (KI-182): the touch-as-couple shortcut obeys the same pair
				 * flag as every other path -- without a matching lock the
				 * travelling locomotive is simply not headed for this consist. */
				R3RCouplePairMatches(loco, v->First())) {
			int x_diff = v->x_pos - moving_front->x_pos;
			int y_diff = v->y_pos - moving_front->y_pos;
			int min_diff = (v->gcache.cached_veh_length + 1) / 2 + (moving_front->gcache.cached_veh_length + 1) / 2 - 1;
			if (x_diff * x_diff + y_diff * y_diff <= min_diff * min_diff) {
				/* R3R 1px probe: the touch/overlap gate fired. min_diff is one pixel
				 * short of the "boxes just touch" distance, so on axis-aligned track
				 * the loco freezes already overlapping by 1px. */
				{
					const uint64_t g_key = ((uint64_t)loco->index.base() << 32) ^ (uint64_t)v->index.base();
					const uint64_t g_payload = ((uint64_t)v->gcache.cached_veh_length << 32) ^ (uint64_t)moving_front->gcache.cached_veh_length;
					if (R3RDbgEdge(R3REDGE_CPLHIT, g_key, g_payload)) {
						FILE *dbg = R3RFopenDbg("a");
						if (dbg != nullptr) {
							const int len_v = v->gcache.cached_veh_length;
							const int len_mf = moving_front->gcache.cached_veh_length;
							const int fit = (len_v + len_mf) / 2;
							const int maxd = std::max(std::abs(x_diff), std::abs(y_diff));
							fprintf(dbg, "[R3R] CPL-HIT site=depot loco=%d v=%d len_v=%d len_mf=%d min_diff=%d need=%d fit=%d dx=%d dy=%d maxd=%d overlap=%d spd=%d dir=%d\n",
									(int)loco->index.base(), (int)v->index.base(), len_v, len_mf,
									min_diff, min_diff + 1, fit, x_diff, y_diff, maxd, fit - maxd,
									(int)moving_front->cur_speed, (int)moving_front->direction);
							fclose(dbg);
						}
					}
				}
				Couple(loco, v->First());
				moving_front->cur_speed = 0;
				moving_front->progress = 0;
			}
		}
		return 0;
	}

	if (_settings_game.vehicle.no_train_crash_other_company) {
		/* do not crash into trains of another company. */
		if (v->owner != moving_front->owner) return 0;
	}

	/* Self-check: a vehicle unit cannot collide with itself.
	 * This is the most common case and skipping it early avoids further calculations. */
	if (v == moving_front) return 0;

	if (v->First() == moving_front->First()) {
		/* If self-collision is disabled, skip all wagons of the same train.
		 * If enabled, only skip immediate neighbors. */
		if (!_settings_game.vehicle.train_self_collision || v == moving_front->Next() || v == moving_front->Previous()) return 0;
	}

	int x_diff = v->x_pos - moving_front->x_pos;
	int y_diff = v->y_pos - moving_front->y_pos;

	/* Do fast calculation to check whether trains are not in close vicinity
	 * and quickly reject trains distant enough for any collision.
	 * Differences are shifted by 7, mapping range [-7 .. 8] into [0 .. 15]
	 * Differences are then ORed and then we check for any higher bits */
	uint hash = (y_diff + 7) | (x_diff + 7);
	if (hash & ~15) return 0;

	/* Slower check using multiplication */
	int min_diff = (v->gcache.cached_veh_length + 1) / 2 + (moving_front->gcache.cached_veh_length + 1) / 2 - 1;
	/* R3R: couple-on-overlap must trigger when the trains are touching/overlapping
	 * (diff <= min_diff). The original '>=' returned 0 exactly at diff == min_diff,
	 * leaving a one-pixel gap where a stopped loco touching the waiting consist
	 * would never couple (while GetCouplePosition only fires at the exact end-to-end
	 * distance). Use '>' so diff == min_diff still proceeds to the couple check. */
	const int r3r_sq_dist = x_diff * x_diff + y_diff * y_diff;
	if (r3r_sq_dist > min_diff * min_diff) return 0;
	/* R3R (KI-240): remember whether the boxes are *exactly* touching
	 * (diff == min_diff). That is precisely the stock "neither couple nor crash"
	 * border the '>' above opened up on purpose. Without handing it back further
	 * down, every mere touch turns into a crash candidate - which is exactly what
	 * a fresh decouple looks like: both halves stay seam to seam, and for odd
	 * vehicle lengths the in-consist spacing equals min_diff *exactly*. */
	const bool r3r_exact_touch = (r3r_sq_dist == min_diff * min_diff);

	/* Happens when there is a train under bridge next to bridge head */
	if (abs(v->z_pos - moving_front->z_pos) > 5) return 0;

	/* R3R: collision-as-couple on open track (YPS semantics). Strict pairing: the
	 * locomotive (chain head) must be executing GOTO_COUPLE and the other chain's
	 * head must declare WAIT_COUPLE. Ordinary trains never carry these orders, so
	 * a normal rear-end collision still crashes as usual.
	 * NOTE: use First() (chain head = locomotive): while reversing, moving_front
	 * is the TRAILING wagon whose current_order is not GOTO_COUPLE. */
	Train *loco = moving_front->First();
	if (loco->current_order.IsType(OT_GOTO_COUPLE) &&
			v->First()->current_order.IsType(OT_WAIT_COUPLE) &&
			loco != v->First() &&
			/* R3R (KI-182): same pair flag as the depot branch and as the whole
			 * pathfinder: touching "the wrong" waiting consist must not couple. */
			R3RCouplePairMatches(loco, v->First())) {
		/* R3R 1px probe: open-track touch/overlap gate (mirror of the depot one). */
		{
			const uint64_t g_key = ((uint64_t)loco->index.base() << 32) ^ (uint64_t)v->index.base();
			const uint64_t g_payload = ((uint64_t)v->gcache.cached_veh_length << 32) ^ (uint64_t)moving_front->gcache.cached_veh_length;
			if (R3RDbgEdge(R3REDGE_CPLHIT, g_key, g_payload)) {
				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					const int len_v = v->gcache.cached_veh_length;
					const int len_mf = moving_front->gcache.cached_veh_length;
					const int fit = (len_v + len_mf) / 2;
					const int maxd = std::max(std::abs(x_diff), std::abs(y_diff));
					fprintf(dbg, "[R3R] CPL-HIT site=open loco=%d v=%d len_v=%d len_mf=%d min_diff=%d need=%d fit=%d dx=%d dy=%d maxd=%d overlap=%d spd=%d dir=%d\n",
							(int)loco->index.base(), (int)v->index.base(), len_v, len_mf,
							min_diff, min_diff + 1, fit, x_diff, y_diff, maxd, fit - maxd,
							(int)moving_front->cur_speed, (int)moving_front->direction);
					fclose(dbg);
				}
			}
		}
		Couple(loco, v->First());
		moving_front->cur_speed = 0;
		moving_front->progress = 0;
		return 0;
	}

	/* R3R (KI-240): a decouple cuts the released part off *seam to seam*, so the
	 * very next collision test sees the two halves touching. Because R3R gates
	 * coupling on the orders, those two halves (in the live report: the
	 * locomotive leaving for a waypoint in the south, the consist parked on
	 * WAIT_COUPLE in the north) are not a pair, and the test therefore used to end
	 * in TrainCrashed() immediately after every decouple.
	 *
	 * (a) Boxes exactly touching: that is the stock border ('>=' used to return
	 *     here) - neither couple nor crash. Hand it back unconditionally.
	 * (b) The released part may even stand inside the last pixel (a fold-corrected
	 *     seam is allowed to be 1px tight, KI-201) while both halves crawl away
	 *     from a dead stop. A chain standing that close to a WAIT_COUPLE consist is
	 *     parked road - the KI-149 seam - not an accident.
	 *
	 * (c) KI-241 (2026-09-28): and the other half must not be *ahead* of the moving
	 *     front along the direction this chain travels. Distance alone can never
	 *     tell a seam from a rear-end collision - both sit at or inside min_diff -
	 *     but the direction can: at a seam the two halves part, so the other half
	 *     lies behind or beside the moving front, whereas in a collision it lies
	 *     ahead, exactly where this chain is driving to. Live report (2026-09-28):
	 *     loco 27 decoupled into the *south* half (head 60,21, moving front = its
	 *     tail at 60,20, DrivingBackwards, so it travels north), the released
	 *     consist 0 stayed parked on 60,20/60,19 and the loco's next order (a
	 *     waypoint) lay in the north -> it reversed north straight through the
	 *     consist: log shows CRT veh=27 order=6 origin=60,18 found=1, 13 SEAM-FREE
	 *     lines (maxd 0..4, mfspd=0, mforder=6 vorder=17) and *zero* CRASH lines.
	 *     With this test the approach is a collision again and TrainCrashed() below
	 *     is reached. '列车的限制脱离方向': the direction the halves depart decides.
	 *
	 * A genuine rear-end collision drives far deeper and far faster and does not
	 * sit next to a WAIT_COUPLE consist, so it still reaches TrainCrashed() below.
	 */
	const TileIndexDiffC r3r_mf_step = TileIndexDiffCByDir(moving_front->GetMovingDirection());
	const bool r3r_seam_ahead = (r3r_mf_step.x * x_diff + r3r_mf_step.y * y_diff) > 0;
	if (!r3r_seam_ahead &&
			(r3r_exact_touch || (std::max(moving_front->cur_speed, v->cur_speed) <= 32 &&
			 (moving_front->First()->current_order.IsType(OT_WAIT_COUPLE) !=
			  v->First()->current_order.IsType(OT_WAIT_COUPLE))))) {
		const uint64_t s_key = ((uint64_t)moving_front->First()->index.base() << 32) ^ (uint64_t)v->First()->index.base();
		if (R3RDbgOn() && R3RDbgEdge(R3REDGE_SEAMFREE, s_key, r3r_exact_touch ? 1ULL : 0ULL)) {
			R3RDbgWrite("SEAM-FREE mf=%d v=%d exact=%d maxd=%d min_diff=%d mfspd=%d vspd=%d mforder=%d vorder=%d tile=%d,%d ahead=%d mfdir=%d\n",
					(int)moving_front->First()->index.base(), (int)v->First()->index.base(),
					r3r_exact_touch ? 1 : 0, (int)std::max(std::abs(x_diff), std::abs(y_diff)), min_diff,
					(int)moving_front->cur_speed, (int)v->cur_speed,
					(int)moving_front->First()->current_order.GetType(), (int)v->First()->current_order.GetType(),
					(int)TileX(v->tile), (int)TileY(v->tile),
					r3r_seam_ahead ? 1 : 0, (int)moving_front->GetMovingDirection());
		}
		return 0;
	}

	/* DEBUG (R3R — remove after locating the crash): log the coupling-condition
	 * failure to a file so it is always visible. Open R3R_debug.log after the crash. */
	{
		FILE *dbg = R3RFopenDbg("a");
		if (dbg != nullptr) {
			fprintf(dbg, "CRASH: movingFirst_order=%d v_order=%d vFirst_order=%d same_chain=%d speed=%d tile=%d\n",
					(int)moving_front->First()->current_order.GetType(), (int)v->current_order.GetType(),
					(int)v->First()->current_order.GetType(), moving_front->First() == v->First(),
					(int)moving_front->cur_speed, (int)moving_front->tile.base());
			/* R3R (KI-220): identify BOTH chains - index, tile, real order index
			 * (and the type stored there), current_order type, dest_tile and speed.
			 * Without this the log only said "order=2 vs order=2" and the two
			 * colliding chains could not be told apart (that is how the wrong
			 * 2026-09-27 root cause got recorded). */
			const Train *const c_mf = moving_front->First();
			const Train *const c_v = v->First();
			fprintf(dbg, "CRASH-INFO mf=%d mftile=%d,%d mfreal=%d(%d) mfcur=%d mfdest=%d mfspd=%d mfdb=%d mfdestStn=%d | "
					"v=%d vtile=%d,%d vreal=%d(%d) vcur=%d vdest=%d vspd=%d vdb=%d | "
					"vhead=%d vheadreal=%d(%d) vheadcur=%d vheaddest=%d vheadspd=%d pair=%d\n",
					(int)c_mf->index.base(), (int)TileX(c_mf->tile), (int)TileY(c_mf->tile),
					(int)c_mf->cur_real_order_index, R3RDbgOrderTypeAt(c_mf, c_mf->cur_real_order_index),
					(int)c_mf->current_order.GetType(), (int)c_mf->dest_tile.base(), (int)c_mf->cur_speed,
					(int)c_mf->IsDrivingBackwards(), (int)c_mf->current_order.GetDestination().base(),
					(int)v->index.base(), (int)TileX(v->tile), (int)TileY(v->tile),
					(int)v->cur_real_order_index, R3RDbgOrderTypeAt(v, v->cur_real_order_index),
					(int)v->current_order.GetType(), (int)v->dest_tile.base(), (int)v->cur_speed,
					(int)v->IsDrivingBackwards(),
					(int)c_v->index.base(), (int)c_v->cur_real_order_index, R3RDbgOrderTypeAt(c_v, c_v->cur_real_order_index),
					(int)c_v->current_order.GetType(), (int)c_v->dest_tile.base(), (int)c_v->cur_speed,
					R3RCouplePairMatches(c_mf, c_v) ? 1 : 0);
			fclose(dbg);
		}
	}

	/* Crash both trains. Two statements required to guarantee execution
	 * order because RandomRange() is involved. */
	uint num_victims = TrainCrashed(moving_front->First());
	return num_victims + TrainCrashed(v->First());
}

/**
 * Checks whether the specified train has a collision with another vehicle. If
 * so, destroys this vehicle, and the other vehicle if its subtype has TS_Front.
 * Reports the incident in a flashy news item, modifies station ratings and
 * plays a sound.
 * @param moving_front %Train to test.
 * @return \c true iff there has been a collision.
 */
static bool CheckTrainCollision(Train *moving_front)
{
	R3RScopeTimer r3r_coll_timer(&r3r_coll_ns, &r3r_coll_calls);
	/* can't collide in depot */
	if (moving_front->track == TRACK_BIT_DEPOT) return false;

	dbg_assert(moving_front->track & TRACK_BIT_WORMHOLE || TileVirtXY(moving_front->x_pos, moving_front->y_pos) == moving_front->tile);

	uint num_victims = 0;

	/* find colliding vehicles */
	if (moving_front->track == TRACK_BIT_WORMHOLE) {
		for (Train *u : VehiclesOnTile<VehicleType::Train>(moving_front->tile)) {
			num_victims += CheckTrainCollision(u, moving_front);
		}
		for (Train *u : VehiclesOnTile<VehicleType::Train>(GetOtherTunnelBridgeEnd(moving_front->tile))) {
			num_victims += CheckTrainCollision(u, moving_front);
		}
	} else {
		for (Train *u : VehiclesNearTileXY<VehicleType::Train>(moving_front->x_pos, moving_front->y_pos, 7)) {
			num_victims += CheckTrainCollision(u, moving_front);
		}
	}

	/* any dead -> no crash */
	if (num_victims == 0) return false;

	AddTileNewsItem(GetEncodedString(STR_NEWS_TRAIN_CRASH, num_victims), NewsType::Accident, moving_front->tile);

	ModifyStationRatingAround(moving_front->tile, moving_front->First()->owner, -160, 30);
	if (_settings_client.sound.disaster) SndPlayVehicleFx(SND_13_TRAIN_COLLISION, moving_front);
	return true;
}

struct FindSpaceBetweenTrainsChecker {
	int32_t pos;
	uint16_t distance;
	DiagDirection direction;

	bool operator()(const Train *v) const;
};

/** Find train in front and keep distance between trains in tunnel/bridge. */
bool FindSpaceBetweenTrainsChecker::operator()(const Train *v) const
{
	/* Don't look at wagons between front and back of train. */
	if ((v->Previous() != nullptr && v->Next() != nullptr)) return false;

	if (!IsDiagonalDirection(v->direction)) {
		/* Check for vehicles on non-across track pieces of custom bridge head */
		if ((GetAcrossTunnelBridgeTrackBits(v->tile) & v->track & TRACK_BIT_ALL) == TRACK_BIT_NONE) return false;
	}

	int32_t a = 0;
	int32_t b = 0;

	switch (this->direction) {
		default: NOT_REACHED();
		case DiagDirection::NE: a = this->pos; b = v->x_pos; break;
		case DiagDirection::SE: a = v->y_pos; b = this->pos; break;
		case DiagDirection::SW: a = v->x_pos; b = this->pos; break;
		case DiagDirection::NW: a = this->pos; b = v->y_pos; break;
	}

	if (a > b && a <= (b + (int)(this->distance)) + (int)(TILE_SIZE) - 1) return true;
	return false;
}

static bool IsTooCloseBehindTrain(Train *moving_front, TileIndex tile, uint16_t distance, bool check_endtile)
{
	Train *consist = moving_front->First();
	if (consist->force_proceed != 0) return false;

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		if (unlikely(consist->lookahead == nullptr)) {
			FillTrainReservationLookAhead(consist);
		}
		if (likely(consist->lookahead != nullptr)) {
			if (LookaheadWithinCurrentTunnelBridge(consist)) {
				/* lookahead is within tunnel/bridge */
				TileIndex veh_tile = moving_front->tile;
				TileIndex end = GetOtherTunnelBridgeEnd(veh_tile);
				const int raw_free_tiles = GetAvailableFreeTilesInSignalledTunnelBridge(veh_tile, end, tile);
				ApplyAvailableFreeTunnelBridgeTiles(consist->lookahead.get(), raw_free_tiles + ((raw_free_tiles != INT_MAX) ? DistanceManhattan(veh_tile, tile) : 0), veh_tile, end);
				SetTrainReservationLookaheadEnd(consist);

				if (!LookaheadWithinCurrentTunnelBridge(consist)) {
					/* Try to extend the reservation beyond the tunnel/bridge exit */
					TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
				}

				if (raw_free_tiles <= (int)(distance / TILE_SIZE)) {
					/* Revert train if not going with tunnel direction. */
					DiagDirection tb_dir = GetTunnelBridgeDirection(veh_tile);
					if (DirToDiagDirAlongAxis(moving_front->GetMovingDirection(), DiagDirToAxis(tb_dir)) != tb_dir) {
						consist->flags.Set(VehicleRailFlag::Reversing);
					}
					return true;
				}
				return false;
			} else {
				/* Try to extend the reservation beyond the tunnel/bridge exit */
				TryLongReserveChooseTrainTrackFromReservationEnd(consist, true);
			}
		}
	}

	FindSpaceBetweenTrainsChecker checker;
	checker.distance = distance;
	checker.direction = DirToDiagDirAlongAxis(moving_front->GetMovingDirection(), DiagDirToAxis(GetTunnelBridgeDirection(moving_front->tile)));
	switch (checker.direction) {
		default: NOT_REACHED();
		case DiagDirection::NE: checker.pos = (TileX(tile) * TILE_SIZE) + TILE_UNIT_MASK; break;
		case DiagDirection::SE: checker.pos = (TileY(tile) * TILE_SIZE); break;
		case DiagDirection::SW: checker.pos = (TileX(tile) * TILE_SIZE); break;
		case DiagDirection::NW: checker.pos = (TileY(tile) * TILE_SIZE) + TILE_UNIT_MASK; break;
	}

	if (HasVehicleOnTile<VehicleType::Train>(moving_front->tile, checker)) {
		/* Revert train if not going with tunnel direction. */
		if (checker.direction != GetTunnelBridgeDirection(moving_front->tile)) {
			consist->flags.Set(VehicleRailFlag::Reversing);
		}
		return true;
	}
	/* Cover blind spot at end of tunnel bridge. */
	if (check_endtile){
		if (HasVehicleOnTile<VehicleType::Train>(GetOtherTunnelBridgeEnd(moving_front->tile), checker)) {
			/* Revert train if not going with tunnel direction. */
			if (checker.direction != GetTunnelBridgeDirection(moving_front->tile)) {
				consist->flags.Set(VehicleRailFlag::Reversing);
			}
			return true;
		}
	}

	return false;
}

static bool CheckTrainStayInWormHolePathReserve(Train *consist, Train *moving_front, TileIndex tile)
{
	bool mark_dirty = false;
	auto guard = scope_guard([&]() {
		if (mark_dirty) MarkTileDirtyByTile(tile, VMDF_NOT_MAP_MODE);
	});

	Trackdir td = GetTunnelBridgeExitTrackdir(tile);
	CFollowTrackRail ft(GetTileOwner(tile), consist->GetIndirectCompatibleRailTypes());

	if (ft.Follow(tile, td)) {
		TrackdirBits reserved = ft.new_td_bits & TrackBitsToTrackdirBits(GetReservedTrackbits(ft.new_tile));
		if (reserved == TRACKDIR_BIT_NONE) {
			/* next tile is not reserved, so reserve the exit tile */
			if (IsBridge(tile)) {
				TryReserveRailBridgeHead(tile, FindFirstTrack(GetAcrossTunnelBridgeTrackBits(tile)));
			} else {
				SetTunnelReservation(tile, true);
			}
			mark_dirty = true;
		}
	}

	auto try_exit_reservation = [&]() -> bool {
		if (IsTunnelBridgeRestrictedSignal(tile)) {
			const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(td));
			if (prog != nullptr && prog->actions_used_flags & (TRPAUF_WAIT_AT_PBS | TRPAUF_SLOT_ACQUIRE)) {
				TraceRestrictProgramResult out;
				TraceRestrictProgramInput input(tile, td, nullptr, nullptr);
				input.permitted_slot_operations = TRPISP_ACQUIRE;
				prog->Execute(consist, input, out);
				if (out.flags & TRPRF_WAIT_AT_PBS) {
					return false;
				}
			}
		}

		if (_extra_aspects > 0) {
			SetTunnelBridgeExitSignalAspect(tile, 0);
			UpdateAspectDeferredWithVehicleTunnelBridgeExit(consist, tile, GetTunnelBridgeExitTrackdir(tile));
		}

		bool ok = TryPathReserve(consist);
		FlushDeferredDetermineCombineNormalShuntMode(consist);
		return ok;
	};

	if (_settings_game.vehicle.train_braking_model == TBM_REALISTIC) {
		if (unlikely(consist->lookahead == nullptr)) {
			FillTrainReservationLookAhead(consist);
		}
		if (likely(consist->lookahead != nullptr)) {
			if (!HasAcrossTunnelBridgeReservation(tile)) return false;

			TrainReservationLookAhead &lookahead = *(consist->lookahead);
			if (lookahead.reservation_end_tile == moving_front->tile && lookahead.reservation_end_position - lookahead.current_position <= (int)TILE_SIZE && !lookahead.flags.Test(TrainReservationLookAheadFlag::TunnelBridgeExitFree)) return false;
			SignalState exit_state = GetTunnelBridgeExitSignalState(tile);
			SetTunnelBridgeExitSignalState(tile, SignalState::Green);

			/* Get tile margin before changing vehicle direction */
			const int tile_margin = GetTileMarginInFrontOfTrain(moving_front);

			TileIndex veh_orig_tile = moving_front->tile;
			TrackBits veh_orig_track = moving_front->track;
			Direction veh_orig_direction = moving_front->direction;
			moving_front->tile = tile;
			moving_front->track = TRACK_BIT_WORMHOLE;
			moving_front->SetMovingDirection(TrackdirToDirection(td));

			if (consist->Next() == nullptr) {
				/* If this is a single-vehicle train, temporarily update the tile hash so that it can be found when scanning tiles.
				 * This is so that the whole train does not become invisible.
				 * Otherwise if the outgoing reservation reaches the entrance tile at the opposite end of this tunnel/bridge,
				 * the reservation would form a loop, resulting in various ill-effects and invariant violations. */
				consist->UpdatePosition();
			}

			bool ok;
			if (lookahead.reservation_end_position >= lookahead.current_position && lookahead.reservation_end_position > lookahead.current_position + tile_margin) {
				/* Reservation was made previously and was valid then.
				 * To avoid unexpected braking due to stopping short of the lookahead end,
				 * just carry on even if the end is not a safe waiting point now. */
				ok = true;
			} else {
				ok = try_exit_reservation();
			}
			if (ok) {
				mark_dirty = true;
				if (lookahead.reservation_end_tile == veh_orig_tile && lookahead.reservation_end_position - lookahead.current_position <= (int)TILE_SIZE) {
					/* Less than a tile of lookahead, advance tile */
					lookahead.reservation_end_tile = tile;
					lookahead.reservation_end_trackdir = td;
					lookahead.flags.Reset(TrainReservationLookAheadFlag::TunnelBridgeExitFree);
					lookahead.flags.Reset(TrainReservationLookAheadFlag::Chunnel);
					lookahead.reservation_end_position += (DistanceManhattan(veh_orig_tile, tile) - 1 - lookahead.tunnel_bridge_reserved_tiles) * (int)TILE_SIZE;
					lookahead.reservation_end_position += IsDiagonalTrackdir(td) ? 16 : 8;
					lookahead.tunnel_bridge_reserved_tiles = 0;
					FillTrainReservationLookAhead(consist);
				}
				/* Try to extend the reservation */
				TryLongReserveChooseTrainTrackFromReservationEnd(consist);
			} else {
				SetTunnelBridgeExitSignalState(tile, exit_state);
			}
			moving_front->tile = veh_orig_tile;
			moving_front->track = veh_orig_track;
			moving_front->direction = veh_orig_direction;
			if (consist->Next() == nullptr) {
				/* See equivalent UpdatePosition call above */
				consist->UpdatePosition();
			}
			return ok;
		}
	}


	TileIndex veh_orig_tile = moving_front->tile;
	TrackBits veh_orig_track = moving_front->track;
	Direction veh_orig_direction = moving_front->direction;
	moving_front->tile = tile;
	moving_front->track = TRACK_BIT_WORMHOLE;
	moving_front->SetMovingDirection(TrackdirToDirection(td));
	bool ok = try_exit_reservation();
	moving_front->tile = veh_orig_tile;
	moving_front->track = veh_orig_track;
	moving_front->direction = veh_orig_direction;
	if (ok && IsTunnelBridgeEffectivelyPBS(tile)) {
		SetTunnelBridgeExitSignalState(tile, SignalState::Green);
		if (_extra_aspects > 0) {
			SetTunnelBridgeExitSignalAspect(tile, 0);
			UpdateAspectDeferred(tile, GetTunnelBridgeExitTrackdir(tile));
		}
		mark_dirty = true;
	}
	return ok;
}

/** Simulate signals in tunnel - bridge. */
static bool CheckTrainStayInWormHole(Train *moving_front, TileIndex tile)
{
	Train *consist = moving_front->First();
	if (consist->force_proceed != 0) return false;

	/* When not exit reverse train. */
	if (!IsTunnelBridgeSignalSimulationExit(tile)) {
		consist->flags.Set(VehicleRailFlag::Reversing);
		return true;
	}
	SigSegState seg_state = (_settings_game.pf.reserve_paths || IsTunnelBridgeEffectivelyPBS(tile)) ? SigSegState::Path : UpdateSignalsOnSegment(tile, DiagDirection::Invalid, moving_front->owner);
	if (seg_state != SigSegState::Path) {
		CFollowTrackRail ft(GetTileOwner(tile), consist->GetIndirectCompatibleRailTypes());
		if (ft.Follow(tile, GetTunnelBridgeExitTrackdir(tile))) {
			if (ft.new_td_bits != TRACKDIR_BIT_NONE && KillFirstBit(ft.new_td_bits) == TRACKDIR_BIT_NONE) {
				Trackdir td = FindFirstTrackdir(ft.new_td_bits);
				if (HasPbsSignalOnTrackdir(ft.new_tile, td)) {
					/* immediately after the exit, there is a PBS signal, switch to PBS mode */
					seg_state = SigSegState::Path;
				}
			}
		}
	}
	if (seg_state == SigSegState::Path) {
		if (!CheckTrainStayInWormHolePathReserve(consist, moving_front, tile)) {
			consist->vehstatus.Set(VehState::TrainSlowing);
			return true;
		}
	} else {
		if (GetTunnelBridgeExitSignalState(tile) == SignalState::Red) {
			consist->vehstatus.Set(VehState::TrainSlowing);
			return true;
		}
	}

	return false;
}

static void HandleSignalBehindTrain(Train *v, int signal_number)
{
	if (!IsTunnelBridgeSignalSimulationEntrance(v->tile)) return;

	const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(v->tile);

	TileIndex tile;
	switch (v->GetMovingDirection()) {
		default: NOT_REACHED();
		case Direction::NE: tile = TileVirtXY(v->x_pos + (TILE_SIZE * simulated_wormhole_signals), v->y_pos); break;
		case Direction::SE: tile = TileVirtXY(v->x_pos, v->y_pos - (TILE_SIZE * simulated_wormhole_signals) ); break;
		case Direction::SW: tile = TileVirtXY(v->x_pos - (TILE_SIZE * simulated_wormhole_signals), v->y_pos); break;
		case Direction::NW: tile = TileVirtXY(v->x_pos, v->y_pos + (TILE_SIZE * simulated_wormhole_signals)); break;
	}

	if (tile == v->tile) {
		/* Flip signal on ramp. */
		SetTunnelBridgeEntranceSignalGreen(tile);
	} else if (IsBridge(v->tile) && signal_number >= 0) {
		SetBridgeEntranceSimulatedSignalState(v->tile, signal_number, SignalState::Green);
		MarkSingleBridgeSignalDirty(tile, v->tile);
		if (_extra_aspects > 0) UpdateAspectFromBridgeMiddleSignalChange(v->tile, TileOffsByDiagDir(GetTunnelBridgeDirection(v->tile)) * simulated_wormhole_signals, signal_number);
	} else if (IsTunnel(v->tile) && signal_number >= 0 && _extra_aspects > 0) {
		UpdateEntranceAspectFromMiddleSignalChange(v->tile, signal_number);
	}
}

inline void DecreaseReverseDistance(Train *v)
{
	if (v->reverse_distance > 1) {
		v->reverse_distance--;
	}
}

int ReversingDistanceTargetSpeed(const Train *v)
{
	if (v->UsingRealisticBraking()) {
		TrainDecelerationStats stats(v, v->lookahead != nullptr ? v->lookahead->cached_zpos : v->CalculateOverallZPos());
		return GetRealisticBrakingSpeedForDistance(stats, v->reverse_distance - 1, 0, 0);
	}
	int target_speed;
	if (_settings_game.vehicle.train_acceleration_model == AM_REALISTIC) {
		target_speed = ((v->reverse_distance - 1) * 5) / 2;
	} else {
		target_speed = (v->reverse_distance - 1) * 10 - 5;
	}
	return std::max(0, target_speed);
}

void DecrementPendingSpeedRestrictions(Train *v)
{
	bool remaining = false;
	for (auto it = _pending_speed_restriction_change_map.lower_bound(v->index); it != _pending_speed_restriction_change_map.end() && it->first == v->index;) {
		if (--it->second.distance == 0) {
			v->speed_restriction = it->second.new_speed;
			it = _pending_speed_restriction_change_map.erase(it);
		} else {
			++it;
			remaining = true;
		}
	}
	if (!remaining) v->flags.Reset(VehicleRailFlag::PendingSpeedRestriction);
}

static void HandleTraceRestrictSpeedRestrictionAction(const TraceRestrictProgramResult &out, Train *consist, Trackdir signal_td)
{
	if (out.flags & TRPRF_SPEED_RESTRICTION_SET) {
		consist->flags.Set(VehicleRailFlag::PendingSpeedRestriction);
		for (auto it = _pending_speed_restriction_change_map.lower_bound(consist->index); it != _pending_speed_restriction_change_map.end() && it->first == consist->index; ++it) {
			if ((uint16_t) (out.speed_restriction + 0xFFFF) < (uint16_t) (it->second.new_speed + 0xFFFF)) it->second.new_speed = out.speed_restriction;
		}
		uint16_t flags = 0;
		if (IsDiagonalTrack(TrackdirToTrack(signal_td))) SetBit(flags, PSRCF_DIAGONAL);
		_pending_speed_restriction_change_map.insert({ consist->index, { (uint16_t) (consist->gcache.cached_total_length + (HasBit(flags, PSRCF_DIAGONAL) ? 8 : 4)), out.speed_restriction, consist->speed_restriction, flags } });
		if ((uint16_t) (out.speed_restriction + 0xFFFF) < (uint16_t) (consist->speed_restriction + 0xFFFF)) consist->speed_restriction = out.speed_restriction;
	}
	if (out.flags & TRPRF_SPEED_ADAPT_EXEMPT && !consist->flags.Test(VehicleRailFlag::SpeedAdaptationExempt)) {
		consist->flags.Set(VehicleRailFlag::SpeedAdaptationExempt);
		SetWindowDirty(WindowClass::VehicleDetails, consist->index);
	}
	if (out.flags & TRPRF_RM_SPEED_ADAPT_EXEMPT && consist->flags.Test(VehicleRailFlag::SpeedAdaptationExempt)) {
		consist->flags.Reset(VehicleRailFlag::SpeedAdaptationExempt);
		SetWindowDirty(WindowClass::VehicleDetails, consist->index);
	}
}

template <typename AllowSlotAcquireT, typename PostProcessResultT>
void TrainControllerTraceRestrictFrontEvaluation(TileIndex tile, Trackdir dir, Train *consist, TraceRestrictProgramActionsUsedFlags extra_action_used_flags, AllowSlotAcquireT allow_slot_acquire, PostProcessResultT post_process_result)
{
	const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(tile, TrackdirToTrack(dir));
	if (prog == nullptr) return;

	TraceRestrictProgramActionsUsedFlags actions_used_flags = extra_action_used_flags | TRPAUF_SLOT_RELEASE_FRONT | TRPAUF_SPEED_RESTRICTION | TRPAUF_SPEED_ADAPTATION | TRPAUF_CHANGE_COUNTER;

	const bool slot_acquire_allowed = allow_slot_acquire();
	if (slot_acquire_allowed) actions_used_flags |= TRPAUF_SLOT_ACQUIRE;

	if ((prog->actions_used_flags & actions_used_flags) == 0) return;

	TraceRestrictProgramResult out;
	TraceRestrictProgramInput input(tile, dir, nullptr, nullptr);
	input.permitted_slot_operations = TRPISP_RELEASE_FRONT | TRPISP_CHANGE_COUNTER;
	if (slot_acquire_allowed) input.permitted_slot_operations |= TRPISP_ACQUIRE;

	prog->Execute(consist, input, out);

	HandleTraceRestrictSpeedRestrictionAction(out, consist, dir);
	post_process_result(out);
}

/**
 * Move a vehicle chain one movement stop forwards.
 * @param v First vehicle to move.
 * @param nomove Stop moving this and all following vehicles.
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return True if the vehicle could be moved forward, false otherwise.
 */
bool TrainController(Train *v, Vehicle *nomove, bool reverse)
{
	/* R3R (KI-207): the moving train re-books the track it enters from here, so
	 * name the phase and the train for the reservation-origin trace. */
	R3RResvPhaseGuard r3r_resv_phase_guard("controller", R3RResvActorID(v));
	R3RScopeTimer r3r_ctrl_timer(&r3r_ctrl_ns, &r3r_ctrl_calls);
	Train *first = v->First();
	Train *prev = nullptr;
	SCOPE_INFO_FMT([&], "TrainController: {}, {}, {}", VehicleInfoDumper(v), VehicleInfoDumper(prev), VehicleInfoDumper(nomove));
	bool direction_changed = false; // has direction of any part changed?
	bool update_signal_tunbridge_exit = false;
	Direction old_direction = Direction::Invalid;
	TrackBits old_trackbits = TrackBits{0xFF};
	uint16_t old_gv_flags = 0;

	auto notify_direction_changed = [&](Direction old_direction, Direction new_direction) {
		if (prev == nullptr && _settings_game.vehicle.train_acceleration_model == AM_ORIGINAL) {
			const AccelerationSlowdownParams *asp = &_accel_slowdown[static_cast<uint>(first->GetAccelerationType())];
			DirDiff diff = DirDifference(old_direction, new_direction);
			first->cur_speed -= (diff == DirDiff::Right45 || diff == DirDiff::Left45 ? asp->small_turn : asp->large_turn) * first->cur_speed >> 8;
		}
		direction_changed = true;
	};

	if (reverse && first->reverse_distance == 1 && (first->cur_speed <= 15 || !first->UsingRealisticBraking())) {
		/* Train is not moving too fast and reversing distance has been reached */
		goto reverse_train_direction;
	}

	/* For every vehicle after and including the given vehicle */
	for (prev = v->GetMovingPrev(); v != nomove; prev = v, v = v->GetMovingNext()) {
		r3r_mov_iters++; // KI-14 (2): measure how many vehicles this call actually walks
		old_direction = v->direction;
		old_trackbits = v->track;
		old_gv_flags = v->gv_flags;
		DiagDirection enterdir = DiagDirection::Begin;
		bool update_signals_crossing = false; // will we update signals or crossing state?


		GetNewVehiclePosResult gp = GetNewVehiclePos(v);
		if (!(v->track & TRACK_BIT_WORMHOLE) && gp.old_tile != gp.new_tile &&
				IsRailBridgeHeadTile(gp.old_tile) && DiagdirBetweenTiles(gp.old_tile, gp.new_tile) == GetTunnelBridgeDirection(gp.old_tile)) {
			/* left a bridge headtile into a wormhole */
			Direction old_direction = v->direction;
			auto vets = VehicleEnterTile(v, gp.old_tile, gp.x, gp.y); // NB: old tile, the bridge head which the train just left
			if (vets.Test(VehicleEnterTileState::CannotEnter)) {
				goto invalid_rail;
			}
			if (old_direction != v->direction) notify_direction_changed(old_direction, v->direction);
			DiagDirection dir = GetTunnelBridgeDirection(gp.old_tile);
			VehicleEnterTileCoordinates(gp, dir, AxisToTrack(DiagDirToAxis(dir)));
		}
		if (!(v->track & TRACK_BIT_WORMHOLE)) {
			/* Not inside tunnel */
			if (gp.old_tile == gp.new_tile) {
				/* Staying in the old tile */
				if (v->track == TRACK_BIT_DEPOT) {
					/* Inside depot */
					gp.x = v->x_pos;
					gp.y = v->y_pos;
					first->reverse_distance = 0;
				} else {
					/* Not inside depot */

					/* Reverse when we are at the end of the track already, do not move to the new position */
					if (v->IsMovingFront() && !TrainCheckIfLineEnds(v, reverse)) return false;

					auto vets = VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
					if (vets.Test(VehicleEnterTileState::CannotEnter)) {
						goto invalid_rail;
					}
					if (vets.Test(VehicleEnterTileState::EnteredStation)) {
						/* The new position is the end of the platform */
						TrainEnterStation(first, GetStationIndex(gp.new_tile));
					}
					if (old_direction != v->direction) notify_direction_changed(old_direction, v->direction);
				}
			} else {
				/* A new tile is about to be entered. */

				/* Determine what direction we're entering the new tile from */
				enterdir = DiagdirBetweenTiles(gp.old_tile, gp.new_tile);
				dbg_assert(IsValidDiagDirection(enterdir));

				enter_new_tile:

				/* Get the status of the tracks in the new tile and mask
				 * away the bits that aren't reachable. */
				TrackStatus ts = GetTileTrackStatus(gp.new_tile, TRANSPORT_RAIL, 0, (v->track & TRACK_BIT_WORMHOLE) ? DiagDirection::Invalid : ReverseDiagDir(enterdir));
				TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(enterdir);

				TrackdirBits trackdirbits = ts.trackdirs & reachable_trackdirs;
				TrackBits red_signals = TrackdirBitsToTrackBits(ts.signals & reachable_trackdirs);

				TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
				if (Rail90DegTurnDisallowedTilesFromDiagDir(gp.old_tile, gp.new_tile, enterdir, _settings_game.pf.forbid_90_deg) && prev == nullptr) {
					/* We allow wagons to make 90 deg turns, because forbid_90_deg
					 * can be switched on halfway a turn */
					if (!(v->track & TRACK_BIT_WORMHOLE)) {
						bits &= ~TrackCrossesTracks(FindFirstTrack(v->track));
					} else if (v->track & TRACK_BIT_MASK) {
						bits &= ~TrackCrossesTracks(FindFirstTrack(v->track & TRACK_BIT_MASK));
					}
				}

				if (bits == TRACK_BIT_NONE) goto invalid_rail;

				/* Check if the new tile constrains tracks that are compatible
				 * with the current train, if not, bail out. */
				if (!CheckCompatibleRail(first, gp.new_tile, enterdir, prev == nullptr)) goto invalid_rail;

				TrackBits chosen_track;
				bool reverse_at_signal = false;
				if (prev == nullptr) {
					/* Currently the locomotive is active. Determine which one of the
					 * available tracks to choose */
					ChooseTrainTrackResult result = ChooseTrainTrack(first, gp.new_tile, enterdir, bits, CTTF_MARK_STUCK | CTTF_NON_LOOKAHEAD);
					assert(IsValidTrack(result.track));
					chosen_track = TrackToTrackBits(result.track);
					reverse_at_signal = (result.ctt_flags & CTTRF_REVERSE_AT_SIGNAL);
					dbg_assert_msg_tile(chosen_track & (bits | GetReservedTrackbits(gp.new_tile)), gp.new_tile, "0x{:X}, 0x{:X}, 0x{:X}", chosen_track, bits, GetReservedTrackbits(gp.new_tile));

					if (first->force_proceed != TFP_NONE && IsPlainRailTile(gp.new_tile) && HasSignals(gp.new_tile)) {
						/* For each signal we find decrease the counter by one.
						 * We start at two, so the first signal we pass decreases
						 * this to one, then if we reach the next signal it is
						 * decreased to zero and we won't pass that new signal. */
						Trackdir dir = FindFirstTrackdir(trackdirbits);
						if (HasSignalOnTrackdir(gp.new_tile, dir) ||
								(HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(dir)) &&
								GetSignalType(gp.new_tile, TrackdirToTrack(dir)) != SignalType::Path)) {
							/* However, we do not want to be stopped by PBS signals
							 * entered via the back. */
							first->force_proceed = (first->force_proceed == TFP_SIGNAL) ? TFP_STUCK : TFP_NONE;
							InvalidateWindowData(WindowClass::VehicleView, first->index);
						}
					}

					/* Check if it's a red signal and that force proceed is not clicked. */
					if ((red_signals & chosen_track) && first->force_proceed == TFP_NONE) {
						/* In front of a red signal */
						Trackdir i = FindFirstTrackdir(trackdirbits);

						if (reverse_at_signal) {
							v->flags.Reset(VehicleRailFlag::Stuck);
							goto reverse_train_direction;
						}

						/* Don't handle stuck trains here. */
						if (first->flags.Test(VehicleRailFlag::Stuck)) return false;

						if (IsNoEntrySignal(gp.new_tile, TrackdirToTrack(i)) && HasSignalOnTrackdir(gp.new_tile, i)) {
							goto reverse_train_direction;
						}

						if (!HasSignalOnTrackdir(gp.new_tile, ReverseTrackdir(i))) {
							first->cur_speed = 0;
							first->subspeed = 0;
							first->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++first->wait_counter < _settings_game.pf.wait_oneway_signal * DAY_TICKS * 2) return false;
						} else if (HasSignalOnTrackdir(gp.new_tile, i)) {
							first->cur_speed = 0;
							first->subspeed = 0;
							first->progress = 255; // make sure that every bit of acceleration will hit the signal again, so speed stays 0.
							if (!_settings_game.pf.reverse_at_signals || ++first->wait_counter < _settings_game.pf.wait_twoway_signal * DAY_TICKS * 2) {
								DiagDirection exitdir = TrackdirToExitdir(i);
								TileIndex o_tile = TileAddByDiagDir(gp.new_tile, exitdir);

								exitdir = ReverseDiagDir(exitdir);

								/* check if a train is waiting on the other side */
								if (!HasVehicleOnTile<VehicleType::Train>(o_tile, [&exitdir](const Train *t) {
										if (t->vehstatus.Test(VehState::Crashed)) return false;

										/* not front engine of a train, inside wormhole or depot, crashed */
										if (!t->IsMovingFront() || !(t->track & TRACK_BIT_MASK)) return false;

										if (t->cur_speed > 5 || VehicleExitDir(t->GetMovingDirection(), t->track) != exitdir) return false;

										return true;
									})) return false;
							}
						}

						/* If we would reverse but are currently in a PBS block and
						 * reversing of stuck trains is disabled, don't reverse.
						 * This does not apply if the reason for reversing is a one-way
						 * signal blocking us, because a train would then be stuck forever. */
						if (!_settings_game.pf.reverse_at_signals && !HasOnewaySignalBlockingTrackdir(gp.new_tile, i) &&
								UpdateSignalsOnSegment(v->tile, enterdir, v->owner) == SigSegState::Path) {
							first->wait_counter = 0;
							return false;
						}
						goto reverse_train_direction;
					} else if (!(v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(gp.new_tile) &&
							IsTunnelBridgeSignalSimulationExitOnly(gp.new_tile) && TrackdirEntersTunnelBridge(gp.new_tile, FindFirstTrackdir(trackdirbits)) &&
							v->force_proceed == TFP_NONE) {
						goto reverse_train_direction;
					} else {
						R3RTrackBitsProbe("reserve-chosen", chosen_track, (int)v->index.base(), gp.new_tile);
						TryReserveRailTrack(gp.new_tile, TrackBitsToTrack(chosen_track), false);

						if (IsPlainRailTile(gp.new_tile) && HasSignals(gp.new_tile) && IsRestrictedSignal(gp.new_tile)) {
							const Trackdir dir = FindFirstTrackdir(trackdirbits);
							if (HasSignalOnTrack(gp.new_tile, TrackdirToTrack(dir))) {
								TrainControllerTraceRestrictFrontEvaluation(gp.new_tile, dir, first, TRPAUF_REVERSE_BEHIND, [&]() -> bool {
									return !IsPbsSignal(GetSignalType(gp.new_tile, TrackdirToTrack(dir)));
								}, [&](const TraceRestrictProgramResult &out) {
									if (out.flags & TRPRF_REVERSE_BEHIND && GetSignalType(gp.new_tile, TrackdirToTrack(dir)) == SignalType::Path &&
											!HasSignalOnTrackdir(gp.new_tile, dir)) {
										first->reverse_distance = first->gcache.cached_total_length + (IsDiagonalTrack(TrackdirToTrack(dir)) ? 16 : 8);
										SetWindowDirty(WindowClass::VehicleView, first->index);
									}
								});
							}
						}
					}
				} else {
					/* The wagon is active, simply follow the prev vehicle. */
					if (TileVirtXY(prev->x_pos, prev->y_pos) == gp.new_tile) {
						/* Choose the same track as prev */
						if (prev->track & TRACK_BIT_WORMHOLE) {
							/* Vehicles entering tunnels enter the wormhole earlier than for bridges.
							 * However, just choose the track into the wormhole. */
							dbg_assert_tile(IsTunnel(prev->tile), prev->tile);
							chosen_track = bits;
						} else {
							chosen_track = prev->track;
						}
					} else {
						/* Choose the track that leads to the tile where prev is.
						 * This case is active if 'prev' is already on the second next tile, when 'v' just enters the next tile.
						 * I.e. when the tile between them has only space for a single vehicle like
						 *  1) horizontal/vertical track tiles and
						 *  2) some orientations of tunnel entries, where the vehicle is already inside the wormhole at 8/16 from the tile edge.
						 *     Is also the train just reversing, the wagon inside the tunnel is 'on' the tile of the opposite tunnel entry.
						 */
						static const DiagDirectionIndexArray<DiagDirectionIndexArray<TrackBits>> _connecting_track{{{
							{TRACK_BIT_X,     TRACK_BIT_LOWER, TRACK_BIT_NONE,  TRACK_BIT_LEFT },
							{TRACK_BIT_UPPER, TRACK_BIT_Y,     TRACK_BIT_LEFT,  TRACK_BIT_NONE },
							{TRACK_BIT_NONE,  TRACK_BIT_RIGHT, TRACK_BIT_X,     TRACK_BIT_UPPER},
							{TRACK_BIT_RIGHT, TRACK_BIT_NONE,  TRACK_BIT_LOWER, TRACK_BIT_Y    }
						}}};
						DiagDirection exitdir = DiagdirBetweenTiles(gp.new_tile, TileVirtXY(prev->x_pos, prev->y_pos));
						if (IsValidDiagDirection(exitdir)) {
							chosen_track = _connecting_track[enterdir][exitdir];
						} else {
							/* R3R: a fold-fix merge (TryTrainCouple, see the
							 * FOLDCHK-ACCEPT line) accepts a chain whose neighbours are
							 * still more than one tile apart: ArrangeTrains re-links the
							 * chain order but does NOT re-space the vehicles, so `prev`
							 * can transiently sit two or more tiles away while the rest
							 * of the chain is being pulled together. Upstream asserts the
							 * adjacent-tile invariant here; an R3R-merged chain is
							 * allowed to violate it until it has closed up (observed
							 * 2026-09-15: this assert aborted the game, crash log
							 * "Assertion failed at line 8823 ... IsValidDiagDirection",
							 * right after a successful COUPLE-FLIP-V merge started to
							 * move). A non-adjacent prev has no "connecting track", so
							 * keep the vehicle going straight ahead and let
							 * `chosen_track &= bits` below decide whether that is
							 * possible at all -- if it is not, the TRACK_BIT_NONE
							 * handling just below already leaves the vehicle where it is
							 * instead of crashing. */
							chosen_track = _connecting_track[enterdir][enterdir];
						}
					}
					chosen_track &= bits;
				}

				/* Update XY to reflect the entrance to the new tile, and select the direction to use */
				if (chosen_track == TRACK_BIT_NONE) {
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "TTB-PROBE fold-geom veh=%d tile=%d,%d x=%d y=%d prev=%d ptile=%d,%d px=%d py=%d bits=0x%X prevtrack=0x%X ptype=%d first=%d mvfront=%d db=%d speed=%d\n",
								(int)v->index.base(), (int)TileX(gp.new_tile), (int)TileY(gp.new_tile), (int)gp.x, (int)gp.y,
								(prev != nullptr) ? (int)prev->index.base() : -1,
								(prev != nullptr) ? (int)TileX(prev->tile) : -1, (prev != nullptr) ? (int)TileY(prev->tile) : -1,
								(prev != nullptr) ? (int)prev->x_pos : -1, (prev != nullptr) ? (int)prev->y_pos : -1,
								(uint)bits, (prev != nullptr) ? (uint)prev->track : 0, (prev != nullptr) ? (int)prev->type : -1,
								(int)first->index.base(), (int)first->GetMovingFront()->index.base(),
								(int)first->vehicle_flags.Test(VehicleFlag::DrivingBackwards), (int)first->cur_speed);
						int dbg_i = 0;
						for (const Train *w = first; w != nullptr && dbg_i < R3R_CHAIN_WALK_LIMIT; w = w->Next(), dbg_i++) {
							fprintf(dbg, "  F%d idx=%d front=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X spd=%d prog=%d flags=0x%X segm=%d nxt=%d\n",
									dbg_i, (int)w->index.base(), (int)w->IsFrontEngine(), (int)w->x_pos, (int)w->y_pos,
									(int)TileX(w->tile), (int)TileY(w->tile), (int)w->direction, (uint)w->track,
									(int)w->cur_speed, (int)w->progress,
									(uint)w->flags.base(), (int)w->IsSegmentFront(), (w->Next() != nullptr) ? (int)w->Next()->index.base() : -1);
						}
						/* R3R (KI-217): the moving chain is walked through
						 * GetMovingNext(), which reads the per-vehicle
						 * DrivingBackwards flag. If that flag is ever mixed along
						 * the chain this walk crosses the seam back and forth
						 * forever -- on 2026-09-27 it ran 19M times, wrote a 1.1 GB
						 * log and froze the game. Cap the dump and say so instead. */
						int dbg_mi = 0;
						for (const Train *w = first->GetMovingFront(); w != nullptr && dbg_mi < R3R_CHAIN_WALK_LIMIT; w = w->GetMovingNext(), dbg_mi++) {
							fprintf(dbg, "  MV%d idx=%d x=%d y=%d tile=%d,%d dir=%d trk=0x%X\n",
									dbg_mi, (int)w->index.base(), (int)w->x_pos, (int)w->y_pos,
									(int)TileX(w->tile), (int)TileY(w->tile), (int)w->direction, (uint)w->track);
						}
						fprintf(dbg, "  MVEND count=%d capped=%d oldtile=%d,%d newtile=%d,%d\n",
								dbg_mi, (dbg_mi >= R3R_CHAIN_WALK_LIMIT) ? 1 : 0,
								(int)TileX(gp.old_tile), (int)TileY(gp.old_tile), (int)TileX(gp.new_tile), (int)TileY(gp.new_tile));
						fclose(dbg);
					}
					/* R3R: a folded chain (see R3RCheckChainFoldedDirection /
					 * TryTrainCouple) leaves a wagon with no legal track to follow
					 * its predecessor once the train starts moving. Do not call
					 * TrackBitsToTrack(TRACK_BIT_NONE) (hard assert / crash) — bail
					 * out and leave the vehicle where it is instead. This branch
					 * is only reached for an active wagon, whose prev is never
					 * null, so returning true matches the invalid_rail handling. */
					return true;
				}
				Direction chosen_dir = VehicleEnterTileCoordinates(gp, enterdir, TrackBitsToTrack(chosen_track));

				/* Call the landscape function and tell it that the vehicle entered the tile */
				auto vets = (v->track & TRACK_BIT_WORMHOLE) ? VehicleEnterTileStates{} : VehicleEnterTile(v, gp.new_tile, gp.x, gp.y);
				if (vets.Test(VehicleEnterTileState::CannotEnter)) {
					goto invalid_rail;
				}

				if (!(v->track & TRACK_BIT_WORMHOLE) && IsTunnelBridgeWithSignalSimulation(gp.new_tile) && (GetAcrossTunnelBridgeTrackBits(gp.new_tile) & chosen_track)) {
					/* If red signal stop. */
					if (v->IsMovingFront() && first->force_proceed == 0) {
						if (IsTunnelBridgeSignalSimulationEntrance(gp.new_tile) && GetTunnelBridgeEntranceSignalState(gp.new_tile) == SignalState::Red) {
							first->cur_speed = 0;
							first->vehstatus.Set(VehState::TrainSlowing);
							return false;
						}
						if (IsTunnelBridgeSignalSimulationExitOnly(gp.new_tile) &&
								TrackdirEntersTunnelBridge(gp.new_tile, TrackDirectionToTrackdir(FindFirstTrack(chosen_track), chosen_dir))) {
							first->cur_speed = 0;
							goto invalid_rail;
						}
						/* Flip signal on tunnel entrance tile red. */
						SetTunnelBridgeEntranceSignalState(gp.new_tile, SignalState::Red);
						if (_extra_aspects > 0) {
							PropagateAspectChange(gp.new_tile, GetTunnelBridgeEntranceTrackdir(gp.new_tile), 0);
						}
						MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
						if (IsTunnelBridgeSignalSimulationBidirectional(gp.new_tile)) {
							/* Set incoming signals in other direction to red as well */
							TileIndex other_end = GetOtherTunnelBridgeEnd(gp.new_tile);
							SetTunnelBridgeEntranceSignalState(other_end, SignalState::Red);
							if (_extra_aspects > 0) {
								PropagateAspectChange(other_end, GetTunnelBridgeEntranceTrackdir(other_end), 0);
							}
							if (IsBridge(other_end)) {
								SetAllBridgeEntranceSimulatedSignalsRed(other_end, gp.new_tile);
								MarkBridgeDirty(other_end, gp.new_tile, VMDF_NOT_MAP_MODE);
							} else {
								MarkTileDirtyByTile(other_end, VMDF_NOT_MAP_MODE);
							}
						}
					}
				}

				if (!vets.Test(VehicleEnterTileState::EnteredWormhole)) {
					Track track = FindFirstTrack(chosen_track);
					Trackdir tdir = TrackDirectionToTrackdir(track, chosen_dir);
					if (v->IsMovingFront() && HasPbsSignalOnTrackdir(gp.new_tile, tdir)) {
						SetSignalStateByTrackdir(gp.new_tile, tdir, SignalState::Red);
						MarkSingleSignalDirty(gp.new_tile, tdir);
					}

					/* Clear any track reservation when the last vehicle leaves the tile */
					if (v->GetMovingNext() == nullptr && !(v->track & TRACK_BIT_WORMHOLE)) ClearPathReservation(v, v->tile, v->GetVehicleTrackdir(), true);

					v->tile = gp.new_tile;
					v->track = chosen_track;
					dbg_assert(v->track);

					if (GetTileRailTypeByTrackBit(gp.new_tile, chosen_track) != GetTileRailTypeByTrackBit(gp.old_tile, old_trackbits)) {
						/* v->track and v->tile must both be valid and consistent before this is called */
						first->ConsistChanged(CCF_TRACK);
					}
				}

				/* We need to update signal status, but after the vehicle position hash
				 * has been updated by UpdateInclination() */
				update_signals_crossing = true;

				Direction moving_direction = v->GetMovingDirection();
				if (chosen_dir != moving_direction) {
					notify_direction_changed(moving_direction, chosen_dir);
					v->SetMovingDirection(chosen_dir);
				}

				if (v->IsMovingFront()) {
					first->wait_counter = 0;

					/* If we are approaching a crossing that is reserved, play the sound now. */
					TileIndex crossing = TrainApproachingCrossingTile(v); // We know we are the moving front, so we can check v.
					if (crossing != INVALID_TILE && HasCrossingReservation(crossing) && _settings_client.sound.ambient) SndPlayTileFx(SND_0E_LEVEL_CROSSING, crossing);

					/* Always try to extend the reservation when entering a tile. */
					CheckNextTrainTile(v);
				}

				if (vets.Test(VehicleEnterTileState::EnteredStation)) {
					/* The new position is the location where we want to stop */
					TrainEnterStation(first, GetStationIndex(gp.new_tile));
				}
			}
		} else {
			/* Handle signal simulation on tunnel/bridge. */
			TileIndex old_tile = TileVirtXY(v->x_pos, v->y_pos);
			if (old_tile != gp.new_tile && IsTunnelBridgeWithSignalSimulation(v->tile) && (v->Previous() == nullptr || v->Next() == nullptr)) {
				const uint simulated_wormhole_signals = GetTunnelBridgeSignalSimulationSpacing(v->tile);
				if (old_tile == v->tile) {
					if (v->IsMovingFront() && first->force_proceed == 0 && IsTunnelBridgeSignalSimulationExitOnly(v->tile)) goto invalid_rail;
					/* Entered wormhole set counters. */
					v->tunnel_bridge_tile_ctr = static_cast<uint8_t>(simulated_wormhole_signals - 1);
					v->tunnel_bridge_signal_num = 0;

					if (v->IsMovingFront() && IsTunnelBridgeSignalSimulationEntrance(old_tile) && (IsTunnelBridgeRestrictedSignal(old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
						const Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(old_tile);
						if (IsTunnelBridgeRestrictedSignal(old_tile)) {
							TrainControllerTraceRestrictFrontEvaluation(old_tile, trackdir, first, TRPAUF_NONE, [&]() -> bool {
								/* Only acquire slot when not using realistic braking, as the tunnel/bridge entrance otherwise acts as a block signal */
								return _settings_game.vehicle.train_braking_model != TBM_REALISTIC;
							}, [&](const TraceRestrictProgramResult &out) {});
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, old_tile, TrackdirToTrack(trackdir));
						}
					}

					if (v->GetMovingNext() == nullptr && IsTunnelBridgeSignalSimulationEntrance(old_tile) && (IsTunnelBridgeRestrictedSignal(old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
						const Trackdir trackdir = GetTunnelBridgeEntranceTrackdir(old_tile);
						const Track track = TrackdirToTrack(trackdir);

						if (IsTunnelBridgeRestrictedSignal(old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(old_tile, trackdir, nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							ApplySignalTrainAdaptationSpeed(v, old_tile, track);
						}
					}
				}

				uint distance = v->tunnel_bridge_tile_ctr;
				bool leaving = false;
				if (distance == 0) v->tunnel_bridge_tile_ctr = simulated_wormhole_signals;

				if (v->IsMovingFront()) {
					/* Check if track in front is free and see if we can leave wormhole. */
					int z = GetSlopePixelZ(gp.x, gp.y, true) - v->z_pos;
					if (IsTileType(gp.new_tile, TileType::TunnelBridge) && !(abs(z) > 2)) {
						if (CheckTrainStayInWormHole(v, gp.new_tile)) {
							first->cur_speed = 0;
							return false;
						}
						leaving = true;
						if (IsTunnelBridgeRestrictedSignal(gp.new_tile) && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
							const Trackdir trackdir = GetTunnelBridgeExitTrackdir(gp.new_tile);
							TrainControllerTraceRestrictFrontEvaluation(gp.new_tile, trackdir, first, TRPAUF_NONE, [&]() -> bool {
								return !IsTunnelBridgeEffectivelyPBS(gp.new_tile);
							}, [&](const TraceRestrictProgramResult &out) {});
						}
					} else {
						if (IsTooCloseBehindTrain(v, gp.new_tile, TILE_SIZE * v->tunnel_bridge_tile_ctr, distance == 0)) {
							if (distance == 0) v->tunnel_bridge_tile_ctr = 0;
							first->cur_speed = 0;
							first->vehstatus.Set(VehState::TrainSlowing);
							return false;
						}
						/* flip signal in front to red on bridges*/
						if (distance == 0 && IsBridge(v->tile) && IsTunnelBridgeSignalSimulationEntrance(v->tile)) {
							SetBridgeEntranceSimulatedSignalState(v->tile, v->tunnel_bridge_signal_num, SignalState::Red);
							MarkSingleBridgeSignalDirty(gp.new_tile, v->tile);
						}
						if (_settings_game.vehicle.train_speed_adaptation && distance == 0 && IsTunnelBridgeSignalSimulationEntrance(v->tile)) {
							ApplySignalTrainAdaptationSpeed(v, v->tile, 0x100 + v->tunnel_bridge_signal_num);
						}
					}
				}
				if (v->GetMovingNext() == nullptr) {
					if (v->tunnel_bridge_signal_num > 0 && distance == (simulated_wormhole_signals - 1)) {
						HandleSignalBehindTrain(v, v->tunnel_bridge_signal_num - 2);
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, v->tile, 0x100 + v->tunnel_bridge_signal_num - 1);
						}
					}
					DiagDirection tunnel_bridge_dir = GetTunnelBridgeDirection(v->tile);
					Axis axis = DiagDirToAxis(tunnel_bridge_dir);
					DiagDirection axial_dir = DirToDiagDirAlongAxis(v->GetMovingDirection(), axis);
					if (old_tile == ((axial_dir == tunnel_bridge_dir) ? v->tile : GetOtherTunnelBridgeEnd(v->tile))) {
						/* We left ramp into wormhole. */
						v->x_pos = gp.x;
						v->y_pos = gp.y;
						UpdateSignalsOnSegment(old_tile, DiagDirection::Invalid, v->owner);
						UnreserveBridgeTunnelTile(old_tile);
						if (_settings_client.gui.show_track_reservation) MarkTileDirtyByTile(old_tile, VMDF_NOT_MAP_MODE);
					}
				}
				if (distance == 0) v->tunnel_bridge_signal_num++;
				if (v->tunnel_bridge_tile_ctr != Train::TBS_INVALID_DISTANCE) v->tunnel_bridge_tile_ctr--;

				if (leaving) { // Reset counters.
					first->force_proceed = TFP_NONE;
					v->tunnel_bridge_tile_ctr = 0;
					v->tunnel_bridge_signal_num = 0;
					update_signal_tunbridge_exit = true;
				}
			}
			if (old_tile == gp.new_tile && IsTunnelBridgeWithSignalSimulation(v->tile) && v->IsMovingFront()) {
				Axis axis = DiagDirToAxis(GetTunnelBridgeDirection(v->tile));
				DiagDirection axial_dir = DirToDiagDirAlongAxis(v->GetMovingDirection(), axis);
				TileIndex next_tile = old_tile + TileOffsByDiagDir(axial_dir);
				bool is_exit = false;
				if (IsTileType(next_tile, TileType::TunnelBridge) && IsTunnelBridgeWithSignalSimulation(next_tile) &&
						ReverseDiagDir(GetTunnelBridgeDirection(next_tile)) == axial_dir) {
					if (IsBridge(next_tile) && IsBridge(v->tile)) {
						// bridge ramp facing towards us
						is_exit = true;
					} else if (IsTunnel(next_tile) && IsTunnel(v->tile)) {
						// tunnel exit at same height
						is_exit = (GetTileZ(next_tile) == GetTileZ(v->tile));
					}
				}
				if (is_exit) {
					if (CheckTrainStayInWormHole(v, next_tile)) {
						TrainApproachingLineEnd(v, true, false);
					}
				} else if (v->tunnel_bridge_tile_ctr == 0) {
					if (IsTooCloseBehindTrain(v, next_tile, TILE_SIZE * GetTunnelBridgeSignalSimulationSpacing(v->tile), true)) {
						TrainApproachingLineEnd(v, true, false);
					}
				}
			}

			if (IsTileType(gp.new_tile, TileType::TunnelBridge) && VehicleEnterTile(v, gp.new_tile, gp.x, gp.y).Test(VehicleEnterTileState::EnteredWormhole)) {
				/* Perform look-ahead on tunnel exit. */
				if (IsRailCustomBridgeHeadTile(gp.new_tile)) {
					enterdir = ReverseDiagDir(GetTunnelBridgeDirection(gp.new_tile));
					goto enter_new_tile;
				}
				if (v->IsMovingFront()) {
					TryReserveRailTrack(gp.new_tile, DiagDirToDiagTrack(GetTunnelBridgeDirection(gp.new_tile)));
					CheckNextTrainTile(v);
				}
				/* Prevent v->UpdateInclination() being called with wrong parameters.
				 * This could happen if the train was reversed inside the tunnel/bridge. */
				if (gp.old_tile == gp.new_tile) {
					gp.old_tile = GetOtherTunnelBridgeEnd(gp.old_tile);
				}
			} else {
				v->x_pos = gp.x;
				v->y_pos = gp.y;
				v->UpdatePosition();
				v->UpdateDeltaXY();
				DecreaseReverseDistance(v);
				if (v->lookahead != nullptr) AdvanceLookAheadPosition(v);
				if (v->flags.Test(VehicleRailFlag::PendingSpeedRestriction)) DecrementPendingSpeedRestrictions(v);
				if (HasBit(v->gv_flags, GVF_CHUNNEL_BIT)) {
					/* update the Z position of the vehicle */
					int old_z = v->UpdateInclination(false, false, true);

					if (prev == nullptr) {
						/* This is the first vehicle in the train */
						AffectSpeedByZChange(v, old_z);
					}
				}
				if (v->IsDrawn()) v->Vehicle::UpdateViewport(true);
				if (update_signal_tunbridge_exit) {
					UpdateSignalsOnSegment(gp.new_tile, DiagDirection::Invalid, v->owner);
					update_signal_tunbridge_exit = false;
					if (v->IsMovingFront() && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
						SetTunnelBridgeExitSignalState(gp.new_tile, SignalState::Red);
						MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
					}
				}
				if (v->IsMovingFront() && !IsTunnelBridgeWithSignalSimulation(v->tile) && (first->lookahead != nullptr &&
						first->cur_speed > 0 && first->lookahead->reservation_end_position <= first->lookahead->current_position + 24)) {
					TryLongReserveChooseTrainTrackFromReservationEnd(first, true);
				}
				continue;
			}
		}

		/* update image of train, as well as delta XY */
		v->UpdateDeltaXY();

		v->x_pos = gp.x;
		v->y_pos = gp.y;
		v->UpdatePosition();
		DecreaseReverseDistance(v);
		if (v->lookahead != nullptr) AdvanceLookAheadPosition(v);
		if (v->flags.Test(VehicleRailFlag::PendingSpeedRestriction)) DecrementPendingSpeedRestrictions(v);

		/* update the Z position of the vehicle */
		int old_z = v->UpdateInclination(gp.new_tile != gp.old_tile, false, v->track == TRACK_BIT_WORMHOLE);

		if (prev == nullptr) {
			/* This is the first vehicle in the train */
			AffectSpeedByZChange(first, v->z_pos - old_z);
		}

		if (update_signal_tunbridge_exit) {
			UpdateSignalsOnSegment(gp.new_tile, DiagDirection::Invalid, v->owner);
			update_signal_tunbridge_exit = false;
			if (v->IsMovingFront() && IsTunnelBridgeSignalSimulationExit(gp.new_tile)) {
				SetTunnelBridgeExitSignalState(gp.new_tile, SignalState::Red);
				MarkTileDirtyByTile(gp.new_tile, VMDF_NOT_MAP_MODE);
			}
		}

		if (update_signals_crossing) {

			if (v->IsMovingFront()) {
				if (_settings_game.vehicle.train_speed_adaptation && IsTileType(gp.old_tile, TileType::Railway) && HasSignals(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					if (HasSignalOnTrackdir(gp.old_tile, ReverseTrackdir(rev_trackdir))) {
						ApplySignalTrainAdaptationSpeed(v, gp.old_tile, TrackdirToTrack(rev_trackdir));
					}
				}
				if (_settings_game.vehicle.train_speed_adaptation && IsTileType(gp.old_tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExit(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					ApplySignalTrainAdaptationSpeed(v, gp.old_tile, TrackdirToTrack(rev_trackdir));
				}

				switch (TrainMovedChangeSignal(first, gp.new_tile, enterdir, true)) {
					case CHANGED_NORMAL_TO_PBS_BLOCK:
						/* We are entering a block with PBS signals right now, but
						* not through a PBS signal. This means we don't have a
						* reservation right now. As a conventional signal will only
						* ever be green if no other train is in the block, getting
						* a path should always be possible. If the player built
						* such a strange network that it is not possible, the train
						* will be marked as stuck and the player has to deal with
						* the problem. */
						if ((!HasReservedTracks(gp.new_tile, v->track) &&
								!TryReserveRailTrack(gp.new_tile, FindFirstTrack(v->track))) ||
								!TryPathReserve(first)) {
							MarkTrainAsStuck(first);
						}

						break;

					case CHANGED_LR_PBS:
						{
							/* We went past a long reserve PBS signal. Try to extend the
							* reservation if reserving failed at another LR signal. */
							TryLongReserveChooseTrainTrackFromReservationEnd(first);
							break;
						}

					default:
						break;
				}
			}

			/* Signals can only change when the first
			 * (above) or the last vehicle moves. */
			if (v->GetMovingNext() == nullptr) {
				TrainMovedChangeSignal(first, gp.old_tile, ReverseDiagDir(enterdir), false);
				if (IsLevelCrossingTile(gp.old_tile)) UpdateLevelCrossing(gp.old_tile);

				if (IsTileType(gp.old_tile, TileType::Railway) && HasSignals(gp.old_tile)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					const Track track = TrackdirToTrack(rev_trackdir);

					if (_settings_game.vehicle.train_speed_adaptation && HasSignalOnTrackdir(gp.old_tile, ReverseTrackdir(rev_trackdir))) {
						SetSignalTrainAdaptationSpeed(v, gp.old_tile, track);
					}

					if (HasSignalOnTrack(gp.old_tile, track)) {
						if (IsRestrictedSignal(gp.old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(gp.old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(gp.old_tile, ReverseTrackdir(rev_trackdir), nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
					}
				}

				if (IsTileType(gp.old_tile, TileType::TunnelBridge) && IsTunnelBridgeSignalSimulationExit(gp.old_tile) && (IsTunnelBridgeRestrictedSignal(gp.old_tile) || _settings_game.vehicle.train_speed_adaptation)) {
					const TrackdirBits rev_tracks = TrackBitsToTrackdirBits(GetTunnelBridgeTrackBits(gp.old_tile)) & DiagdirReachesTrackdirs(ReverseDiagDir(enterdir));
					const Trackdir rev_trackdir = FindFirstTrackdir(rev_tracks);
					const Track track = TrackdirToTrack(rev_trackdir);

					if (TrackdirEntersTunnelBridge(gp.old_tile, rev_trackdir)) {
						if (IsTunnelBridgeRestrictedSignal(gp.old_tile)) {
							const TraceRestrictProgram *prog = GetExistingTraceRestrictProgram(gp.old_tile, track);
							if (prog != nullptr && prog->actions_used_flags & TRPAUF_SLOT_RELEASE_BACK) {
								TraceRestrictProgramResult out;
								TraceRestrictProgramInput input(gp.old_tile, ReverseTrackdir(rev_trackdir), nullptr, nullptr);
								input.permitted_slot_operations = TRPISP_RELEASE_BACK;
								prog->Execute(first, input, out);
							}
						}
						if (_settings_game.vehicle.train_speed_adaptation) {
							SetSignalTrainAdaptationSpeed(v, gp.old_tile, track);
						}
					}
				}
			}
		}

		/* Do not check on every tick to save some computing time. */
		if (v->IsMovingFront()) {
			if (first->lookahead != nullptr && first->cur_speed > 0 && first->lookahead->reservation_end_position <= first->lookahead->current_position + 24) {
				TryLongReserveChooseTrainTrackFromReservationEnd(first, true);
			} else if (first->tick_counter % _settings_game.pf.path_backoff_interval == 0) {
				CheckNextTrainTile(v);
			}
		}
	}

	if (direction_changed) first->tcache.cached_max_curve_speed = first->GetCurveSpeedLimit();

	return true;

invalid_rail:
	/* We've reached end of line?? */
	if (prev != nullptr) return true; //FatalError("Disconnecting train");

reverse_train_direction:
	if (old_trackbits != TrackBits{0xFF} && (v->track ^ old_trackbits) & TRACK_BIT_WORMHOLE) {
		/* Entering/exiting wormhole failed/aborted, back out changes to vehicle direction and track */
		v->track = old_trackbits;
		v->direction = old_direction;
		v->gv_flags = old_gv_flags;
		if (!(v->track & TRACK_BIT_WORMHOLE)) v->z_pos = GetSlopePixelZ(v->x_pos, v->y_pos, true);
	}
	if (reverse) {
		first->wait_counter = 0;
		first->cur_speed = 0;
		first->subspeed = 0;
		ReverseTrainDirection(first);
	}

	return false;
}

static TrackBits GetTrackbitsFromCrashedVehicle(const Train *t)
{
	TrackBits train_tbits = t->track;
	if (train_tbits & TRACK_BIT_WORMHOLE) {
		/* Vehicle is inside a wormhole, v->track contains no useful value then. */
		train_tbits = GetAcrossTunnelBridgeReservationTrackBits(t->tile);
		if (train_tbits != TRACK_BIT_NONE) return train_tbits;
		/* Pick the first available tunnel/bridge head track which could be reserved */
		train_tbits = GetAcrossTunnelBridgeTrackBits(t->tile);
		return train_tbits ^ KillFirstBit(train_tbits);
	} else if (train_tbits == TRACK_BIT_DEPOT) {
		return TRACK_BIT_NONE;
	} else {
		return train_tbits;
	}
}

static void SetSignalledBridgeTunnelGreenIfClear(TileIndex tile, TileIndex end)
{
	if (TunnelBridgeIsFree(tile, end, nullptr, TBIFM_ACROSS_ONLY).Succeeded()) {
		auto process_tile = [&](TileIndex t) {
			if (IsTunnelBridgeSignalSimulationEntrance(t)) {
				if (IsBridge(t)) {
					SetAllBridgeEntranceSimulatedSignalsGreen(t);
					MarkBridgeDirty(tile, end, VMDF_NOT_MAP_MODE);
				}
				SetTunnelBridgeEntranceSignalGreen(t);
			}
		};
		process_tile(tile);
		process_tile(end);
	}
}

static bool IsRailStationPlatformOccupied(TileIndex tile)
{
	TileIndexDiff delta = TileOffsByAxis(GetRailStationAxis(tile));

	for (TileIndex t = tile; IsCompatibleTrainStationTile(t, tile); t -= delta) {
		if (GetFirstVehicleOnTile(t, VehicleType::Train) != nullptr) return true;
	}
	for (TileIndex t = tile + delta; IsCompatibleTrainStationTile(t, tile); t += delta) {
		if (GetFirstVehicleOnTile(t, VehicleType::Train) != nullptr) return true;
	}

	return false;
}

/**
 * Deletes/Clears the last wagon of a crashed train. It takes the engine of the
 * train, then goes to the last wagon and deletes that. Each call to this function
 * will remove the last wagon of a crashed train. If this wagon was on a crossing,
 * or inside a tunnel/bridge, recalculate the signals as they might need updating
 * @param v the Vehicle of which last wagon is to be removed
 */
static void DeleteLastWagon(Train *v)
{
	Train *first = v->First();

	/* Go to the last wagon and delete the link pointing there
	 * new_last is then the one-before-last wagon, and v the last
	 * one which will physically be removed */
	Train *new_last = v;
	for (; v->Next() != nullptr; v = v->Next()) new_last = v;
	new_last->SetNext(nullptr);

	if (first != v) {
		/* Recalculate cached train properties */
		first->ConsistChanged(CCF_ARRANGE);
		/* Update the depot window in case a part of the consist is in a depot.
		 * If v == first, then it is updated in PreDestructor(). */

		if (first->track == TRACK_BIT_DEPOT) {
			SetWindowDirty(WindowClass::VehicleDepot, first->tile.base());
		}
		if (v->track == TRACK_BIT_DEPOT) {
			SetWindowDirty(WindowClass::VehicleDepot, v->tile.base());
		}
		v->last_station_visited = first->last_station_visited; // for PreDestructor
	}

	/* 'v' shouldn't be accessed after it has been deleted */
	const TrackBits orig_trackbits = v->track;
	TrackBits trackbits = GetTrackbitsFromCrashedVehicle(v);
	const TileIndex tile = v->tile;
	const Owner owner = v->owner;

	delete v;
	v = nullptr; // make sure nobody will try to read 'v' anymore

	R3RTrackBitsProbe("crashed-del", trackbits, -1, tile);
	Track track = TrackBitsToTrack(trackbits);
	if (HasReservedTracks(tile, trackbits)) {
		UnreserveRailTrack(tile, track);

		/* If there are still crashed vehicles on the tile, give the track reservation to them */
		TrackBits remaining_trackbits = TRACK_BIT_NONE;
		for (const Train *u : VehiclesOnTile<VehicleType::Train>(tile)) {
			if (!u->vehstatus.Test(VehState::Crashed)) continue;
			remaining_trackbits |= GetTrackbitsFromCrashedVehicle(u);
		}

		/* It is important that these two are the first in the loop, as reservation cannot deal with every trackbit combination */
		dbg_assert(TRACK_BEGIN == TRACK_X && TRACK_Y == TRACK_BEGIN + 1);
		for (Track t : SetTrackBitIterator(remaining_trackbits)) TryReserveRailTrack(tile, t);
	}

	/* check if the wagon was on a road/rail-crossing */
	if (IsLevelCrossingTile(tile)) UpdateLevelCrossing(tile);

	if (IsRailStationTile(tile)) {
		bool occupied = IsRailStationPlatformOccupied(tile);
		DiagDirection dir = AxisToDiagDir(GetRailStationAxis(tile));
		SetRailStationPlatformReservation(tile, dir, occupied);
		SetRailStationPlatformReservation(tile, ReverseDiagDir(dir), occupied);
	}

	/* Update signals */
	if (IsTunnelBridgeWithSignalSimulation(tile)) {
		TileIndex end = GetOtherTunnelBridgeEnd(tile);
		UpdateSignalsOnSegment(end, DiagDirection::Invalid, owner);
		SetSignalledBridgeTunnelGreenIfClear(tile, end);
	}
	if ((orig_trackbits & TRACK_BIT_WORMHOLE) || IsRailDepotTile(tile)) {
		UpdateSignalsOnSegment(tile, DiagDirection::Invalid, owner);
	} else {
		SetSignalsOnBothDir(tile, track, owner);
	}
}

/**
 * Rotate all vehicles of a (crashed) train chain randomly to animate the crash.
 * @param v First crashed vehicle.
 */
static void ChangeTrainDirRandomly(Train *v)
{
	static const DirDiff delta[] = {
		DirDiff::Left45, DirDiff::Same, DirDiff::Same, DirDiff::Right45
	};

	do {
		/* We don't need to twist around vehicles if they're not visible */
		if (!v->vehstatus.Test(VehState::Hidden)) {
			v->direction = ChangeDir(v->direction, delta[GB(Random(), 0, 2)]);
			/* Refrain from updating the z position of the vehicle when on
			 * a bridge, because UpdateInclination() will put the vehicle under
			 * the bridge in that case */
			if (!(v->track & TRACK_BIT_WORMHOLE)) {
				v->UpdatePosition();
				v->UpdateInclination(false, true);
			} else {
				v->UpdateViewport(false, true);
			}
		}
	} while ((v = v->Next()) != nullptr);
}

/**
 * Handle a crashed train.
 * @param v First train vehicle.
 * @return %Vehicle chain still exists.
 */
static bool HandleCrashedTrain(Train *v)
{
	int state = ++v->crash_anim_pos;

	if (state == 4 && !v->vehstatus.Test(VehState::Hidden)) {
		CreateEffectVehicleRel(v, 4, 4, 8, EV_EXPLOSION_LARGE);
	}

	uint32_t r;
	if (state <= 200 && Chance16R(1, 7, r)) {
		int index = (r * 10 >> 16);

		Vehicle *u = v;
		do {
			if (--index < 0) {
				r = Random();

				CreateEffectVehicleRel(u,
					GB(r,  8, 3) + 2,
					GB(r, 16, 3) + 2,
					GB(r,  0, 3) + 5,
					EV_EXPLOSION_SMALL);
				break;
			}
		} while ((u = u->Next()) != nullptr);
	}

	if (state <= 240 && !(v->tick_counter & 3)) ChangeTrainDirRandomly(v);

	if (state >= 4440 && !(v->tick_counter & 0x1F)) {
		bool ret = v->Next() != nullptr;
		DeleteLastWagon(v);
		return ret;
	}

	return true;
}

/** Maximum speeds for train that is broken down or approaching line end */
static const uint16_t _breakdown_speeds[16] = {
	225, 210, 195, 180, 165, 150, 135, 120, 105, 90, 75, 60, 45, 30, 15, 15
};


/**
 * Train is approaching line end, slow down and possibly reverse
 *
 * @param moving_front moving front vehicle
 * @param signal not line end, just a red signal
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainApproachingLineEnd(Train *moving_front, bool signal, bool reverse)
{
	/* Calc position within the current tile */
	uint x = moving_front->x_pos & 0xF;
	uint y = moving_front->y_pos & 0xF;

	Direction vdir = moving_front->GetMovingDirection();

	/* for diagonal directions, 'x' will be 0..15 -
	 * for other directions, it will be 1, 3, 5, ..., 15 */
	switch (vdir) {
		case Direction::N : x = ~x + ~y + 25; break;
		case Direction::NW: x = y;            [[fallthrough]];
		case Direction::NE: x = ~x + 16;      break;
		case Direction::E : x = ~x + y + 9;   break;
		case Direction::SE: x = y;            break;
		case Direction::S : x = x + y - 7;    break;
		case Direction::W : x = ~y + x + 9;   break;
		default: break;
	}

	Train *consist = moving_front->First();

	/* Do not reverse when approaching red signal. Make sure the vehicle's front
	 * does not cross the tile boundary when we do reverse, but as the vehicle's
	 * location is based on their center, use half a vehicle's length as offset.
	 * Multiply the half-length by two for straight directions to compensate that
	 * we only get odd x offsets there. */
	uint8_t rounding = moving_front->IsDrivingBackwards() ? 0 : 1;
	if (!signal && x + (moving_front->gcache.cached_veh_length + rounding) / 2 * (IsDiagonalDirection(vdir) ? 1 : 2) >= TILE_SIZE) {
		/* we are too near the tile end, reverse now */
		consist->cur_speed = 0;
		if (reverse) ReverseTrainDirection(consist);
		return false;
	}

	/* slow down */
	consist->vehstatus.Set(VehState::TrainSlowing);
	uint16_t break_speed = _breakdown_speeds[x & 0xF];
	if (break_speed < consist->cur_speed) consist->cur_speed = break_speed;

	return true;
}


/**
 * Determines whether train would like to leave the tile.
 * @param moving_front The moving front vehicle of the train.
 * @return true iff vehicle is NOT entering or inside a depot or tunnel/bridge.
 */
static bool TrainCanLeaveTile(const Train *moving_front)
{
	/* Exit if inside a tunnel/bridge or a depot */
	if (moving_front->track & TRACK_BIT_WORMHOLE || moving_front->track == TRACK_BIT_DEPOT) return false;

	TileIndex tile = moving_front->tile;

	/* entering a tunnel/bridge? */
	if (IsTileType(tile, TileType::TunnelBridge)) {
		DiagDirection dir = GetTunnelBridgeDirection(tile);
		Direction moving_direction = moving_front->GetMovingDirection();
		if (DiagDirToDir(dir) == moving_direction) return false;
		if (IsRailCustomBridgeHeadTile(tile) && VehicleExitDir(moving_direction, moving_front->track) == dir) {
			if (_settings_game.pf.forbid_90_deg && GetTunnelBridgeLength(tile, GetOtherTunnelBridgeEnd(tile)) == 0) {
				/* Check for 90 degree turn on zero-length bridge span */
				if (!(GetCustomBridgeHeadTrackBits(tile) & ~TrackCrossesTracks(FindFirstTrack(moving_front->track)))) return true;
			}
			return false;
		}
	}

	/* entering a depot? */
	if (IsRailDepotTile(tile)) {
		DiagDirection dir = ReverseDiagDir(GetRailDepotDirection(tile));
		if (DiagDirToDir(dir) == moving_front->GetMovingDirection()) return false;
	}

	return true;
}


/**
 * Determines whether train is approaching a rail-road crossing
 *   (thus making it barred)
 * @param moving_front moving front of train
 * @return TileIndex of crossing the train is approaching, else INVALID_TILE
 * @pre v in non-crashed front engine
 */
static TileIndex TrainApproachingCrossingTile(const Train *moving_front)
{
	dbg_assert(moving_front->IsMovingFront());
	dbg_assert(!moving_front->vehstatus.Test(VehState::Crashed));

	if (!TrainCanLeaveTile(moving_front)) return INVALID_TILE;

	DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
	TileIndex tile = moving_front->tile + TileOffsByDiagDir(dir);

	/* not a crossing || wrong axis || unusable rail (wrong type or owner) */
	if (!IsLevelCrossingTile(tile) || DiagDirToAxis(dir) == GetCrossingRoadAxis(tile) ||
			!CheckCompatibleRail(moving_front->First(), tile, dir, true)) {
		return INVALID_TILE;
	}

	return tile;
}

/**
 * Checks for line end. Also, bars crossing at next tile if needed
 *
 * @param moving_front moving vehicle front we are checking
 * @param reverse Set to false to not execute the vehicle reversing. This does not change any other logic.
 * @return true iff we did NOT have to reverse
 */
static bool TrainCheckIfLineEnds(Train *moving_front, bool reverse)
{
	/* First, handle broken down train */

	Train *consist = moving_front->First();
	if (consist->flags.Test(VehicleRailFlag::BreakdownBraking)) {
		consist->vehstatus.Set(VehState::TrainSlowing);
	} else {
		consist->vehstatus.Reset(VehState::TrainSlowing);
	}

	if (!TrainCanLeaveTile(moving_front)) return true;

	/* Determine the non-diagonal direction in which we will exit this tile */
	DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
	/* Calculate next tile */
	TileIndex tile = moving_front->tile + TileOffsByDiagDir(dir);

	/* Determine the track status on the next tile */
	TrackStatus ts = GetTileTrackStatus(tile, TRANSPORT_RAIL, RoadTramType::Invalid, ReverseDiagDir(dir));
	TrackdirBits reachable_trackdirs = DiagdirReachesTrackdirs(dir);

	TrackdirBits trackdirbits = ts.trackdirs & reachable_trackdirs;
	TrackdirBits red_signals = ts.signals & reachable_trackdirs;

	/* We are sure the train is not entering a depot, it is detected above */

	/* mask unreachable track bits if we are forbidden to do 90deg turns */
	TrackBits bits = TrackdirBitsToTrackBits(trackdirbits);
	if (Rail90DegTurnDisallowedTilesFromDiagDir(moving_front->tile, tile, dir, _settings_game.pf.forbid_90_deg)) {
		bits &= ~TrackCrossesTracks(FindFirstTrack(moving_front->track));
	}

	/* no suitable trackbits at all || unusable rail (wrong type or owner) */
	if (bits == TRACK_BIT_NONE || !CheckCompatibleRail(consist, tile, dir, true)) {
		return TrainApproachingLineEnd(moving_front, false, reverse);
	}

	/* approaching red signal */
	if ((trackdirbits & red_signals) != 0) return TrainApproachingLineEnd(moving_front, true, reverse);

	/* approaching a rail/road crossing? then make it red */
	if (IsLevelCrossingTile(tile)) MaybeBarCrossingWithSound(tile);

	if (IsTunnelBridgeSignalSimulationEntranceTile(tile) && GetTunnelBridgeEntranceSignalState(tile) == SignalState::Red) {
		return TrainApproachingLineEnd(moving_front, true, reverse);
	}

	return true;
}

/* Calculate the summed up value of all parts of a train */
Money Train::CalculateCurrentOverallValue() const
{
	Money ovr_value = 0;
	const Train *v = this;
	do {
		ovr_value += v->value;
	} while ((v = v->GetNextVehicle()) != nullptr);
	return ovr_value;
}

/**
 * Per-tick handler of each front engine.
 * @param consist The front engine we are working with.
 * @param mode Set to \c True if we want the train to keep existing, \c False if we want to consider deleting it (it has been crashed).
 * @return \c true if we want the train to keep existing, \c False if we want to delete it (it has been crashed).
 */
static bool TrainLocoHandler(Train *consist, bool mode)
{
	R3RScopeTimer r3r_loco_timer(&r3r_loco_ns, &r3r_loco_calls);
	/* R3R (KI-203c): read-only reservation audit, at most one full map pass every
	 * R3R_RESV_AUDIT_TICKS. It lists reserved tiles whose nearest train is far
	 * away -- the "black reservation preview nobody owns" the player keeps
	 * reporting (60,16). Never modifies the map, so it cannot introduce new
	 * failures. The first entry after a loadgame tells whether a given reserved
	 * tile came in with the savegame or was created during this session. */
	{
		static uint32_t r3r_resv_audit_tick = 0;
		static const uint32_t R3R_RESV_AUDIT_TICKS = 2048;
		if (_tick_counter - r3r_resv_audit_tick >= R3R_RESV_AUDIT_TICKS) {
			r3r_resv_audit_tick = _tick_counter;
			R3RReservationAudit("tick");
		}
	}
	/* train has crashed? */
	if (consist->vehstatus.Test(VehState::Crashed)) {
		return mode ? true : HandleCrashedTrain(consist); // 'this' can be deleted here
	} else if (consist->crash_anim_pos > 0) {
		/* Reduce realistic braking brake overheating */
		consist->crash_anim_pos -= (consist->crash_anim_pos + 255) >> 8;
	}

	if (consist->force_proceed != TFP_NONE) {
		consist->flags.Reset(VehicleRailFlag::Stuck);
		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
	}

	/* train is broken down? */
	if (consist->flags.Test(VehicleRailFlag::ConsistBreakdown) && HandlePossibleBreakdowns(consist)) return true;

	if (consist->flags.Test(VehicleRailFlag::Reversing) && consist->cur_speed == 0) {
		R3RScopeTimer r3r_rev_timer(&r3r_rev_ns, &r3r_rev_calls);
		ReverseTrainDirection(consist);
	}

	/* R3R: a consist waiting on a platform keeps its track reserved so the
	 * locomotive's couple pathfinder can detect it (PfDetectDestination's
	 * HasReservedTracks gate). Arrival at the station released it; restore it.
	 * Also reserve the entire platform along its axis so the couple pathfinder
	 * sees a fully-reserved path right up to the consist: a coupling
	 * locomotive finds and reaches the waiting consist precisely through this
	 * whole-platform reservation (it drives over the platform tiles in front
	 * of the formation, which only the formation reserves).
	 * The gate must cover not only car-only formations but also plain primary
	 * trains parked on a platform whose current order is WAIT_COUPLE (the
	 * pxp-decouple flavour: the decoupled part keeps its own schedule and
	 * waits with a WAIT_COUPLE order, it never becomes a formation). Without
	 * the every-tick re-reservation the platform under the waiting formation
	 * is left unreserved once the locomotive that carried it departs (it
	 * owned the whole-train reservation and releases it as it leaves) — the
	 * couple pathfinder's HasReservedTracks gate then never finds the
	 * formation and the locomotive loops "waiting for free track" /
	 * COUPLE-FAIL forever. */
	bool r3r_platform_waiter = R3RIsCarOnlyFormation(consist) ||
			(consist->IsPrimaryVehicle() && consist->current_order.IsType(OT_WAIT_COUPLE));
	if (r3r_platform_waiter && consist->cur_speed == 0 && IsTileType(consist->tile, TileType::Station)) {
		R3RResvPhaseGuard r3r_phase("waiter-platform", R3RResvActorID(consist));
		R3RScopeTimer r3r_plat_timer(&r3r_plat_ns, &r3r_plat_calls);

		/* R3R (KI-206): the consist also still holds the *route* reservation that
		 * brought it onto the platform (or that its schedule pointed at). It starts
		 * at the consist and runs out of the station ahead of it -- that is the
		 * "mysterious reservation out of the platform" the player keeps seeing.
		 * This waiter will never drive it (it is picked up and pulled away, usually
		 * in the other direction), so release it on the first tick the consist
		 * stands still here, while the chain still answers for those bits. Left in
		 * place they survive until a coupling locomotive merges the chains, and at
		 * that instant GetTrainForReservation() can no longer follow them back to
		 * the merged train: they turn ownerless (stray) and nothing in the game can
		 * ever clear them afterwards (the commit-point release R3RReleaseChainReservations
		 * only frees bits that still answer to the chain head). Only the reservation
		 * under the consist and on its platform (installed just below) is wanted.
		 * Do this once per waiting episode: while parked the consist never books a
		 * new route, so one purge is enough and the map-wide scan stays rare. */
		if (_r3r_waiter_purged.insert(consist->index).second) {
			/* KI-211: purge first, drop the lookahead afterwards. The purge now
			 * walks the consist's *own* booked path on the map, while the lookahead
			 * is the engine's cached view of exactly that path; releasing bits while
			 * the cache still describes them keeps the two consistent, whereas
			 * resetting first would leave the cached view ahead of the map for the
			 * duration of the sweep. */
			R3RReleaseChainReservations(consist, "waiter");
			consist->lookahead.reset();
		}

		consist->ReserveTrackUnderConsist();
		const Axis axis = GetRailStationAxis(consist->tile);
		const TileIndexDiff delta = TileOffsByAxis(axis);
		const Track track = AxisToTrack(axis);
		for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t -= delta) {
			TryReserveRailTrack(t, track);
		}
		for (TileIndex t = consist->tile; IsCompatibleTrainStationTile(t, consist->tile); t += delta) {
			TryReserveRailTrack(t, track);
		}
	} else {
		/* No longer a parked waiter: arm the purge again so a future waiting
		 * episode releases whatever route the consist books in the meantime. */
		_r3r_waiter_purged.erase(consist->index);
		_r3r_waiter_nobook_logged.erase(consist->index);
	}

	/* R3R: a locomotive that was ordered to couple *inside this very depot* is
	 * expected to be parked (VehState::Stopped) when it gets here -- the depot
	 * couple block below even clears that flag once the couple ran. The generic
	 * "stopped train bails out" gate must therefore not swallow this case: the
	 * chain gets its Stopped flag from R3R's own depot tools (MakeSegment /
	 * DemoteSegment call R3RStopChainInDepot), and a parked train never runs
	 * ProcessOrders, so it would sit in its target depot forever while the
	 * consist it was ordered to pick up stays stranded next to it (no couple
	 * attempt is ever made -- confirmed on test_multi_company.sav, where the
	 * cross-company couple gate itself answers allowed=1). Because a parked
	 * train keeps current_order at OT_NOTHING, the pending order is read by
	 * index. The exemption is deliberately narrow: it only applies when the
	 * real order is a GOTO_COUPLE whose depot destination IS the depot the
	 * locomotive is standing in, so a locomotive the player stopped anywhere
	 * else keeps its old "give control back" behaviour. KI-118 fix A: the
	 * exemption additionally requires Train::r3r_parked, which only R3R's own
	 * depot tools (R3RStopChainInDepot) set -- that is what distinguishes "R3R
	 * parked this chain" from "the player pressed stop" and from "a freshly
	 * bought train that was never started". */
	const Order *r3r_pend = (consist->orders != nullptr) ? consist->GetOrder(consist->cur_real_order_index) : nullptr;
	const bool r3r_pending_depot_couple = consist->IsEngine() && consist->r3r_parked && r3r_pend != nullptr &&
			r3r_pend->IsType(OT_GOTO_COUPLE) && r3r_pend->GetCoupleIsDepot() &&
			IsRailDepotTile(consist->tile) &&
			r3r_pend->GetDestination().ToDepotID() == GetDepotIndex(consist->tile);

	/* exit if train is stopped (a stopped locomotive should not keep trying to
	 * couple: the player stopped it, so give control back for stop/skip/return
	 * commands instead of parking it in the GOTO_COUPLE state forever). */
	if (!r3r_pending_depot_couple && consist->vehstatus.Test(VehState::Stopped) && consist->cur_speed == 0) {
		/* R3R DEBUG: log a stopped train bailing out here.
		 * KI-14 (1): edge-triggered -- this gate is reached every tick while the
		 * train stays stopped (4902 of the 28838 lines written in 38 s). Since
		 * cur_speed is 0 the payload only changes with the order or the tile. */
		const Order *r = (consist->orders != nullptr) ? consist->GetOrder(consist->cur_real_order_index) : nullptr;
		const uint64_t skip_key = (uint64_t)consist->index.base();
		const uint64_t skip_payload = ((uint64_t)(uint)consist->current_order.GetType() << 40) |
				((uint64_t)(uint)((r != nullptr) ? ((uint)r->GetType() + 1) : 0) << 32) |
				((uint64_t)(uint)consist->r3r_parked << 31) |
				(uint64_t)consist->tile.base();
		if (R3RDbgEdge(R3REDGE_SKIPSTOPPED, skip_key, skip_payload)) {
			/* R3R (item 3, "神秘跳命令"): this gate is by design -- a Stopped consist
			 * with no in-depot couple pending must not process orders -- but that
			 * also makes it the prime suspect whenever orders look like they are
			 * silently skipped. Log every input that decides whether the train
			 * *should* be stopped, so a field report can be traced to a cause
			 * instead of guessed at: r3r_parked (a depot tool parked it), front
			 * (a non-front chain can never leave), nord/index (no schedule, or an
			 * index that no longer resolves), tt (timetable index). */
			R3RDbgWrite("SKIP-STOPPED veh=%d order=%d real=%d spd=%d tile=%d,%d parked=%d front=%d nord=%d idx=%d tt=%d\n",
					(int)consist->index.base(), (int)consist->current_order.GetType(),
					(int)(r != nullptr ? r->GetType() : -1),
					(int)consist->cur_speed, (int)TileX(consist->tile), (int)TileY(consist->tile),
					(int)consist->r3r_parked, (int)consist->IsFrontEngine(),
					(int)(consist->orders != nullptr ? consist->GetNumOrders() : -1),
					(int)consist->cur_real_order_index, (int)consist->cur_timetable_order_index);
		}
		return true;
	}

	/* R3R (KI-182): from the moment the "go to and couple" order starts running
	 * the locomotive locks onto exactly one waiting consist, and both sides mark
	 * that lock ("已有耦合目标"); the couple pathfinder then only ever resolves
	 * that one consist. */
	R3REnsureCouplePair(consist);

	/* If a locomotive is about to leave the depot, automatically couple onto any
	 * wagon chain (free wagon or independent consist) that waits inside the depot.
	 * When executing a GOTO_COUPLE order, couple onto the depot's waiting consist directly. */
	if (consist->track == TRACK_BIT_DEPOT && consist->IsEngine()) {
		/* R3R: the depot GOTO_COUPLE block below must only apply when the couple
		 * order's target IS this depot. A locomotive that merely sits in its HOME
		 * depot (e.g. after a GOTO_DEPOT stop) while holding a GOTO_COUPLE order
		 * for a station platform or ANOTHER depot must keep driving out and couple
		 * there. The previous unconditional park (cur_speed = 0 every tick) locked
		 * such a locomotive inside its HOME depot forever: COUPLE-FAIL retried each
		 * tick because the depot-internal couple scan only checks the depot tile and
		 * the tile in front of the door, while the waiting consist sat elsewhere.
		 * The destination check mirrors CheckTrainStayInDepot (IsRailDepotTile +
		 * GetDepotIndex). */
		const bool couple_targets_this_depot = r3r_pending_depot_couple ||
				(consist->current_order.IsType(OT_GOTO_COUPLE) &&
				consist->current_order.GetCoupleIsDepot() &&
				IsRailDepotTile(consist->tile) &&
				consist->current_order.GetDestination().ToDepotID() == GetDepotIndex(consist->tile));
		if (couple_targets_this_depot) {
			/* R3R: a locomotive that was parked in its target depot (R3R's own
			 * depot tools call R3RStopChainInDepot) never ran ProcessOrders, so it
			 * still carries current_order == OT_NOTHING and the Stopped flag.
			 * Materialise the state a running locomotive would have before trying
			 * the couple: otherwise R3RCanCoupleNow() rejects the one legitimate
			 * candidate with "active-not-goto" / "active-stopped" and the parked
			 * locomotive can never pick up the consist it was ordered to collect. */
			if (r3r_pending_depot_couple) {
				if (consist->vehstatus.Test(VehState::Stopped)) {
					consist->StopSeparation();
					consist->cur_timetable_order_index = INVALID_VEH_ORDER_ID;
				}
				if (!consist->current_order.IsType(OT_GOTO_COUPLE)) consist->current_order = *r3r_pend;
				/* R3R (KI-182): the order is only materialised now, so the lock
				 * has to be (re)established here -- the call earlier in this
				 * handler still saw OT_NOTHING and therefore dropped it. */
				R3REnsureCouplePair(consist);
			}
			/* R3R: VehState::Stopped must be off while the couple gate runs (it
			 * rejects a stopped coupler as "active-stopped"), but the very same flag
			 * is the player's own Start/Stop state. Clearing it for good made a
			 * stopped train report itself as running again, and because the couple
			 * block is re-entered every tick while the consist is still missing, the
			 * flag was wiped again and again -- the train then looked like it had
			 * started by itself. Clear it only for the duration of the attempt and
			 * restore it when nothing was coupled (KI-110). */
			const bool r3r_was_stopped = consist->vehstatus.Test(VehState::Stopped);
			consist->vehstatus.Reset(VehState::Stopped);
			const bool r3r_coupled = TrainCoupleHandler(consist);
			if (r3r_coupled) {
				consist->cur_speed = 0;
				consist->progress = 0;
				/* R3R KI-118 fix A: the couple this chain was parked for succeeded,
				 * so it is no longer "parked by an R3R depot tool". */
				consist->r3r_parked = false;
			}
			/* R3R: a GOTO_COUPLE locomotive that arrived at its target depot stays
			 * parked inside it. On success the next ProcessOrders tick advances into
			 * the consist's schedule; on failure it simply waits here and retries
			 * coupling on the next tick instead of driving out again. */
			consist->cur_speed = 0;
			consist->progress = 0;
			if (!r3r_coupled && r3r_was_stopped && consist->IsFrontEngine()) consist->vehstatus.Set(VehState::Stopped);
			consist->flags.Reset(VehicleRailFlag::LeavingStation);

			/* R3R: the depot GOTO_COUPLE coupling above may have handed the train
			 * identity over to a new head (TryTrainCouple head != v relocation after a
			 * logical flip of a multi-engine locomotive chain). `consist` is then a
			 * mid-chain engine, not the front engine any more, and must not run the
			 * rest of this front-engine handler: its cached_weight was cleared by
			 * ConsistChanged(CCF_LENGTH) and the UpdateSpeed below would divide by
			 * zero in GroundVehicle::GetAcceleration (crash C0000094). Bail out; the
			 * new head takes over from the next tick (TryTrainCouple already
			 * invalidated the vehicle tick cache). */
			if (!consist->IsFrontEngine()) return true;
		} else if (!consist->current_order.IsType(OT_GOTO_COUPLE)) {
			NormalizeTrainVehInDepot(consist, true);
		}
	}

	/* R3R: DECOUPLE triggers when the train is stopped AND has genuinely arrived
	 * at the destination of its CURRENT travel order (a station or a depot) and
	 * its NEXT order (cur_implicit_order_index+1) is DECOUPLE — matching the
	 * design in DecoupleTrain/GetDecoupleVehicle, which read the decouple order
	 * from implicit+1. Requiring the current order to actually match the tile
	 * the train is parked on prevents an instant decouple on the tick right
	 * after a locomotive couples onto a consist: the combined train then
	 * inherits the consist's schedule, so its current order is the NEXT travel
	 * order (e.g. "go to depot") and the coupling station/depot tile does not
	 * match that order — the train must first travel to the decouple
	 * destination before it decouples there. */
	if (consist->cur_speed == 0) {
		R3RScopeTimer r3r_dec_timer(&r3r_dec_ns, &r3r_dec_calls);
		/* R3R: DECOUPLE triggers only when the train is genuinely parked at the
		 * destination of its CURRENT REAL travel order. Read the real order by
		 * INDEX (cur_real_order_index) rather than current_order, because
		 * current_order is overwritten to OT_LOADING while the train dwells at a
		 * station — using it would never match OT_GOTO_STATION and the decouple
		 * would never fire. Reading the real order by index keeps the travel
		 * order (e.g. ② "go to East station") visible during the dwell.
		 *
		 * This also distinguishes a genuine arrival (round 1: real order is
		 * ② OT_GOTO_STATION and the tile is a station → fire) from a coupling
		 * stop (round 2: after coupling at East station the combined train's
		 * real order is ⑤ OT_GOTO_DEPOT, which does NOT match a station tile →
		 * must first travel to the depot before it may decouple there). */
		/* R3R DEBUG (stable): universal arrival-state dump for EVERY stopped train,
		 * not just OT_GOTO_DEPOT. This captures the moment a train sits ON a depot
		 * tile even when its real order has already advanced to DECOUPLE (the old
		 * DEPOT-only dump missed that case), and records the depot tile coordinates
		 * + entry direction + locomotive heading so we can see why a train parks at
		 * the depot door (7,36) and never reaches the depot tile itself.
		 * SAFETY: every Railway-only accessor (HasDepotReservation /
		 * GetRailDepotDirection) is guarded by IsRailDepotTile(dbg_dest) first, so
		 * a station destination tile can never trip rail_map.h:39's assertion. */
		{
			/* KI-14 (2): gate FIRST. Compute the state signature (a handful of
			 * integer ops on consist fields) and only open the log when the state
			 * actually changed. Previously the fopen ran unconditionally, so every
			 * parked train paid a syscall per tick and emitted a full ~16-line
			 * CHAIN snapshot; this was the largest remaining log source after the
			 * other edge gates (KI-18). The signature is unchanged from before. */
			uint64_t dbg_sig = ((uint64_t)consist->tile.base() << 32) ^
				((uint64_t)consist->cur_real_order_index * 0x9E3779B97F4A7C15ull) ^
				((uint64_t)(consist->GetOrder(consist->cur_real_order_index) ? consist->GetOrder(consist->cur_real_order_index)->GetType() + 1 : 0) * 0xBF58476D1CE4E5B9ull) ^
				((uint64_t)(consist->current_order.GetType() + 1) * 0x94D049BB133111EBull) ^
				((uint64_t)consist->flags.Test(VehicleRailFlag::Stuck) * 0xDEADBEEFCAFEBABEull);
			/* Per-vehicle last-signature store. With a single shared slot the
			 * record gets clobbered when two parked vehicles alternate every
			 * tick (seen in the 19:29 run: veh 0 and veh 3 both idling in
			 * depot 42,28), so every tick kept logging. The dump is edge-triggered:
			 * emit only when the stopped state differs from the previous tick. */
			static std::vector<uint64_t> s_dbg_dump_sigs;
			const uint32_t dbg_idx = consist->index.base();
			if (s_dbg_dump_sigs.size() <= dbg_idx) s_dbg_dump_sigs.resize(dbg_idx + 1, 0);
			if (s_dbg_dump_sigs[dbg_idx] != dbg_sig) {
				s_dbg_dump_sigs[dbg_idx] = dbg_sig;

				FILE *dbg = R3RFopenDbg("a");
				if (dbg != nullptr) {
					bool tile_is_depot = IsRailDepotTile(consist->tile);
					TileIndex dbg_dest = consist->dest_tile;
					bool dbg_destValid = dbg_dest != INVALID_TILE;
					bool dbg_destDepot = dbg_destValid && IsRailDepotTile(dbg_dest);
					bool dbg_destResv = dbg_destDepot && HasDepotReservation(dbg_dest);
					bool dbg_tileEqDest = dbg_destValid && (consist->tile == dbg_dest);
					int dbg_destTx = dbg_destValid ? (int)TileX(dbg_dest) : -1;
					int dbg_destTy = dbg_destValid ? (int)TileY(dbg_dest) : -1;
					int dbg_depotDir = dbg_destDepot ? (int)GetRailDepotDirection(dbg_dest) : -1;
					/* R3R (KI-108): guard this probe as well - TrackdirToExitdir()
					 * asserts on INVALID_TRACKDIR (track_func.h:396) and this line was
					 * the actual crash site of 2026-09-18 21:05 (a chain end parked on
					 * TRACK_BIT_Y while facing a direction not along that track). Log -1. */
					const Trackdir dbg_td = consist->GetVehicleTrackdir();
					int dbg_enterDir = (dbg_td == INVALID_TRACKDIR) ? -1 : (int)TrackdirToExitdir(dbg_td);

					fprintf(dbg, "DEPOT-ARR veh=%d spd=%d real=%d(%d) curType=%d stuck=%d tileDepot=%d tx=%d ty=%d destTx=%d destTy=%d destDepot=%d destResv=%d tileEqDest=%d depotDir=%d enterDir=%d tt=%d impl=%d\n",
						(int)consist->index.base(), (int)consist->cur_speed,
						(int)consist->cur_real_order_index, (int)(consist->GetOrder(consist->cur_real_order_index) ? consist->GetOrder(consist->cur_real_order_index)->GetType() : -1),
						(int)consist->current_order.GetType(),
						(int)consist->flags.Test(VehicleRailFlag::Stuck),
						(int)tile_is_depot, (int)TileX(consist->tile), (int)TileY(consist->tile),
						dbg_destTx, dbg_destTy, (int)dbg_destDepot, (int)dbg_destResv, (int)dbg_tileEqDest,
						dbg_depotDir, dbg_enterDir,
						/* R3R: the timetable index is what UpdateVehicleTimetable asserts on.
						 * Logging it here shows whether the desync (TT-DESYNC) already
						 * existed when the depot arrival was handled, i.e. which R3R block
						 * rewrote the order indices behind each other's back. */
						(int)consist->cur_timetable_order_index, (int)consist->cur_implicit_order_index);
					fclose(dbg);
				}
			}
		}
		const Order *cur_real = consist->GetOrder(consist->cur_real_order_index);
		bool at_order_dest = false;
		if (cur_real != nullptr && cur_real->IsType(OT_GOTO_STATION)) {
			/* R3R: must be parked at THIS order's station, not merely on some
			 * station tile. When a locomotive couples onto a consist that is
			 * waiting at a station (WAIT_COUPLE), the merged train inherits the
			 * consist's order list and skips that WAIT_COUPLE (see Couple()),
			 * so its first travel order is the delivery order that FOLLOWS the
			 * waiting point. If that order says "go to station B" while the
			 * consist is parked at station A, a bare IsTileType() test counted
			 * A's platform as the destination, the DECOUPLE right after it fired
			 * on the next tick and undid the couple at the very same tile
			 * (observed 2026-09-22: COUPLE-OK loco=7 real=4 at 60,51 = station 0,
			 * immediately followed by DECOUPLE-FIRE consist=7 real=4 whose order
			 * was GOTO_STATION -> station 2; the consist was then stranded at
			 * station 0 and every later couple was undone the same way). Compare
			 * station indices, mirroring the GOTO_DEPOT branch below. */
			at_order_dest = IsTileType(consist->tile, TileType::Station) &&
				cur_real->GetDestination().ToStationID() == GetStationIndex(consist->tile);
		} else if (cur_real != nullptr && cur_real->IsType(OT_GOTO_DEPOT)) {
			/* R3R: must be parked at THIS order's target depot. A train that just
			 * finished the previous GOTO_DEPOT (e.g. 42,28) has already advanced
			 * its real order to the next GOTO_DEPOT (e.g. 38,27) while still
			 * sitting inside the old depot, not yet rolled out; a bare
			 * IsRailDepotTile() check would then count the old depot tile as
			 * "arrived at the new order" and decouple at the wrong place
			 * (observed 2026-09-04: DECOUPLE-FIRE fired at 42,28 although the
			 * real order was GOTO_DEPOT -> 38,27). Compare depot indices
			 * instead. */
			at_order_dest = IsRailDepotTile(consist->tile) &&
				cur_real->GetDestination().ToDepotID() == GetDepotIndex(consist->tile);
		} else if (cur_real != nullptr && cur_real->IsType(OT_DECOUPLE)) {
			/* R3R: entering a depot advances the real order from GOTO_DEPOT to
			 * DECOUPLE (log evidence: real=5(15) with tileDepot=1 at tx=6 ty=30).
			 * When the train is already on the depot tile under a DECOUPLE order,
			 * treat that as "at destination" too — otherwise the gate stays false
			 * and the depot decouple never fires. Station decouples keep
			 * GOTO_STATION during dwell, so they already hit the first branch. */
			at_order_dest = IsRailDepotTile(consist->tile);
		}
		if (at_order_dest) {
			/* R3R: locate the NEXT REAL order by real index (wrapped), not by
			 * cur_implicit_order_index+1. The implicit index drifts away from the
			 * real index once a travel order expands into implicit sub-steps
			 * (e.g. entering/occupying a depot), so implicit+1 no longer points
			 * at the real DECOUPLE order when that order sits at the end of the
			 * list. Reading by real index keeps arrival and "next order" aligned. */
			bool should_decouple = false;
			if (cur_real != nullptr && cur_real->IsType(OT_DECOUPLE)) {
				/* Arrived in depot and the real order already advanced to
				 * DECOUPLE — decouple directly, no "next order" check needed. */
				should_decouple = true;
			} else {
				VehicleOrderID next_real = consist->cur_real_order_index + 1;
				if (consist->GetNumOrders() > 0 && next_real >= consist->GetNumOrders()) next_real = 0;
				const Order *next_order = consist->GetOrder(next_real);
				should_decouple = (next_order != nullptr && next_order->IsType(OT_DECOUPLE));
			}
			if (should_decouple) {
				/* R3R (2026-09-23): a DECOUPLE order releases a part that is
				 * coupled on. A chain that carries no coupled-on segment (no ★
				 * segment head behind ours) and whose own body is not a
				 * locomotive + wagons consist either has nothing at all to
				 * release: the only thing the native fallback could still cut off
				 * is the traction unit itself (a lone multi-unit locomotive
				 * would be split in two by its own DECOUPLE order) or the
				 * formation's own wagons (a car-only formation without a ★).
				 * Refusing that cut but keeping the order would fire this gate
				 * again on every tick for as long as the train stands there, so
				 * treat the order as done instead: step past it (the helper also
				 * steps over the arrival order of the pair we are standing on, so
				 * the train cannot end up parked on the DECOUPLE either) and let
				 * it continue with the next order. */
				const bool nothing_to_release =
						GetSegmentHeadFromRear(consist, 1) == nullptr &&
						!(consist->IsEngine() && !R3RIsCarOnlyFormation(consist) && R3RHasWagonBehindEngine(consist));
				if (nothing_to_release) {
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "DECOUPLE-SKIP consist=%d tx=%d ty=%d real=%d segs=%u (nothing to release)\n",
							(int)consist->index.base(), (int)TileX(consist->tile), (int)TileY(consist->tile),
							(int)consist->cur_real_order_index, (uint)(R3RHasWagonBehindEngine(consist) ? 1 : 0));
						fclose(dbg);
					}
					uint skipped = R3RSkipUnfireableDecoupleOrder(consist);
					if (skipped == 0) {
						/* Safety net: the gate fired on a shape the helper did not
						 * recognise (an exotic arrival order). Never leave the index
						 * where it is -- that would let the gate re-fire the same
						 * refused decouple on the next tick. Step off the order and
						 * past any DECOUPLE behind it. */
						consist->IncrementRealOrderIndex();
						for (uint i = 0; i < consist->GetNumOrders(); ++i) {
							const Order *o = consist->GetOrder(consist->cur_real_order_index);
							if (o == nullptr || !o->IsType(OT_DECOUPLE)) break;
							consist->IncrementRealOrderIndex();
						}
					}
					consist->cur_timetable_order_index = consist->cur_real_order_index;
					R3RCheckTtSync(consist, "decouple-skip");
					consist->current_order.Free();
					consist->SetDestTile(INVALID_TILE);
					InvalidateVehicleOrder(consist, 0);
				} else {
				{
					/* R3R (2026-09-23): the probe must carry the requested decouple
					 * boundary AND the effective split point, otherwise the log cannot
					 * tell whether the boundary was honoured or clamped (玩家报告
					 * "只会解挂尾部段" 时无法从日志定位). */
					const Order *dc_order = consist->GetOrder(consist->cur_real_order_index);
					if (dc_order == nullptr || !dc_order->IsType(OT_DECOUPLE)) {
						VehicleOrderID next_real = consist->cur_real_order_index + 1;
						if (consist->GetNumOrders() > 0 && next_real >= consist->GetNumOrders()) next_real = 0;
						dc_order = consist->GetOrder(next_real);
					}
					const bool dc_valid = (dc_order != nullptr && dc_order->IsType(OT_DECOUPLE));
					uint segs = 0;
					for (Train *t = consist->GetNextVehicle(); t != nullptr; t = t->GetNextVehicle()) {
						if (t->IsSegmentFront()) segs++;
					}
					const Train *eff = GetDecoupleVehicle(consist);
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "DECOUPLE-FIRE consist=%d tx=%d ty=%d real=%d mode=%d num=%u segs=%u eff=%d\n",
							(int)consist->index.base(), (int)TileX(consist->tile), (int)TileY(consist->tile),
							(int)consist->cur_real_order_index,
							dc_valid ? (int)dc_order->GetDecoupleBoundaryMode() : -1,
							dc_valid ? (uint)dc_order->GetNumDecouple() : 0,
							segs,
							(eff != nullptr) ? (int)eff->index.base() : -1);
						fclose(dbg);
					}
				}
				DecoupleTrain(consist, true);
				{
					FILE *dbg = R3RFopenDbg("a");
					if (dbg != nullptr) {
						fprintf(dbg, "LOCO-AFTER-DECOUPLE veh=%d curType=%d real=%d tile=%d,%d\n",
							(int)consist->index.base(), (int)consist->current_order.GetType(),
							(int)consist->cur_real_order_index, (int)TileX(consist->tile), (int)TileY(consist->tile));
						if (consist->orders != nullptr) {
							for (VehicleOrderID i = 0; i < consist->GetNumOrders(); ++i) {
								const Order *o = consist->GetOrder(i);
								fprintf(dbg, "  L-ORD %d type=%d dest=%d", (int)i, (int)(o ? o->GetType() : -1), (int)(o ? o->GetDestination().base() : -1));
							if (o != nullptr && o->IsType(OT_CONDITIONAL)) {
								fprintf(dbg, " condVar=%d condCmp=%d condVal=%d skipTo=%d", (int)o->GetConditionVariable(), (int)o->GetConditionComparator(), (int)o->GetConditionValue(), (int)o->GetConditionSkipToOrder());
							}
							fprintf(dbg, "\n");
							}
						}
						fclose(dbg);
					}
				}
				}
			}
		}
	}

	bool valid_order = !consist->current_order.IsType(OT_NOTHING) && consist->current_order.GetType() != OT_CONDITIONAL && !consist->current_order.IsSlotCounterOrder() && !consist->current_order.IsType(OT_LABEL);
	/* R3R: ProcessOrders() calls UpdateVehicleTimetable() as soon as the train
	 * arrives at / leaves an order. That function asserts that the timetable index
	 * points at the same order as the real index; a stale index took the game down
	 * (timetable_cmd.cpp "real_timetable_order == real_current_order"). Trap the
	 * state directly in front of the call so R3R_debug.log names the tick on which
	 * the desync became visible, next to the DEPOT-ARR dump above. */
	R3RCheckTtSync(consist, "pre-processorders");
	bool r3r_may_reverse;
	{
		R3RScopeTimer r3r_ord_timer(&r3r_ord_ns, &r3r_ord_calls);
		/* R3R (KI-149): a decouple leaves the released part standing seam to
		 * seam behind us, inside the same signal block. Turning around here
		 * would drive us straight into it, so treat that direction as blocked
		 * road and let the train leave through the other end of the station
		 * instead of reversing in place. */
		r3r_may_reverse = ProcessOrders(consist) && CheckReverseTrain(consist) &&
				R3RWaitingCoupleSeam(consist) != R3RSeamEnd::Back;
	}
	if (r3r_may_reverse) {
		consist->wait_counter = 0;
		consist->cur_speed = 0;
		consist->subspeed = 0;
		consist->flags.Reset(VehicleRailFlag::LeavingStation);
		ReverseTrainDirection(consist);
		return true;
	} else if (consist->flags.Test(VehicleRailFlag::LeavingStation) &&
			/* R3R (KI-149): mirror of the reversal veto above -- a waiting part
			 * touching our nose blocks the road forwards. Never reserve a path
			 * that would drive us into it; the train may still reverse away
			 * from the seam and leave through the other end. */
			R3RWaitingCoupleSeam(consist) != R3RSeamEnd::Front) {
		/* Try to reserve a path when leaving the station as we
		 * might not be marked as wanting a reservation, e.g.
		 * when an overlength train gets turned around in a station. */
		const Train *moving_front = consist->GetMovingFront();
		DiagDirection dir = VehicleExitDir(moving_front->GetMovingDirection(), moving_front->track);
		if (IsRailDepotTile(moving_front->tile) || IsTileType(moving_front->tile, TileType::TunnelBridge)) dir = DiagDirection::Invalid;

		{
			R3RScopeTimer r3r_prv_timer(&r3r_prv_ns, &r3r_prv_calls);
			if (UpdateSignalsOnSegment(moving_front->tile, dir, consist->owner) == SigSegState::Path || _settings_game.pf.reserve_paths) {
				TryPathReserve(consist, true, true);
			}
		}
		consist->flags.Reset(VehicleRailFlag::LeavingStation);
	}

	{
		R3RScopeTimer r3r_load_timer(&r3r_load_ns, &r3r_load_calls);
		consist->HandleLoading(mode);
	}

	if (consist->current_order.IsType(OT_LOADING)) return true;

	/* R3R: a WAIT_COUPLE order parks the train in place ANYWHERE (station platform
	 * as well as depot). The CheckTrainStayInDepot-based check only covers depots;
	 * without this a consist waiting on a platform would keep creeping along. */
	if (consist->current_order.IsType(OT_WAIT_COUPLE)) return true;

	/* R3R (KI-136 / P7): a chain without real traction must not move on its own --
	 * whatever order it happens to hold, not just WAIT_COUPLE.
	 *
	 * R3R promotes a wagon-only chain into a zero-power train front so that the
	 * ordinary subsystems treat it as a train (R3RCreateCarOnlyFormation: "it just
	 * waits in a depot/station until a locomotive couples onto it"). Zero power
	 * does NOT keep it stationary by itself: Train::UpdateAcceleration() is
	 * `Clamp(power / weight * 4, 1, 255)`, i.e. every train keeps at least the
	 * "1 hp minimum", so a formation that holds an ordinary GOTO order creeps out
	 * of the depot (or along a platform) under its own steam. CheckTrainStayInDepot()
	 * cannot catch that either: its `cached_power == 0` auto-stop deliberately
	 * exempts car-only formations, because being flagged VehState::Stopped would
	 * make them uncouplable forever (R3RCanCoupleNow rejects "target-stopped").
	 *
	 * Parking it here is exactly the WAIT_COUPLE park above -- same place, same
	 * "return true without setting VehState::Stopped" -- so everything that must
	 * still run has already run: the orders/path reservation above, station
	 * loading, the timetable. Whatever legitimately moves such a chain still works:
	 * a locomotive either couples onto it (the merged chain's front is that
	 * locomotive, so this test stops matching) or pulls it out of the depot by
	 * re-linking the chain (NormalizeTrainVehInDepot).
	 *
	 * `cached_power == 0` pins the condition to "no real locomotive anywhere in
	 * this chain": a chain whose head happens to be a wagon but which does carry
	 * real traction must still be able to drive. */
	if (R3RIsCarOnlyFormation(consist) && consist->gcache.cached_power == 0) {
		/* Only worth a log line when the formation was really rolling -- i.e. the
		 * "it drove itself" symptom in the field; a parked formation would
		 * otherwise write once per tick. */
		if (consist->cur_speed != 0) {
			R3RDbgWrite("CARONLY-PARK head=%d ord=%d tile=%d,%d spd=%d\n",
					(int)consist->index.base(), (int)consist->current_order.GetType(),
					TileX(consist->tile), TileY(consist->tile), (int)consist->cur_speed);
		}
		consist->cur_speed = 0;
		consist->subspeed = 0;
		return true;
	}

	{
		R3RScopeTimer r3r_dep_timer(&r3r_dep_ns, &r3r_dep_calls);
		if (CheckTrainStayInDepot(consist)) return true;
	}

	if (consist->current_order.IsType(OT_WAITING) && consist->reverse_distance == 0) {
		if (mode) return true;
		consist->HandleWaiting(false, true);
		if (consist->current_order.IsType(OT_WAITING)) return true;
		Train *moving_front = consist->GetMovingFront();
		if (IsRailWaypointTile(moving_front->tile)) {
			StationID station_id = GetStationIndex(moving_front->tile);
			if (consist->current_order.ShouldStopAtStation(consist, station_id, true)) {
				UpdateVehicleTimetable(consist, true);
				consist->last_station_visited = station_id;
				SetWindowDirty(WindowClass::VehicleView, consist->index);
				consist->current_order.MakeWaiting();
				consist->current_order.SetNonStopType(ONSF_NO_STOP_AT_ANY_STATION);
				return true;
			}
		}
	}

	/* We had no order but have an order now, do look ahead. */
	if (!valid_order && !consist->current_order.IsType(OT_NOTHING)) {
		CheckNextTrainTile(consist->GetMovingFront());
	}

	/* R3R (KI-199): a consist that has just been coupled owes itself one real path
	 * reservation. CheckNextTrainTile() above only *extends* an existing reservation,
	 * and both halves carried their own reservation to the coupling point, neither of
	 * which matches the merged front any more -- so without this the merged consist
	 * stood there with no reservation at all and never moved (player report
	 * 2026-09-24: "coupling succeeds and nothing is ever reserved").
	 *
	 * mark_as_stuck is TRUE (player decision 2026-09-25, round 114). A merged consist
	 * that cannot reach the next safe stopping position must behave exactly like a
	 * train standing in front of a PBS signal: mark itself as waiting for free track
	 * (VehicleRailFlag::Stuck, retried every path_backoff_interval and reversed after
	 * wait_for_pbs_path when reverse_at_signals is on -- the "Handle stuck trains"
	 * block right below), not sit there silently as if it were left in the
	 * force-proceed-through-signal state. This also restores what KI-179 always
	 * documented ("kept while no reservation can be made -- the train then takes the
	 * ordinary waiting-for-free-track punishment"); round 113's mark_as_stuck=false was
	 * the wrong call.
	 *
	 * first_tile_okay is FALSE for the same reason: "the tile I am standing on is
	 * already a safe position" would consume the one-shot ability without laying any
	 * reservation, which is precisely the state this hook exists to leave behind.
	 *
	 * The request stays queued while current_order is still empty or the consist is
	 * still loading: a loading train keeps its own schedule, leaves on its own and
	 * reserves then (marking it stuck meanwhile could reverse a train that is merely
	 * loading). */
	if (_r3r_couple_autoreserve.erase(consist->index) != 0) {
		if (consist->current_order.IsType(OT_NOTHING) || consist->current_order.IsAnyLoadingType()) {
			_r3r_couple_autoreserve.insert(consist->index);
		} else {
			const TryPathReserveResultFlags r3r_auto_res = TryPathReserveWithResultFlags(consist, true, false);
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "TRP-AUTO veh=%d real=%d type=%d dest=%u ok=%d res=%d stuck=%d wc=%u\n",
						(int)consist->index.base(), (int)consist->cur_real_order_index,
						(int)consist->current_order.GetType(), (unsigned)consist->dest_tile.base(),
						(r3r_auto_res & TPRRF_RESERVATION_OK) ? 1 : 0, (int)(uint8_t)r3r_auto_res,
						(int)consist->flags.Test(VehicleRailFlag::Stuck), (unsigned)consist->wait_counter);
				fclose(dbg);
			}
		}
	}

	/* Handle stuck trains. */
	if (!mode && consist->flags.Test(VehicleRailFlag::Stuck)) {
		R3RScopeTimer r3r_stk_timer(&r3r_stk_ns, &r3r_stk_calls);
		++consist->wait_counter;

		/* Should we try reversing this tick if still stuck? */
		bool turn_around = consist->wait_counter % (_settings_game.pf.wait_for_pbs_path * DAY_TICKS) == 0 && _settings_game.pf.reverse_at_signals;

		if (!turn_around && consist->wait_counter % _settings_game.pf.path_backoff_interval != 0 && consist->force_proceed == TFP_NONE) return true;
		TryPathReserveResultFlags path_result = TryPathReserveWithResultFlags(consist);
		if ((path_result & TPRRF_RESERVATION_OK) == 0) {
			/* Still stuck. */
			if (turn_around || (path_result & TPRRF_REVERSE_AT_SIGNAL)) ReverseTrainDirection(consist);

			if (consist->flags.Test(VehicleRailFlag::Stuck) && consist->wait_counter > 2 * _settings_game.pf.wait_for_pbs_path * DAY_TICKS) {
				/* Show message to player. */
				if (consist->owner == _local_company && (consist->flags.Test(VehicleRailFlag::WaitingRestriction) ? _settings_client.gui.restriction_wait_vehicle_warn : _settings_client.gui.lost_vehicle_warn)) {
					AddVehicleAdviceNewsItem(AdviceType::TrainStuck, GetEncodedString(STR_NEWS_TRAIN_IS_STUCK, consist->index), consist->index);
				}
				consist->wait_counter = 0;
			}
			/* Exit if force proceed not pressed, else reset stuck flag anyway. */
			if (consist->force_proceed == TFP_NONE) return true;
			consist->flags.Reset(VehicleRailFlag::Stuck);
			consist->wait_counter = 0;
			SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		}
	}

	if (consist->current_order.IsType(OT_LEAVESTATION)) {
		StationID station_id = consist->current_order.GetDestination().ToStationID();
		consist->current_order.Free();

		bool may_reverse;
		{
			R3RScopeTimer r3r_ord_timer(&r3r_ord_ns, &r3r_ord_calls);
			may_reverse = ProcessOrders(consist);
		}

		Train *moving_front = consist->GetMovingFront();
		if (IsRailStationTile(moving_front->tile) && GetStationIndex(moving_front->tile) == station_id && Company::Get(consist->owner)->settings.remain_if_next_order_same_station) {
			if (consist->current_order.IsType(OT_GOTO_STATION) && consist->current_order.GetDestination() == station_id &&
					!(consist->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION)) {
				consist->last_station_visited = station_id;
				consist->BeginLoading();
				return true;
			}
		}

		consist->PlayLeaveStationSound();

		if (may_reverse && CheckReverseTrain(consist)) {
			consist->wait_counter = 0;
			consist->cur_speed = 0;
			consist->subspeed = 0;
			consist->flags.Reset(VehicleRailFlag::LeavingStation);
			ReverseTrainDirection(consist);
		}

		SetWindowWidgetDirty(WindowClass::VehicleView, consist->index, WID_VV_START_STOP);
		return true;
	}

	int j;
	{
		/* KI-14 (2): both of these walk the whole consist every tick. */
		R3RScopeTimer r3r_spd_timer(&r3r_spd_ns, &r3r_spd_calls);
		Train::MaxSpeedInfo max_speed_info = consist->GetCurrentMaxSpeedInfoAndUpdate();

		if (!mode) consist->ShowVisualEffect(std::min(max_speed_info.strict_max_speed, max_speed_info.advisory_max_speed));
		j = consist->UpdateSpeed(max_speed_info);
	}

	/* we need to invalidate the widget if we are stopping from 'Stopping 0 km/h' to 'Stopped' */
	if (consist->cur_speed == 0 && consist->vehstatus.Test(VehState::Stopped)) {
		/* If we manually stopped, we're not force-proceeding anymore. */
		consist->force_proceed = TFP_NONE;
		InvalidateWindowData(WindowClass::VehicleView, consist->index);
	}

	Train *moving_front = consist->GetMovingFront();
	int adv_spd = moving_front->GetAdvanceDistance();
	if (j < adv_spd) {
		/* if the vehicle has speed 0, update the last_speed field. */
		if (consist->cur_speed == 0) {
			consist->SetLastSpeed();
			/* R3R: a GOTO_COUPLE locomotive that stopped overlapping the waiting
			 * consist must still couple. The normal collision check only runs
			 * inside the movement loop, which a stopped train never enters
			 * (j < adv_spd) — so a stationary locomotive touching the consist
			 * would never couple. Run the check here for stopped couplers.
			 * CheckTrainCollision returns >0 only on a real crash; a successful
			 * couple returns 0 (and couples the chains). */
			if (consist->current_order.IsType(OT_GOTO_COUPLE)) {
				/* R3R DEBUG: reached the stopped-coupler check.
				 * KI-14 (2): edge-triggered -- this runs for every stationary
				 * GOTO_COUPLE loco on every tick, so the raw fopen here was a hot
				 * syscall with no diagnostic value once the state was known. */
				const uint64_t s0_key = R3RDbgTagHash("CPL-S0-CHK") | ((uint64_t)consist->index.base() << 32);
				if (R3RDbgEdge(R3REDGE_CPLS0, s0_key, (uint64_t)consist->tile.base())) {
					R3RDbgWrite("CPL-S0-CHK veh=%d tile=%d,%d\n",
							(int)consist->index.base(), (int)TileX(consist->tile), (int)TileY(consist->tile));
				}
				if (CheckTrainCollision(moving_front) || TrainCoupleHandler(consist)) return true;
			}
		}
	} else {
		TrainCheckIfLineEnds(moving_front);
		moving_front = moving_front->GetMovingFront();
		/* Loop until the train has finished moving. */
		for (;;) {
			j -= adv_spd;
			TrainController(moving_front, nullptr);
			moving_front = moving_front->GetMovingFront();
			/* Couple now? Try to couple onto the waiting consist when executing a GOTO_COUPLE order. */
			if (consist->current_order.IsType(OT_GOTO_COUPLE)) {
				if (TrainCoupleHandler(consist)) {
					consist->cur_speed = 0;
					consist->progress = 0;
					break;
				}
			}
			/* Don't continue to move if the train crashed. */
			if (CheckTrainCollision(moving_front)) break;
			/* Determine distance to next map position */
			adv_spd = moving_front->GetAdvanceDistance();

			/* No more moving this tick */
			if (j < adv_spd || consist->cur_speed == 0) break;

			OrderType order_type = consist->current_order.GetType();
			/* Do not skip waypoints (incl. 'via' stations) when passing through at full speed. */
			if ((order_type == OT_GOTO_WAYPOINT || order_type == OT_GOTO_STATION) &&
						(consist->current_order.GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) &&
						IsTileType(moving_front->tile, TileType::Station) &&
						consist->current_order.GetDestination() == GetStationIndex(moving_front->tile)) {
				{
					R3RScopeTimer r3r_ord_timer(&r3r_ord_ns, &r3r_ord_calls);
					ProcessOrders(consist);
				}
			}
		}
		consist->SetLastSpeed();
	}

	{
		/* KI-14 (2): O(chain length) per tick per front engine -- this is the
		 * pass that an R3R-merged mega-chain amplifies. */
		R3RScopeTimer r3r_vp_timer(&r3r_vp_ns, &r3r_vp_calls);
		for (Train *u = consist; u != nullptr; u = u->Next()) {
			if (!(u->IsDrawn())) continue;

			u->UpdateViewport(false, false);
		}
	}

	if (consist->progress == 0) consist->progress = j; // Save unused spd for next time, if TrainController didn't set progress

	return true;
}

/**
 * Get running cost for the train consist.
 * @return Yearly running costs.
 */
Money Train::GetRunningCost() const
{
	Money cost = 0;
	const Train *v = this;

	do {
		const Engine *e = v->GetEngine();
		if (e->VehInfo<RailVehicleInfo>().running_cost_class == Price::Invalid) continue;

		uint cost_factor = GetVehicleProperty(v, PROP_TRAIN_RUNNING_COST_FACTOR, e->VehInfo<RailVehicleInfo>().running_cost);
		if (cost_factor == 0) continue;

		/* Halve running cost for multiheaded parts */
		if (v->IsMultiheaded()) cost_factor /= 2;

		cost += GetPrice(e->VehInfo<RailVehicleInfo>().running_cost_class, cost_factor, e->GetGRF());
	} while ((v = v->GetNextVehicle()) != nullptr);

	if (this->cur_speed == 0) {
		if (this->IsInDepot()) {
			/* running costs if in depot */
			cost = CeilDivT<Money>(cost, _settings_game.difficulty.vehicle_costs_in_depot);
		} else {
			/* running costs if stopped */
			cost = CeilDivT<Money>(cost, _settings_game.difficulty.vehicle_costs_when_stopped);
		}
	}

	return cost;
}

/**
 * Update train vehicle data for a tick.
 * @return True if the vehicle still exists, false if it has ceased to exist (front of consists only).
 */
bool Train::Tick()
{
	DEBUG_UPDATESTATECHECKSUM("Train::Tick: v: {}, x: {}, y: {}, track: {}", this->index, this->x_pos, this->y_pos, this->track);
	UpdateStateChecksum((((uint64_t) this->x_pos) << 32) | (this->y_pos << 16) | this->track);
	if (this->IsFrontEngine()) {
		if (!(this->vehstatus.Test(VehState::Stopped) || this->IsWaitingInDepot()) || this->cur_speed > 0) this->running_ticks++;

		this->current_order_time++;

		if (!TrainLocoHandler(this, false)) return false;

		/* R3R: TryTrainCouple 的换端重排(head != v → R3RRelocateFrontIdentity)
		 * 可能在本次 tick 第一次 handler(mode=false)内把列车身份迁往新链头
		 * (多节真引擎机车逻辑翻 v 的场景),本对象已降为链内普通引擎:它的
		 * 重量缓存被 ConsistChanged(CCF_LENGTH) 清除,若再以旧前端身份跑第二
		 * 次 handler,会在 UpdateSpeed → GetAcceleration 里对 cached_weight == 0
		 * 除零崩溃(C0000094)。跳过本次 tick,由下一 tick(缓存已重建)的新链头
		 * 接管。 */
		if (!this->IsFrontEngine()) return true;

		return TrainLocoHandler(this, true);
	} else if (this->IsFreeWagon() && this->vehstatus.Test(VehState::Crashed)) {
		/* Delete flooded standalone wagon chain */
		if (++this->crash_anim_pos >= 4400) {
			delete this;
			return false;
		}
	}

	return true;
}

/**
 * Check whether a train needs service, and if so, find a depot or service it.
 * @param v %Train to check.
 */
static void CheckIfTrainNeedsService(Train *v)
{
	if (Company::Get(v->owner)->settings.vehicle.servint_trains == 0 || !v->NeedsAutomaticServicing()) return;
	if (v->IsChainInDepot()) {
		VehicleServiceInDepot(v);
		return;
	}

	/* R3R (KI-220): the automatic service order must never hijack a chain that is
	 * in the middle of the R3R couple protocol. MakeGoToDepot() below rewrites
	 * current_order but leaves cur_real_order_index pointing at
	 * WAIT_COUPLE/GOTO_COUPLE, so the chain starts driving toward a depot that may
	 * lie *behind* the chain it was just split from. That is exactly the
	 * 2026-09-27 crash: the decoupled car-only chain showed real=23(WAIT_COUPLE)
	 * with curType=2 GOTO_DEPOT to the same depot (1,11) the split-off locomotive
	 * chain was already heading for, and rear-ended it at the split tile (4,11)
	 * (CRASH ... speed=17 tile=1412, the two chains still sharing that tile).
	 * A waiting/coupling chain is parked by design - leave its orders alone. */
	{
		const Order *const real_order = (v->orders != nullptr && v->cur_real_order_index < v->GetNumOrders()) ? v->GetOrder(v->cur_real_order_index) : nullptr;
		if (v->current_order.IsType(OT_WAIT_COUPLE) || v->current_order.IsType(OT_GOTO_COUPLE) ||
				(real_order != nullptr && (real_order->IsType(OT_WAIT_COUPLE) || real_order->IsType(OT_GOTO_COUPLE)))) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "[R3R] SVC-DEPOT-SKIP veh=%d cur=%d real=%d(%d) spd=%d tile=%d,%d tag=couple-protocol\n",
						(int)v->index.base(), (int)v->current_order.GetType(),
						(int)v->cur_real_order_index, R3RDbgOrderTypeAt(v, v->cur_real_order_index),
						(int)v->cur_speed, (int)TileX(v->tile), (int)TileY(v->tile));
				fclose(dbg);
			}
			return;
		}
	}

	uint max_penalty = _settings_game.pf.yapf.maximum_go_to_depot_penalty;

	FindDepotData tfdd = FindClosestTrainDepot(v, max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) ? 2 : 1));
	/* Only go to the depot if it is not too far out of our way. */
	if (tfdd.best_length == UINT_MAX || tfdd.best_length > max_penalty * (v->current_order.IsType(OT_GOTO_DEPOT) && v->current_order.GetDestination() == GetDepotIndex(tfdd.tile) ? 2 : 1)) {
		if (v->current_order.IsType(OT_GOTO_DEPOT)) {
			/* If we were already heading for a depot but it has
			 * suddenly moved farther away, we continue our normal
			 * schedule? */
			v->current_order.MakeDummy();
			SetWindowWidgetDirty(WindowClass::VehicleView, v->index, WID_VV_START_STOP);
		}
		return;
	}

	DepotID depot = GetDepotIndex(tfdd.tile);

	if (v->current_order.IsType(OT_GOTO_DEPOT) &&
			v->current_order.GetDestination() != depot &&
			!Chance16(3, 16)) {
		return;
	}

	SetBit(v->gv_flags, GVF_SUPPRESS_IMPLICIT_ORDERS);
	v->current_order.MakeGoToDepot(depot, {OrderDepotTypeFlag::Service}, ONSF_NO_STOP_AT_INTERMEDIATE_STATIONS, ODATFB_NEAREST_DEPOT);
	v->dest_tile = tfdd.tile;
	SetWindowWidgetDirty(WindowClass::VehicleView, v->index, WID_VV_START_STOP);

	for (Train *u = v; u != nullptr; u = u->Next()) {
		u->flags.Reset(VehicleRailFlag::BeyondPlatformEnd);
	}
}

/** Update day counters of the train vehicle. */
void Train::OnNewDay()
{
	if (!EconTime::UsingWallclockUnits()) AgeVehicle(this);
	EconomyAgeVehicle(this);

	if ((++this->day_counter & 7) == 0) DecreaseVehicleValue(this);
}

void Train::OnPeriodic()
{
	if (this->IsFrontEngine()) {
		CheckIfTrainNeedsService(this);

		CheckOrders(this);

		/* update destination */
		if (this->current_order.IsType(OT_GOTO_STATION)) {
			TileIndex tile = Station::Get(this->current_order.GetDestination().ToStationID())->train_station.tile;
			if (tile != INVALID_TILE) this->dest_tile = tile;
		}

		if (this->running_ticks != 0) {
			/* running costs */
			CommandCost cost(ExpensesType::TrainRun, this->GetRunningCost() * this->running_ticks / (DAYS_IN_YEAR  * DAY_TICKS));

			/* sharing fee */
			PayDailyTrackSharingFee(this);

			this->profit_this_year -= cost.GetCost();
			this->running_ticks = 0;

			SubtractMoneyFromCompanyFract(this->owner, cost);

			SetWindowDirty(WindowClass::VehicleDetails, this->index);
			DirtyVehicleListWindowForVehicle(this);
		}
	}
	if (IsEngine() || IsMultiheaded()) {
		CheckVehicleBreakdown(this);
	}
}

/**
 * Get the tracks of the train vehicle.
 * @return Current tracks of the vehicle.
 */
Trackdir Train::GetVehicleTrackdir() const
{
	if (this->vehstatus.Test(VehState::Crashed)) return INVALID_TRACKDIR;

	if (this->track == TRACK_BIT_DEPOT) {
		/* We'll assume the train is facing outwards */
		return DiagDirToDiagTrackdir(GetRailDepotDirection(this->tile)); // Train in depot
	}

	if (this->track == TRACK_BIT_WORMHOLE) {
		/* Train in tunnel or on bridge, so just use its direction and make an educated guess
		 * given the track bits on the tunnel/bridge head tile.
		 * If a reachable track piece is reserved, use that, otherwise use the first reachable track piece.
		 */
		TrackBits tracks = GetAcrossTunnelBridgeReservationTrackBits(this->tile);
		if (tracks == TRACK_BIT_NONE) {
			tracks = GetAcrossTunnelBridgeTrackBits(this->tile);
		}
		Track track = FindFirstTrack(tracks);
		/* R3R (KI-108): never hand INVALID_TRACKDIR back to the callers - they pass
		 * the value on to ReverseTrackdir()/TrackdirToExitdir()/TrackdirToTrack(),
		 * all of which assert (track_func.h:249/396/264). Guess from the facing
		 * direction if the tunnel/bridge head gives us nothing to work with. */
		if (unlikely(!IsValidTrack(track))) return DiagDirToDiagTrackdir(DirToDiagDir(this->GetMovingDirection()));
		Trackdir td = TrackExitdirToTrackdir(track, GetTunnelBridgeDirection(this->tile));
		if (unlikely(td == INVALID_TRACKDIR)) return DiagDirToDiagTrackdir(DirToDiagDir(this->GetMovingDirection()));
		if (GetTunnelBridgeDirection(this->tile) != DirToDiagDir(this->GetMovingDirection())) td = ReverseTrackdir(td);
		return td;
	} else if (this->track & TRACK_BIT_WORMHOLE) {
		const Track w_track = FindFirstTrack(this->track & TRACK_BIT_MASK);
		if (unlikely(!IsValidTrack(w_track))) return DiagDirToDiagTrackdir(DirToDiagDir(this->GetMovingDirection()));
		const Trackdir w_td = TrackDirectionToTrackdir(w_track, this->GetMovingDirection());
		return unlikely(w_td == INVALID_TRACKDIR) ? TrackToTrackdir(w_track) : w_td;
	}

	/* R3R (KI-107): a train end may legitimately have no track bits set - e.g. a
	 * freshly created vehicle, or a chain end that is being (re)positioned while
	 * a locomotive couples onto it half outside a platform. FindFirstTrack() then
	 * yields an invalid track and TrackDirectionToTrackdir() would trip its own
	 * assertion; returning INVALID_TRACKDIR from here would in turn crash every
	 * caller that hands the result to ReverseTrackdir()/TrackdirToExitdir()
	 * (that is exactly how track_func.h:249 killed the game right after Couple()).
	 * Guess from the facing direction instead, just like the tunnel/bridge case
	 * above does. */
	const Track track = FindFirstTrack(this->track);
	if (unlikely(!IsValidTrack(track))) return DiagDirToDiagTrackdir(DirToDiagDir(this->GetMovingDirection()));
	const Trackdir td = TrackDirectionToTrackdir(track, this->GetMovingDirection());
	/* R3R (KI-108): the vehicle may sit on a track that does not run in the
	 * direction it faces - R3R creates this state transiently while merging
	 * chains (crash log 2026-09-18 21:05: a chain end parked on TRACK_BIT_Y
	 * while facing a direction that is not along the track, GetVehicleTrackdir()
	 * returned INVALID_TRACKDIR). TrackDirectionToTrackdir() returns
	 * INVALID_TRACKDIR for that combination (it does not assert), and every
	 * caller that forwards the result to ReverseTrackdir()/TrackdirToExitdir()/
	 * TrackdirToTrack() then trips its own assertion (track_func.h:249/396/264).
	 * Stay on the vehicle's own track and use its canonical trackdir instead,
	 * and record the state (edge triggered) so the origin of the mismatch can
	 * be pinned down. */
	if (unlikely(td == INVALID_TRACKDIR)) {
		const uint64_t vehtd_payload = ((uint64_t)this->tile.base() << 32) |
				((uint64_t)(uint)this->track << 16) | (uint64_t)(uint)this->direction;
		if (R3RDbgEdge(R3REDGE_VEHTD, (uint64_t)this->index.base(), vehtd_payload)) {
			FILE *dbg = R3RFopenDbg("a");
			if (dbg != nullptr) {
				fprintf(dbg, "VEHTD-MISMATCH veh=%d tile=%d,%d track=0x%X dir=%d mdir=%d -> coerced=%d\n",
						(int)this->index.base(), (int)TileX(this->tile), (int)TileY(this->tile),
						(uint)this->track, (int)this->direction, (int)this->GetMovingDirection(),
						(int)TrackToTrackdir(track));
				fclose(dbg);
			}
		}
		return TrackToTrackdir(track);
	}
	return td;
}

/**
 * Delete a train while it is visible.
 * This happens when a company bankrupts when infrastructure sharing is enabled.
 * @param v The train to delete.
 */
void DeleteVisibleTrain(Train *v)
{
	SCOPE_INFO_FMT([v], "DeleteVisibleTrain: {}", VehicleInfoDumper(v));

	assert(!v->IsVirtual());

	FreeTrainTrackReservation(v);
	TileIndex crossing = TrainApproachingCrossingTile(v);

	/* delete train from back to front */
	Train *u;
	Train *prev = v->Last();
	FreeTrainStationPlatformReservation(v);
	do {
		u = prev;
		prev = u->Previous();
		if (prev != nullptr) prev->SetNext(nullptr);

		/* 'u' shouldn't be accessed after it has been deleted */
		TileIndex tile = u->tile;
		TrackBits trackbits = u->track;
		bool in_wormhole = trackbits & TRACK_BIT_WORMHOLE;

		delete u;

		if (in_wormhole) {
			/* Vehicle is inside a wormhole, u->track contains no useful value then. */
			if (IsTunnelBridgeWithSignalSimulation(tile)) {
				TileIndex end = GetOtherTunnelBridgeEnd(tile);
				AddSideToSignalBuffer(end, DiagDirection::Invalid, GetTileOwner(tile));
				SetSignalledBridgeTunnelGreenIfClear(tile, end);
			}
		} else {
			R3RTrackBitsProbe("del-visible", trackbits, -1, tile);
			Track track = TrackBitsToTrack(trackbits);
			if (HasReservedTracks(tile, trackbits)) UnreserveRailTrack(tile, track);
			if (IsLevelCrossingTile(tile)) UpdateLevelCrossing(tile);
		}

		/* Update signals */
		if (in_wormhole || IsRailDepotTile(tile)) {
			AddSideToSignalBuffer(tile, DiagDirection::Invalid, GetTileOwner(tile));
		} else {
			AddTrackToSignalBuffer(tile, TrackBitsToTrack(trackbits), GetTileOwner(tile));
		}
	} while (prev != nullptr);

	if (crossing != INVALID_TILE) UpdateLevelCrossing(crossing);

	UpdateSignalsInBuffer();
}

static Train *CmdBuildVirtualRailWagon(const Engine *e, ClientID user, bool no_consist_change)
{
	const RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();

	Train *v = Train::Create();

	v->x_pos = 0;
	v->y_pos = 0;

	v->spritenum = rvi.image_index;

	v->engine_type = e->index;
	v->gcache.first_engine = EngineID::Invalid(); // needs to be set before first callback

	v->direction = Direction::W;
	v->tile = {};

	v->owner = _current_company;
	v->track = TRACK_BIT_DEPOT;
	v->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
	v->vehstatus = {VehState::Hidden, VehState::DefaultPalette};
	v->motion_counter = (uint32_t)user;

	v->SetWagon();
	v->SetFreeWagon();
	v->SetVirtual();

	v->cargo_type = e->GetDefaultCargoType();
	v->cargo_cap = rvi.capacity;

	v->railtypes = rvi.railtypes;

	v->build_year = CalTime::CurYear();
	v->sprite_seq.Set(SPR_IMG_QUERY);
	v->random_bits = Random();

	v->group_id = DEFAULT_GROUP;

	AddArticulatedParts(v);

	// Make sure we set EVERYTHING to virtual, even articulated parts.
	for (Train *train_part = v; train_part != nullptr; train_part = train_part->Next()) {
		train_part->SetVirtual();
	}

	if (no_consist_change) return v;

	v->First()->ConsistChanged(CCF_ARRANGE);

	CheckConsistencyOfArticulatedVehicle(v);

	InvalidateVehicleTickCaches();

	return v;
}

Train *BuildVirtualRailVehicle(EngineID eid, StringID &error, ClientID user, bool no_consist_change)
{
	const Engine *e = Engine::GetIfValid(eid);
	if (e == nullptr || e->type != VehicleType::Train) {
		error = STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + to_underlying(VehicleType::Train);
		return nullptr;
	}

	const RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();

	/* Check whether the number of vehicles we need to build can be built according to pool space.
	 * If 2 + MAX_ARTICULATED_PARTS are available, then there's no need to call CountArticulatedParts, which is potentially expensive. */
	if (!Vehicle::CanAllocateItem(2 + MAX_ARTICULATED_PARTS)) {
		uint num_vehicles = (rvi.railveh_type == RailVehicleType::Multihead ? 2 : 1) + CountArticulatedParts(eid);
		if (!Train::CanAllocateItem(num_vehicles)) {
			error = STR_ERROR_TOO_MANY_VEHICLES_IN_GAME;
			return nullptr;
		}
	}

	RegisterGameEvents(GEF_VIRT_TRAIN);

	if (rvi.railveh_type == RailVehicleType::Wagon) {
		return CmdBuildVirtualRailWagon(e, user, no_consist_change);
	}

	Train *v = Train::Create();

	v->x_pos = 0;
	v->y_pos = 0;

	v->direction = Direction::W;
	v->tile = {};
	v->owner = _current_company;
	v->track = TRACK_BIT_DEPOT;
	v->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
	v->vehstatus = {VehState::Hidden, VehState::Stopped, VehState::DefaultPalette};
	v->spritenum = rvi.image_index;
	v->cargo_type = e->GetDefaultCargoType();
	v->cargo_cap = rvi.capacity;
	v->last_station_visited = StationID::Invalid();
	v->motion_counter = (uint32_t)user;

	v->engine_type = e->index;
	v->gcache.first_engine = EngineID::Invalid(); // needs to be set before first callback

	v->reliability = e->reliability;
	v->reliability_spd_dec = e->reliability_spd_dec;
	v->max_age = e->GetLifeLengthInDays();

	v->SetServiceInterval(Company::Get(_current_company)->settings.vehicle.servint_trains);
	v->SetServiceIntervalIsPercent(Company::Get(_current_company)->settings.vehicle.servint_ispercent);
	v->vehicle_flags.Set(VehicleFlag::AutomateTimetable, Company::Get(_current_company)->settings.vehicle.auto_timetable_by_default);
	v->vehicle_flags.Set(VehicleFlag::TimetableSeparation, Company::Get(_current_company)->settings.vehicle.auto_separation_by_default);

	v->railtypes = rvi.railtypes;

	v->build_year = CalTime::CurYear();
	v->sprite_seq.Set(SPR_IMG_QUERY);
	v->random_bits = Random();

	v->group_id = DEFAULT_GROUP;

	v->SetFrontEngine();
	v->SetEngine();
	v->SetVirtual();

	if (rvi.railveh_type == RailVehicleType::Multihead) {
		AddRearEngineToMultiheadedTrain(v);
	} else {
		AddArticulatedParts(v);
	}

	// Make sure we set EVERYTHING to virtual, even articulated parts.
	for (Train *train_part = v; train_part != nullptr; train_part = train_part->Next()) {
		train_part->SetVirtual();
	}

	if (no_consist_change) return v;

	v->ConsistChanged(CCF_ARRANGE);

	CheckConsistencyOfArticulatedVehicle(v);

	InvalidateVehicleTickCaches();

	return v;
}

/**
 * Build a virtual train vehicle.
 * @param flags for command
 * @param eid vehicle type being built.
 * @param cargo refit cargo type.
 * @param client_id User
 * @param move_target Where to move the virtual train vehicle after construction
 * @return the cost of this operation or an error
 */
CommandCost CmdBuildVirtualRailVehicle(DoCommandFlags flags, EngineID eid, CargoType cargo, ClientID client, VehicleID move_target)
{
	if (!IsEngineBuildable(eid, VehicleType::Train, _current_company)) {
		return CommandCost(STR_ERROR_RAIL_VEHICLE_NOT_AVAILABLE + to_underlying(VehicleType::Train));
	}

	/* Validate the cargo type. */
	if (cargo >= NUM_CARGO && cargo != INVALID_CARGO) return CMD_ERROR;

	CommandCost cost;

	if (flags.Test(DoCommandFlag::Execute)) {
		StringID err = INVALID_STRING_ID;
		Train *train = BuildVirtualRailVehicle(eid, err, client, false);

		if (train == nullptr) {
			return CommandCost(err);
		}

		cost.SetResultData(train->index);

		if (cargo != INVALID_CARGO) {
			CargoType default_cargo = Engine::Get(eid)->GetDefaultCargoType();
			if (default_cargo != cargo) {
				CommandCost refit_res = CmdRefitVehicle(flags, train->index, cargo, 0, false, false, 0);
				if (!refit_res.Succeeded()) {
					Command<Commands::SellVehicle>::Do(flags, train->tile, train->index, SellVehicleFlags::VirtualOnly, client);
					return refit_res;
				}
			}
		}

		if (move_target != VehicleID::Invalid()) {
			Train *move_target_train = Train::GetIfValid(move_target);

			CommandCost move_result = CMD_ERROR;
			if (move_target_train != nullptr) {
				move_result = Command<Commands::MoveVirtualRailVehicle>::Do(flags, train->index, move_target_train->GetLastUnit()->index, MoveRailVehicleFlags::Virtual);
			}

			if (move_result.Failed()) {
				Command<Commands::SellVehicle>::Do(flags, train->tile, train->index, SellVehicleFlags::VirtualOnly, client);
				return move_result;
			}
		}
	}

	return cost;
}

void ClearVehicleWindows(const Train *v)
{
	if (v->IsPrimaryVehicle()) {
		CloseWindowById(WindowClass::VehicleView, v->index);
		CloseWindowById(WindowClass::VehicleOrders, v->index);
		CloseWindowById(WindowClass::VehicleRefit, v->index);
		CloseWindowById(WindowClass::VehicleDetails, v->index);
		CloseWindowById(WindowClass::VehicleTimetable, v->index);
		CloseWindowById(WindowClass::ScheduledDispatchSlots, v->index);
		CloseWindowById(WindowClass::VehicleCargoTypeLoadOrders, v->index);
		CloseWindowById(WindowClass::VehicleCargoTypeUnloadOrders, v->index);
		CloseWindowById(WindowClass::VehicleOrderImportErrors, v->index);
	}
}

/**
 * Issue a start/stop command
 * @param v a vehicle
 * @param evaluate_callback shall the start/stop callback be evaluated?
 * @return success or error
 */
static inline CommandCost CmdStartStopVehicle(const Vehicle *v, bool evaluate_callback)
{
	return CmdStartStopVehicle({DoCommandFlag::Execute, DoCommandFlag::AutoReplace}, v->index, evaluate_callback);
}

/**
* Replace a vehicle based on a template replacement order.
* @param flags type of operation
* @param incoming the incoming train to replace.
* @param outgoing the replaced train, or incoming.
* @return the cost of this operation or an error
*/
static CommandCost CmdTemplateReplaceVehicle(DoCommandFlags flags, Train *incoming, Train *&outgoing)
{
	CommandCost buy(ExpensesType::NewVehicles);

	const bool was_stopped = incoming->vehstatus.Test(VehState::Stopped);
	if (!was_stopped) {
		CommandCost cost = CmdStartStopVehicle(incoming, true);
		if (cost.Failed()) return cost;
	}
	auto guard = scope_guard([&]() {
		outgoing = incoming;
		if (!was_stopped) buy.AddCost(CmdStartStopVehicle(incoming, false));
	});

	Train *new_chain = nullptr;
	Train *remainder_chain = nullptr;
	TemplateVehicle *tv = GetTemplateVehicleByGroupIDRecursive(incoming->group_id);
	if (tv == nullptr) {
		return CMD_ERROR;
	}
	EngineID eid = tv->engine_type;

	CommandCost tmp_result(ExpensesType::NewVehicles);

	/* first some tests on necessity and sanity */
	if (tv == nullptr) return CommandCost();
	if (tv->IsReplaceOldOnly() && !incoming->NeedsAutorenewing(Company::Get(incoming->owner), false)) {
		return CommandCost();
	}
	const TBTRDiffFlags diff = TrainTemplateDifference(incoming, tv);
	if (diff == TBTRDF_NONE) return CommandCost();

	const bool need_replacement = (diff & TBTRDF_CONSIST);
	const bool need_refit = (diff & TBTRDF_REFIT);
	const bool refit_to_template = tv->refit_as_template;
	const TileIndex tile = incoming->tile;

	CargoType store_refit_ct = INVALID_CARGO;
	uint16_t store_refit_csubt = 0;
	// if a train shall keep its old refit, store the refit setting of its first vehicle
	if (!refit_to_template) {
		for (Train *getc = incoming; getc != nullptr; getc = getc->GetNextUnit()) {
			if (getc->cargo_type != INVALID_CARGO && getc->cargo_cap > 0) {
				store_refit_ct = getc->cargo_type;
				break;
			}
		}
	}

	if (need_replacement) {
		CommandCost buy_cost = TestBuyAllTemplateVehiclesInChain(tv, tile);
		if (buy_cost.Failed()) {
			if (buy_cost.GetErrorMessage() == INVALID_STRING_ID) return CommandCost(STR_ERROR_CAN_T_BUY_TRAIN);
			return buy_cost;
		} else if (!CheckCompanyHasMoney(buy_cost)) {
			return CommandCost(STR_ERROR_NOT_ENOUGH_CASH_REQUIRES_CURRENCY);
		}
	}

	if (tv->IsFreeWagonChain()) {
		return CommandCost(STR_TMPL_ERROR_NOT_RUNNABLE);
	}

	TemplateDepotVehicles depot_vehicles;
	if (tv->IsSetReuseDepotVehicles()) depot_vehicles.Init(tile);

	auto refit_unit = [&](const Train *unit, CargoType cid, uint16_t csubt) {
		CommandCost refit_cost = Command<Commands::RefitVehicle>::Do(flags, unit->index, cid, csubt, false, false, 1);
		if (refit_cost.Succeeded()) buy.AddCost(refit_cost.GetCost());
	};

	if (!flags.Test(DoCommandFlag::Execute)) {
		/* Simplified operation for cost estimation, this doesn't have to exactly match the actual cost due to CMD_NO_TEST */
		if (need_replacement || need_refit) {
			std::vector<const Train *> in;
			for (const Train *u = incoming; u != nullptr; u = u->GetNextUnit()) {
				in.push_back(u);
			}
			auto process_unit = [&](const TemplateVehicle *cur_tmpl) {
				for (auto iter = in.begin(); iter != in.end(); ++iter) {
					const Train *u = *iter;
					if (u->engine_type == cur_tmpl->engine_type) {
						/* use existing engine */
						in.erase(iter);
						if (refit_to_template) {
							buy.AddCost(Command<Commands::RefitVehicle>::Do(flags, u->index, cur_tmpl->cargo_type, cur_tmpl->cargo_subtype, false, false, 1));
						} else {
							refit_unit(u, store_refit_ct, store_refit_csubt);
						}
						return;
					}
				}

				if (tv->IsSetReuseDepotVehicles()) {
					const Train *depot_eng = depot_vehicles.ContainsEngine(cur_tmpl->engine_type, incoming);
					if (depot_eng != nullptr) {
						depot_vehicles.RemoveVehicle(depot_eng->index);
						if (refit_to_template) {
							buy.AddCost(Command<Commands::RefitVehicle>::Do(flags, depot_eng->index, cur_tmpl->cargo_type, cur_tmpl->cargo_subtype, false, false, 1));
						} else {
							refit_unit(depot_eng, store_refit_ct, store_refit_csubt);
						}
						return;
					}
				}

				CargoType refit_cargo = refit_to_template ? cur_tmpl->cargo_type : store_refit_ct;
				buy.AddCost(Command<Commands::BuildVehicle>::Do(flags, tile, cur_tmpl->engine_type, false, refit_cargo, INVALID_CLIENT_ID));
			};
			for (const TemplateVehicle *cur_tmpl = tv; cur_tmpl != nullptr; cur_tmpl = cur_tmpl->GetNextUnit()) {
				process_unit(cur_tmpl);
			}
			if (!tv->IsSetKeepRemainingVehicles()) {
				/* Sell leftovers */
				for (const Train *u : in) {
					/* Do not dry-run selling each part using Commands::SellVehicle because this can fail due to consist/wagon-attachment callbacks */
					buy.AddCost(-u->value);
					if (u->other_multiheaded_part != nullptr) {
						buy.AddCost(-u->other_multiheaded_part->value);
					}
				}
			}
		}
		if (buy.Failed()) buy.MultiplyCost(0);
		return buy;
	}

	RegisterGameEvents(GEF_TBTR_REPLACEMENT);

	if (need_replacement) {
		const bool old_driving_backwards = incoming->vehicle_flags.Test(VehicleFlag::DrivingBackwards);

		// step 1: generate primary for newchain and generate remainder_chain
		// 1. primary of incoming might already fit the template
		//    leave incoming's primary as is and move the rest to a free chain = remainder_chain
		// 2. needed primary might be one of incoming's member vehicles
		// 3. primary might be available as orphan vehicle in the depot
		// 4. we need to buy a new engine for the primary
		// all options other than 1. need to make sure to copy incoming's primary's status
		auto setup_head = [&]() -> CommandCost {
			/* Case 1 */
			if (eid == incoming->engine_type) {
				new_chain = incoming;
				remainder_chain = incoming->GetNextUnit();
				if (remainder_chain != nullptr) {
					DoCommandFlags subflags = flags;
					if (!tv->IsSetKeepRemainingVehicles()) subflags.Set(DoCommandFlag::AutoReplace);
					CommandCost move_cost = CmdMoveRailVehicle(subflags, remainder_chain->index, VehicleID::Invalid(), MoveRailVehicleFlags::MoveChain);
					if (move_cost.Failed()) {
						/* This should not fail, if it does give up immediately */
						return move_cost;
					}
				}
				return CommandCost();
			}

			/* Case 2 */
			new_chain = ChainContainsEngine(eid, incoming);
			if (new_chain != nullptr) {
				/* new_chain is the needed engine, move it to an empty spot in the depot */
				CommandCost move_cost = Command<Commands::MoveRailVehicle>::Do(flags | DoCommandFlag::AutoReplace, new_chain->index, VehicleID::Invalid(), MoveRailVehicleFlags::None);
				if (move_cost.Succeeded()) {
					remainder_chain = incoming;
					return CommandCost();
				}
			}

			/* Case 3 */
			if (tv->IsSetReuseDepotVehicles()) {
				new_chain = depot_vehicles.ContainsEngine(eid, incoming);
				if (new_chain != nullptr) {
					ClearVehicleWindows(new_chain);
					CommandCost move_cost = Command<Commands::MoveRailVehicle>::Do(flags | DoCommandFlag::AutoReplace, new_chain->index, VehicleID::Invalid(), MoveRailVehicleFlags::None);
					if (move_cost.Succeeded()) {
						depot_vehicles.RemoveVehicle(new_chain->index);
						remainder_chain = incoming;
						return CommandCost();
					}
				}
			}

			/* Case 4 */
			CommandCost buy_cost = Command<Commands::BuildVehicle>::Do(flags | DoCommandFlag::AutoReplace, tile, eid, false, INVALID_CARGO, INVALID_CLIENT_ID);
			/* break up in case buying the vehicle didn't succeed */
			if (buy_cost.Failed()) return buy_cost;
			auto buy_veh_id = buy_cost.GetResultData<VehicleID>();
			if (!buy_veh_id.has_value()) return buy_cost;

			buy.AddCost(buy_cost.GetCost());
			new_chain = Train::Get(*buy_veh_id);
			/* prepare the remainder chain */
			remainder_chain = incoming;
			return CommandCost();
		};
		CommandCost head_result = setup_head();
		if (head_result.Failed()) return head_result;

		// If we bought a new engine or reused one from the depot, copy some parameters from the incoming primary engine
		if (incoming != new_chain) {
			CopyHeadSpecificThings(incoming, new_chain, flags, false);
			NeutralizeStatus(incoming);

			// additionally, if we don't want to use the template refit, refit as incoming
			// the template refit will be set further down, if we use it at all
			if (!refit_to_template) {
				refit_unit(new_chain, store_refit_ct, store_refit_csubt);
			}
		}

		// step 2: fill up newchain according to the template
		// foreach member of template (after primary):
		// 1. needed engine might be within remainder_chain already
		// 2. needed engine might be orphaned within the depot (copy status)
		// 3. we need to buy (again)                           (copy status)
		Train *last_veh = new_chain;
		for (TemplateVehicle *cur_tmpl = tv->GetNextUnit(); cur_tmpl != nullptr; cur_tmpl = cur_tmpl->GetNextUnit()) {
			Train *new_part = nullptr;
			auto setup_chain_part = [&]() {
				/* Case 1: engine contained in remainder chain */
				new_part = ChainContainsEngine(cur_tmpl->engine_type, remainder_chain);
				if (new_part != nullptr) {
					Train *remainder_chain_next = remainder_chain;
					if (new_part == remainder_chain) {
						remainder_chain_next = remainder_chain->GetNextUnit();
					}
					CommandCost move_cost = CmdMoveRailVehicle(flags | DoCommandFlag::AutoReplace, new_part->index, last_veh->index, MoveRailVehicleFlags::None);
					if (move_cost.Succeeded()) {
						remainder_chain = remainder_chain_next;
						return;
					}
				}

				/* Case 2: engine contained somewhere else in the depot */
				if (tv->IsSetReuseDepotVehicles()) {
					new_part = depot_vehicles.ContainsEngine(cur_tmpl->engine_type, new_chain);
					if (new_part != nullptr) {
						CommandCost move_cost = CmdMoveRailVehicle(flags, new_part->index, last_veh->index, MoveRailVehicleFlags::None);
						if (move_cost.Succeeded()) {
							depot_vehicles.RemoveVehicle(new_part->index);
							return;
						}
					}
				}

				/* Case 3: must buy new engine */
				CommandCost buy_cost = Command<Commands::BuildVehicle>::Do(flags | DoCommandFlag::AutoReplace, tile, cur_tmpl->engine_type, false, INVALID_CARGO, INVALID_CLIENT_ID);
				if (buy_cost.Failed()) {
					new_part = nullptr;
					return;
				}
				auto buy_veh_id = buy_cost.GetResultData<VehicleID>();
				if (!buy_veh_id.has_value()) {
					new_part = nullptr;
					return;
				}

				new_part = Train::Get(*buy_veh_id);
				CommandCost move_cost = CmdMoveRailVehicle(flags | DoCommandFlag::AutoReplace, new_part->index, last_veh->index, MoveRailVehicleFlags::None);
				if (move_cost.Succeeded()) {
					buy.AddCost(buy_cost.GetCost());
				} else {
					Command<Commands::SellVehicle>::Do(flags, new_part->tile, new_part->index, SellVehicleFlags::None, INVALID_CLIENT_ID);
					new_part = nullptr;
				}
			};
			setup_chain_part();
			if (new_part != nullptr) {
				last_veh = new_part;
			}

			if (!refit_to_template && new_part != nullptr) {
				refit_unit(new_part, store_refit_ct, store_refit_csubt);
			}
		}

		if (new_chain != nullptr && old_driving_backwards && !new_chain->vehicle_flags.Test(VehicleFlag::DrivingBackwards) &&
				(_settings_game.difficulty.train_flip_reverse_allowed == TrainFlipReversingAllowed::None || new_chain->Last()->CanLeadTrain())) {
			new_chain->vehicle_flags.Set(VehicleFlag::DrivingBackwards);
			new_chain->ConsistChanged(CCF_ARRANGE);
		}
	} else {
		/* no replacement done */
		new_chain = incoming;
	}

	/// step 3: reorder and neutralize the remaining vehicles from incoming
	// wagons remaining from remainder_chain should be filled up in as few free wagon chains as possible
	// each loco might be left as singular in the depot
	// neutralize each remaining engine's status

	// refit, only if the template option is set so
	if (refit_to_template && (need_refit || need_replacement)) {
		buy.AddCost(CmdRefitTrainFromTemplate(new_chain, tv, flags));
	}

	CmdSetTrainUnitDirectionFromTemplate(new_chain, tv, flags);

	if (new_chain != nullptr && remainder_chain != nullptr) {
		for (Train *ct = remainder_chain; ct != nullptr; ct = ct->Next()) {
			TransferCargoForTrain(ct, new_chain);
		}
	}

	// point incoming to the newly created train so that starting/stopping affects the replacement train
	incoming = new_chain;

	if (remainder_chain != nullptr && tv->IsSetKeepRemainingVehicles()) {
		BreakUpRemainders(remainder_chain);
	} else if (remainder_chain != nullptr) {
		buy.AddCost(Command<Commands::SellVehicle>::Do(flags, remainder_chain->tile, remainder_chain->index, SellVehicleFlags::SellChain, INVALID_CLIENT_ID));
	}

	/* Redraw main gui for changed statistics */
	SetWindowClassesDirty(WindowClass::TemplateReplacementGuiMain);

	return buy;
}

/**
* Replace a vehicle based on a template replacement order.
* @param flags type of operation
* @param veh_id the ID of the vehicle to replace.
* @return the cost of this operation or an error
*/
CommandCost CmdTemplateReplaceVehicle(DoCommandFlags flags, VehicleID veh_id)
{
	Train *incoming = Train::GetIfValid(veh_id);

	if (incoming == nullptr || !incoming->IsPrimaryVehicle() || !incoming->IsChainInDepot()) {
		return CMD_ERROR;
	}

	/* R3R: template replacement rebuilds the entire chain, selling whatever the template
	 * does not cover and buying a new train in its place. Upstream assumes the chain to be
	 * owned by one company, so a cross-company coupled chain would sell vehicles of the
	 * other company. There is no valid template replacement semantics for such a chain,
	 * so skip it silently (CMD_ERROR carries no message). */
	for (const Vehicle *w = incoming->Next(); w != nullptr; w = w->Next()) {
		if (w->owner != incoming->owner) return CMD_ERROR;
	}

	Train *outgoing = incoming;
	CommandCost cost = CmdTemplateReplaceVehicle(flags, incoming, outgoing);
	cost.SetResultData(outgoing->index);
	return cost;
}

void TrainRoadVehicleCrashBreakdown(Vehicle *v)
{
	Train *t = Train::From(v)->First();
	t->breakdown_ctr = 2;
	t->flags.Set(VehicleRailFlag::ConsistBreakdown);
	t->breakdown_delay = 255;
	t->breakdown_type = BREAKDOWN_RV_CRASH;
	t->breakdown_severity = 0;
	t->reliability = 0;
}

void TrainBrakesOverheatedBreakdown(Vehicle *v, int speed, int max_speed)
{
	if (v->type != VehicleType::Train) return;
	Train *t = Train::From(v)->First();
	if (t->breakdown_ctr != 0 || t->vehstatus.Test(VehState::Crashed)) return;

	if (unlikely(HasBit(_misc_debug_flags, MDF_OVERHEAT_BREAKDOWN_OPEN_WIN)) && !IsHeadless()) {
		ShowVehicleViewWindow(t);
	}

	t->crash_anim_pos = static_cast<uint16_t>(std::min<uint>(1500, t->crash_anim_pos + Clamp(((speed - max_speed) * speed) / 2, 0, 500)));
	if (t->crash_anim_pos < 1500) return;

	t->breakdown_ctr = 2;
	t->flags.Set(VehicleRailFlag::ConsistBreakdown);
	t->breakdown_delay = 255;
	t->breakdown_type = BREAKDOWN_BRAKE_OVERHEAT;
	t->breakdown_severity = 0;
}

int GetTrainRealisticAccelerationAtSpeed(const int speed, const int mass, const uint32_t cached_power, const uint32_t max_te, const uint32_t air_drag, const RailTypes railtypes)
{
	const int64_t power = cached_power * 746ll;
	int64_t resistance = 0;

	const bool maglev = GetAccelerationTypeRailTypes(VehicleAccelerationModel::Maglev).All(railtypes);

	if (!maglev) {
		/* Static resistance plus rolling friction. */
		resistance = 10 * mass;
		resistance += (int64_t)mass * (int64_t)(15 * (512 + speed) / 512);
	}

	const int area = 14;

	resistance += (area * air_drag * speed * speed) / 1000;

	int64_t force;

	if (speed > 0) {
		if (!maglev) {
			/* Conversion factor from km/h to m/s is 5/18 to get [N] in the end. */
			force = power * 18 / (speed * 5);

			if (force > static_cast<int>(max_te)) {
				force = max_te;
			}
		} else {
			force = power / 25;
		}
	} else {
		force = (!maglev) ? std::min<uint64_t>(max_te, power) : power;
		force = std::max(force, (mass * 8) + resistance);
	}

	/* Easy way out when there is no acceleration. */
	if (force == resistance) return 0;

	int acceleration = ClampTo<int32_t>((force - resistance) / (mass * 4));
	acceleration = force < resistance ? std::min(-1, acceleration) : std::max(1, acceleration);

	return acceleration;
}

int GetTrainEstimatedMaxAchievableSpeed(const Train *train, int mass, const int speed_cap)
{
	int max_speed = 0;
	int acceleration;

	if (mass < 1) mass = 1;

	do
	{
		max_speed++;
		acceleration = GetTrainRealisticAccelerationAtSpeed(max_speed, mass, train->gcache.cached_power, train->gcache.cached_max_te, train->gcache.cached_air_drag, train->railtypes);
	} while (acceleration > 0 && max_speed < speed_cap);

	return max_speed;
}

int64_t GetTrainPowerToWeightRatio(const Train *train, int mass)
{
	if (mass < 1) mass = 1;
	return ((int64_t)train->gcache.cached_power * 100) / mass;
}

int64_t GetTrainMaxTractiveEffortToWeightRatio(const Train *train, int mass)
{
	if (mass < 1) mass = 1;
	return ((int64_t)train->gcache.cached_max_te * 100) / mass;
}

void SetSignalTrainAdaptationSpeed(const Train *v, TileIndex tile, uint16_t track)
{
	SignalSpeedKey speed_key = {};
	speed_key.signal_tile = tile;
	speed_key.signal_track = track;
	speed_key.last_passing_train_dir = v->GetVehicleTrackdir();

	const Train *first = v->First();

	if (first->cur_speed < SPEED_ADAPTATION_MIN_SPEED) {
		_signal_speeds.erase(speed_key);
		return;
	}

	SignalSpeedValue speed_value = {};
	speed_value.train_speed = first->cur_speed;
	speed_value.time_stamp = GetSpeedRestrictionTimeout(first);

	_signal_speeds[speed_key] = speed_value;
}

static uint16_t GetTrainAdaptationSpeed(TileIndex tile, uint16_t track, Trackdir last_passing_train_dir)
{
	SignalSpeedKey speed_key = { tile, track, last_passing_train_dir };
	const auto found_speed_restriction = _signal_speeds.find(speed_key);

	if (found_speed_restriction != _signal_speeds.end()) {
		if (found_speed_restriction->second.IsOutOfDate()) {
			_signal_speeds.erase(found_speed_restriction);
			return 0;
		} else {
			return std::max<uint16_t>(25, found_speed_restriction->second.train_speed);
		}
	} else {
		return 0;
	}
}

void ApplySignalTrainAdaptationSpeed(Train *v, TileIndex tile, uint16_t track)
{
	uint16_t speed = GetTrainAdaptationSpeed(tile, track, v->GetVehicleTrackdir());

	Train *consist = v->First();
	if (speed > 0 && consist->lookahead != nullptr) {
		for (const TrainReservationLookAheadItem &item : consist->lookahead->items) {
			if (item.type == TRLIT_SPEED_ADAPTATION && item.end + 1 < consist->lookahead->reservation_end_position) {
				uint16_t signal_speed = GetLowestSpeedTrainAdaptationSpeedAtSignal(TileIndex{item.data_id}, item.data_aux);

				if (signal_speed == 0) {
					/* unrestricted signal ahead, disregard speed adaptation at earlier signal */
					consist->UpdateTrainSpeedAdaptationLimit(0);
					return;
				}
				if (signal_speed > speed) {
					/* signal ahead with higher speed adaptation speed, override speed adaptation at earlier signal */
					speed = signal_speed;
				}
			}
		}
	}

	consist->UpdateTrainSpeedAdaptationLimit(speed);
}

uint16_t GetLowestSpeedTrainAdaptationSpeedAtSignal(TileIndex tile, uint16_t track)
{
	uint16_t lowest_speed = 0;

	SignalSpeedKey speed_key = { tile, track, (Trackdir)0 };
	for (auto iter = _signal_speeds.lower_bound(speed_key); iter != _signal_speeds.end() && iter->first.signal_tile == tile && iter->first.signal_track == track;) {
		if (iter->second.IsOutOfDate()) {
			iter = _signal_speeds.erase(iter);
		} else {
			uint16_t adapt_speed = std::max<uint16_t>(25, iter->second.train_speed);
			if (lowest_speed == 0 || adapt_speed < lowest_speed) lowest_speed = adapt_speed;
			++iter;
		}
	}

	return lowest_speed;
}

VehicleAccelerationModel Train::GetAccelerationTypeFromTrack() const
{
	return GetRailTypeInfo(GetRailTypeByTrackBit(this->tile, this->track))->acceleration_type;
}

uint16_t Train::GetMaxWeight() const
{
	uint16_t weight = CargoSpec::Get(this->cargo_type)->WeightOfNUnitsInTrain(this->GetEngine()->DetermineCapacity(this));

	/* R3R: de-articulated-group baked override takes precedence (exact conserved value). */
	if (this->weight_override != UINT16_MAX) {
		weight += this->weight_override;
	} else if (!this->IsArticulatedPart()) {
		/* Vehicle weight is not added for articulated parts. */
		weight += GetVehicleProperty(this, PROP_TRAIN_WEIGHT, RailVehInfo(this->engine_type)->weight);
	}

	/* Powered wagons have extra weight added. */
	if (this->flags.Test(VehicleRailFlag::PoweredWagon)) {
		weight += RailVehInfo(this->gcache.first_engine)->pow_wag_weight;
	}

	return weight;
}

void Train::UpdateTrainSpeedAdaptationLimitInternal(uint16_t speed)
{
	this->signal_speed_restriction = speed;
	if (!this->flags.Test(VehicleRailFlag::SpeedAdaptationExempt)) {
		SetWindowDirty(WindowClass::VehicleDetails, this->index);
	}
}

/**
 * Set train speed restriction
 * @param flags type of operation
 * @param veh_id vehicle
 * @param speed new speed restriction value
 * @return the cost of this operation or an error
 */
CommandCost CmdSetTrainSpeedRestriction(DoCommandFlags flags, VehicleID veh_id, uint16_t speed)
{
	Train *v = Train::GetIfValid(veh_id);
	if (v == nullptr || !v->IsPrimaryVehicle()) return CMD_ERROR;

	CommandCost ret = CheckVehicleControlAllowed(v);
	if (ret.Failed()) return ret;

	if (v->vehstatus.Test(VehState::Crashed)) return CommandCost(STR_ERROR_VEHICLE_IS_DESTROYED);

	if (flags.Test(DoCommandFlag::Execute)) {
		if (v->flags.Test(VehicleRailFlag::PendingSpeedRestriction)) {
			_pending_speed_restriction_change_map.erase(v->index);
			v->flags.Reset(VehicleRailFlag::PendingSpeedRestriction);
		}
		v->speed_restriction = speed;

		SetWindowDirty(WindowClass::VehicleDetails, v->index);
	}
	return CommandCost();
}

bool Train::StopFoundAtVehiclePosition() const
{
	ChooseTrainTrackLookAheadState lookahead_state;
	VehicleOrderSaver orders(const_cast<Train *>(this));
	orders.AdvanceOrdersFromVehiclePosition(lookahead_state);
	return HasBit(lookahead_state.flags, CTTLASF_STOP_FOUND);
}

/**
 * Check if this vehicle can lead a train.
 * @return \c true iff this vehicle can lead a train.
 */
bool Train::CanLeadTrain() const
{
	/* NewGRFs can allow unpowered wagons to lead trains. */
	if (this->GetEngine()->info.extra_flags.Test(ExtraEngineFlag::HasCab)) return true;

	/* This might be an articulated engine. */
	if (this->IsArticulatedPart()) {
		return this->GetFirstEnginePart()->IsEngine();
	}

	return this->IsEngine() || this->IsRearDualheaded();
}
