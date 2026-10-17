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

// McpShape.h
// Circle/rect area argument shared by the MCP terrain commands.

#pragma once

#include "mcp/McpCommands.h"
#include "WHeightMapEdit.h"

#include <math.h>

/// A world-space shape for area operations. Circles fall off linearly over 'feather'.
struct McpShape
{
	bool circle;
	double cx, cy, radius, feather;
	double x0, y0, x1, y1;

	void parse(const McpJson &args, bool allowFeather)
	{
		std::string kind = mcpArgString(args, "shape", args.has("radius") ? "circle" : "rect");
		feather = allowFeather ? mcpArgNumber(args, "feather", 0) : 0;
		if (feather < 0) feather = 0;
		if (kind == "circle") {
			circle = true;
			cx = mcpArgNumber(args, "x");
			cy = mcpArgNumber(args, "y");
			radius = mcpArgNumber(args, "radius");
			if (radius < 0) mcpFail("radius must be >= 0");
			x0 = cx - radius - feather;
			y0 = cy - radius - feather;
			x1 = cx + radius + feather;
			y1 = cy + radius + feather;
		} else if (kind == "rect") {
			circle = false;
			x0 = mcpArgNumber(args, "x0");
			y0 = mcpArgNumber(args, "y0");
			x1 = mcpArgNumber(args, "x1");
			y1 = mcpArgNumber(args, "y1");
			if (x1 < x0) { double t = x0; x0 = x1; x1 = t; }
			if (y1 < y0) { double t = y0; y0 = y1; y1 = t; }
			cx = (x0 + x1) / 2;
			cy = (y0 + y1) / 2;
			radius = 0;
			x0 -= feather; y0 -= feather; x1 += feather; y1 += feather;
		} else {
			mcpFail("shape must be 'circle' (x,y,radius) or 'rect' (x0,y0,x1,y1)");
		}
	}

	/// 1 inside the shape, falling to 0 across the feather band, 0 outside.
	double weight(double wx, double wy) const
	{
		double dist;
		if (circle) {
			double dx = wx - cx, dy = wy - cy;
			dist = sqrt(dx * dx + dy * dy) - radius;
		} else {
			double dx = 0, dy = 0;
			if (wx < x0 + feather) dx = x0 + feather - wx;
			if (wx > x1 - feather) dx = wx - (x1 - feather);
			if (wy < y0 + feather) dy = y0 + feather - wy;
			if (wy > y1 - feather) dy = wy - (y1 - feather);
			dist = sqrt(dx * dx + dy * dy);
		}
		const double CLOSE_ENOUGH = 0.01;
		if (dist <= CLOSE_ENOUGH) return 1.0;
		if (feather <= 0 || dist >= feather) return 0.0;
		return (feather - dist) / feather;
	}

	void indexRange(WorldHeightMapEdit *map, Int &ix0, Int &iy0, Int &ix1, Int &iy1) const
	{
		ix0 = (Int)floor(x0 / MAP_XY_FACTOR) + map->getBorderSize();
		iy0 = (Int)floor(y0 / MAP_XY_FACTOR) + map->getBorderSize();
		ix1 = (Int)ceil(x1 / MAP_XY_FACTOR) + map->getBorderSize();
		iy1 = (Int)ceil(y1 / MAP_XY_FACTOR) + map->getBorderSize();
		if (ix0 < 0) ix0 = 0;
		if (iy0 < 0) iy0 = 0;
		if (ix1 > map->getXExtent() - 1) ix1 = map->getXExtent() - 1;
		if (iy1 > map->getYExtent() - 1) iy1 = map->getYExtent() - 1;
	}
};
