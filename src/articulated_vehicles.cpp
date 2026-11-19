/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file articulated_vehicles.cpp Implementation of articulated vehicles. */

#include "stdafx.h"
#include "articulated_vehicles.h"
#include "core/bitmath_func.hpp"
#include "core/random_func.hpp"
#include "train.h"
#include "roadveh.h"
#include "ship.h"
#include "vehicle_func.h"
#include "engine_func.h"
#include "company_func.h"
#include "newgrf.h"
#include "newgrf_extension.h"
#include "settings_type.h"
#include "r3r_perf.h"
#include <vector>

#include "table/strings.h"

#include "safeguards.h"

/**
 * Determines the next articulated part to attach
 * @param index Position in chain
 * @param front_type Front engine type
 * @param front Front engine
 * @param mirrored Returns whether the part shall be flipped.
 * @return engine to add or EngineID::Invalid()
 */
static EngineID GetNextArticulatedPart(uint index, EngineID front_type, Vehicle *front = nullptr, bool *mirrored = nullptr)
{
	assert(front == nullptr || front->engine_type == front_type);

	const Engine *front_engine = Engine::Get(front_type);

	if (front_engine->type == VehicleType::Ship && !(front_engine->GetGRF() != nullptr && HasBit(front_engine->GetGRF()->observed_feature_tests, GFTOF_MULTI_PART_SHIPS))) {
		return EngineID::Invalid();
	}

	uint16_t callback = GetVehicleCallback(CBID_VEHICLE_ARTIC_ENGINE, index, 0, front_type, front);
	if (callback == CALLBACK_FAILED) return EngineID::Invalid();

	if (front_engine->GetGRF()->grf_version < 8) {
		/* 8 bits, bit 7 for mirroring */
		callback = GB(callback, 0, 8);
		if (callback == 0xFF) return EngineID::Invalid();
		if (mirrored != nullptr) *mirrored = HasBit(callback, 7);
		callback = GB(callback, 0, 7);
	} else {
		/* 15 bits, bit 14 for mirroring */
		if (callback == 0x7FFF) return EngineID::Invalid();
		if (mirrored != nullptr) *mirrored = HasBit(callback, 14);
		callback = GB(callback, 0, 14);
	}

	return GetNewEngineID(front_engine->GetGRF(), front_engine->type, callback);
}

/**
 * Does a NewGRF report that this should be an articulated vehicle?
 * @param engine_type The engine to check.
 * @return True iff the articulated engine callback flag is set.
 */
bool IsArticulatedEngine(EngineID engine_type)
{
	return EngInfo(engine_type)->callback_mask.Test(VehicleCallbackMask::ArticEngine);
}

/**
 * Count the number of articulated parts of an engine.
 * @param engine_type The engine to get the number of parts of.
 * @return The number of parts.
 */
uint CountArticulatedParts(EngineID engine_type)
{
	if (!EngInfo(engine_type)->callback_mask.Test(VehicleCallbackMask::ArticEngine)) return 0;

	Vehicle v(VehicleID::Invalid());
	v.engine_type = engine_type;
	v.owner = _current_company;

	uint i;
	for (i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		if (GetNextArticulatedPart(i, engine_type, &v) == EngineID::Invalid()) break;
	}

	return i - 1;
}

/**
 * Returns the default (non-refitted) cargo and capacity of a specific EngineID.
 * @param engine the EngineID of interest
 * @param attempt_refit cargo ID to attempt to use
 * @return cargo and capacity
 */
static inline std::pair<CargoType, uint> GetVehicleDefaultCapacity(EngineID engine, CargoType attempt_refit = INVALID_CARGO)
{
	const Engine *e = Engine::Get(engine);
	CargoType cargo = INVALID_CARGO;
	if (e->CanCarryCargo()) {
		if (attempt_refit != INVALID_CARGO && e->info.refit_mask.Test(attempt_refit)) {
			cargo = attempt_refit;
		} else {
			cargo = e->GetDefaultCargoType();
		}
	}
	return {cargo, IsValidCargoType(cargo) ? e->GetDisplayDefaultCapacity(nullptr, cargo) : 0};
}

/**
 * Returns all cargoes a vehicle can carry.
 * @param engine the EngineID of interest
 * @param include_initial_cargo_type if true the default cargo type of the vehicle is included; if false only the refit_mask
 * @return bit set of CargoTypes
 */
static inline CargoTypes GetAvailableVehicleCargoTypes(EngineID engine, bool include_initial_cargo_type)
{
	const Engine *e = Engine::Get(engine);
	if (!e->CanCarryCargo()) return {};

	CargoTypes cargoes = e->info.refit_mask;

	if (include_initial_cargo_type) {
		cargoes.Set(e->GetDefaultCargoType());
	}

	return cargoes;
}

/**
 * Get the capacity of the parts of a given engine.
 * @param engine The engine to get the capacities from.
 * @param attempt_refit Attempt to get capacity when refitting to this cargo.
 * @return The cargo capacities.
 */
CargoArray GetCapacityOfArticulatedParts(EngineID engine, CargoType attempt_refit)
{
	CargoArray capacity{};
	const Engine *e = Engine::Get(engine);

	auto get_engine_cargo = [&capacity, attempt_refit](EngineID eng) {
		if (auto [cargo, cap] = GetVehicleDefaultCapacity(eng, attempt_refit); IsValidCargoType(cargo)) {
			capacity[cargo] += cap;
		}
	};
	get_engine_cargo(engine);

	if (!e->IsArticulatedCallbackVehicleType()) return capacity;

	if (!e->info.callback_mask.Test(VehicleCallbackMask::ArticEngine)) return capacity;

	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		EngineID artic_engine = GetNextArticulatedPart(i, engine);
		if (artic_engine == EngineID::Invalid()) break;

		get_engine_cargo(artic_engine);
	}

	return capacity;
}

/**
 * Get the cargo mask of the parts of a given engine.
 * @param engine The engine to get the capacities from.
 * @return The cargo mask.
 */
CargoTypes GetCargoTypesOfArticulatedParts(EngineID engine)
{
	CargoTypes cargoes{};
	const Engine *e = Engine::Get(engine);

	if (auto [cargo, cap] = GetVehicleDefaultCapacity(engine); IsValidCargoType(cargo) && cap > 0) {
		cargoes.Set(cargo);
	}

	if (!e->IsArticulatedCallbackVehicleType()) return cargoes;

	if (!e->info.callback_mask.Test(VehicleCallbackMask::ArticEngine)) return cargoes;

	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		EngineID artic_engine = GetNextArticulatedPart(i, engine);
		if (artic_engine == EngineID::Invalid()) break;

		if (auto [cargo, cap] = GetVehicleDefaultCapacity(artic_engine); IsValidCargoType(cargo) && cap > 0) {
			cargoes.Set(cargo);
		}
	}

	return cargoes;
}

/**
 * Checks whether any of the articulated parts is refittable
 * @param engine the first part
 * @return true if refittable
 */
bool IsArticulatedVehicleRefittable(EngineID engine)
{
	if (IsEngineRefittable(engine)) return true;

	const Engine *e = Engine::Get(engine);
	if (!e->IsArticulatedCallbackVehicleType()) return false;

	if (!e->info.callback_mask.Test(VehicleCallbackMask::ArticEngine)) return false;

	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		EngineID artic_engine = GetNextArticulatedPart(i, engine);
		if (artic_engine == EngineID::Invalid()) break;

		if (IsEngineRefittable(artic_engine)) return true;
	}

	return false;
}

/**
 * Merges the refit_masks of all articulated parts.
 * @param engine the first part
 * @param include_initial_cargo_type if true the default cargo type of the vehicle is included; if false only the refit_mask
 * @param union_mask returns bit mask of CargoTypes which are a refit option for at least one articulated part
 * @param intersection_mask returns bit mask of CargoTypes which are a refit option for every articulated part (with default capacity > 0)
 */
void GetArticulatedRefitMasks(EngineID engine, bool include_initial_cargo_type, CargoTypes *union_mask, CargoTypes *intersection_mask)
{
	const Engine *e = Engine::Get(engine);
	CargoTypes veh_cargoes = GetAvailableVehicleCargoTypes(engine, include_initial_cargo_type);
	*union_mask = veh_cargoes;
	*intersection_mask = veh_cargoes.Any() ? veh_cargoes : ALL_CARGOTYPES;

	if (!e->IsArticulatedCallbackVehicleType()) return;
	if (!e->info.callback_mask.Test(VehicleCallbackMask::ArticEngine)) return;

	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		EngineID artic_engine = GetNextArticulatedPart(i, engine);
		if (artic_engine == EngineID::Invalid()) break;

		veh_cargoes = GetAvailableVehicleCargoTypes(artic_engine, include_initial_cargo_type);
		union_mask->Set(veh_cargoes);
		if (veh_cargoes.Any()) *intersection_mask = *intersection_mask & veh_cargoes;
	}
}
/**
 * Gets the individual refit_masks of each articulated part.
 * @param engine the first part
 * @return vector of cargo types
 */
std::vector<CargoTypes> GetArticulatedRefitMaskVector(EngineID engine, bool include_initial_cargo_type)
{
	std::vector<CargoTypes> output;

	const Engine *e = Engine::Get(engine);
	output.push_back(GetAvailableVehicleCargoTypes(engine, include_initial_cargo_type));

	if (!e->IsArticulatedCallbackVehicleType()) return output;
	if (!e->info.callback_mask.Test(VehicleCallbackMask::ArticEngine)) return output;

	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		EngineID artic_engine = GetNextArticulatedPart(i, engine);
		if (artic_engine == EngineID::Invalid()) break;

		output.push_back(GetAvailableVehicleCargoTypes(artic_engine, include_initial_cargo_type));
	}

	return output;
}

/**
 * Ors the refit_masks of all articulated parts.
 * @param engine the first part
 * @param include_initial_cargo_type if true the default cargo type of the vehicle is included; if false only the refit_mask
 * @return bit mask of CargoTypes which are a refit option for at least one articulated part
 */
CargoTypes GetUnionOfArticulatedRefitMasks(EngineID engine, bool include_initial_cargo_type)
{
	CargoTypes union_mask, intersection_mask;
	GetArticulatedRefitMasks(engine, include_initial_cargo_type, &union_mask, &intersection_mask);
	return union_mask;
}

/**
 * Get cargo mask of all cargoes carried by an articulated vehicle.
 * Note: Vehicles not carrying anything are ignored
 * @param v the first vehicle in the chain
 * @param cargo_type returns the common CargoType if needed. (INVALID_CARGO if no part is carrying something or they are carrying different things)
 * @return cargo mask, may be 0 if the no vehicle parts have cargo capacity
 */
CargoTypes GetCargoTypesOfArticulatedVehicle(const Vehicle *v, CargoType *cargo_type)
{
	CargoTypes cargoes{};
	CargoType first_cargo = INVALID_CARGO;

	do {
		if (v->cargo_type != INVALID_CARGO && v->GetEngine()->CanCarryCargo()) {
			if (first_cargo == INVALID_CARGO) first_cargo = v->cargo_type;
			if (first_cargo != v->cargo_type) {
				if (cargo_type != nullptr) {
					*cargo_type = INVALID_CARGO;
					cargo_type = nullptr;
				}
			}
		}

		v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr;
	} while (v != nullptr);

	if (cargo_type != nullptr) *cargo_type = first_cargo;
	return cargoes;
}

/**
 * Returns the overall cargo of an articulated vehicle if all parts are refitted to the same cargo.
 * Note: Vehicles not carrying anything are ignored
 * @param v the first vehicle in the chain
 * @return the common CargoType. (INVALID_CARGO if no part is carrying something or they are carrying different things)
 */
CargoType GetOverallCargoOfArticulatedVehicle(const Vehicle *v)
{
	CargoType cargo_id;
	GetCargoTypesOfArticulatedVehicle(v, &cargo_id);
	return cargo_id;
}

/**
 * Checks whether the specs of freshly build articulated vehicles are consistent with the information specified in the purchase list.
 * Only essential information is checked to leave room for magic tricks/workarounds to grfcoders.
 * It checks:
 *   For autoreplace/-renew:
 *    - Default cargo type (without capacity)
 *    - intersection and union of refit masks.
 * @param v The vehicle to check.
 */
void CheckConsistencyOfArticulatedVehicle(const Vehicle *v)
{
	/* R3R (第 455 轮, KI-653): 探针要用的原始头指针 —— v 在下面的 do-while 里被推进到 nullptr。 */
	const Vehicle *const head = v;
	const Engine *engine = v->GetEngine();

	CargoTypes purchase_refit_union, purchase_refit_intersection;
	GetArticulatedRefitMasks(v->engine_type, true, &purchase_refit_union, &purchase_refit_intersection);
	CargoArray purchase_default_capacity = GetCapacityOfArticulatedParts(v->engine_type);

	CargoTypes real_refit_union{};
	CargoTypes real_refit_intersection = ALL_CARGOTYPES;
	CargoTypes real_default_cargoes{};

	do {
		CargoTypes refit_mask = GetAvailableVehicleCargoTypes(v->engine_type, true);
		real_refit_union.Set(refit_mask);
		if (refit_mask.Any()) real_refit_intersection = real_refit_intersection & refit_mask;

		assert(v->cargo_type < NUM_CARGO || (v->type == VehicleType::Train && Train::From(v)->IsVirtual()));
		if (v->cargo_cap > 0) real_default_cargoes.Set(v->cargo_type);

		/* R3R (第 455 轮, KI-653 修正): 这里沿用原生的 HasArticulatedPart() 即可。
		 * vehicle_base.h 的该谓词是 Next()->IsArticGroupMember()，而
		 * Train::IsArticGroupMember()（train.h:212-215）本身就是
		 * "IsArticulatedPart() || ArticGroupMember 位" ⇒ 真 artic 部件与 R3R 打散组
		 * 都能走到，"只认组角色位" 的推断是错的（见备忘 §11.3bis 修正）。
		 * 因此本函数不是需求 6 的成因，保留原写法以免与上游分叉。 */
		v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : nullptr;
	} while (v != nullptr);

	/* Check whether the vehicle carries more cargoes than expected */
	bool carries_more = false;
	for (CargoType cargo_type : real_default_cargoes) {
		if (purchase_default_capacity[cargo_type] == 0) {
			carries_more = true;
			break;
		}
	}

	/* R3R (第 459 轮, KI-653): 命中判据单独算一次，并在下面**无条件**打一行。
	 * 原有 BUY-REFIT 系列整块在这条 if 内部 ⇒ 买了不弹窗的车一行日志都没有，于是
	 * "日志里没有 BUY-REFIT"既可能是「没命中」也可能是「这版 exe 的探针没编进去」，
	 * 两种结论相反却不可区分。CONSIST-CHK 是负判据的地基。零行为变更。 */
	const bool r3r_hit = (real_refit_union != purchase_refit_union)
			|| (real_refit_intersection != purchase_refit_intersection) || carries_more;
	{
		uint r3r_n = 0;
		for (const Vehicle *w = head; w != nullptr; w = (w->HasArticulatedPart() ? w->GetNextArticulatedPart() : nullptr)) r3r_n++;
		uint r3r_pn = 1;
		for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
			if (GetNextArticulatedPart(i, head->engine_type) == EngineID::Invalid()) break;
			r3r_pn++;
		}
		R3RDbgWrite("CONSIST-CHK veh=%u eng=%u n=%u pn=%u hit=%d more=%d ru=%08X pu=%08X ri=%08X pi=%08X dflt=%08X\n",
				(uint)head->index.base(), (uint)head->engine_type.base(), r3r_n, r3r_pn,
				r3r_hit ? 1 : 0, carries_more ? 1 : 0,
				real_refit_union.base(), purchase_refit_union.base(),
				real_refit_intersection.base(), purchase_refit_intersection.base(),
				real_default_cargoes.base());
	}

	/* show a warning once for each GRF after each game load */
	if (r3r_hit) {
		/* R3R (第 455 轮, KI-653): 只读取证。玩家报「R3R 版购买列车会弹原生不弹的
		 * "货物/改装信息与购买后不同"」。这一行落在唯一触发点上，输出两侧掩码与
		 * 实际走到的节数，用来判定不一致是「节数少了（artic 链断了）」还是
		 * 「同一节的 refit 掩码本身不同（引擎数据被改）」。 */
		uint n_parts = 0;
		for (const Vehicle *w = head; w != nullptr; w = (w->HasArticulatedPart() ? w->GetNextArticulatedPart() : nullptr)) n_parts++;
		/* 购买侧的节数：与 GetArticulatedRefitMasks() 内部同一条引擎回调链，但 front 传
		 * nullptr（购买列表的口径）。n != pn 才是"实车链与购买链长度不同"的直接证据。 */
		uint purchase_parts = 1;
		for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
			if (GetNextArticulatedPart(i, head->engine_type) == EngineID::Invalid()) break;
			purchase_parts++;
		}
		R3RDbgWrite("BUY-REFIT eng=%u veh=%u n=%u pn=%u pu=%08X pi=%08X ru=%08X ri=%08X more=%d dflt=%08X\n",
				(uint)head->engine_type.base(), (uint)head->index.base(), n_parts, purchase_parts,
				purchase_refit_union.base(), purchase_refit_intersection.base(),
				real_refit_union.base(), real_refit_intersection.base(),
				carries_more ? 1 : 0, real_default_cargoes.base());
		/* 逐节对照：实车侧每条车的引擎/货种/容量/掩码，并把该货种在购买侧的默认容量
		 * 一起打出来 —— carries_more 的真正成因就在 pcap==0 的那一节上。 */
		uint part_no = 0;
		for (const Vehicle *w = head; w != nullptr && part_no < MAX_ARTICULATED_PARTS;
				w = (w->HasArticulatedPart() ? w->GetNextArticulatedPart() : nullptr), part_no++) {
			const CargoType cargo = w->cargo_type;
			R3RDbgWrite("BUY-REFIT-PART i=%u eng=%u cargo=%u cap=%u mask=%08X pcap=%u\n",
					part_no, (uint)w->engine_type.base(), (uint)cargo, w->cargo_cap,
					GetAvailableVehicleCargoTypes(w->engine_type, true).base(),
					IsValidCargoType(cargo) ? purchase_default_capacity[cargo] : 0);
		}
		/* 购买侧的每一节引擎（front=nullptr 口径），用于和上面的 eng= 逐节对照。 */
		R3RDbgWrite("BUY-REFIT-PP i=0 eng=%u\n", (uint)head->engine_type.base());
		for (uint i = 1; i < purchase_parts; i++) {
			R3RDbgWrite("BUY-REFIT-PP i=%u eng=%u\n", i, (uint)GetNextArticulatedPart(i, head->engine_type).base());
		}
		ShowNewGrfVehicleError(engine->index, STR_NEWGRF_BUGGY, STR_NEWGRF_BUGGY_ARTICULATED_CARGO, GRFBug::VehRefit, false);
	}
}

/**
 * Add the remaining articulated parts to the given vehicle.
 * @param first The head of the articulated bit.
 */
void AddArticulatedParts(Vehicle *first)
{
	VehicleType type = first->type;
	if (!EngInfo(first->engine_type)->callback_mask.Test(VehicleCallbackMask::ArticEngine)) return;

	Vehicle *v = first;
	for (uint i = 1; i < MAX_ARTICULATED_PARTS; i++) {
		bool flip_image;
		EngineID engine_type = GetNextArticulatedPart(i, first->engine_type, first, &flip_image);
		if (engine_type == EngineID::Invalid()) return;

		/* In the (very rare) case the GRF reported wrong number of articulated parts
		 * and we run out of available vehicles, bail out. */
		if (!Vehicle::CanAllocateItem()) return;

		GroundVehicleCache *gcache = nullptr;
		if (type == VehicleType::Train || type == VehicleType::Road) {
			gcache = v->GetGroundVehicleCache();
			gcache->first_engine = v->engine_type; // Needs to be set before first callback
		}

		const Engine *e_artic = Engine::Get(engine_type);
		switch (type) {
			default: NOT_REACHED();

			case VehicleType::Train: {
				Train *front = Train::From(first);
				Train *t = Train::Create();
				v->SetNext(t);
				v = t;

				t->subtype = 0;
				t->track = front->track;
				t->railtypes = front->railtypes;
				t->flags.Set(VehicleRailFlag::AllowedOnNormalRail, _settings_game.vehicle.disable_elrails && RailVehInfo(front->engine_type)->intended_railtypes.Test(RAILTYPE_ELECTRIC));

				t->spritenum = e_artic->VehInfo<RailVehicleInfo>().image_index;
				if (e_artic->CanCarryCargo()) {
					t->cargo_type = e_artic->GetDefaultCargoType();
					t->cargo_cap = e_artic->VehInfo<RailVehicleInfo>().capacity;  // Callback 36 is called when the consist is finished
				} else {
					t->cargo_type = front->cargo_type; // Needed for livery selection
					t->cargo_cap = 0;
				}
				t->refit_cap = 0;

				if (front->IsVirtual()) t->SetVirtual();

				t->SetArticulatedPart();
				break;
			}

			case VehicleType::Road: {
				RoadVehicle *front = RoadVehicle::From(first);
				RoadVehicle *rv = RoadVehicle::Create();
				v->SetNext(rv);
				v = rv;

				rv->subtype = 0;
				gcache->cached_veh_length = VEHICLE_LENGTH; // Callback is called when the consist is finished
				rv->state = RVSB_IN_DEPOT;

				rv->roadtype = front->roadtype;
				rv->compatible_roadtypes = front->compatible_roadtypes;

				rv->spritenum = e_artic->VehInfo<RoadVehicleInfo>().image_index;
				if (e_artic->CanCarryCargo()) {
					rv->cargo_type = e_artic->GetDefaultCargoType();
					assert(IsValidCargoType(rv->cargo_type));
					rv->cargo_cap = e_artic->VehInfo<RoadVehicleInfo>().capacity;  // Callback 36 is called when the consist is finished
				} else {
					rv->cargo_type = front->cargo_type; // Needed for livery selection
					rv->cargo_cap = 0;
				}
				rv->refit_cap = 0;

				rv->SetArticulatedPart();
				break;
			}

			case VehicleType::Ship: {
				Ship *front = Ship::From(first);
				Ship *s = Ship::Create();
				v->SetNext(s);
				v = s;

				s->direction = Direction::N;
				s->rotation = s->direction;
				s->x_pos = 0;
				s->y_pos = 0;
				s->z_pos = 0;
				s->vehstatus = {VehState::Hidden, VehState::Unclickable};
				s->subtype = (1 << GVSF_VIRTUAL);

				if (e_artic->CanCarryCargo()) {
					s->cargo_type = e_artic->GetDefaultCargoType();
					s->cargo_cap = e_artic->VehInfo<ShipVehicleInfo>().capacity;  // Callback 36 is called when the consist is finished
				} else {
					s->cargo_type = front->cargo_type;
					s->cargo_cap = 0;
				}
				break;
			}
		}

		/* get common values from first engine */
		v->owner = first->owner;
		v->date_of_last_service = first->date_of_last_service;
		v->date_of_last_service_newgrf = first->date_of_last_service_newgrf;
		v->build_year = first->build_year;

		v->cargo_subtype = 0;
		/* R3R（第 433 轮 / KI-628）：车厢也有使用寿命 —— GRF 与默认表里车厢引擎本就有 lifelength
		 * （例如默认 Passenger Carriage = 20 年，见 table/engines.h 的 MW() 第 3 参），上游这里
		 * 一律清 0，才让"车厢/纯车底段寿命 0 年"。列车的铰接部件是真实车厢，跟随自己的引擎寿命；
		 * 其它载具类型的铰接部件是虚拟部件（GVSF_VIRTUAL），可见性与 primary 判定完全不同，仍保持 0。 */
		v->max_age = (type == VehicleType::Train) ? e_artic->GetLifeLengthInDays() : CalTime::DateDelta{0};
		v->engine_type = engine_type;
		v->value = 0;
		v->random_bits = Random();

		if (type == VehicleType::Ship) continue;

		v->direction = first->direction;
		v->tile = first->tile;
		v->x_pos = first->x_pos;
		v->y_pos = first->y_pos;
		v->z_pos = first->z_pos;
		v->vehstatus = first->vehstatus;
		v->vehstatus.Reset(VehState::Stopped);

		v->sprite_seq.Set(SPR_IMG_QUERY);

		if (flip_image) v->spritenum++;

		if (v->type == VehicleType::Train) {
			auto prob = TestVehicleBuildProbability(v, BuildProbabilityType::Reversed);
			if (prob.has_value()) Train::From(v)->flags.Set(VehicleRailFlag::Flipped, prob.value());
		}
		v->UpdatePosition();
	}
}
