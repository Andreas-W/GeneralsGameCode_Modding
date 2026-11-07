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

// What fields a ParticleSystem block and each FXList nugget can have, and of what kind, so the
// editor can show a fitting control. The particle system fields come from the game's own parse
// table, so fields added to the engine show up without changes here.

#pragma once

#include "Lib/BaseType.h"

#include <string>
#include <vector>

enum FieldType
{
	FIELD_STRING,
	FIELD_REAL,
	FIELD_INT,
	FIELD_UINT,
	FIELD_BOOL,
	FIELD_ENUM,					///< one of enumNames
	FIELD_RANDOM,				///< "min max" (GameClientRandomVariable)
	FIELD_COORD3D,				///< "X:1 Y:2 Z:3"
	FIELD_RGB,					///< "R:255 G:255 B:255"
	FIELD_ALPHA_KEY,			///< "low high frame"
	FIELD_COLOR_KEY,			///< "R:255 G:255 B:255 frame"
	FIELD_DURATION,			///< milliseconds
	FIELD_PERCENT,				///< "50%"
	FIELD_ANGLE,				///< degrees
	FIELD_TEXTURE,				///< particle texture or model name
	FIELD_PARTICLE_SYSTEM,	///< name of a ParticleSystem
	FIELD_FXLIST,				///< name of an FXList
	FIELD_SOUND,				///< name of an audio event
	FIELD_OBJECT				///< name of a thing template
};

struct FieldDef
{
	std::string name;
	FieldType type;
	const char *const *enumNames;	///< null-terminated, for FIELD_ENUM
	std::string group;				///< section in the inspector
	std::string help;
};

namespace FieldSchema
{
	/// The fields of a ParticleSystem block, in the order of the game's parse table.
	const std::vector<FieldDef> &particleSystemFields();

	/// The nugget types of an FXList.
	const std::vector<std::string> &nuggetTypes();
	/// The fields of one nugget type; empty for an unknown type.
	const std::vector<FieldDef> &nuggetFields(const std::string &type);

	const FieldDef *findParticleField(const char *name);
	const FieldDef *findNuggetField(const std::string &type, const char *name);

	/// Whether a particle system field matters for the current values of the block (VelocityType,
	/// VolumeType, WindMotion pick which of their fields are used).
	Bool isParticleFieldRelevant(const FieldDef &field, const std::string &velocityType, const std::string &volumeType,
		const std::string &windMotion);

	const char *typeName(FieldType type);

	// Value text <-> numbers, in the formats the INI parser reads.
	Bool parseFloats(const std::string &text, float *values, Int count);
	Bool parseLabeled(const std::string &text, const char *labels, float *values);	///< "R:1 G:2 B:3" with labels "RGB"
	std::string formatReal(float v);
	std::string formatRandom(float a, float b);
	std::string formatCoord(const float v[3]);
	std::string formatRGB(const float v[3]);
	std::string formatAlphaKey(float low, float high, Int frame);
	std::string formatColorKey(const float rgb[3], Int frame);
}
