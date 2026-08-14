/*
 * This file is part of OpenTTD (adapted for the R3R consist-group feature).
 * A consist (car-only formation) is represented as a zero-power locomotive:
 * physically a normal train front (so every OpenTTD subsystem treats it natively),
 * but with no power/speed so it just waits in a depot/station until coupled onto.
 */

#include "stdafx.h"
#include "consist_group.h"

#include "train.h"
#include "train_cmd.h"
#include "engine_func.h"
#include "engine_base.h"
#include "engine_type.h"
#include "vehicle_cmd.h"
#include "vehicle_func.h"
#include "command_func.h"
#include "date_func.h"

/**
 * Create (or recreate, after a NewGRF config change) the dedicated consist-group
 * engine: a zero-power locomotive that backs every car-only consist.
 * The engine pool is configuration-level data (not saved); this must be called
 * after every StartupEngines() so the pool tail index stays stable per GRF config.
 * @return The engine ID, or #EngineID::Invalid() on failure.
 */
EngineID CreateConsistGroupEngine()
{
	if (!Engine::CanAllocateItem()) return EngineID::Invalid();

	/* Append after the current engine pool tail (stable per GRF config). */
	EngineID index = EngineID::Begin();
	for (const Engine *e : Engine::Iterate()) {
		if (e->index >= index) index = EngineID(e->index.base() + 1);
	}

	Engine *e = Engine::CreateAtIndex(index, VehicleType::Train, 0x7FFF);
	if (e == nullptr) return EngineID::Invalid();

	/* Keep the consist engine OUT of the purchase list: company_avail stays empty
	 * and the intro date is far in the future, so "IsEngineAvailable" (rail.cpp)
	 * never returns true for it. Players must not be able to buy it (it is not a
	 * real engine; buying it would break the game). */
	e->company_avail = CompanyMask{};
	e->flags.Set(EngineFlag::Available);
	e->intro_date = CalTime::ConvertYMDToDate(CalTime::Year(2400), 0, 1);
	e->age = 0;
	e->reliability = 100;
	e->reliability_start = 100;
	e->reliability_max = 100;
	e->reliability_final = 100;
	e->duration_phase_1 = 0;
	e->duration_phase_2 = 0;
	e->duration_phase_3 = 0;
	e->reliability_spd_dec = 0;

	EngineInfo &info = e->info;
	info.base_life = CalTime::YearDelta{0xFF};
	info.lifelength = CalTime::YearDelta{0xFF};
	info.climates.Set();
	info.cargo_type = INVALID_CARGO;
	info.misc_flags = EngineMiscFlags{};
	info.extra_flags = ExtraEngineFlags{};

	RailVehicleInfo &rvi = e->VehInfo<RailVehicleInfo>();
	rvi.railveh_type = RailVehicleType::Singlehead;
	rvi.cost_factor = 0;
	rvi.railtypes = RAILTYPE_RAIL;
	rvi.intended_railtypes = RAILTYPE_RAIL;
	/* Minimum (1 hp) power so the consist is NOT auto-stopped by the "no power"
	 * check (a 0-power train is kept stopped, which releases its reservation and
	 * makes it impossible to couple onto). max_speed = 0 means "no speed limit"
	 * per user request (the consist never runs on its own anyway; WAIT_COUPLE
	 * keeps it waiting). */
	rvi.max_speed = 0;
	rvi.power = 1;
	rvi.weight = 1;
	rvi.running_cost = 0;
	rvi.running_cost_class = Price::Invalid;
	rvi.engclass = EngineClass::Steam;
	rvi.capacity = 0;
	rvi.visual_effect = VE_DEFAULT;
	rvi.tractive_effort = 0;
	rvi.air_drag = 0;

	return e->index;
}

/**
 * Find (or create) the dedicated consist-group engine.
 * @return The engine ID, or #EngineID::Invalid() on failure.
 */
EngineID GetConsistGroupEngine()
{
	static EngineID id = EngineID::Invalid();
	if (id == EngineID::Invalid() || Engine::GetIfValid(id) == nullptr) {
		id = CreateConsistGroupEngine();
	}
	return id;
}

/**
 * Turn a car-only chain into a consist: give its front vehicle the zero-power
 * locomotive identity, so every OpenTTD subsystem treats it as a normal train.
 * @param front_wagon First vehicle of the real consist.
 * @return The consist front (now an engine), or nullptr on failure.
 */
Train *CreateConsistGroup(Train *front_wagon)
{
	if (front_wagon == nullptr) return nullptr;

	/* Give the chain front the locomotive identity WITHOUT replacing its engine:
	 * the real wagon's engine_type (appearance, capacity, stats) is preserved.
	 * The consist is recognised by IsConsistGroup() (front engine whose rail
	 * vehicle type is Wagon). Its cached power is 0; CheckTrainStayInDepot has
	 * an exemption so it is not auto-stopped (equivalent to a 1 hp minimum). */
	front_wagon->SetEngine();
	front_wagon->ClearWagon();
	front_wagon->ClearFreeWagon();
	front_wagon->SetFrontEngine();
	front_wagon->ConsistChanged(CCF_ARRANGE);
	return front_wagon;
}

/**
 * Remove the consist (zero-power locomotive) identity from a chain front.
 * @param front The consist front.
 */
void DestroyConsistGroup(Train *front)
{
	if (front == nullptr) return;

	front->ClearFrontEngine();
	front->ClearEngine();
	front->SetWagon();
	front->SetFreeWagon();
	/* NOTE: no ConsistChanged() here! After ArrangeTrains merged the consist into
	 * the locomotive's chain, `front` is no longer a chain head, and ConsistChanged
	 * requires `this` to be the head (assertion at train_cmd.cpp:325). The caller
	 * (Couple) refreshes the merged chain via NormaliseTrainHead(v) on the
	 * locomotive head afterwards. */
}

/**
 * Whether a train front is a consist (car-only formation with the zero-power
 * locomotive identity).
 * @param v The train front.
 * @return True when it is a consist.
 */
bool IsConsistGroup(const Train *v)
{
	if (v == nullptr || !v->IsEngine()) return false;
	/* R3R: the consist keeps the real wagon's engine_type (appearance/stats are
	 * preserved), so it is recognised structurally: a front engine whose rail
	 * vehicle type is a Wagon. (Legacy: the old dedicated consist engine also
	 * counts, for saves created before the change.) */
	if (v->engine_type == GetConsistGroupEngine()) return true;
	return RailVehInfo(v->engine_type)->railveh_type == RailVehicleType::Wagon;
}

