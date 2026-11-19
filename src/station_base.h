/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_base.h Base classes/functions for stations. */

#ifndef STATION_BASE_H
#define STATION_BASE_H

#include "core/random_func.hpp"
#include "base_station_base.h"
#include "newgrf_airport.h"
#include "cargopacket.h"
#include "industry_type.h"
#include "linkgraph/linkgraph_type.h"
#include "newgrf_storage.h"
#include "3rdparty/cpp-btree/btree_map.h"
#include "3rdparty/cpp-btree/btree_set.h"
#include "bitmap_type.h"
#include "core/alignment.hpp"
#include "core/alloc_func.hpp"
#include "strings_id_type.h"
#include "vehicle_type.h"
#include <vector>
#include <array>
#include <iterator>
#include <algorithm>

static const uint8_t INITIAL_STATION_RATING = 175;
static const uint8_t MAX_STATION_RATING = 255;

static const uint MAX_EXTRA_STATION_NAMES = 1024;

/** Extra station name string flags. */
enum ExtraStationNameInfoFlags {
	/* Bits 0 - 5 used for StationNaming enum */
	ESNIF_CENTRAL               =  8,
	ESNIF_NOT_CENTRAL           =  9,
	ESNIF_NEAR_WATER            = 10,
	ESNIF_NOT_NEAR_WATER        = 11,
};

/** Extra station name string */
struct ExtraStationNameInfo {
	StringID str;
	uint16_t flags;
};

extern std::vector<ExtraStationNameInfo> _extra_station_names;
extern uint8_t _extra_station_names_probability;

class FlowStatMap;

extern const StationCargoList _empty_cargo_list;
extern const FlowStatMap _empty_flows;

/**
 * Flow statistics telling how much flow should be sent along a link. This is
 * done by creating "flow shares" and using std::map's upper_bound() method to
 * look them up with a random number. A flow share is the difference between a
 * key in a map and the previous key. So one key in the map doesn't actually
 * mean anything by itself.
 */
class FlowStat {
	friend FlowStatMap;
public:
	struct ShareEntry {
#if OTTD_ALIGNMENT == 0
		unaligned_uint32 first;
#else
		uint32_t first;
#endif
		StationID second;
	};
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) && !defined(__clang__))
	static_assert(sizeof(ShareEntry) == 6, "");
#endif

	friend bool operator<(const ShareEntry &a, const ShareEntry &b) noexcept
	{
		return a.first < b.first;
	}

	friend bool operator<(uint a, const ShareEntry &b) noexcept
	{
		return a < b.first;
	}

	typedef ShareEntry* iterator;
	typedef const ShareEntry* const_iterator;

	/**
	 * Invalid constructor. This can't be called as a FlowStat must not be
	 * empty.
	 */
	FlowStat() = delete;

	/**
	 * Create a FlowStat with an initial entry.
	 * @param origin Origin station for this flow.
	 * @param via Station the initial entry refers to.
	 * @param flow Amount of flow for the initial entry.
	 * @param restricted If the flow to be added is restricted.
	 */
	inline FlowStat(StationID origin, StationID via, uint flow, bool restricted = false)
	{
		assert(flow > 0);
		this->storage.inline_shares[0].first = flow;
		this->storage.inline_shares[0].second = via;
		this->unrestricted = restricted ? 0 : flow;
		this->count = 1;
		this->origin = origin;
		this->flags = 0;
	}

private:
	inline bool inline_mode() const
	{
		return this->count <= 2;
	}

	inline const ShareEntry *data() const
	{
		return this->inline_mode() ? this->storage.inline_shares : this->storage.ptr_shares.buffer;
	}

	inline ShareEntry *data()
	{
		return const_cast<ShareEntry *>(const_cast<const FlowStat*>(this)->data());
	}

	inline void clear()
	{
		if (!inline_mode()) {
			free(this->storage.ptr_shares.buffer);
		}
		this->count = 0;
		this->flags = 0;
	}

	iterator erase_item(iterator iter, uint flow_reduction);

	inline void MoveCommon(FlowStat &&other) noexcept
	{
		this->storage = std::move(other.storage);
		this->count = other.count;
		other.count = 0; // Take ownership of any storage ptr
		this->unrestricted = other.unrestricted;
		this->origin = other.origin;
		this->flags = other.flags;
	}

	inline void CopyCommon(const FlowStat &other)
	{
		this->count = other.count;
		if (!other.inline_mode()) {
			this->storage.ptr_shares.elem_capacity = other.storage.ptr_shares.elem_capacity;
			this->storage.ptr_shares.buffer = MallocT<ShareEntry>(other.storage.ptr_shares.elem_capacity);
		}
		MemCpyT(this->data(), other.data(), this->count);
		this->unrestricted = other.unrestricted;
		this->origin = other.origin;
		this->flags = other.flags;
	}

public:
	inline FlowStat(const FlowStat &other)
	{
		this->CopyCommon(other);
	}

	inline FlowStat(FlowStat &&other) noexcept
	{
		this->MoveCommon(std::move(other));
	}

	inline ~FlowStat()
	{
		this->clear();
	}

	inline FlowStat &operator=(const FlowStat &other)
	{
		this->clear();
		this->CopyCommon(other);
		return *this;
	}

	inline FlowStat &operator=(FlowStat &&other) noexcept
	{
		this->clear();
		this->MoveCommon(std::move(other));
		return *this;
	}

	inline size_t size() const { return this->count; }
	inline bool empty() const { return this->count == 0; }
	inline iterator begin() { return this->data(); }
	inline const_iterator begin() const { return this->data(); }
	inline iterator end() { return this->data() + this->count; }
	inline const_iterator end() const { return this->data() + this->count; }
	inline iterator upper_bound(uint32_t key) { return std::upper_bound(this->begin(), this->end(), key); }
	inline const_iterator upper_bound(uint32_t key) const { return std::upper_bound(this->begin(), this->end(), key); }

	/**
	 * Add some flow to the end of the shares map. Only do that if you know
	 * that the station isn't in the map yet. Anything else may lead to
	 * inconsistencies.
	 * @param st Remote station.
	 * @param flow Amount of flow to be added.
	 * @param restricted If the flow to be added is restricted.
	 */
	inline void AppendShare(StationID st, uint flow, bool restricted = false)
	{
		assert(flow > 0);
		uint32_t key = this->GetLastKey() + flow;
		if (unlikely(this->count >= 2)) {
			if (this->count == 2) {
				// convert inline buffer to ptr
				ShareEntry *ptr = MallocT<ShareEntry>(4);
				ptr[0] = this->storage.inline_shares[0];
				ptr[1] = this->storage.inline_shares[1];
				this->storage.ptr_shares.buffer = ptr;
				this->storage.ptr_shares.elem_capacity = 4;
			} else if (this->count == this->storage.ptr_shares.elem_capacity) {
				// grow buffer
				uint16_t new_size = this->storage.ptr_shares.elem_capacity * 2;
				this->storage.ptr_shares.buffer = ReallocT<ShareEntry>(this->storage.ptr_shares.buffer, new_size);
				this->storage.ptr_shares.elem_capacity = new_size;
			}
			this->storage.ptr_shares.buffer[this->count] = { key, st };
		} else {
			this->storage.inline_shares[this->count] = { key, st };
		}
		this->count++;
		if (!restricted) this->unrestricted += flow;
	}

	uint GetShare(StationID st) const;

	void ChangeShare(StationID st, int flow);

	void RestrictShare(StationID st);

	void ReleaseShare(StationID st);

	void ScaleToMonthly(uint runtime, uint8_t day_length_factor);

	/**
	 * Return total amount of unrestricted shares.
	 * @return Amount of unrestricted shares.
	 */
	inline uint GetUnrestricted() const { return this->unrestricted; }

	/**
	 * Swap the shares maps, and thus the content of this FlowStat with the
	 * other one.
	 * @param other FlowStat to swap with.
	 */
	inline void SwapShares(FlowStat &other)
	{
		std::swap(this->storage, other.storage);
		std::swap(this->unrestricted, other.unrestricted);
		std::swap(this->count, other.count);
		std::swap(this->flags, other.flags);
	}

	/**
	 * Get a station a package can be routed to. This done by drawing a
	 * random number between 0 and sum_shares and then looking that up in
	 * the map with lower_bound. So each share gets selected with a
	 * probability dependent on its flow. Do include restricted flows here.
	 * @param is_restricted Output if a restricted flow was chosen.
	 * @return A station ID from the shares map.
	 */
	inline StationID GetViaWithRestricted(bool &is_restricted) const
	{
		assert(!this->empty());
		uint rand = RandomRange(this->GetLastKey());
		is_restricted = rand >= this->unrestricted;
		return this->upper_bound(rand)->second;
	}

	/**
	 * Get a station a package can be routed to. This done by drawing a
	 * random number between 0 and sum_shares and then looking that up in
	 * the map with lower_bound. So each share gets selected with a
	 * probability dependent on its flow. Don't include restricted flows.
	 * @return A station ID from the shares map.
	 */
	inline StationID GetVia() const
	{
		assert(!this->empty());
		return this->unrestricted > 0 ?
				this->upper_bound(RandomRange(this->unrestricted))->second :
				StationID::Invalid();
	}

	StationID GetVia(StationID excluded, StationID excluded2 = StationID::Invalid()) const;

	/**
	 * Mark this flow stat as invalid, such that it is not included in link statistics.
	 * @return True if the flow stat should be deleted.
	 */
	inline bool Invalidate()
	{
		if ((this->flags & 0x1F) == 0x1F) return true;
		this->flags++;
		return false;
	}

	inline StationID GetOrigin() const
	{
		return this->origin;
	}

	inline bool IsInvalid() const
	{
		return (this->flags & 0x1F) != 0;
	}

	/* for save/load use only */
	inline uint16_t GetRawFlags() const
	{
		return this->flags;
	}

	/* for save/load use only */
	inline void SetRawFlags(uint16_t flags)
	{
		this->flags = flags;
	}

private:
	uint32_t GetLastKey() const
	{
		return this->data()[this->count - 1].first;
	}

	struct ptr_buffer {
		ShareEntry *buffer;
		uint16_t elem_capacity;
	}
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) || defined(__clang__))
	__attribute__((packed, aligned(4)))
#endif
	;
	union storage_union {
		ShareEntry inline_shares[2]; ///< Small buffer optimisation: size = 1 is ~90%, size = 2 is ~9%, size >= 3 is ~1%
		ptr_buffer ptr_shares;

		// Actual construction/destruction done by class FlowStat
		storage_union() {}
		~storage_union() {}
	};
	storage_union storage; ///< Shares of flow to be sent via specified station (or consumed locally).
	uint unrestricted; ///< Limit for unrestricted shares.
	uint16_t count;
	StationID origin;
	uint16_t flags;
};
static_assert(std::is_nothrow_move_constructible<FlowStat>::value, "FlowStat must be nothrow move constructible");
#if OTTD_ALIGNMENT == 0 && (defined(__GNUC__) && !defined(__clang__))
static_assert(sizeof(FlowStat) == 24, "");
#endif

template <typename cv_value, typename cv_container, typename cv_index_iter>
class FlowStatMapIterator
{
	friend FlowStatMap;
	friend FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator>;
	friend FlowStatMapIterator<const FlowStat, const FlowStatMap, btree::btree_map<StationID, uint16_t>::const_iterator>;
public:
	typedef FlowStat value_type;
	typedef cv_value& reference;
	typedef cv_value* pointer;
	typedef ptrdiff_t difference_type;
	typedef std::forward_iterator_tag iterator_category;

	FlowStatMapIterator(cv_container *fsm, cv_index_iter current) :
		fsm(fsm), current(current) {}

	FlowStatMapIterator(const FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator> &other) :
		fsm(other.fsm), current(other.current) {}

	FlowStatMapIterator &operator=(const FlowStatMapIterator &) = default;

	reference operator*() const { return this->fsm->flows_storage[this->current->second]; }
	pointer operator->() const { return &(this->fsm->flows_storage[this->current->second]); }

	FlowStatMapIterator& operator++()
	{
		++this->current;
		return *this;
	}

	bool operator==(const FlowStatMapIterator& rhs) const { return this->current == rhs.current; }
	bool operator!=(const FlowStatMapIterator& rhs) const { return !(operator==(rhs)); }

private:
	cv_container *fsm;
	cv_index_iter current;
};

/** Flow descriptions by origin stations. */
class FlowStatMap {
	std::vector<FlowStat> flows_storage;
	btree::btree_map<StationID, uint16_t> flows_index;

public:
	using iterator = FlowStatMapIterator<FlowStat, FlowStatMap, btree::btree_map<StationID, uint16_t>::iterator>;
	using const_iterator = FlowStatMapIterator<const FlowStat, const FlowStatMap, btree::btree_map<StationID, uint16_t>::const_iterator>;

	friend iterator;
	friend const_iterator;

	uint GetFlow() const;
	uint GetFlowVia(StationID via) const;
	uint GetFlowFrom(StationID from) const;
	uint GetFlowFromVia(StationID from, StationID via) const;

	void AddFlow(StationID origin, StationID via, uint amount);
	void PassOnFlow(StationID origin, StationID via, uint amount);
	StationIDVector DeleteFlows(StationID via);
	void RestrictFlows(StationID via);
	void FinalizeLocalConsumption(StationID self);

private:
	btree::btree_map<StationID, uint16_t>::iterator erase_priv(btree::btree_map<StationID, uint16_t>::iterator iter)
	{
		uint16_t index = iter->second;
		iter = this->flows_index.erase(iter);
		if (index != this->flows_storage.size() - 1) {
			this->flows_storage[index] = std::move(this->flows_storage.back());
			this->flows_index[this->flows_storage[index].GetOrigin()] = index;
		}
		this->flows_storage.pop_back();
		return iter;
	}

public:
	iterator begin() { return iterator(this, this->flows_index.begin()); }
	const_iterator begin() const { return const_iterator(this, this->flows_index.begin()); }
	iterator end() { return iterator(this, this->flows_index.end()); }
	const_iterator end() const { return const_iterator(this, this->flows_index.end()); }

	iterator find(StationID from)
	{
		return iterator(this, this->flows_index.find(from));
	}
	const_iterator find(StationID from) const
	{
		return const_iterator(this, this->flows_index.find(from));
	}

	bool empty() const
	{
		return this->flows_storage.empty();
	}

	size_t size() const
	{
		return this->flows_storage.size();
	}

	void erase(StationID st)
	{
		auto iter = this->flows_index.find(st);
		if (iter != this->flows_index.end()) {
			this->erase_priv(iter);
		}
	}

	iterator erase(iterator iter)
	{
		return iterator(this, this->erase_priv(iter.current));
	}

	std::pair<iterator, bool> insert(FlowStat flow_stat)
	{
		StationID st = flow_stat.GetOrigin();
		auto res = this->flows_index.insert(std::pair<StationID, uint16_t>(st, (uint16_t)this->flows_storage.size()));
		if (res.second) {
			this->flows_storage.push_back(std::move(flow_stat));
		}
		return std::make_pair(iterator(this, res.first), res.second);
	}

	iterator insert(iterator hint, FlowStat flow_stat)
	{
		auto res = this->flows_index.insert(hint.current, std::pair<StationID, uint16_t>(flow_stat.GetOrigin(), (uint16_t)this->flows_storage.size()));
		if (res->second == this->flows_storage.size()) {
			this->flows_storage.push_back(std::move(flow_stat));
		}
		return iterator(this, res);
	}

	StationID FirstStationID() const
	{
		return this->flows_index.begin()->first;
	}

	void reserve(size_t size)
	{
		this->flows_storage.reserve(size);
	}

	void SortStorage();

	std::span<const FlowStat> IterateUnordered() const
	{
		return std::span<const FlowStat>(this->flows_storage.data(), this->flows_storage.size());
	}
};

struct GoodsEntryData {
	StationCargoList cargo{}; ///< The cargo packets of cargo waiting in this station
	FlowStatMap flows{};      ///< Planned flows through this station.

	bool MayBeRemoved() const
	{
		return this->cargo.Packets()->MapSize() == 0 && this->cargo.ReservedCount() == 0 && this->flows.empty();
	}
};

/**
 * Stores station stats for a single cargo.
 */
struct GoodsEntry {
	/** Status of this cargo for the station. */
	enum class State : uint8_t {
		/**
		 * Set when the station accepts the cargo currently for final deliveries.
		 * It is updated every STATION_ACCEPTANCE_TICKS ticks by checking surrounding tiles for acceptance >= 8/8.
		 */
		Acceptance = 0,

		/**
		 * This indicates whether a cargo has a rating at the station.
		 * Set when cargo was ever waiting at the station.
		 * It is set when cargo supplied by surrounding tiles is moved to the station, or when
		 * arriving vehicles unload/transfer cargo without it being a final delivery.
		 *
		 * This flag is cleared after 255 * STATION_RATING_TICKS of not having seen a pickup.
		 */
		Rating = 1,

		/**
		 * Set when a vehicle ever delivered cargo to the station for final delivery.
		 * This flag is never cleared.
		 */
		EverAccepted = 2,

		/**
		 * Set when cargo was delivered for final delivery last month.
		 * This flag is set to the value of State::CurrentMonth at the start of each month.
		 */
		LastMonth = 3,

		/**
		 * Set when cargo was delivered for final delivery this month.
		 * This flag is reset on the beginning of every month.
		 */
		CurrentMonth = 4,

		/**
		 * Set when cargo was delivered for final delivery during the current STATION_ACCEPTANCE_TICKS interval.
		 * This flag is reset every STATION_ACCEPTANCE_TICKS ticks.
		 */
		AcceptedBigtick = 5,

		/**
		 * Set when cargo is not permitted to be supplied by nearby industries/houses.
		 */
		NoCargoSupply = 7,
	};
	using States = EnumBitSet<State, uint8_t>;

	GoodsEntry() :
		status(0),
		time_since_pickup(255),
		last_vehicle_type(VehicleType::Invalid),
		rating(INITIAL_STATION_RATING),
		last_speed(0),
		last_age(255),
		amount_fract(0),
		link_graph(LinkGraphID::Invalid()),
		node(INVALID_NODE),
		max_waiting_cargo(0)
	{}

	States status{}; ///< Status of this cargo, see #State.

	/**
	 * Number of rating-intervals (up to 255) since the last vehicle tried to load this cargo.
	 * The unit used is STATION_RATING_TICKS.
	 * This does not imply there was any cargo to load.
	 */
	uint8_t time_since_pickup;

	VehicleType last_vehicle_type = VehicleType::Invalid;

	uint8_t rating;         ///< %Station rating for this cargo.

	/**
	 * Maximum speed (up to 255) of the last vehicle that tried to load this cargo.
	 * This does not imply there was any cargo to load.
	 * The unit used is a special vehicle-specific speed unit for station ratings.
	 *  - Trains: km-ish/h
	 *  - RV: km-ish/h
	 *  - Ships: 0.5 * km-ish/h
	 *  - Aircraft: 8 * mph
	 */
	uint8_t last_speed;

	/**
	 * Age in years (up to 255) of the last vehicle that tried to load this cargo.
	 * This does not imply there was any cargo to load.
	 */
	uint8_t last_age;

	uint8_t amount_fract;   ///< Fractional part of the amount in the cargo list

	std::unique_ptr<GoodsEntryData> data;

	LinkGraphID link_graph; ///< Link graph this station belongs to.
	NodeID node;            ///< ID of node in link graph referring to this goods entry.

	uint max_waiting_cargo; ///< Max cargo from this station waiting at any station.

	bool IsSupplyAllowed() const
	{
		return !this->status.Test(GoodsEntry::State::NoCargoSupply);
	}

	/**
	 * Reports whether a vehicle has ever tried to load the cargo at this station.
	 * This does not imply that there was cargo available for loading. Refer to GoodsEntry::State::Rating for that.
	 * @return true if vehicle tried to load.
	 */
	bool HasVehicleEverTriedLoading() const { return this->last_speed != 0; }

	/**
	 * Does this cargo have a rating at this station?
	 * @return true if the cargo has a rating, i.e. cargo has been moved to the station.
	 */
	inline bool HasRating() const
	{
		return this->status.Test(GoodsEntry::State::Rating);
	}

	/**
	 * Get the best next hop for a cargo packet from station source.
	 * @param source Source of the packet.
	 * @return The chosen next hop or StationID::Invalid() if none was found.
	 */
	inline StationID GetVia(StationID source) const
	{
		if (this->data == nullptr) return StationID::Invalid();

		FlowStatMap::const_iterator flow_it(this->data->flows.find(source));
		return flow_it != this->data->flows.end() ? flow_it->GetVia() : StationID::Invalid();
	}

	/**
	 * Get the best next hop for a cargo packet from station source, optionally
	 * excluding one or two stations.
	 * @param source Source of the packet.
	 * @param excluded If this station would be chosen choose the second best one instead.
	 * @param excluded2 Second station to be excluded, if != StationID::Invalid().
	 * @return The chosen next hop or StationID::Invalid() if none was found.
	 */
	inline StationID GetVia(StationID source, StationID excluded, StationID excluded2 = StationID::Invalid()) const
	{
		if (this->data == nullptr) return StationID::Invalid();

		FlowStatMap::const_iterator flow_it(this->data->flows.find(source));
		return flow_it != this->data->flows.end() ? flow_it->GetVia(excluded, excluded2) : StationID::Invalid();
	}

	GoodsEntryData &CreateData()
	{
		if (this->data == nullptr) this->data.reset(new GoodsEntryData());
		return *this->data;
	}

	const GoodsEntryData &CreateData() const
	{
		if (this->data == nullptr) const_cast<GoodsEntry *>(this)->data.reset(new GoodsEntryData());
		return *this->data;
	}

	inline uint CargoAvailableCount() const
	{
		return this->data != nullptr ? this->data->cargo.AvailableCount() : 0;
	}

	inline uint CargoReservedCount() const
	{
		return this->data != nullptr ? this->data->cargo.ReservedCount() : 0;
	}

	inline uint CargoTotalCount() const
	{
		return this->data != nullptr ? this->data->cargo.TotalCount() : 0;
	}

	inline uint CargoAvailableViaCount(StationID next) const
	{
		return this->data != nullptr ? this->data->cargo.AvailableViaCount(next) : 0;
	}

	const StationCargoList &ConstCargoList() const
	{
		return this->data != nullptr ? this->data->cargo : _empty_cargo_list;
	}

	const FlowStatMap &ConstFlows() const
	{
		return this->data != nullptr ? this->data->flows : _empty_flows;
	}

	void RemoveDataIfUnused()
	{
		if (this->data != nullptr && this->data->MayBeRemoved()) this->data.reset();
	}

	uint8_t ConvertState() const;
};

/** All airport-related information. Only valid if tile != INVALID_TILE. */
struct Airport : public TileArea {
	Airport() : TileArea(INVALID_TILE, 0, 0) {}

	AirportBlocks blocks{};                  ///< stores which blocks on the airport are taken. was 16 bit earlier on, then 32
	uint8_t type = 0;                        ///< Type of this airport, @see AirportTypes
	uint8_t layout = 0;                      ///< Airport layout number.
	Direction rotation = Direction::Invalid; ///< How this airport is rotated.

	PersistentStorage *psa = nullptr; ///< Persistent storage for NewGRF airports.

	/**
	 * Get the AirportSpec that from the airport type of this airport. If there
	 * is no airport (\c tile == INVALID_TILE) then return the dummy AirportSpec.
	 * @return The AirportSpec for this airport.
	 */
	const AirportSpec *GetSpec() const
	{
		if (this->tile == INVALID_TILE) return &AirportSpec::dummy;
		return AirportSpec::Get(this->type);
	}

	/**
	 * Get the finite-state machine for this airport or the finite-state machine
	 * for the dummy airport in case this isn't an airport.
	 * @pre this->type < NEW_AIRPORT_OFFSET.
	 * @return The state machine for this airport.
	 */
	const AirportFTAClass *GetFTA() const
	{
		return this->GetSpec()->fsm;
	}

	/**
	 * Check if this airport has at least one hangar.
	 * @return \c true iff there are one or more hangars.
	 */
	inline bool HasHangar() const
	{
		return !this->GetSpec()->depots.empty();
	}

	/**
	 * Add the tileoffset to the base tile of this airport but rotate it first.
	 * The base tile is the northernmost tile of this airport. This function
	 * helps to make sure that getting the tile of a hangar works even for
	 * rotated airport layouts without requiring a rotated array of hangar tiles.
	 * @param tidc The tilediff to add to the airport tile.
	 * @return The tile of this airport plus the rotated offset.
	 */
	inline TileIndex GetRotatedTileFromOffset(TileIndexDiffC tidc) const
	{
		const AirportSpec *as = this->GetSpec();
		switch (this->rotation) {
			case Direction::N: return this->tile + ToTileIndexDiff(tidc);

			case Direction::E: return this->tile + TileDiffXY(tidc.y, as->size_x - 1 - tidc.x);

			case Direction::S: return this->tile + TileDiffXY(as->size_x - 1 - tidc.x, as->size_y - 1 - tidc.y);

			case Direction::W: return this->tile + TileDiffXY(as->size_y - 1 - tidc.y, tidc.x);

			default: NOT_REACHED();
		}
	}

	/**
	 * Get the first tile of the given hangar.
	 * @param hangar_num The hangar to get the location of.
	 * @pre hangar_num < GetNumHangars().
	 * @return A tile with the given hangar.
	 */
	inline TileIndex GetHangarTile(uint hangar_num) const
	{
		for (const auto &depot : this->GetSpec()->depots) {
			if (depot.hangar_num == hangar_num) {
				return this->GetRotatedTileFromOffset(depot.ti);
			}
		}
		NOT_REACHED();
	}

	/**
	 * Get the exit direction of the hangar at a specific tile.
	 * @param tile The tile to query.
	 * @pre IsHangarTile(tile).
	 * @return The exit direction of the hangar, taking airport rotation into account.
	 */
	inline Direction GetHangarExitDirection(TileIndex tile) const
	{
		const AirportSpec *as = this->GetSpec();
		const HangarTileTable *htt = GetHangarDataByTile(tile);
		return ChangeDir(htt->dir, DirDifference(this->rotation, as->layouts[0].rotation));
	}

	/**
	 * Get the hangar number of the hangar at a specific tile.
	 * @param tile The tile to query.
	 * @pre IsHangarTile(tile).
	 * @return The hangar number of the hangar at the given tile.
	 */
	inline uint GetHangarNum(TileIndex tile) const
	{
		const HangarTileTable *htt = GetHangarDataByTile(tile);
		return htt->hangar_num;
	}

	/**
	 * Get the number of hangars on this airport.
	 * @return The number of unique hangars.
	 */
	inline uint GetNumHangars() const
	{
		uint num = 0;
		uint counted = 0;
		for (const auto &depot : this->GetSpec()->depots) {
			if (!HasBit(counted, depot.hangar_num)) {
				num++;
				SetBit(counted, depot.hangar_num);
			}
		}
		return num;
	}

private:
	/**
	 * Retrieve hangar information of a hangar at a given tile.
	 * @param tile %Tile containing the hangar.
	 * @return The requested hangar information.
	 * @pre The \a tile must be at a hangar tile at an airport.
	 */
	inline const HangarTileTable *GetHangarDataByTile(TileIndex tile) const
	{
		for (const auto &depot : this->GetSpec()->depots) {
			if (this->GetRotatedTileFromOffset(depot.ti) == tile) {
				return &depot;
			}
		}
		NOT_REACHED();
	}
};

struct IndustryListEntry {
	uint distance = 0;
	Industry *industry = nullptr;

	bool operator==(const IndustryListEntry &other) const { return this->distance == other.distance && this->industry == other.industry; }
	bool operator!=(const IndustryListEntry &other) const { return !(*this == other); }
};

struct IndustryCompare {
	bool operator() (const IndustryListEntry &lhs, const IndustryListEntry &rhs) const;
};

typedef btree::btree_set<IndustryListEntry, IndustryCompare> IndustryList;

/**
 * R3R (第 451 轮, KI-642): 调度池条目的「分配规则」变量。
 *
 * 领取方到站申请调度表时，用**它自己这列车**的性能去比对池条目上写的规则，
 * 满足才发牌（第 412 轮 C-8 口径）；不满足就继续扫下一条。
 * 这里刻意不复用 OrderConditionVariable：那套是订单级、带一整套存档与 UI，
 * 而这里的规则只是「这一份表发给谁」的一个随身标签，独立枚举互不影响。
 */
enum class R3RDispatchRuleVariable : uint8_t {
	None = 0,          ///< 不设条件：任何列车都能领
	MaxSpeed,          ///< 最高速度（km/h，显示值）
	Power,             ///< 功率（hp，显示值）
	MaxTractiveEffort, ///< 最大牵引力（kN）
	Reliability,       ///< 可靠性（%）
	/* --- R3R (第 451 轮续): 与可编程信号机的既有条件逐条对应 ---
	 * 可编程信号机的条件清单见 src\programmable_signals.h 的 SignalConditionCode。
	 * 「前方绿灯数 / 红灯数 / 另一信号的状态」在领表上下文里没有信号格可看，
	 * 第 451 轮曾照抄进下拉，求值恒为 0（备忘 C-451-5 的 (B) 方案）。
	 *
	 * R3R (第 455 轮, KI-652): 这三个「恒 0」条件已不再对玩家提供 —— 玩家口径是
	 * 「能抄的抄、不能抄的别硬抄」，一个永远读 0 的条件只会让规则悄悄永远不成立。
	 * 但**枚举值本身必须保留**：它们以 uint8 逐条存进调度池条目（R3RDispatchInstruction::var），
	 * 删掉会让其后所有变量的编号前移，旧档的条件会被静默解释成另一个变量。
	 * 因此这里只做「保留编号 + 编辑器的下拉里不出现 + 求值视为不设限（恒真）」。 */
	PoolIssued,        ///< 本池已发份数（= PSC_SLOT_OCC「槽占用」）
	PoolRemaining,     ///< 本池剩余坑位（= PSC_SLOT_OCC_REM「槽剩余」）
	NumGreen,          ///< 【已停用，仅读旧档】= PSC_NUM_GREEN「前方绿灯数」（恒 0）
	NumRed,            ///< 【已停用，仅读旧档】= PSC_NUM_RED「前方红灯数」（恒 0）
	OtherSignalState,  ///< 【已停用，仅读旧档】= PSC_SIGNAL_STATE「另一信号的状态」（恒 0）
	Counter,           ///< = PSC_COUNTER「计数器值」（计数器编号见 R3RDispatchInstruction::extra）
	EngineClass,       ///< R3R (第 455 轮): 机车类型（EngineClass 的数值；见 R3RDispatchInstruction::value）
	/* 新增变量必须追加在末尾 —— 值即存档里的编号，插在中间会让旧档错位。 */
};

/** R3R (第 451 轮, KI-642): 调度池规则的比较符。 */
enum class R3RDispatchRuleOp : uint8_t {
	AtLeast = 0, ///< 来车指标 >= 阈值（「达 XXX」）
	AtMost,      ///< 来车指标 <= 阈值（「不超过 XXX」）
	/* R3R (第 455 轮续二, KI-652): 第 455 轮复测玩家反馈「比较符只有至多/至少」——
	 * 数值指标要与可编程信号灯对齐给全，枚举指标（机车类型）需要「是/否」。
	 * 新增值必须追加在末尾：编号随指令存进存档（见 R3RDispatchInstruction::op）。 */
	Equal,       ///< == 阈值（「等于」）
	NotEqual,    ///< != 阈值（「不等于」，枚举指标用它表示「不是」）
	Greater,     ///< > 阈值（「大于」）
	Less,        ///< < 阈值（「小于」）
};

/** R3R (第 451 轮续): 「分配规则」程序里一条指令的种类。
 *  照抄可编程信号机（SignalOpcode）的分支结构，只是把「置信号」换成「允许/拒绝分配调度」。 */
enum class R3RDispatchInstructionKind : uint8_t {
	If = 0,     ///< 如果 <条件>：条件假则跳到本层下一个「或者 / 否则 / 结束如果」
	Else,       ///< 否则：走到这里就跳到配对的「结束如果」之后
	Or,         ///< 或者（= elif）：条件假则跳到本层下一个「或者 / 否则 / 结束如果」
	EndIf,      ///< 结束如果（空操作，靠它配对）
	Result,     ///< 允许分配调度 X / 拒绝分配调度 X：立即终止求值并返回结果
};

/** R3R (第 451 轮续): 「允许分配调度」还是「拒绝分配调度」。 */
enum class R3RDispatchResultKind : uint8_t {
	Allow = 0, ///< 允许分配调度 X
	Deny,      ///< 拒绝分配调度 X
};

/** R3R (第 451 轮续): 分配规则程序的一条指令（定长，便于存档与命令传输）。 */
struct R3RDispatchInstruction {
	R3RDispatchInstructionKind kind = R3RDispatchInstructionKind::If; ///< 指令种类
	R3RDispatchRuleVariable var = R3RDispatchRuleVariable::None;       ///< If/Or：条件变量
	R3RDispatchRuleOp op = R3RDispatchRuleOp::AtLeast;                 ///< If/Or：比较符
	R3RDispatchResultKind result = R3RDispatchResultKind::Allow;       ///< Result：允许/拒绝
	uint32_t value = 0; ///< If/Or：比较阈值；Counter：被比较的计数器值
	uint32_t extra = 0xFFFFFFFF; ///< Result：允许/拒绝的调度掩码（bit i = 去重槽位 i，见 R3R_DISPATCH_MASK_BITS；全 0 = 一条都不允许）；Counter：计数器编号
};

/** R3R (第 451 轮续): 一份「分配规则」程序的条数上限（照抄可编程信号机的小规模定位）。 */
static constexpr uint16_t R3R_DISPATCH_PROGRAM_MAX = 64;

/** R3R (第 451 轮, KI-642): 领取方这列车的性能快照，用于池条目规则求值。 */
struct R3RDispatchClaimStats {
	uint32_t max_speed = 0;   ///< 最高速度（km/h）
	uint32_t power = 0;       ///< 功率（hp）
	uint32_t max_te = 0;      ///< 最大牵引力（kN）
	uint32_t reliability = 0; ///< 可靠性（%）
	uint8_t engine_class = 0; ///< R3R (第 455 轮): 机车类型（EngineClass 数值；0 = 蒸汽）
	bool valid = false;       ///< false = 求值方没提供（此时只允许无条件条目）
};

/** Station data structure */
struct Station final : SpecializedStation<Station, false> {
public:
	RoadStop *GetPrimaryRoadStop(RoadStopType type) const
	{
		return type == RoadStopType::Bus ? bus_stops : truck_stops;
	}

	RoadStop *GetPrimaryRoadStop(const struct RoadVehicle *v) const;

	RoadStop *bus_stops = nullptr;          ///< All the road stops
	TileArea bus_station{};                 ///< Tile area the bus 'station' part covers
	RoadStop *truck_stops = nullptr;        ///< All the truck stops
	TileArea truck_station{};               ///< Tile area the truck 'station' part covers

	Airport airport{};                      ///< Tile area the airport covers
	TileArea ship_station{};                ///< Tile area the ship 'station' part covers
	TileArea docking_station{};             ///< Tile area the docking tiles cover
	std::vector<TileIndex> docking_tiles{}; ///< Tile vector the docking tiles cover

	IndustryType indtype = IT_INVALID;      ///< Industry type to get the name from
	uint16_t extra_name_index = 0;          ///< Extra name index in use (or UINT16_MAX)

	BitmapTileArea catchment_tiles{};       ///< NOSAVE: Set of individual tiles covered by catchment area
	uint station_tiles = 0;                 ///< NOSAVE: Count of station tiles owned by this station

	StationVehicleTypes had_vehicle_of_type{};

	uint8_t time_since_load = 0;
	uint8_t time_since_unload = 0;

	uint8_t station_cargo_history_offset = 0;  ///< Start offset in station_cargo_history cargo ring buffer, here for alignment

	std::vector<Vehicle *> loading_vehicles{};
	std::array<GoodsEntry, NUM_CARGO> goods;   ///< Goods at this station
	CargoTypes always_accepted{};              ///< Bitmask of always accepted cargo types (by houses, HQs, industry tiles when industry doesn't accept cargo)

	IndustryList industries_near{};            ///< Cached list of industries near the station that can accept cargo, @see DeliverGoodsToIndustry()
	Industry *industry = nullptr;              ///< NOSAVE: Associated industry for neutral stations. (Rebuilt on load from Industry->st)

	CargoTypes station_cargo_history_cargoes{};                                              ///< Bitmask of cargoes in station_cargo_history
	std::vector<std::array<uint16_t, MAX_STATION_CARGO_HISTORY_DAYS>> station_cargo_history; ///< Station history of waiting cargo, dynamic range compressed (see RXCompressUint)

	/* R3R: station yard (站场). 一个「场」是若干铁路平台的集合（一个平台的全部 tile 恒属于
	 * 同一个场）。订单里的场 ID = 1 + 场的下标；0 表示整站（不设场）。
	 * 场列表按下标寻址，因此「删除场」会移动其后各场的下标：删场必须同时把全图订单里
	 * 指向本车站的场 ID 重编号（见 CmdR3RRemoveStationYard），否则订单会悄悄指向另一个场。
	 * 它不走车站表(STNN)存档，而是独立 chunk "SYRD"（见 src/sl/station_yard_sl.cpp），
	 * 这样本 fork 永不改动与上游共享的车站表布局。 */
	struct R3RStationYard {
		std::vector<TileIndex> tiles; ///< 本场的铁路车站 tile（升序、无重复）
		/* 本场停满时允许回落的「共享场」ID（0 = R3R_YARD_NONE，不回落）。
		 * 多个场可以指向同一个共享场，这就是「1/2 场共享一个 A 场」的表达方式：
		 * 场1.shared_with = 场2.shared_with = A场。回落只做一层，不做链式展开。 */
		uint16_t shared_with = 0;
		/* R3R: 检修所（workshop）。为 true 时本场不再吸引/产生/接受货物，停在
		 * 本场 tile 的车跳过装卸货、只做停靠，且当车辆的订单目的地明确是本场时
		 * 触发一次进厂检修（VehicleServiceInDepot）。 */
		bool is_workshop = false;
		/* R3R: 玩家给这个场起的名字（UTF-8）。空 = 未命名，界面一律显示默认的
		 * 「N 场」。名字只是标签，不参与寻路/占用判定，也不要求全局唯一。 */
		std::string name;
		/* R3R (第 416 轮): 调度池条目。
		 *
		 * 一条 = 「一份可以被领取的调度表」+「同时允许几列车领用(limit)」+
		 * 「已经发出几份(issued)」。列车在带「等待分配」标记的到站命令处领取一条，
		 * 跑完池表的最后一条命令再把命令指回自己表的下一条。
		 *
		 * 第 428 轮 (KI-621) 起，条目的身份是**排程**（schedule = OrderList 池下标），
		 * 不再按列车登记：
		 *  - 同一个表被多列车共享时，池里只出现一次（旧实现按模板车登记，会重复）；
		 *  - 无主排程（第 417 轮，first_shared == nullptr）也能进池。旧实现在这里只存
		 *    VehicleID，无主排程根本无处安放，于是池里压根看不到它们。
		 * host 保留为「模板车」缓存，只用于显示与旧档兼容；它可以无效（无主排程）。
		 * 表本体仍由 OrderList 池持有，与车辆名下的表完全同构（第 417 轮已把
		 * GetFirstSharedVehicle() 的调用点全部做了空值守卫）。
		 *
		 * 容量上限 = 游戏设置 order.r3r_dispatch_pool_capacity（默认 256）。
		 */
		struct R3RDispatchPoolEntry {
			VehicleID host = VehicleID::Invalid(); ///< 持有该调度表的模板车
			uint32_t schedule = R3R_DISPATCH_NO_SCHEDULE; ///< R3R (第 428 轮): 排程（OrderList 池下标）
			uint16_t limit = 1;                    ///< 同时可领用的列车数（坑位），0 视为 1
			uint16_t issued = 0;                   ///< 已发出、尚未归还的份数
			/* R3R (第 451 轮, KI-642): 分配规则 —— 只有满足规则的来车才能领这份表。
			 * rule_var == None 表示无条件（等价于第 451 轮之前的旧行为）。
			 * 一条条目只携带一个 (变量, 比较符, 阈值) 三元组；因为池内按排程去重
			 * （同一张表在同一场只登记一次），所以「一份表一条规则」是本轮的口径。 */
			R3RDispatchRuleVariable rule_var = R3RDispatchRuleVariable::None;
			R3RDispatchRuleOp rule_op = R3RDispatchRuleOp::AtLeast;
			int32_t rule_value = 0;                ///< 阈值（单位随 rule_var）
		};
		std::vector<R3RDispatchPoolEntry> pool; ///< R3R: 本场的调度池
		/* R3R (第 451 轮续): 本场的「分配规则」程序（如果/否则/或者/结束如果 + 允许/拒绝分配调度）。
		 * 空 = 无条件放行（与第 451 轮之前的行为一致）。整站（R3R_YARD_NONE）那一份存在
		 * Station::r3r_whole_program。求值见 src\station_cmd.cpp 的 R3RDispatchProgramAllows()。 */
		std::vector<R3RDispatchInstruction> program;
	};
	std::vector<R3RStationYard> r3r_yards{}; ///< R3R: 场的列表（有效场 ID = 下标 + 1）

	/* R3R (第 420 轮, KI-599): 「整站」（R3R_YARD_NONE）自己的调度池与检修所标志。
	 * 在此之前整站只是「不设场」的一个空概念，没有任何存储实体，于是站场窗口切到
	 * 整站时列表恒空、检修所/调度池按钮恒灰 —— 玩家要求整站也能管理这两样。
	 * 与各场池**相互独立、不合并**（整站池只服务「订单里没有指定场」的领取方）。
	 * 存档：独立 chunk "SYRD" v7 追加块（v6 已被第 416 轮的各场池占用；
	 * 第 428 轮把池条目扩成带排程 ⇒ 升到 v8，见 src/sl/station_yard_sl.cpp）。 */
	std::vector<R3RStationYard::R3RDispatchPoolEntry> r3r_whole_pool{};
	/* R3R (第 451 轮续): 整站（R3R_YARD_NONE）自己的「分配规则」程序。
	 * 与各场程序**相互独立、不合并**：整站池只服务「订单里没有指定场」的领取方，
	 * 程序也跟着池走（存档 SYRD v10，见 src\sl\station_yard_sl.cpp）。 */
	std::vector<R3RDispatchInstruction> r3r_whole_program{};
	bool r3r_whole_workshop = false; ///< R3R: 整站是否为检修所（该站全部平台都不接货）

	/* R3R: [存档兼容] SYRD v1 的三个场列表。v2 起场数据统一编码进 r3r_yard_a（见
	 * src/sl/station_yard_sl.cpp），r3r_yard_b / r3r_yard_shared 恒为空：保留它们只是
	 * 为了不改动 SYRD 的字段布局。读 v1 存档时由这三者迁移出 r3r_yards。 */
	std::vector<TileIndex> r3r_yard_a{};      ///< R3R: [存档] v1=A场；v2=编码后的全部场数据
	std::vector<TileIndex> r3r_yard_b{};      ///< R3R: [存档] v1=B场（v2 恒空）
	std::vector<TileIndex> r3r_yard_shared{}; ///< R3R: [存档] v1=共用场（v2 恒空）

	Station(StationID index, TileIndex tile = INVALID_TILE);
	~Station() override;

	void AddFacility(StationFacility new_facility_bit, TileIndex facil_xy);

	void MarkTilesDirty(bool cargo_change) const;

	void UpdateVirtCoord() override;

	void UpdateCargoHistory();

	void CheckCargoOverflow() const;

	void MoveSign(TileIndex new_xy) override;

	void AfterStationTileSetChange(bool adding, StationType type);

	uint GetPlatformLength(TileIndex tile, DiagDirection dir) const override;
	uint GetPlatformLength(TileIndex tile) const override;

	/* R3R: station yard (站场) 管理接口，实现见 src/station_cmd.cpp。
	 * 场 ID 语义：0 = 不设场（订单按整站处理）；k(>=1) = r3r_yards[k - 1]。
	 * 场数量不限（受 16 位 order 存储上限约束，最多 65534 个）。 */
	static constexpr uint16_t R3R_YARD_NONE = 0; ///< R3R: 无场，订单按整站处理
	static constexpr uint16_t R3R_MAX_YARDS = 65534; ///< R3R: 单车站场数量上限
	/** R3R (第 428 轮, KI-621): 调度池条目尚未记录排程（旧档条目，按模板车的表推导）。 */
	static constexpr uint32_t R3R_DISPATCH_NO_SCHEDULE = 0xFFFFFFFF;

	/* R3R: [存档兼容] SYRD v1 的三个固定场。v2 起场数据统一编码进 r3r_yard_a，
	 * 读 v1 存档时按「A→场1、B→场2、共用→场3(shared)」迁移。 */
	static constexpr uint16_t R3R_YARD_A      = 1; ///< R3R: [存档兼容] v1 A场 == 场1
	static constexpr uint16_t R3R_YARD_B      = 2; ///< R3R: [存档兼容] v1 B场 == 场2
	static constexpr uint16_t R3R_YARD_SHARED = 3; ///< R3R: [存档兼容] v1 共用场 == 场3

	/** R3R: 场数量（有效场 ID 为 1..R3RNumYards()）。 */
	uint16_t R3RNumYards() const { return static_cast<uint16_t>(this->r3r_yards.size()); }
	/** R3R: 新建一个场并返回其 ID（达到上限时返回 R3R_YARD_NONE）。 */
	uint16_t R3RAddYard();
	/** R3R: 删除某个场（失败返回 false）。它只做数据搬运：tile 随场一起消失、
	 * 回落目标被修正、其后各场的 ID 前移一位。订单里的场 ID 由命令层统一重编号
	 * （CmdR3RRemoveStationYard），所以调用者必须先做重编号再调用本函数。 */
	bool     R3RRemoveYard(uint16_t yard);
	/** R3R: 清空某个场的全部 tile（保留场本身与其下标，避免已有订单的场 ID 失效）。 */
	void     R3RClearYard(uint16_t yard);
	/** R3R: 某个场的玩家自定义名称（未命名/无效场返回空串）。 */
	const std::string &R3RGetYardName(uint16_t yard) const;
	/** R3R: 设置某个场的名称（空串 = 恢复未命名）。 */
	void     R3RSetYardName(uint16_t yard, const std::string &name);
	/** R3R: 某个场停满时的「共享回落场」（R3R_YARD_NONE = 不回落）。 */
	uint16_t R3RYardSharedWith(uint16_t yard) const;
	/** R3R: 设置某个场的「共享回落场」（R3R_YARD_NONE = 取消回落）。 */
	void     R3RSetYardSharedWith(uint16_t yard, uint16_t shared_with);
	/** R3R: 某个场是否被别的场指定为共享回落场（仅用于 UI 标注）。 */
	bool     R3RIsYardShared(uint16_t yard) const;
	/** R3R: 某个场是否为检修所（不接货、不装卸、到访即检修）。 */
	bool     R3RIsYardWorkshop(uint16_t yard) const;
	/** R3R: 设置某个场是否为检修所。 */
	void     R3RSetYardWorkshop(uint16_t yard, bool workshop);
	/** R3R: tile 是否位于检修所场内（无场/无效 tile 返回 false）。 */
	bool     R3RIsWorkshopTile(TileIndex tile) const;

	const std::vector<TileIndex> &R3RYardTiles(uint16_t yard) const;
	uint16_t R3RGetYardOfTile(TileIndex tile) const;
	void    R3RSetTileYard(TileIndex tile, uint16_t yard);
	void    R3RRemoveTileFromYards(TileIndex tile);
	void    R3RPruneYardTiles();
	bool    R3RHasAnyYard() const;
	/** R3R: 收集某个场中仍然有效的铁路 tile（升序、无重复）。 */
	void    R3RCollectYardTiles(uint16_t yard, std::vector<TileIndex> &out) const;

	/* R3R (第 416 轮): 调度池（见 R3RStationYard::R3RDispatchPoolEntry）。
	 * 实现见 src/station_cmd.cpp。 */
	/** R3R (第 420 轮, KI-599): 调度池的存储选址。yard == R3R_YARD_NONE 时指向整站池
	 *  （r3r_whole_pool），否则指向 r3r_yards[yard - 1].pool。
	 *  @return nullptr 表示场 ID 越界，调用方必须按「没有池」处理。 */
	std::vector<R3RStationYard::R3RDispatchPoolEntry>       *R3RDispatchPoolRef(uint16_t yard);
	const std::vector<R3RStationYard::R3RDispatchPoolEntry> *R3RDispatchPoolRef(uint16_t yard) const;
	/** R3R: 本场调度池里仍然有效的条目数（排程已消失/已清空的不计）。 */
	uint16_t R3RDispatchPoolCount(uint16_t yard) const;
	/** R3R: 丢弃无效条目（排程已消失且模板车也追不回），返回丢弃条数。 */
	uint16_t R3RDispatchPoolPrune(uint16_t yard);
	/** R3R (第 428 轮, KI-621): 加入一条调度池条目（排程 + 坑位），成功返回下标，失败返回 -1。
	 * @param schedule OrderList 池下标（0 起）；池条目的身份是排程而不是车，模板车只由它推导。 */
	int      R3RDispatchPoolAdd(uint16_t yard, uint32_t schedule, uint16_t limit);
	/** R3R: 删除指定下标的池条目。 */
	bool     R3RDispatchPoolRemove(uint16_t yard, uint16_t index);
	/** R3R: 修改指定下标的坑位（1..容量上限）。 */
	bool     R3RDispatchPoolSetLimit(uint16_t yard, uint16_t index, uint16_t limit);
	/** R3R (第 451 轮): 设置指定下标池条目的分配规则（rule_var == None 表示清掉规则）。
	 *  **第 451 轮续起废弃**：玩家口径改为「一个规则管全站场，不要一条调度一个规则」，
	 *  规则搬到 R3RDispatchProgramSet() 的程序里。这里的三字段与存档保留只为兼容旧档，
	 *  R3RDispatchPoolPick() 已不再读它们，GUI 也不再编辑。 */
	bool     R3RDispatchPoolSetRule(uint16_t yard, uint16_t index, R3RDispatchRuleVariable var, R3RDispatchRuleOp op, int32_t value);

	/* R3R (第 451 轮续): 「分配规则」程序（如果 / 否则 / 或者 / 结束如果 + 允许·拒绝分配调度）。 */
	/** R3R: 取某个场的规则程序（yard == R3R_YARD_NONE ⇒ 整站那一份）。越界返回 nullptr。 */
	std::vector<R3RDispatchInstruction>       *R3RDispatchProgramRef(uint16_t yard);
	const std::vector<R3RDispatchInstruction> *R3RDispatchProgramRef(uint16_t yard) const;
	/** R3R: 整体替换某个场的规则程序（调用方须已校验；空程序 = 清除规则，回到无条件放行）。 */
	bool     R3RDispatchProgramSet(uint16_t yard, const std::vector<R3RDispatchInstruction> &program);
	/** R3R: 求值规则程序，返回 true = 允许把 @p schedule 这份调度发给来车。
	 *  空程序恒 true（旧行为）；@p stats 为空或 !valid 时，需要性能的条件一律不成立。 */
	bool     R3RDispatchProgramAllows(uint16_t yard, uint32_t schedule, const R3RDispatchClaimStats *stats) const;
	/** R3R: 为一次领取挑一个还有空坑位的条目，返回下标（-1 = 没有可用的）。
	 * @param except_schedule 领取方自己正在跑的表（它不能领自己名下的表）。
	 * @param out_schedule 输出被选中的排程（OrderList 池下标）。
	 * @param stats 领取方这列车的性能快照（第 451 轮起用于求值池条目的分配规则）。
	 *              nullptr 或 !valid 时，带规则的条目一律视为不匹配。 */
	int      R3RDispatchPoolPick(uint16_t yard, uint32_t except_schedule, uint32_t &out_schedule, const R3RDispatchClaimStats *stats = nullptr) const;
	/** R3R: 领取成功后占掉一个坑位（按排程查找）。 */
	bool     R3RDispatchPoolAcquire(uint32_t schedule);
	/** R3R: 归还一个坑位（按排程查找全部车站的场）。 */
	bool     R3RDispatchPoolRelease(uint32_t schedule);

	void RecomputeCatchment(bool no_clear_nearby_lists = false);
	static void RecomputeCatchmentForAll();

	uint GetCatchmentRadius() const;
	Rect GetCatchmentRectUsingRadius(uint radius) const;
	inline Rect GetCatchmentRect() const
	{
		return GetCatchmentRectUsingRadius(this->GetCatchmentRadius());
	}

	bool CatchmentCoversTown(TownID t) const;
	void AddIndustryToDeliver(Industry *ind, TileIndex tile);
	void RemoveIndustryToDeliver(Industry *ind);
	void RemoveFromAllNearbyLists();

	inline bool TileIsInCatchment(TileIndex tile) const
	{
		return this->catchment_tiles.HasTile(tile);
	}

	inline bool TileBelongsToRailStation(TileIndex tile) const override
	{
		return IsRailStationTile(tile) && GetStationIndex(tile) == this->index;
	}

	inline bool TileBelongsToRoadStop(TileIndex tile) const
	{
		return IsAnyRoadStopTile(tile) && GetStationIndex(tile) == this->index;
	}

	inline bool TileBelongsToAirport(TileIndex tile) const
	{
		return IsAirportTile(tile) && GetStationIndex(tile) == this->index;
	}

	bool IsWithinRangeOfDockingTile(TileIndex tile, uint max_distance) const;

	uint32_t GetNewGRFVariable(const ResolverObject &object, uint16_t variable, uint8_t parameter, bool &available) const override;

	TileArea GetTileArea(StationType type) const override;
};

/** Iterator to iterate over all tiles belonging to an airport. */
class AirportTileIterator : public OrthogonalTileIterator {
private:
	const Station *st = nullptr; ///< The station the airport is a part of.

public:
	/**
	 * Construct the iterator.
	 * @param st Station the airport is part of.
	 */
	AirportTileIterator(const Station *st) : OrthogonalTileIterator(st->airport), st(st)
	{
		if (!st->TileBelongsToAirport(this->tile)) ++(*this);
	}

	inline TileIterator& operator ++() override
	{
		this->OrthogonalTileIterator::operator++();
		while (this->tile != INVALID_TILE && !st->TileBelongsToAirport(this->tile)) {
			this->OrthogonalTileIterator::operator++();
		}
		return *this;
	}

	std::unique_ptr<TileIterator> Clone() const override
	{
		return std::make_unique<AirportTileIterator>(*this);
	}
};

void RebuildStationKdtree();

/* R3R: station yard (站场) helpers, implementation in src/station_cmd.cpp. */

/** R3R: 收集 \a tile 所在铁路平台的**全部** tile（沿站台轴双向扫描），\a out 先被清空。 */
void R3RCollectPlatformTiles(TileIndex tile, std::vector<TileIndex> &out);

/**
 * R3R: 枚举 \a st 的全部铁路平台，\a out 存各平台的「北端」tile（每个平台一项，按 tile 升序）。
 * 供站场 UI 逐平台划场使用，\a out 先被清空。
 */
void R3REnumeratePlatforms(const Station *st, std::vector<TileIndex> &out);

/**
 * R3R: 计算一个「带场」的 GOTO_STATION 订单允许停靠的 tile 集合（一次性）。
 * - \a yard == R3R_YARD_NONE，或该场没有任何 tile ⇒ \a out 为空（调用方应视为「整站」）。
 * - 若该场已满（全部平台都被预留）⇒ 再并入它的「共享回落场」的 tile 作为回落。
 * \a out 按 tile 升序且无重复。
 */
void R3RCollectYardDestinationTiles(const Station *st, uint16_t yard, std::vector<TileIndex> &out);

/**
 * R3R: 判断 \a yard 是否「满」= 该场内每一个平台的每一节 tile 都被预留。
 * 这是一次性判定（只在列车选平台时调用），不会在每 tick 遍历站台。
 * \a yard 无任何平台时返回 false。
 */
bool R3RStationYardIsFull(const Station *st, uint16_t yard);

using ForAllStationsAroundTilesFunc = bool(Station *, TileIndex);
using ForAllStationsAroundTilesIntlFunc = void(Station *, const TileArea &, uintptr_t);

/**
 * Call a function on all stations that have any part of the requested area within their catchment.
 * @tparam Func The type of function to call
 * @param ta The TileArea to check.
 * @param func The function to call, must take two parameters: Station* and TileIndex and return true
 *             if coverage of that tile is acceptable for a given station or false if search should continue
 */
template <typename Func>
void ForAllStationsAroundTiles(const TileArea &ta, Func func)
{
	/* There are no stations, so we will never find anything. */
	if (Station::GetNumItems() == 0) return;

	extern void ForAllStationsAroundTilesIntl(const TileArea &ta, ForAllStationsAroundTilesIntlFunc *func, uintptr_t data);

	struct Helper {
		static void Exec(Station *st, const TileArea &ta, Func &f) {
			/* Test if the tile is within the station's catchment */
			for (TileIndex tile : ta) {
				if (st->TileIsInCatchment(tile)) {
					if (f(st, tile)) break;
				}
			}
		};
	};

	if constexpr (std::is_convertible_v<Func, ForAllStationsAroundTilesFunc *>) {
		ForAllStationsAroundTilesIntl(ta, [](Station *st, const TileArea &ta, uintptr_t data) {
			Func f;
			Helper::Exec(st, ta, f);
		}, 0);
	} else {
		Func *ptr = &func;
		ForAllStationsAroundTilesIntl(ta, [](Station *st, const TileArea &ta, uintptr_t data) {
			Helper::Exec(st, ta, *reinterpret_cast<Func *>(data));
		}, reinterpret_cast<uintptr_t>(ptr));
	}
}

#endif /* STATION_BASE_H */
