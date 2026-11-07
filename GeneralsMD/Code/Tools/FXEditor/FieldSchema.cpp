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

#include "Precompiled/PreRTS.h"

#include "FieldSchema.h"

#include "Common/INI.h"
#include "GameClient/ParticleSys.h"

#include <map>
#include <stdio.h>
#include <stdlib.h>

namespace
{

const char *const ALLOWED_SURFACE_NAMES[] = { "ALL", "LAND", "WATER", nullptr };
const char *const SHAKE_NAMES[] = { "SUBTLE", "NORMAL", "STRONG", "SEVERE", "CINE_EXTREME", "CINE_INSANE", nullptr };
const char *const SCORCH_NAMES[] = { "SCORCH_1", "SCORCH_2", "SCORCH_3", "SCORCH_4", "SHADOW_SCORCH", "RANDOM", nullptr };

FieldDef def(const char *name, FieldType type, const char *help, const char *const *enumNames = nullptr)
{
	FieldDef d;
	d.name = name;
	d.type = type;
	d.enumNames = enumNames;
	d.help = help;
	return d;
}

/// The section of the inspector a particle system field is shown in.
const char *particleGroup(const std::string &name)
{
	struct Prefix { const char *prefix; const char *group; };
	static const Prefix prefixes[] = {
		{ "Alpha", "Alpha keyframes" },
		{ "Color", "Color keyframes" },
		{ "Vel", "Velocity" },
		{ "DriftVelocity", "Velocity" },
		{ "Gravity", "Velocity" },
		{ "Vol", "Emission volume" },
		{ "IsHollow", "Emission volume" },
		{ "Wind", "Wind" },
		{ "Burst", "Emission" },
		{ "InitialDelay", "Emission" },
		{ "SystemLifetime", "Emission" },
		{ "IsEmitAboveGroundOnly", "Emission" },
		{ "Lifetime", "Particles" },
		{ "Size", "Particles" },
		{ "StartSizeRate", "Particles" },
		{ "Angle", "Particles" },
		{ "Angular", "Particles" },
		{ "IsGroundAligned", "Particles" },
		{ "IsParticleUpTowardsEmitter", "Particles" },
		{ "Slave", "Attached systems" },
		{ "PerParticleAttachedSystem", "Attached systems" },
	};
	for (size_t i = 0; i < ARRAY_SIZE(prefixes); ++i)
	{
		if (name.compare(0, strlen(prefixes[i].prefix), prefixes[i].prefix) == 0)
			return prefixes[i].group;
	}
	return "General";
}

const char *particleHelp(const std::string &name)
{
	struct Help { const char *name; const char *help; };
	static const Help helps[] = {
		{ "Priority", "Which particles are dropped first when there are too many." },
		{ "IsOneShot", "Emit one burst, then stop." },
		{ "Shader", "How the particles blend with what is behind them." },
		{ "Type", "PARTICLE (texture), DRAWABLE, STREAK, VOLUME_PARTICLE or SMUDGE." },
		{ "ParticleName", "Texture (.tga) of the particles, or model for drawables." },
		{ "Lifetime", "Frames each particle lives (30 per second), random between min and max." },
		{ "SystemLifetime", "Frames the system emits; 0 forever." },
		{ "Size", "Start size of a particle." },
		{ "SizeRate", "Size added per frame." },
		{ "StartSizeRate", "Size added per frame at the start." },
		{ "SizeRateDamping", "SizeRate is multiplied with this every frame." },
		{ "BurstDelay", "Frames between bursts." },
		{ "BurstCount", "Particles per burst." },
		{ "InitialDelay", "Frames before the first burst." },
		{ "ColorScale", "Added to the colour every frame." },
		{ "Gravity", "Added to the vertical speed every frame (negative falls)." },
		{ "VelocityDamping", "Speed is multiplied with this every frame." },
		{ "AngularDamping", "Rotation speed is multiplied with this every frame." },
		{ "SlaveSystem", "A second system that is started and moved with this one." },
		{ "PerParticleAttachedSystem", "A system started on every particle." },
		{ "VolParticleDepth", "Layers of a volume particle." },
	};
	for (size_t i = 0; i < ARRAY_SIZE(helps); ++i)
	{
		if (name == helps[i].name)
			return helps[i].help;
	}
	return "";
}

FieldType particleFieldType(const FieldParse &fp)
{
	const std::string name = fp.token;
	if (name == "SlaveSystem" || name == "PerParticleAttachedSystem")
		return FIELD_PARTICLE_SYSTEM;
	if (name == "ParticleName")
		return FIELD_TEXTURE;
	if (fp.parse == INI::parseIndexList)
		return FIELD_ENUM;
	if (fp.parse == INI::parseBool)
		return FIELD_BOOL;
	if (fp.parse == INI::parseReal)
		return FIELD_REAL;
	if (fp.parse == INI::parseUnsignedInt)
		return FIELD_UINT;
	if (fp.parse == INI::parseInt)
		return FIELD_INT;
	if (fp.parse == INI::parseCoord3D)
		return FIELD_COORD3D;
	if (fp.parse == INI::parseGameClientRandomVariable)
		return FIELD_RANDOM;
	if (fp.parse == ParticleSystemTemplate::parseRandomKeyframe)
		return FIELD_ALPHA_KEY;
	if (fp.parse == ParticleSystemTemplate::parseRGBColorKeyframe)
		return FIELD_COLOR_KEY;
	return FIELD_STRING;
}

typedef std::map<std::string, std::vector<FieldDef> > NuggetMap;

const NuggetMap &nuggetMap()
{
	static NuggetMap map;
	if (!map.empty())
		return map;

	std::vector<FieldDef> &sound = map["Sound"];
	sound.push_back(def("Name", FIELD_SOUND, "Audio event to play."));
	sound.push_back(def("MinAllowedHeight", FIELD_REAL, "Only played at this height or higher."));
	sound.push_back(def("MaxAllowedHeight", FIELD_REAL, "Only played at this height or lower."));
	sound.push_back(def("AllowedSurface", FIELD_ENUM, "Only played over this surface.", ALLOWED_SURFACE_NAMES));

	std::vector<FieldDef> &ray = map["RayEffect"];
	ray.push_back(def("Name", FIELD_OBJECT, "Thing template of the ray (needs a secondary position)."));
	ray.push_back(def("PrimaryOffset", FIELD_COORD3D, "Offset of the start."));
	ray.push_back(def("SecondaryOffset", FIELD_COORD3D, "Offset of the end."));

	std::vector<FieldDef> &tracer = map["Tracer"];
	tracer.push_back(def("TracerName", FIELD_OBJECT, "Thing template of the tracer."));
	tracer.push_back(def("BoneName", FIELD_STRING, "Bone it starts from."));
	tracer.push_back(def("Speed", FIELD_REAL, "World units per second."));
	tracer.push_back(def("DecayAt", FIELD_REAL, "Fraction of the way where it fades."));
	tracer.push_back(def("Length", FIELD_REAL, ""));
	tracer.push_back(def("Width", FIELD_REAL, ""));
	tracer.push_back(def("Color", FIELD_RGB, ""));
	tracer.push_back(def("Probability", FIELD_REAL, "Chance 0..1 that it is drawn."));

	std::vector<FieldDef> &decal = map["Decal"];
	decal.push_back(def("DecalName", FIELD_OBJECT, "Thing template of a decal; repeat the line for a random choice."));
	decal.push_back(def("Scale", FIELD_RANDOM, ""));
	decal.push_back(def("Lifetime", FIELD_DURATION, "Milliseconds."));
	decal.push_back(def("Offset", FIELD_COORD3D, ""));
	decal.push_back(def("Angle", FIELD_REAL, ""));
	decal.push_back(def("RandomAngle", FIELD_BOOL, ""));
	decal.push_back(def("OrientToObject", FIELD_BOOL, ""));
	decal.push_back(def("Probability", FIELD_REAL, "Chance 0..1."));

	std::vector<FieldDef> &light = map["LightPulse"];
	light.push_back(def("Color", FIELD_RGB, ""));
	light.push_back(def("Radius", FIELD_REAL, ""));
	light.push_back(def("RadiusAsPercentOfObjectSize", FIELD_PERCENT, "Radius from the size of the object (object FX only)."));
	light.push_back(def("IncreaseTime", FIELD_DURATION, "Milliseconds to full brightness."));
	light.push_back(def("DecreaseTime", FIELD_DURATION, "Milliseconds to fade out."));

	std::vector<FieldDef> &shake = map["ViewShake"];
	shake.push_back(def("Type", FIELD_ENUM, "How strongly the camera shakes.", SHAKE_NAMES));

	std::vector<FieldDef> &scorch = map["TerrainScorch"];
	scorch.push_back(def("Type", FIELD_ENUM, "", SCORCH_NAMES));
	scorch.push_back(def("Radius", FIELD_REAL, ""));

	std::vector<FieldDef> &ps = map["ParticleSystem"];
	ps.push_back(def("Name", FIELD_PARTICLE_SYSTEM, "Particle system to start."));
	ps.push_back(def("Count", FIELD_INT, "Number of systems."));
	ps.push_back(def("Offset", FIELD_COORD3D, ""));
	ps.push_back(def("Radius", FIELD_RANDOM, "Systems are placed randomly within this radius."));
	ps.push_back(def("Height", FIELD_RANDOM, ""));
	ps.push_back(def("InitialDelay", FIELD_RANDOM, "Frames before it starts."));
	ps.push_back(def("RotateX", FIELD_ANGLE, "Degrees."));
	ps.push_back(def("RotateY", FIELD_ANGLE, "Degrees."));
	ps.push_back(def("RotateZ", FIELD_ANGLE, "Degrees."));
	ps.push_back(def("OrientToObject", FIELD_BOOL, ""));
	ps.push_back(def("OrientOffset", FIELD_BOOL, ""));
	ps.push_back(def("OrientXY", FIELD_BOOL, ""));
	ps.push_back(def("Ricochet", FIELD_BOOL, ""));
	ps.push_back(def("AttachToObject", FIELD_BOOL, "Follows the object (object FX only)."));
	ps.push_back(def("CreateAtGroundHeight", FIELD_BOOL, ""));
	ps.push_back(def("UseCallersRadius", FIELD_BOOL, ""));
	ps.push_back(def("MinAllowedHeight", FIELD_REAL, ""));
	ps.push_back(def("MaxAllowedHeight", FIELD_REAL, ""));
	ps.push_back(def("AllowedSurface", FIELD_ENUM, "", ALLOWED_SURFACE_NAMES));
	ps.push_back(def("UseCachedSurfaceInfo", FIELD_BOOL, ""));

	std::vector<FieldDef> &bone = map["FXListAtBonePos"];
	bone.push_back(def("FX", FIELD_FXLIST, "FXList played at the bone (object FX only)."));
	bone.push_back(def("BoneName", FIELD_STRING, ""));
	bone.push_back(def("OrientToBone", FIELD_BOOL, ""));

	return map;
}

} // namespace

//-------------------------------------------------------------------------------------------------
const std::vector<FieldDef> &FieldSchema::particleSystemFields()
{
	static std::vector<FieldDef> fields;
	if (!fields.empty() || TheParticleSystemManager == nullptr)
		return fields;

	// Any template has the table.
	ParticleSystemManager::TemplateMap::const_iterator it = TheParticleSystemManager->beginParticleSystemTemplate();
	if (it == TheParticleSystemManager->endParticleSystemTemplate())
		return fields;
	const FieldParse *table = it->second->getFieldParse();
	for (const FieldParse *fp = table; fp->token != nullptr; ++fp)
	{
		FieldDef d;
		d.name = fp->token;
		d.type = particleFieldType(*fp);
		d.enumNames = d.type == FIELD_ENUM ? (const char *const *)fp->userData : nullptr;
		d.group = particleGroup(d.name);
		d.help = particleHelp(d.name);
		fields.push_back(d);
	}
	return fields;
}

//-------------------------------------------------------------------------------------------------
const std::vector<std::string> &FieldSchema::nuggetTypes()
{
	static std::vector<std::string> types;
	if (types.empty())
	{
		const char *const order[] = { "ParticleSystem", "Sound", "LightPulse", "ViewShake", "TerrainScorch", "Tracer", "Decal",
			"RayEffect", "FXListAtBonePos" };
		for (size_t i = 0; i < ARRAY_SIZE(order); ++i)
			types.push_back(order[i]);
	}
	return types;
}

//-------------------------------------------------------------------------------------------------
const std::vector<FieldDef> &FieldSchema::nuggetFields(const std::string &type)
{
	static const std::vector<FieldDef> none;
	const NuggetMap &map = nuggetMap();
	for (NuggetMap::const_iterator it = map.begin(); it != map.end(); ++it)
	{
		if (_stricmp(it->first.c_str(), type.c_str()) == 0)
			return it->second;
	}
	return none;
}

//-------------------------------------------------------------------------------------------------
const FieldDef *FieldSchema::findParticleField(const char *name)
{
	const std::vector<FieldDef> &fields = particleSystemFields();
	for (size_t i = 0; i < fields.size(); ++i)
	{
		if (_stricmp(fields[i].name.c_str(), name) == 0)
			return &fields[i];
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const FieldDef *FieldSchema::findNuggetField(const std::string &type, const char *name)
{
	const std::vector<FieldDef> &fields = nuggetFields(type);
	for (size_t i = 0; i < fields.size(); ++i)
	{
		if (_stricmp(fields[i].name.c_str(), name) == 0)
			return &fields[i];
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool FieldSchema::isParticleFieldRelevant(const FieldDef &field, const std::string &velocityType, const std::string &volumeType,
	const std::string &windMotion)
{
	const std::string &n = field.name;
	struct Rule { const char *field; const char *selector; const char *value; };
	static const Rule rules[] = {
		{ "VelOrthoX", "vel", "ORTHO" }, { "VelOrthoY", "vel", "ORTHO" }, { "VelOrthoZ", "vel", "ORTHO" },
		{ "VelSpherical", "vel", "SPHERICAL" },
		{ "VelHemispherical", "vel", "HEMISPHERICAL" },
		{ "VelCylindricalRadial", "vel", "CYLINDRICAL" }, { "VelCylindricalNormal", "vel", "CYLINDRICAL" },
		{ "VelOutward", "vel", "OUTWARD" }, { "VelOutwardOther", "vel", "OUTWARD" },
		{ "VolLineStart", "vol", "LINE" }, { "VolLineEnd", "vol", "LINE" },
		{ "VolBoxHalfSize", "vol", "BOX" },
		{ "VolSphereRadius", "vol", "SPHERE" },
		{ "VolCylinderRadius", "vol", "CYLINDER" }, { "VolCylinderLength", "vol", "CYLINDER" },
		{ "WindAngleChangeMin", "wind", "Circular" }, { "WindAngleChangeMax", "wind", "Circular" },
		{ "WindPingPongStartAngleMin", "wind", "PingPong" }, { "WindPingPongStartAngleMax", "wind", "PingPong" },
		{ "WindPingPongEndAngleMin", "wind", "PingPong" }, { "WindPingPongEndAngleMax", "wind", "PingPong" },
	};
	for (size_t i = 0; i < ARRAY_SIZE(rules); ++i)
	{
		if (n != rules[i].field)
			continue;
		const std::string &current = rules[i].selector[1] == 'e' ? velocityType : (rules[i].selector[1] == 'o' ? volumeType : windMotion);
		return _stricmp(current.c_str(), rules[i].value) == 0;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
const char *FieldSchema::typeName(FieldType type)
{
	switch (type)
	{
		case FIELD_STRING: return "string";
		case FIELD_REAL: return "real";
		case FIELD_INT: return "int";
		case FIELD_UINT: return "uint";
		case FIELD_BOOL: return "bool";
		case FIELD_ENUM: return "enum";
		case FIELD_RANDOM: return "random (min max)";
		case FIELD_COORD3D: return "coord (X:x Y:y Z:z)";
		case FIELD_RGB: return "color (R:r G:g B:b)";
		case FIELD_ALPHA_KEY: return "alpha keyframe (low high frame)";
		case FIELD_COLOR_KEY: return "color keyframe (R:r G:g B:b frame)";
		case FIELD_DURATION: return "duration (ms)";
		case FIELD_PERCENT: return "percent";
		case FIELD_ANGLE: return "angle (degrees)";
		case FIELD_TEXTURE: return "texture";
		case FIELD_PARTICLE_SYSTEM: return "particle system";
		case FIELD_FXLIST: return "fxlist";
		case FIELD_SOUND: return "sound";
		case FIELD_OBJECT: return "object";
	}
	return "unknown";
}

//-------------------------------------------------------------------------------------------------
Bool FieldSchema::parseFloats(const std::string &text, float *values, Int count)
{
	const char *p = text.c_str();
	for (Int i = 0; i < count; ++i)
	{
		char *end = nullptr;
		values[i] = (float)strtod(p, &end);
		if (end == p)
		{
			// a missing second value of a random variable repeats the first
			if (i > 0)
			{
				for (Int j = i; j < count; ++j)
					values[j] = values[i - 1];
				return TRUE;
			}
			return FALSE;
		}
		p = end;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool FieldSchema::parseLabeled(const std::string &text, const char *labels, float *values)
{
	const size_t count = strlen(labels);
	for (size_t i = 0; i < count; ++i)
	{
		values[i] = 0.0f;
		const char tag[3] = { labels[i], ':', 0 };
		size_t at = text.find(tag);
		if (at == std::string::npos)
		{
			const char lower[3] = { (char)tolower(labels[i]), ':', 0 };
			at = text.find(lower);
		}
		if (at == std::string::npos)
			return FALSE;
		values[i] = (float)atof(text.c_str() + at + 2);
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
std::string FieldSchema::formatReal(float v)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%.2f", v);
	return buf;
}

std::string FieldSchema::formatRandom(float a, float b)
{
	return formatReal(a) + " " + formatReal(b);
}

std::string FieldSchema::formatCoord(const float v[3])
{
	return "X:" + formatReal(v[0]) + " Y:" + formatReal(v[1]) + " Z:" + formatReal(v[2]);
}

std::string FieldSchema::formatRGB(const float v[3])
{
	char buf[64];
	snprintf(buf, sizeof(buf), "R:%d G:%d B:%d", (int)(v[0] + 0.5f), (int)(v[1] + 0.5f), (int)(v[2] + 0.5f));
	return buf;
}

std::string FieldSchema::formatAlphaKey(float low, float high, Int frame)
{
	char buf[96];
	snprintf(buf, sizeof(buf), "%.2f %.2f %d", low, high, frame);
	return buf;
}

std::string FieldSchema::formatColorKey(const float rgb[3], Int frame)
{
	char buf[96];
	snprintf(buf, sizeof(buf), "%s %d", formatRGB(rgb).c_str(), frame);
	return buf;
}
