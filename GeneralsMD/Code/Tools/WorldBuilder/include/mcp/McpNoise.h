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

// McpNoise.h
// Seeded Perlin noise for procedural terrain in the MCP bridge.
//
// Based on PerlinNoise.cs from Genesis, Copyright 2013 The CWC Team, licensed under the Apache
// License, Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0), itself following Ken
// Perlin's reference implementation. Changes: seeded with our own PRNG, 2D sampling helpers,
// ridged variant.

#pragma once

class McpNoise
{
public:
	explicit McpNoise(unsigned int seed);

	/// Raw 3D Perlin noise, roughly in -1..1.
	double noise(double x, double y, double z) const;

	/// Fractal sum over octaves: frequency doubles and amplitude scales by persistence per octave.
	/// Like Genesis the result is clamped, to 0..1 (or -1..1 when signedResult is set).
	double fractal(double x, double y, double frequency, int octaves, double persistence, double amplitude, bool signedResult) const;

	/// Ridged variant (sharp crests), 0..1 before amplitude, clamped to 0..1.
	double ridged(double x, double y, double frequency, int octaves, double persistence, double amplitude) const;

private:
	int m_p[512];
	double m_z;	///< Fixed slice through the 3D noise, derived from the seed.
};
