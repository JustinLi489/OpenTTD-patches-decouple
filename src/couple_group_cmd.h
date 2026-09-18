/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file couple_group_cmd.h R3R: commands to manage couple groups. */

#ifndef COUPLE_GROUP_CMD_H
#define COUPLE_GROUP_CMD_H

#include "command_type.h"
#include "couple_group_type.h"
#include "vehicle_type.h"
#include <string>

CommandCost CmdCreateCoupleGroup(DoCommandFlags flags, const std::string &name);
CommandCost CmdRenameCoupleGroup(DoCommandFlags flags, CoupleGroupID group, const std::string &name);
CommandCost CmdDeleteCoupleGroup(DoCommandFlags flags, CoupleGroupID group);
CommandCost CmdSetCoupleGroup(DoCommandFlags flags, VehicleID vehicle, CoupleGroupID group);
CommandCost CmdRemoveCoupleGroup(DoCommandFlags flags, VehicleID vehicle, CoupleGroupID group);
CommandCost CmdSetCoupleGroupAllowOthers(DoCommandFlags flags, CoupleGroupID group, bool allow_others);

DEF_CMD_TUPLE_NT (Commands::CreateCoupleGroup, CmdCreateCoupleGroup, {}, CommandType::OtherManagement,   CmdDataT<std::string>)
DEF_CMD_TUPLE_NT (Commands::RenameCoupleGroup, CmdRenameCoupleGroup, {}, CommandType::OtherManagement,   CmdDataT<CoupleGroupID, std::string>)
DEF_CMD_TUPLE_NT (Commands::DeleteCoupleGroup, CmdDeleteCoupleGroup, {}, CommandType::OtherManagement,   CmdDataT<CoupleGroupID>)
DEF_CMD_TUPLE_NT (Commands::SetCoupleGroup,    CmdSetCoupleGroup,    {}, CommandType::VehicleManagement, CmdDataT<VehicleID, CoupleGroupID>)
DEF_CMD_TUPLE_NT (Commands::RemoveCoupleGroup, CmdRemoveCoupleGroup, {}, CommandType::VehicleManagement, CmdDataT<VehicleID, CoupleGroupID>)
DEF_CMD_TUPLE_NT (Commands::SetCoupleGroupAllowOthers, CmdSetCoupleGroupAllowOthers, {}, CommandType::OtherManagement, CmdDataT<CoupleGroupID, bool>)

#endif /* COUPLE_GROUP_CMD_H */
