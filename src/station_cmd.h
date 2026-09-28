/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file station_cmd.h Command definitions related to stations. */

#ifndef STATION_CMD_H
#define STATION_CMD_H

#include "command_type.h"
#include "cargo_type.h"
#include "direction_type.h"
#include "station_type.h"
#include "newgrf_station_id.h"

enum RailType : uint8_t;
enum RoadType : uint8_t;

DEF_CMD_TUPLE   (Commands::BuildAirport,                   CmdBuildAirport,     CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<uint8_t, uint8_t, StationID, bool>)
DEF_CMD_TUPLE   (Commands::BuildDock,                      CmdBuildDock,                       CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<StationID, bool>)
DEF_CMD_TUPLE   (Commands::BuildRailStation,               CmdBuildRailStation, CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<RailType, Axis, uint8_t, uint8_t, StationClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (Commands::RemoveFromRailStation,          CmdRemoveFromRailStation,                 {}, CommandType::LandscapeConstruction, CmdDataT<TileIndex, bool>)
DEF_CMD_TUPLE   (Commands::BuildRoadStop,                  CmdBuildRoadStop,    CMD_NO_WATER | CMD_AUTO, CommandType::LandscapeConstruction, CmdDataT<uint8_t, uint8_t, RoadStopType, bool, DiagDirection, RoadType, RoadStopClassID, uint16_t, StationID, bool>)
DEF_CMD_TUPLE   (Commands::RemoveRoadStop,                 CmdRemoveRoadStop,                        {}, CommandType::LandscapeConstruction, CmdDataT<uint8_t, uint8_t, RoadStopType, bool>)
DEF_CMD_TUPLE_NT(Commands::RenameStation,                  CmdRenameStation,                         {}, CommandType::OtherManagement,       CmdDataT<StationID, bool, std::string>)
DEF_CMD_TUPLE_NT(Commands::MoveStationName,                CmdMoveStationName,                       {}, CommandType::OtherManagement,       CmdDataT<StationID, TileIndex>)
DEF_CMD_TUPLE_NT(Commands::OpenCloseAirport,               CmdOpenCloseAirport,                      {}, CommandType::RouteManagement,       CmdDataT<StationID>)
DEF_CMD_TUPLE_NT(Commands::ExchangeStationNames,           CmdExchangeStationNames,                  {}, CommandType::OtherManagement,       CmdDataT<StationID, StationID>)
DEF_CMD_TUPLE_NT(Commands::SetStationCargoAllowedSupply,   CmdSetStationCargoAllowedSupply,          {}, CommandType::OtherManagement,       CmdDataT<StationID, CargoType, bool>)
DEF_CMD_TUPLE_NT(Commands::SetR3RStationYard,              CmdR3RSetStationYard,                     {}, CommandType::OtherManagement,       CmdDataT<StationID, TileIndex, uint16_t>)
DEF_CMD_TUPLE_NT(Commands::SetR3RStationYardShared,        CmdR3RSetStationYardShared,               {}, CommandType::OtherManagement,       CmdDataT<StationID, uint16_t, uint16_t>)
DEF_CMD_TUPLE_NT(Commands::SetR3RStationYardWorkshop,      CmdR3RSetStationYardWorkshop,             {}, CommandType::OtherManagement,       CmdDataT<StationID, uint16_t, bool>)
DEF_CMD_TUPLE_NT(Commands::SetR3RStationYardName,          CmdR3RSetStationYardName,                 {}, CommandType::OtherManagement,       CmdDataT<StationID, uint16_t, std::string>)
DEF_CMD_TUPLE_NT(Commands::RemoveR3RStationYard,           CmdR3RRemoveStationYard,                  {}, CommandType::OtherManagement,       CmdDataT<StationID, uint16_t>)

#endif /* STATION_CMD_H */
