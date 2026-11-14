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

#pragma once

#include "WWLib/always.h"

class SortingNodeStruct;
class SphereClass;
class Vector3;

class SortingRendererClass
{
	static bool _EnableTriangleDraw;

	static void Flush_Sorting_Pool();
	static void Insert_To_Sorted_List(SortingNodeStruct* state);
	static void Insert_To_Sorting_Pool(SortingNodeStruct* state);
	static bool Get_World_Box(const SortingNodeStruct* state, Vector3& world_min, Vector3& world_max);

public:
	static void Insert_Triangles(
		const SphereClass& bounding_sphere,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Insert_Triangles(
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Flush();

	/// Tells whether a world space box lies entirely below the water surface.
	typedef bool (*SubmergedTest)(const Vector3& world_min, const Vector3& world_max);
	/// Draws now, sorted among themselves, the queued triangles whose world space box the test calls
	/// submerged, and leaves the rest for Flush. For drawing what is under water before the water.
	static void Flush_Submerged(SubmergedTest is_submerged);

	static void Deinit();

	static void SetMinVertexBufferSize( unsigned val );

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }
};
