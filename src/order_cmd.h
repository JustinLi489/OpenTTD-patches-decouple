/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file order_cmd.h Command definitions related to orders. */

#ifndef ORDER_CMD_H
#define ORDER_CMD_H

#include "command_type.h"
#include "order_base.h"
#include "order_type.h"

enum class ReverseOrderOperation : uint8_t {
	Reverse,
	AppendReversed,
};

struct InsertOrderCmdData final : public CommandPayloadSerialisable<InsertOrderCmdData> {
	static constexpr bool HasStringSanitiser = false;

	using OrderFields = MemberPtrTupleTypeAdapter<decltype(Order::GetCmdRefFields())>;

	VehicleID veh;
	VehicleOrderID sel_ord; // This may be INVALID_VEH_ORDER_ID to append to the end of the order list
	OrderFields::Value new_order;

	InsertOrderCmdData() = default;
	InsertOrderCmdData(VehicleID veh, VehicleOrderID sel_ord, const Order &order) :
			veh(veh), sel_ord(sel_ord), new_order(MemberPtrsTie(order, Order::GetCmdRefFields())) {}

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(struct format_target &) const;
};

/**
 * R3R (第 421 轮, C-8 / E-2): payload of Commands::InsertR3RLooseOrder.
 *
 * Identical to InsertOrderCmdData except that the target is an order list index
 * instead of a VehicleID: an ownerless (loose) schedule has no vehicle whose
 * `orders` pointer could name it. See CmdR3RInsertLooseOrder().
 */
struct InsertLooseOrderCmdData final : public CommandPayloadSerialisable<InsertLooseOrderCmdData> {
	static constexpr bool HasStringSanitiser = false;

	using OrderFields = MemberPtrTupleTypeAdapter<decltype(Order::GetCmdRefFields())>;

	uint32_t order_list;    ///< OrderList::index.base() of the ownerless schedule.
	VehicleOrderID sel_ord; // This may be INVALID_VEH_ORDER_ID to append to the end of the order list
	OrderFields::Value new_order;

	InsertLooseOrderCmdData() = default;
	InsertLooseOrderCmdData(uint32_t order_list, VehicleOrderID sel_ord, const Order &order) :
			order_list(order_list), sel_ord(sel_ord), new_order(MemberPtrsTie(order, Order::GetCmdRefFields())) {}

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(struct format_target &) const;
};

DEF_CMD_TUPLE_LT (Commands::ModifyOrder,            CmdModifyOrder,                     {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID, ModifyOrderFlags, uint16_t, CargoType, std::string>)
DEF_CMD_TUPLE_LT (Commands::SkipToOrder,            CmdSkipToOrder,                     {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (Commands::DeleteOrder,            CmdDeleteOrder,                     {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_DIRECT_LT(Commands::InsertOrder,            CmdInsertOrder,                     {}, CommandType::RouteManagement, InsertOrderCmdData)
DEF_CMD_TUPLE_LT (Commands::OrderRefit,             CmdOrderRefit,                      {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID, CargoType>)
DEF_CMD_TUPLE_LT (Commands::CloneOrder,             CmdCloneOrder,                      {}, CommandType::RouteManagement, CmdDataT<CloneOptions, VehicleID, VehicleID>)
DEF_CMD_TUPLE_LT (Commands::InsertOrdersFromVeh,    CmdInsertOrdersFromVehicle,         {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (Commands::MoveOrder,              CmdMoveOrder,                       {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID, VehicleOrderID, uint16_t>)
DEF_CMD_TUPLE_LT (Commands::ReverseOrderList,       CmdReverseOrderList,                {}, CommandType::RouteManagement, CmdDataT<VehicleID, ReverseOrderOperation>)
DEF_CMD_TUPLE_LT (Commands::DuplicateOrder,         CmdDuplicateOrder,                  {}, CommandType::RouteManagement, CmdDataT<VehicleID, VehicleOrderID>)
DEF_CMD_TUPLE_LT (Commands::SetRouteOverlayColour,  CmdSetRouteOverlayColour,           {}, CommandType::RouteManagement, CmdDataT<VehicleID, Colours>)
DEF_CMD_TUPLE_NT (Commands::MassChangeOrder,        CmdMassChangeOrder,                 {}, CommandType::RouteManagement, CmdDataT<DestinationID, VehicleType, OrderType, CargoType, DestinationID>)
DEF_CMD_TUPLE    (Commands::ClearOrderBackup,       CmdClearOrderBackup,     CMD_CLIENT_ID, CommandType::ServerSetting,   CmdDataT<ClientID>)

struct BulkOrderCmdData final : public CommandPayloadSerialisable<BulkOrderCmdData> {
	static constexpr bool HasStringSanitiser = false;

	VehicleID veh;
	std::vector<uint8_t> cmds;

	void SerialisePayload(BufferSerialisationRef buffer) const;
	bool Deserialise(DeserialisationBuffer &buffer, StringValidationSettings default_string_validation);
	void FormatDebugSummary(format_target &output) const;
};

DEF_CMD_DIRECT_NT(Commands::BulkOrder,         CmdBulkOrder,              CMD_NO_TEST, CommandType::RouteManagement, BulkOrderCmdData)

DEF_CMD_TUPLE_NT (Commands::CreateR3RLooseSchedule,   CmdR3RCreateLooseSchedule,   {}, CommandType::OtherManagement, CmdDataT<uint32_t>)
DEF_CMD_TUPLE_NT (Commands::DeleteR3RLooseSchedule,   CmdR3RDeleteLooseSchedule,   {}, CommandType::OtherManagement, CmdDataT<uint32_t>)
DEF_CMD_TUPLE_NT (Commands::AssignR3RSchedule,        CmdR3RAssignSchedule,        {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint32_t>)
DEF_CMD_TUPLE_NT (Commands::RenameR3RSchedule,        CmdR3RRenameSchedule,        {}, CommandType::OtherManagement, CmdDataT<uint32_t, std::string>)
DEF_CMD_TUPLE_NT (Commands::SetR3RSchedulePublic,     CmdR3RSetSchedulePublic,     {}, CommandType::OtherManagement, CmdDataT<uint32_t, bool>)

/* R3R (第 421 轮, C-8 / E-2): editing an ownerless schedule. These commands never
 * touch a vehicle; a list which some vehicle runs is edited through that vehicle's
 * own commands so that the per-vehicle order indices stay consistent. */
DEF_CMD_DIRECT_NT(Commands::InsertR3RLooseOrder,    CmdR3RInsertLooseOrder,    {}, CommandType::OtherManagement, InsertLooseOrderCmdData)
DEF_CMD_TUPLE_NT (Commands::DeleteR3RLooseOrder,    CmdR3RDeleteLooseOrder,    {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t>)
DEF_CMD_TUPLE_NT (Commands::MoveR3RLooseOrder,      CmdR3RMoveLooseOrder,      {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t, uint16_t, uint16_t>)
DEF_CMD_TUPLE_NT (Commands::DuplicateR3RLooseOrder, CmdR3RDuplicateLooseOrder, {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t>)
/* R3R (第 424 轮, KI-607 C-1=R2): modify one order of an ownerless schedule. Shares the
 * ModifyOrderFlags whitelist with Commands::ModifyOrder, only the target differs: an
 * OrderList index instead of a VehicleID. */
DEF_CMD_TUPLE_NT (Commands::ModifyR3RLooseOrder,    CmdR3RModifyLooseOrder,    {}, CommandType::OtherManagement, CmdDataT<uint32_t, uint16_t, ModifyOrderFlags, uint16_t, CargoType, std::string>)

#endif /* ORDER_CMD_H */
