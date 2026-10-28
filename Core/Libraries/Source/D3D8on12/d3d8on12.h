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

// D3D8 interfaces implemented on Direct3D 12.
//
// Direct3DCreate8On12 stands in for Direct3DCreate8 from d3d8.dll: everything the engine does through
// IDirect3D8, IDirect3DDevice8 and the D3D8 resources is carried out with Direct3D 12 instead.

#pragma once

#include <d3d8.h>

// Returns nullptr if Direct3D 12 is not available (no Windows 10, no feature level 11 adapter).
IDirect3D8* WINAPI Direct3DCreate8On12(UINT sdk_version);
