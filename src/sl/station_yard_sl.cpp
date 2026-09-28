/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_yard_sl.cpp R3R: save/load of station yards (站场). */

#include "../stdafx.h"
#include "../station_base.h"

#include "saveload.h"

#include "../safeguards.h"

/**
 * R3R: station yard chunk.
 *
 * 场的 tile 列表被刻意排除在车站表(STNN)之外，改为独立 chunk。理由与
 * couple_group_sl.cpp 的 CGVR 相同：这样本 fork 永不改动与上游共享的车站表
 * 布局，且没建过场的存档里根本不会出现本 chunk（稀疏 chunk）。
 *
 * ---------------------------------------------------------------------------
 * SYRD 编码历史
 * ---------------------------------------------------------------------------
 * v1：场固定为 3 个（A / B / 共享场），分别直接存进 yard_a / yard_b /
 *     yard_shared 三个变长列表；订单里的场 ID 0/1/2/3 = 整站/A/B/共享。
 *
 * v2：场数量不再固定。为了不改动 SYRD 的字段布局（仍只有 yard_a / yard_b /
 *     yard_shared 三个 UINT32 变长列表），v2 把所有场数据编码进 yard_a，
 *     yard_b / yard_shared 恒为空。订单里的场 ID 就是场在列表中的下标+1，
 *     因此 v1 的 A/B/共享恰好对应 v2 的场 1/2/3，迁移是恒等映射。
 *
 * v2 编码（每一项都是一个 32 位 word，即 std::vector<TileIndex> 的一个元素）：
 *     word 0 : #SYRD_V2_MAGIC（大于任何合法 tile 序号，用于和 v1 列表区分）
 *     word 1 : 场的数量 N（低 16 位有效）
 *     之后 N 组，每组：
 *         word   : 共享标志（0 或 1）
 *         word   : tile 数量 M
 *         M 个 word : tile 序号
 *
 * v3：把每组的「共享标志」升级为「共享回落场 ID」（shared_with），字段布局与
 *     v2 完全一致，只是语义不同。这样「1/2 场共享一个 A 场」= 场1/场2 的
 *     shared_with 都等于 A 场的 ID。读取 v2 时按旧语义迁移：所有非共享场回落
 *     到第一个共享场（只有一个共享场的存档即完全等价），共享场自身不回落。
 *
 * v4：每组新增一个「场标志」word（bit0 = is_workshop 检修所），放在
 *     shared_with 之后、tile 数量之前。读取 v3/v2 时该标志恒为 false。
 *
 * v5：每组新增「名称」块（玩家自定义的场名，见 Station::R3RStationYard::name）。
 *     名称是 std::string，而本 chunk 的载体是 std::vector<TileIndex>（32 位 word），
 *     所以按字节打包成 word：先写字节数（一个字），再写 ceil(字节数/4) 个字，
 *     每字装 4 个 UTF-8 字节（低位在前，末字高位补 0）。名称块放在「场标志」之后、
 *     tile 数量之前。读取 v4/v3/v2 时名称恒为空（界面显示默认的「N 场」）。
 *
 * 读档：yard_a 以 MAGIC 开头 ⇒ 按 v5/v4/v3/v2 解码；否则按 v1 迁移（任一 v1 列表
 * 非空时构造场 1/2/3，保证旧存档里订单引用的场 ID 仍然有效）。
 */
static constexpr uint32_t SYRD_V2_MAGIC = 0xFFFFFF01;
static constexpr uint32_t SYRD_V3_MAGIC = 0xFFFFFF02;
static constexpr uint32_t SYRD_V4_MAGIC = 0xFFFFFF03;
static constexpr uint32_t SYRD_V5_MAGIC = 0xFFFFFF04;

/** R3R: SYRD v5 场标志 word 的位定义。 */
static constexpr uint32_t SYRD_YARD_FLAG_WORKSHOP = 0x00000001; ///< 本场是检修所

/** R3R: 场名解码时的合法长度上限（字节）。命令层按
 * #MAX_LENGTH_STATION_NAME_CHARS 个字符校验，最坏 4 字节/字符，所以上限取
 * 4 倍字符数；超出（损坏/恶意编码）的部分截断，避免巨大的内存分配。 */
static constexpr uint32_t SYRD_MAX_NAME_BYTES = MAX_LENGTH_STATION_NAME_CHARS * 4;

/** R3R: 把名称按字节打包进 SYRD word 流（见文件头 v5 说明）。 */
static void R3RPackName(std::vector<TileIndex> &out, const std::string &name)
{
	const size_t len = name.size();
	out.push_back(static_cast<TileIndex>(len));
	for (size_t i = 0; i < len; i += 4) {
		uint32_t word = 0;
		for (size_t k = 0; k < 4 && i + k < len; k++) {
			word |= static_cast<uint32_t>(static_cast<uint8_t>(name[i + k])) << (8 * k);
		}
		out.push_back(static_cast<TileIndex>(word));
	}
}

/** R3R: 从 SYRD word 流解出名称，并把 \a p 推进到名称块之后。 */
static std::string R3RUnpackName(const std::vector<TileIndex> &in, size_t &p)
{
	if (p >= in.size()) return {};
	const uint32_t len = std::min<uint32_t>(in[p++].base(), SYRD_MAX_NAME_BYTES);

	std::string name;
	name.reserve(len);
	for (uint32_t i = 0; i < len; i++) {
		const size_t wi = p + (i / 4);
		if (wi >= in.size()) break;
		name.push_back(static_cast<char>((in[wi].base() >> (8 * (i % 4))) & 0xFF));
	}

	p += (static_cast<size_t>(len) + 3) / 4;
	if (p > in.size()) p = in.size();
	return name;
}

static const NamedSaveLoad _r3r_station_yard_desc[] = {
	NSL("yard_a",      SLE_VARVEC(Station, r3r_yard_a,      SLE_UINT32)),
	NSL("yard_b",      SLE_VARVEC(Station, r3r_yard_b,      SLE_UINT32)),
	NSL("yard_shared", SLE_VARVEC(Station, r3r_yard_shared, SLE_UINT32)),
};

/**
 * R3R: encode the general yard list of \a st into its SYRD save buffer
 * (r3r_yard_a); r3r_yard_b / r3r_yard_shared are kept empty from v2 on.
 */
static void R3REncodeYards(Station *st)
{
	std::vector<TileIndex> &out = st->r3r_yard_a;
	out.clear();
	st->r3r_yard_b.clear();
	st->r3r_yard_shared.clear();

	if (st->r3r_yards.empty()) return;

	out.push_back(static_cast<TileIndex>(SYRD_V5_MAGIC));
	out.push_back(static_cast<TileIndex>(st->r3r_yards.size() & 0xFFFF));
	for (const Station::R3RStationYard &y : st->r3r_yards) {
		out.push_back(static_cast<TileIndex>(y.shared_with));
		out.push_back(static_cast<TileIndex>(y.is_workshop ? SYRD_YARD_FLAG_WORKSHOP : 0));
		R3RPackName(out, y.name);
		out.push_back(static_cast<TileIndex>(y.tiles.size()));
		out.insert(out.end(), y.tiles.begin(), y.tiles.end());
	}
}

/** R3R: decode a v5/v4/v3/v2 yard buffer into \a st->r3r_yards. Returns false if \a in is none of them. */
static bool R3RDecodeYards(Station *st, const std::vector<TileIndex> &in)
{
	if (in.empty()) return false;
	const uint32_t magic = in[0].base();
	if (magic != SYRD_V5_MAGIC && magic != SYRD_V4_MAGIC && magic != SYRD_V3_MAGIC && magic != SYRD_V2_MAGIC) return false;
	const bool legacy_v2 = (magic == SYRD_V2_MAGIC);
	const bool has_flags = (magic == SYRD_V5_MAGIC || magic == SYRD_V4_MAGIC);
	const bool has_names = (magic == SYRD_V5_MAGIC);

	st->r3r_yards.clear();

	size_t p = 1;
	if (p >= in.size()) return true;
	const uint32_t n = in[p++].base() & 0xFFFF;
	uint16_t v2_first_shared = Station::R3R_YARD_NONE;
	std::vector<uint16_t> v2_shared; // v2：每场是否为共享场
	v2_shared.reserve(n);
	for (uint32_t k = 0; k < n; k++) {
		if (p + 2 > in.size()) break; // 截断的编码，安全停止
		const uint32_t w = in[p++].base();
		uint32_t flags = 0;
		if (has_flags) {
			if (p >= in.size()) break;
			flags = in[p++].base();
		}
		std::string name;
		if (has_names) name = R3RUnpackName(in, p);
		if (p >= in.size()) break; // 截断的编码，安全停止
		const uint32_t m = in[p++].base();
		Station::R3RStationYard &y = st->r3r_yards.emplace_back();
		if (legacy_v2) {
			v2_shared.push_back(w != 0 ? 1 : 0);
			if (w != 0 && v2_first_shared == Station::R3R_YARD_NONE) v2_first_shared = static_cast<uint16_t>(st->r3r_yards.size());
		} else {
			y.shared_with = static_cast<uint16_t>(w & 0xFFFF);
		}
		y.is_workshop = (flags & SYRD_YARD_FLAG_WORKSHOP) != 0;
		y.name = std::move(name);
		for (uint32_t i = 0; i < m && p < in.size(); i++) y.tiles.push_back(in[p++]);
	}

	if (legacy_v2) {
		/* v2 语义是「所有非共享场都可以回落到任意共享场」，v3 表达不了多对多，
		 * 迁移为全部回落到第一个共享场；只有一个共享场的存档即完全等价。 */
		for (size_t i = 0; i < st->r3r_yards.size(); i++) {
			const bool is_shared = (i < v2_shared.size() && v2_shared[i] != 0);
			st->r3r_yards[i].shared_with = is_shared ? Station::R3R_YARD_NONE : v2_first_shared;
		}
	}
	return true;
}

/** R3R: migrate the three fixed v1 yards into the general yard list (A/B fall back to the shared yard). */
static void R3RMigrateLegacyYards(Station *st)
{
	if (st->r3r_yard_a.empty() && st->r3r_yard_b.empty() && st->r3r_yard_shared.empty()) return;

	st->r3r_yards.clear();
	const std::vector<TileIndex> *src[3] = { &st->r3r_yard_a, &st->r3r_yard_b, &st->r3r_yard_shared };
	for (int i = 0; i < 3; i++) {
		Station::R3RStationYard &y = st->r3r_yards.emplace_back();
		/* v1：A(1)/B(2) 都回落到共享场(3)，共享场自身不回落。 */
		y.shared_with = (i < 2) ? 3 : Station::R3R_YARD_NONE;
		y.tiles = *src[i];
	}
}

static void Load_SYRD()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_r3r_station_yard_desc);

	int index;
	while ((index = SlIterateArray()) != -1) {
		Station *st = Station::GetIfValid(index);
		if (st == nullptr) continue; // 车站已被 STNN 丢弃（例如无用的 neutral 站），忽略本条

		SlObjectLoadFiltered(st, slt);

		if (!R3RDecodeYards(st, st->r3r_yard_a)) {
			R3RMigrateLegacyYards(st);
		}
		/* v2 起 yard_b / yard_shared 不再承载数据。 */
		st->r3r_yard_b.clear();
		st->r3r_yard_shared.clear();

		/* 读档后规范化：恒保持「有序 + 无重复 + 不含失效 tile」，R3RGetYardOfTile()
		 * 的二分查找与之互相依赖。 */
		const uint16_t num_yards = st->R3RNumYards();
		for (size_t i = 0; i < st->r3r_yards.size(); i++) {
			Station::R3RStationYard &y = st->r3r_yards[i];
			std::sort(y.tiles.begin(), y.tiles.end());
			y.tiles.erase(std::unique(y.tiles.begin(), y.tiles.end()), y.tiles.end());
			/* 共享回落场必须是「存在且不是自己」，否则视为无回落。 */
			if (y.shared_with == static_cast<uint16_t>(i + 1) || y.shared_with > num_yards) y.shared_with = Station::R3R_YARD_NONE;
		}
		st->R3RPruneYardTiles();
	}
}

static void Save_SYRD()
{
	SaveLoadTableData slt = SlTableHeader(_r3r_station_yard_desc);

	/* 稀疏：只写出真正建过场（哪怕场还是空的）的车站，以保住场的数量与下标。 */
	for (Station *st : Station::Iterate()) {
		if (st->r3r_yards.empty()) continue;
		R3REncodeYards(st);
		SlSetArrayIndex(st->index);
		SlObjectSaveFiltered(st, slt);
		/* 编码缓冲区只在存盘期使用，写完立即释放，避免长期占用内存。 */
		st->r3r_yard_a.clear();
	}
}

extern const ChunkHandler station_yard_chunk_handlers[] = {
	{ 'SYRD', Save_SYRD, Load_SYRD, nullptr, nullptr, CH_SPARSE_TABLE }, // R3R Station Yard chunk
};

extern const ChunkHandlerTable _station_yard_chunk_handlers(station_yard_chunk_handlers);
