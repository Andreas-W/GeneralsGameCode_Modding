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

#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameClient/PostProcess.h"
#include "GameLogic/GameLogic.h"

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
	{ "ParamMorning",	parseParameter,		nullptr,	offsetof( PostProcessPassTemplate, m_timeOfDayParameters[TIME_OF_DAY_MORNING] ) },
	{ "ParamAfternoon",	parseParameter,		nullptr,	offsetof( PostProcessPassTemplate, m_timeOfDayParameters[TIME_OF_DAY_AFTERNOON] ) },
	{ "ParamEvening",	parseParameter,		nullptr,	offsetof( PostProcessPassTemplate, m_timeOfDayParameters[TIME_OF_DAY_EVENING] ) },
	{ "ParamNight",		parseParameter,		nullptr,	offsetof( PostProcessPassTemplate, m_timeOfDayParameters[TIME_OF_DAY_NIGHT] ) },
	{ nullptr, nullptr, nullptr, 0 },
};

//-------------------------------------------------------------------------------------------------
void PostProcessPassTemplate::getParameters(TimeOfDay time, std::vector<PostProcessParameter>& parameters) const
{
	parameters = m_parameters;
	if (time < 0 || time >= TIME_OF_DAY_COUNT)
		return;
	const std::vector<PostProcessParameter>& replacements = m_timeOfDayParameters[time];
	for (size_t i = 0; i < replacements.size(); ++i)
	{
		size_t j = 0;
		while (j < parameters.size() && parameters[j].m_name.compareNoCase(replacements[i].m_name) != 0)
			++j;
		if (j < parameters.size())
			parameters[j] = replacements[i];
		else
			parameters.push_back(replacements[i]);
	}
}

//-------------------------------------------------------------------------------------------------
const FieldParse PostProcessChainTemplate::s_fieldParseTable[] =
{
	{ "Passes",		parseInputs,			nullptr,	offsetof( PostProcessChainTemplate, m_passes ) },
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
	for (size_t i = 0; i < m_namedChains.size(); ++i)
		delete m_namedChains[i];
}

//-------------------------------------------------------------------------------------------------
PostProcessSettings* PostProcessSettings::clone() const
{
	PostProcessSettings *copy = new PostProcessSettings;
	copy->m_chain = m_chain;
	copy->m_hdrScene = m_hdrScene;
	for (size_t i = 0; i < m_passes.size(); ++i)
		copy->m_passes.push_back(new PostProcessPassTemplate(*m_passes[i]));
	for (size_t i = 0; i < m_namedChains.size(); ++i)
		copy->m_namedChains.push_back(new PostProcessChainTemplate(*m_namedChains[i]));
	return copy;
}

//-------------------------------------------------------------------------------------------------
PostProcessChainTemplate* PostProcessSettings::findOrCreateChain(const AsciiString& name)
{
	for (size_t i = 0; i < m_namedChains.size(); ++i)
		if (m_namedChains[i]->m_name.compareNoCase(name) == 0)
			return m_namedChains[i];
	PostProcessChainTemplate *chain = new PostProcessChainTemplate;
	chain->m_name = name;
	m_namedChains.push_back(chain);
	return chain;
}

//-------------------------------------------------------------------------------------------------
const std::vector<AsciiString>* PostProcessSettings::findChain(const AsciiString& name) const
{
	if (name.isEmpty() || name.compareNoCase("Default") == 0)
		return &m_chain;
	for (size_t i = 0; i < m_namedChains.size(); ++i)
		if (m_namedChains[i]->m_name.compareNoCase(name) == 0)
			return &m_namedChains[i]->m_passes;
	return nullptr;
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
	m_minSunElevation(0.0f),
	m_terrain(TRUE),
	m_trees(TRUE),
	m_maxUpdateRate(60.0f),
	m_detailCascade(TRUE),
	m_detailCoverage(0.3f),
	m_detailSize(0)
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
	{ "Terrain",	INI::parseBool,			nullptr,	offsetof( ShadowMapSettings, m_terrain ) },
	{ "Trees",		INI::parseBool,			nullptr,	offsetof( ShadowMapSettings, m_trees ) },
	{ "MaxUpdateRate",	INI::parseReal,		nullptr,	offsetof( ShadowMapSettings, m_maxUpdateRate ) },
	{ "DetailCascade",	INI::parseBool,		nullptr,	offsetof( ShadowMapSettings, m_detailCascade ) },
	{ "DetailCoverage",	INI::parseReal,		nullptr,	offsetof( ShadowMapSettings, m_detailCoverage ) },
	{ "DetailSize",	INI::parseInt,			nullptr,	offsetof( ShadowMapSettings, m_detailSize ) },
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
static const char *const s_materialUseNames[] = { "MESHES", "PARTICLES", "TERRAIN", "WATER", nullptr };

MaterialTemplate::MaterialTemplate()
{
	m_use = USE_MESHES;
	m_shader = "MAT_Standard";
	// Not _n, _s, _e: the game's own night, snow and other variants of building textures use those.
	m_maps[0] = "_nrm";
	m_maps[1] = "_spec";
	m_maps[2] = "_emis";
	m_maps[3] = "None";
}

const FieldParse MaterialTemplate::s_fieldParseTable[] =
{
	{ "Use",			INI::parseIndexList,	s_materialUseNames,	offsetof( MaterialTemplate, m_use ) },
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

//-------------------------------------------------------------------------------------------------
void INI::parsePostProcessChainDefinition( INI *ini )
{
	if (ThePostProcessSettings == nullptr)
		ThePostProcessSettings = new PostProcessSettings;
	AsciiString name = ini->getNextToken();
	PostProcessChainTemplate *chain = ThePostProcessSettings->findOrCreateChain(name);
	chain->m_passes.clear();
	ini->initFromINI( chain, PostProcessChainTemplate::s_fieldParseTable );
}

//-------------------------------------------------------------------------------------------------
PostProcessControl ThePostProcessControl;

PostProcessControl::PostProcessControl() :
	m_savedPostProcess(nullptr),
	m_savedShadowMap(nullptr),
	m_saved(FALSE),
	m_generation(1),
	m_darknessFaded(FALSE)
{
	memset(&m_darkness, 0, sizeof(m_darkness));
}

PostProcessControl::~PostProcessControl()
{
	delete m_savedPostProcess;
	delete m_savedShadowMap;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::beginMap()
{
	// A game that was not ended properly leaves its map's settings behind: those go first.
	if (m_saved)
		endGame();
	m_savedPostProcess = ThePostProcessSettings != nullptr ? ThePostProcessSettings->clone() : nullptr;
	m_savedShadowMap = TheShadowMapSettings != nullptr ? new ShadowMapSettings(*TheShadowMapSettings) : nullptr;
	m_saved = TRUE;
	clearScriptChanges();
	++m_generation;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::endGame()
{
	if (m_saved)
	{
		delete ThePostProcessSettings;
		ThePostProcessSettings = m_savedPostProcess;
		delete TheShadowMapSettings;
		TheShadowMapSettings = m_savedShadowMap;
		m_savedPostProcess = nullptr;
		m_savedShadowMap = nullptr;
		m_saved = FALSE;
	}
	clearScriptChanges();
	++m_generation;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::settingsReloaded()
{
	delete m_savedPostProcess;
	delete m_savedShadowMap;
	m_savedPostProcess = nullptr;
	m_savedShadowMap = nullptr;
	m_saved = FALSE;
	++m_generation;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::clearScriptChanges()
{
	m_chainName.clear();
	m_disabledPasses.clear();
	m_overrides.clear();
	m_darknessFaded = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Fades follow the game's logic frames, so they stop while the game is paused. */
//-------------------------------------------------------------------------------------------------
void PostProcessControl::valueOf(const Fade& fade, Real* values)
{
	Real t = 1.0f;
	if (fade.frames > 0 && TheGameLogic != nullptr)
	{
		const UnsignedInt frame = TheGameLogic->getFrame();
		t = frame <= fade.startFrame ? 0.0f : min(1.0f, (Real)(frame - fade.startFrame) / (Real)fade.frames);
	}
	for (Int i = 0; i < fade.count; ++i)
		values[i] = fade.from[i] + (fade.to[i] - fade.from[i]) * t;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::setParameter(const AsciiString& pass, const AsciiString& name, const Real* values, Int count, Real seconds)
{
	count = max(1, min(count, 4));
	Fade fade;
	memset(&fade, 0, sizeof(fade));
	fade.count = count;
	for (Int i = 0; i < count; ++i)
		fade.to[i] = fade.from[i] = values[i];
	fade.startFrame = TheGameLogic != nullptr ? TheGameLogic->getFrame() : 0;
	fade.frames = seconds > 0.0f ? (UnsignedInt)(seconds * LOGICFRAMES_PER_SECOND + 0.5f) : 0;

	// The fade starts from the value the parameter has now: an earlier script value, else the INI value.
	size_t index = 0;
	while (index < m_overrides.size() && !(m_overrides[index].pass.compareNoCase(pass) == 0 && m_overrides[index].name.compareNoCase(name) == 0))
		++index;
	if (index < m_overrides.size())
	{
		Real now[4];
		valueOf(m_overrides[index].fade, now);
		for (Int i = 0; i < count && i < m_overrides[index].fade.count; ++i)
			fade.from[i] = now[i];
	}
	else if (ThePostProcessSettings != nullptr)
	{
		const PostProcessPassTemplate *passTemplate = ThePostProcessSettings->findPass(pass);
		if (passTemplate != nullptr)
		{
			std::vector<PostProcessParameter> parameters;
			passTemplate->getParameters(TheGlobalData->m_timeOfDay, parameters);
			for (size_t i = 0; i < parameters.size(); ++i)
				if (parameters[i].m_name.compareNoCase(name) == 0)
					for (Int j = 0; j < count && j < parameters[i].m_count; ++j)
						fade.from[j] = parameters[i].m_values[j];
		}
	}

	if (index == m_overrides.size())
	{
		Override added;
		added.pass = pass;
		added.name = name;
		m_overrides.push_back(added);
	}
	m_overrides[index].fade = fade;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::enablePass(const AsciiString& pass, Bool enable)
{
	for (size_t i = 0; i < m_disabledPasses.size(); ++i)
	{
		if (m_disabledPasses[i].compareNoCase(pass) == 0)
		{
			if (enable)
			{
				m_disabledPasses.erase(m_disabledPasses.begin() + i);
				++m_generation;
			}
			return;
		}
	}
	if (!enable)
	{
		m_disabledPasses.push_back(pass);
		++m_generation;
	}
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::useChain(const AsciiString& name)
{
	m_chainName = name.compareNoCase("Default") == 0 ? AsciiString::TheEmptyString : name;
	++m_generation;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::setShadowDarkness(Real darkness, Real seconds)
{
	Fade fade;
	memset(&fade, 0, sizeof(fade));
	fade.count = 1;
	fade.from[0] = getShadowDarkness(TheShadowMapSettings != nullptr ? TheShadowMapSettings->m_darkness : 1.0f);
	fade.to[0] = darkness;
	fade.startFrame = TheGameLogic != nullptr ? TheGameLogic->getFrame() : 0;
	fade.frames = seconds > 0.0f ? (UnsignedInt)(seconds * LOGICFRAMES_PER_SECOND + 0.5f) : 0;
	m_darkness = fade;
	m_darknessFaded = TRUE;
}

//-------------------------------------------------------------------------------------------------
const std::vector<AsciiString>* PostProcessControl::getChain() const
{
	if (ThePostProcessSettings == nullptr)
		return nullptr;
	const std::vector<AsciiString> *chain = ThePostProcessSettings->findChain(m_chainName);
	// An unknown chain name keeps the default chain.
	return chain != nullptr ? chain : &ThePostProcessSettings->m_chain;
}

//-------------------------------------------------------------------------------------------------
Bool PostProcessControl::isPassEnabled(const AsciiString& pass) const
{
	for (size_t i = 0; i < m_disabledPasses.size(); ++i)
		if (m_disabledPasses[i].compareNoCase(pass) == 0)
			return FALSE;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::getParameterOverride(Int index, AsciiString& pass, AsciiString& name, Real* values, Int& count) const
{
	const Override& entry = m_overrides[index];
	pass = entry.pass;
	name = entry.name;
	count = entry.fade.count;
	valueOf(entry.fade, values);
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::xferFade(Xfer *xfer, Fade& fade)
{
	xfer->xferInt(&fade.count);
	for (Int i = 0; i < 4; ++i)
	{
		xfer->xferReal(&fade.from[i]);
		xfer->xferReal(&fade.to[i]);
	}
	xfer->xferUnsignedInt(&fade.startFrame);
	xfer->xferUnsignedInt(&fade.frames);
	fade.count = max(0, min(fade.count, 4));
}

//-------------------------------------------------------------------------------------------------
/** The script changes; fades keep their start frame, as the save game keeps the logic frame. */
//-------------------------------------------------------------------------------------------------
void PostProcessControl::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferAsciiString(&m_chainName);

	Int count = (Int)m_disabledPasses.size();
	xfer->xferInt(&count);
	if (xfer->getXferMode() == XFER_LOAD)
		m_disabledPasses.resize(count);
	for (Int i = 0; i < count; ++i)
		xfer->xferAsciiString(&m_disabledPasses[i]);

	count = (Int)m_overrides.size();
	xfer->xferInt(&count);
	if (xfer->getXferMode() == XFER_LOAD)
		m_overrides.resize(count);
	for (Int i = 0; i < count; ++i)
	{
		xfer->xferAsciiString(&m_overrides[i].pass);
		xfer->xferAsciiString(&m_overrides[i].name);
		xferFade(xfer, m_overrides[i].fade);
	}

	xfer->xferBool(&m_darknessFaded);
	xferFade(xfer, m_darkness);
}

//-------------------------------------------------------------------------------------------------
void PostProcessControl::loadPostProcess()
{
	++m_generation;
}

//-------------------------------------------------------------------------------------------------
Real PostProcessControl::getShadowDarkness(Real darkness) const
{
	if (!m_darknessFaded)
		return darkness;
	Real value;
	valueOf(m_darkness, &value);
	return value;
}
