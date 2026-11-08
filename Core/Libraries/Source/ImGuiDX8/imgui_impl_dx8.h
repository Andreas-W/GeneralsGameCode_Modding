/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// Dear ImGui renderer backend for a Direct3D 8 device.
//
// Draws through the plain IDirect3DDevice8 interface, so it works on the real D3D8 runtime and on
// the D3D8-on-12 layer (-dx12) alike. D3D8 has no scissor test: every draw command is clipped by
// setting the viewport to its clip rectangle and an orthographic projection of the same rectangle.
//
// Buffers and the font texture live in the managed pool, so they survive a device reset.
// The device state is saved in a state block before drawing and restored afterwards.

#pragma once

#include "imgui.h"

struct IDirect3DDevice8;

IMGUI_IMPL_API bool ImGui_ImplDX8_Init(IDirect3DDevice8 *device);
IMGUI_IMPL_API void ImGui_ImplDX8_Shutdown();
IMGUI_IMPL_API void ImGui_ImplDX8_NewFrame();
IMGUI_IMPL_API void ImGui_ImplDX8_RenderDrawData(ImDrawData *drawData);

// Releases and recreates the device objects, e.g. after the device itself was recreated.
IMGUI_IMPL_API bool ImGui_ImplDX8_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX8_InvalidateDeviceObjects();
