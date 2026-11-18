/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WaterDisturbance.cpp /////////////////////////////////////////////////
// The disturbance that does nothing, for builds and renderers without the Direct3D 12 water.
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/WaterDisturbance.h"

static WaterDisturbance s_noDisturbance;
WaterDisturbance *TheWaterDisturbance = &s_noDisturbance;
