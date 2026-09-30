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

// McpNoise.cpp
// Based on PerlinNoise.cs from Genesis, Copyright 2013 The CWC Team, Apache License 2.0.

#include "StdAfx.h"
#include "mcp/McpNoise.h"

#include <math.h>

static double fade(double t)
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

static double lerp(double t, double a, double b)
{
	return a + t * (b - a);
}

static double grad(int hash, double x, double y, double z)
{
	// Convert the low 4 bits of the hash into 12 gradient directions.
	const int h = hash & 15;
	const double u = h < 8 ? x : y;
	const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
	return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

McpNoise::McpNoise(unsigned int seed)
{
	// xorshift32; a zero state would stay zero.
	unsigned int state = seed * 2654435761u + 0x9E3779B9u;
	if (state == 0) state = 1;
	int perm[256];
	for (int i = 0; i < 256; i++) {
		perm[i] = i;
	}
	for (int i = 255; i > 0; i--) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		const int j = (int)(state % (unsigned int)(i + 1));
		const int t = perm[i];
		perm[i] = perm[j];
		perm[j] = t;
	}
	for (int i = 0; i < 256; i++) {
		m_p[i] = m_p[i + 256] = perm[i];
	}
	m_z = (seed % 997) * 0.618 + 0.5;
}

double McpNoise::noise(double x, double y, double z) const
{
	const int X = (int)floor(x) & 255;
	const int Y = (int)floor(y) & 255;
	const int Z = (int)floor(z) & 255;
	x -= floor(x);
	y -= floor(y);
	z -= floor(z);
	const double u = fade(x);
	const double v = fade(y);
	const double w = fade(z);
	const int A = m_p[X] + Y, AA = m_p[A] + Z, AB = m_p[A + 1] + Z;
	const int B = m_p[X + 1] + Y, BA = m_p[B] + Z, BB = m_p[B + 1] + Z;
	return lerp(w, lerp(v, lerp(u, grad(m_p[AA], x, y, z), grad(m_p[BA], x - 1, y, z)),
	                       lerp(u, grad(m_p[AB], x, y - 1, z), grad(m_p[BB], x - 1, y - 1, z))),
	               lerp(v, lerp(u, grad(m_p[AA + 1], x, y, z - 1), grad(m_p[BA + 1], x - 1, y, z - 1)),
	                       lerp(u, grad(m_p[AB + 1], x, y - 1, z - 1), grad(m_p[BB + 1], x - 1, y - 1, z - 1))));
}

double McpNoise::fractal(double x, double y, double frequency, int octaves, double persistence, double amplitude, bool signedResult) const
{
	double sum = 0;
	double amp = amplitude;
	double freq = frequency;
	for (int i = 0; i < octaves; i++) {
		sum += noise(x * freq, y * freq, m_z) * amp;
		freq *= 2;
		amp *= persistence;
	}
	const double lo = signedResult ? -1.0 : 0.0;
	if (sum < lo) return lo;
	if (sum > 1.0) return 1.0;
	return sum;
}

double McpNoise::ridged(double x, double y, double frequency, int octaves, double persistence, double amplitude) const
{
	double sum = 0;
	double total = 0;
	double amp = 1;
	double freq = frequency;
	for (int i = 0; i < octaves; i++) {
		const double n = 1.0 - fabs(noise(x * freq, y * freq, m_z));
		sum += n * n * amp;
		total += amp;
		freq *= 2;
		amp *= persistence;
	}
	double v = total > 0 ? sum / total * amplitude : 0;
	if (v < 0) return 0;
	if (v > 1) return 1;
	return v;
}
