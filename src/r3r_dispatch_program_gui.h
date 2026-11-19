/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file r3r_dispatch_program_gui.h R3R (第 451 轮续): 调度池「分配规则」程序编辑窗口的入口。 */

#ifndef R3R_DISPATCH_PROGRAM_GUI_H
#define R3R_DISPATCH_PROGRAM_GUI_H

#include "station_type.h"

/**
 * R3R (第 451 轮续): 打开某个站的某个场（yard == Station::R3R_YARD_NONE 即整站）的
 * 「分配规则」程序编辑窗口；已经开着就提到最前面。
 * @param station_id 车站。
 * @param yard       场号，0 表示整站那一份规则。
 */
void ShowDispatchProgramWindow(StationID station_id, uint16_t yard);

#endif /* R3R_DISPATCH_PROGRAM_GUI_H */
