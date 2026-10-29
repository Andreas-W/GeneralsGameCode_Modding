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

// FILE: PostProcess.cpp ///////////////////////////////////////////////////////
// Post-processing settings from INI.
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/INI.h"
#include "GameClient/PostProcess.h"

PostProcessSettings* ThePostProcessSettings = nullptr;
ShadowMapSettings* TheShadowMapSettings = nullptr;
MaterialSettings* TheMaterialSettings = nullptr;

//-------------------------------------------------------------------------------------------------
static void parseInputs( INI *ini, void *instance, void *store, const void *userData )
{
	std::vector<AsciiString> *inputs = (std::vector<AsciiString> *)store;
	inputs->clear();
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
		inputs->push_back(token);
}

//-------------------------------------------------------------------------------------------------
/** Param = <name> <value> [<value> ...]; a later Param of the same name replaces the earlier one. */
//-------------------------------------------------------------------------------------------------
static void parseParameter( INI *ini, void *instance, void *store, const void *userData )
{
	std::vector<PostProcessParameter> *parameters = (std::vector<PostProcessParameter> *)store;
	PostProcessParameter parameter;
	parameter.m_name = ini->getNextToken();
	parameter.m_count = 0;
	for (const char *token = ini->getNextTokenOrNull(); token && parameter.m_count < 4; token = ini->getNextTokenOrNull())
		parameter.m_values[parameter.m_count++] = INI::scanReal(token);
	if (parameter.m_count == 0)
		throw INI_INVALID_DATA;

	for (size_t i = 0; i < parameters->size(); ++i)
	{
		if ((*parameters)[i].m_name.compareNoCase(parameter.m_name) == 0)
		{
			(*parameters)[i] = parameter;
			return;
		}
	}
	parameters->push_back(parameter);
}

//-------------------------------------------------------------------------------------------------
PostProcessPassTemplate::PostProcessPassTemplate() :
	m_scale(1),
	m_hdr(FALSE)
{
}

const FieldParse PostProcessPassTemplate::s_fieldParseTable[] =
{
	{ "Shader",		INI::parseAsciiString,	nullptr,	offsetof( PostProcessPassTemplate, m_shader ) },
	{ "Inputs",		parseInputs,			nullptr,	offsetof( PostProcessPassTemplate, m_inputs ) },
	{ "Scale",		INI::parseInt,			nullptr,	offsetof( PostProcessPassTemplate, m_scale ) },
	{ "HDR",		INI::parseBool,			nullptr,	offsetof( PostProcessPassTemplate, m_hdr ) },
	{ "Param",		parseParameter,			nullptr,	offsetof( PostProcessPassTemplate, m_parameters ) },
	{ nullptr, nullptr, nullptr, 0 },
};

//-------------------------------------------------------------------------------------------------
const FieldParse PostProcessSettings::s_fieldParseTable[] =
{
	{ "Passes",		parseInputs,			nullptr,	offsetof( PostProcessSettings, m_chain ) },
	{ "HDRScene",	INI::parseBool,			nullptr,	offsetof( PostProcessSettings, m_hdrScene ) },
	{ nullptr, nullptr, nullptr, 0 },
};

//-------------------------------------------------------------------------------------------------
PostProcessSettings::PostProcessSettings() :
	m_hdrScene(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
PostProcessSettings::~PostProcessSettings()
{
	for (size_t i = 0; i < m_passes.size(); ++i)
		delete m_passes[i];
}

//-------------------------------------------------------------------------------------------------
const PostProcessPassTemplate* PostProcessSettings::findPass(const AsciiString& name) const
{
	for (size_t i = 0; i < m_passes.size(); ++i)
		if (m_passes[i]->m_name.compareNoCase(name) == 0)
			return m_passes[i];
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
PostProcessPassTemplate* PostProcessSettings::findOrCreatePass(const AsciiString& name)
{
	for (size_t i = 0; i < m_passes.size(); ++i)
		if (m_passes[i]->m_name.compareNoCase(name) == 0)
			return m_passes[i];
	PostProcessPassTemplate *pass = new PostProcessPassTemplate;
	pass->m_name = name;
	m_passes.push_back(pass);
	return pass;
}

//-------------------------------------------------------------------------------------------------
/** A pass defined again (for example by the mod after the defaults) is replaced as a whole. */
//-------------------------------------------------------------------------------------------------
void INI::parsePostProcessPassDefinition( INI *ini )
{
	if (ThePostProcessSettings == nullptr)
		ThePostProcessSettings = new PostProcessSettings;

	AsciiString name = ini->getNextToken();
	PostProcessPassTemplate *pass = ThePostProcessSettings->findOrCreatePass(name);
	*pass = PostProcessPassTemplate();
	pass->m_name = name;
	ini->initFromINI( pass, PostProcessPassTemplate::s_fieldParseTable );
}

//-------------------------------------------------------------------------------------------------
ShadowMapSettings::ShadowMapSettings() :
	m_enabled(TRUE),
	m_size(2048),
	m_softness(1.5f),
	m_bias(2.0f),
	m_depthBias(0),
	m_slopeBias(2.0f),
	m_darkness(1.0f),
	m_minSunElevation(0.0f)
{
}

const FieldParse ShadowMapSettings::s_fieldParseTable[] =
{
	{ "Enabled",	INI::parseBool,			nullptr,	offsetof( ShadowMapSettings, m_enabled ) },
	{ "Size",		INI::parseInt,			nullptr,	offsetof( ShadowMapSettings, m_size ) },
	{ "Softness",	INI::parseReal,			nullptr,	offsetof( ShadowMapSettings, m_softness ) },
	{ "Bias",		INI::parseReal,			nullptr,	offsetof( ShadowMapSettings, m_bias ) },
	{ "DepthBias",	INI::parseInt,			nullptr,	offsetof( ShadowMapSettings, m_depthBias ) },
	{ "SlopeBias",	INI::parseReal,			nullptr,	offsetof( ShadowMapSettings, m_slopeBias ) },
	{ "Darkness",	INI::parseReal,			nullptr,	offsetof( ShadowMapSettings, m_darkness ) },
	{ "MinSunElevation",	INI::parseReal,	nullptr,	offsetof( ShadowMapSettings, m_minSunElevation ) },
	{ nullptr, nullptr, nullptr, 0 },
};

//-------------------------------------------------------------------------------------------------
void INI::parseShadowMapDefinition( INI *ini )
{
	if (TheShadowMapSettings == nullptr)
		TheShadowMapSettings = new ShadowMapSettings;
	ini->initFromINI( TheShadowMapSettings, ShadowMapSettings::s_fieldParseTable );
}

//-------------------------------------------------------------------------------------------------
MaterialTemplate::MaterialTemplate()
{
	m_shader = "MAT_Standard";
	// Not _n, _s, _e: the game's own night, snow and other variants of building textures use those.
	m_maps[0] = "_nrm";
	m_maps[1] = "_spec";
	m_maps[2] = "_emis";
	m_maps[3] = "None";
}

const FieldParse MaterialTemplate::s_fieldParseTable[] =
{
	{ "Textures",		parseInputs,			nullptr,	offsetof( MaterialTemplate, m_textures ) },
	{ "Shader",			INI::parseAsciiString,	nullptr,	offsetof( MaterialTemplate, m_shader ) },
	{ "NormalMap",		INI::parseAsciiString,	nullptr,	offsetof( MaterialTemplate, m_maps[0] ) },
	{ "SpecularMap",	INI::parseAsciiString,	nullptr,	offsetof( MaterialTemplate, m_maps[1] ) },
	{ "EmissiveMap",	INI::parseAsciiString,	nullptr,	offsetof( MaterialTemplate, m_maps[2] ) },
	{ "ExtraMap",		INI::parseAsciiString,	nullptr,	offsetof( MaterialTemplate, m_maps[3] ) },
	{ "Param",			parseParameter,			nullptr,	offsetof( MaterialTemplate, m_parameters ) },
	{ nullptr, nullptr, nullptr, 0 },
};

//-------------------------------------------------------------------------------------------------
MaterialSettings::~MaterialSettings()
{
	for (size_t i = 0; i < m_materials.size(); ++i)
		delete m_materials[i];
}

//-------------------------------------------------------------------------------------------------
MaterialTemplate* MaterialSettings::findOrCreateMaterial(const AsciiString& name)
{
	for (size_t i = 0; i < m_materials.size(); ++i)
		if (m_materials[i]->m_name.compareNoCase(name) == 0)
			return m_materials[i];
	MaterialTemplate *material = new MaterialTemplate;
	material->m_name = name;
	m_materials.push_back(material);
	return material;
}

//-------------------------------------------------------------------------------------------------
/** A material defined again (for example by the mod after the defaults) is replaced as a whole. */
//-------------------------------------------------------------------------------------------------
void INI::parseMaterialDefinition( INI *ini )
{
	if (TheMaterialSettings == nullptr)
		TheMaterialSettings = new MaterialSettings;

	AsciiString name = ini->getNextToken();
	MaterialTemplate *material = TheMaterialSettings->findOrCreateMaterial(name);
	*material = MaterialTemplate();
	material->m_name = name;
	ini->initFromINI( material, MaterialTemplate::s_fieldParseTable );
}

//-------------------------------------------------------------------------------------------------
void INI::parsePostProcessDefinition( INI *ini )
{
	if (ThePostProcessSettings == nullptr)
		ThePostProcessSettings = new PostProcessSettings;
	ini->initFromINI( ThePostProcessSettings, PostProcessSettings::s_fieldParseTable );
}
