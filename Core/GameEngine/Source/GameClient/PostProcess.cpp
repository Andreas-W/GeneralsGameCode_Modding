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
	{ nullptr, nullptr, nullptr, 0 },
};

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
void INI::parsePostProcessDefinition( INI *ini )
{
	if (ThePostProcessSettings == nullptr)
		ThePostProcessSettings = new PostProcessSettings;
	ini->initFromINI( ThePostProcessSettings, PostProcessSettings::s_fieldParseTable );
}
