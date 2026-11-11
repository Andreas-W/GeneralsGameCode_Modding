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

#include "FXEditorMcp.h"
#include "FXEditorApp.h"
#include "FieldSchema.h"
#include "IniDatabase.h"
#include "PassOrder.h"

#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/McpJson.h"
#include "Common/ThingFactory.h"
#include "GameClient/PostProcess.h"
#include "GameClient/FXList.h"
#include "GameClient/MapUtil.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DMaterials.h"
#include "W3DDevice/GameClient/W3DPostProcess.h"

#include "GameMcpCommands.h"

#include <algorithm>
#include <stdio.h>

namespace
{

//-------------------------------------------------------------------------------------------------
// Arguments
//-------------------------------------------------------------------------------------------------

void requireReady()
{
	if (!FXEditorApp::scene().isReady())
		gameMcpFail("the preview game is not running yet (see fx.status)");
	if (!FXEditorApp::ini().isLoaded())
		gameMcpFail("the INI text is not read yet");
}

PreviewKind kindArg(const McpJson &args, const char *defaultKind = nullptr)
{
	const std::string name = defaultKind ? gameMcpArgString(args, "kind", defaultKind) : gameMcpArgString(args, "kind");
	PreviewKind kind;
	if (!previewKindFromName(name.c_str(), kind))
		gameMcpFail("unknown kind '%s' (particle, fxlist or sound)", name.c_str());
	return kind;
}

/// The block named by kind and name; fails if there is no INI text for it.
Int blockArg(const McpJson &args)
{
	requireReady();
	const PreviewKind kind = kindArg(args);
	if (kind == PREVIEW_SOUND)
		gameMcpFail("sounds are defined in the audio INI files, which the editor does not edit");
	const std::string name = gameMcpArgString(args, "name");
	const Int block = FXEditorApp::ini().findBlock(kind, AsciiString(name.c_str()));
	if (block < 0)
		gameMcpFail("no INI text for %s '%s' (fx.list shows what there is)", previewKindName(kind), name.c_str());
	return block;
}

const char *kindHeader(PreviewKind kind)
{
	return kind == PREVIEW_FXLIST ? "FXList" : "ParticleSystem";
}

McpJson coord(const Coord3D &c)
{
	return gameMcpCoord(c);
}

//-------------------------------------------------------------------------------------------------
// Descriptions
//-------------------------------------------------------------------------------------------------

McpJson describeField(const FieldDef &f)
{
	McpJson j = McpJson::makeObject().set("name", f.name).set("type", FieldSchema::typeName(f.type));
	if (f.enumNames)
	{
		McpJson values = McpJson::makeArray();
		for (const char *const *e = f.enumNames; *e; ++e)
			values.push(*e);
		j.set("values", values);
	}
	if (!f.group.empty())
		j.set("group", f.group);
	if (!f.help.empty())
		j.set("help", f.help);
	return j;
}

/// The fields of the block or of one nugget, as they are in the text.
McpJson fieldsOf(const std::string &text, Int nugget)
{
	std::vector<IniLine> lines;
	IniText::parseLines(text, lines);
	McpJson fields = McpJson::makeObject();
	const Int depth = nugget < 0 ? 1 : 2;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (lines[i].type == IniLine::FIELD && lines[i].depth == depth && lines[i].nugget == nugget)
			fields.set(lines[i].key.c_str(), lines[i].value);
	}
	return fields;
}

McpJson describeBlock(Int index, Bool withText)
{
	IniDatabase &ini = FXEditorApp::ini();
	const IniBlock *b = ini.getBlock(index);
	McpJson j = McpJson::makeObject().set("kind", previewKindName(b->kind)).set("name", b->name.str());
	const IniSourceFile *file = ini.getFile(b->file);
	j.set("file", file ? file->path.str() : "(new, not saved)");
	if (file)
		j.set("in_archive", !file->loose);
	j.set("dirty", b->isDirty()).set("undo_steps", (int)b->undo.size()).set("redo_steps", (int)b->redo.size());
	if (!b->error.empty())
		j.set("error", b->error);
	if (withText)
		j.set("text", b->text);
	if (b->kind == PREVIEW_PARTICLE_SYSTEM)
	{
		j.set("fields", fieldsOf(b->text, -1));
	}
	else
	{
		std::vector<std::string> types;
		IniText::getNuggets(b->text, types);
		McpJson nuggets = McpJson::makeArray();
		for (size_t n = 0; n < types.size(); ++n)
			nuggets.push(McpJson::makeObject().set("index", (int)n).set("type", types[n]).set("fields", fieldsOf(b->text, (Int)n)));
		j.set("nuggets", nuggets);
	}
	return j;
}

//-------------------------------------------------------------------------------------------------
// Validation: what the game would silently not find
//-------------------------------------------------------------------------------------------------

Bool textureExists(const std::string &name)
{
	if (name.empty())
		return FALSE;
	std::string base = name;
	const size_t dot = base.find_last_of('.');
	if (dot != std::string::npos)
		base.erase(dot);
	const char *const exts[] = { ".tga", ".dds" };
	for (size_t i = 0; i < ARRAY_SIZE(exts); ++i)
	{
		std::string path = "Art\\Textures\\" + base + exts[i];
		if (TheFileSystem->doesFileExist(path.c_str()))
			return TRUE;
	}
	// Drawable particles name a model.
	std::string w3d = "Art\\W3D\\" + base + ".w3d";
	return TheFileSystem->doesFileExist(w3d.c_str());
}

void checkReference(McpJson &problems, const FieldDef *def, const std::string &where, const std::string &value)
{
	if (def == nullptr || value.empty() || _stricmp(value.c_str(), "None") == 0)
		return;
	const char *what = nullptr;
	switch (def->type)
	{
		case FIELD_PARTICLE_SYSTEM:
			if (TheParticleSystemManager->findTemplate(AsciiString(value.c_str())) == nullptr)
				what = "unknown particle system";
			break;
		case FIELD_FXLIST:
			if (TheFXListStore->findFXList(value.c_str()) == nullptr)
				what = "unknown FXList";
			break;
		case FIELD_SOUND:
			if (TheAudio->findAudioEventInfo(AsciiString(value.c_str())) == nullptr)
				what = "unknown audio event";
			break;
		case FIELD_OBJECT:
			if (TheThingFactory->findTemplate(AsciiString(value.c_str())) == nullptr)
				what = "unknown object";
			break;
		case FIELD_TEXTURE:
			if (!textureExists(value))
				what = "texture or model not found";
			break;
		default:
			break;
	}
	if (what)
		problems.push(McpJson::makeObject().set("where", where).set("field", def->name).set("value", value).set("problem", what));
}

McpJson validateBlock(Int index)
{
	const IniBlock *b = FXEditorApp::ini().getBlock(index);
	McpJson problems = McpJson::makeArray();
	if (!b->error.empty())
		problems.push(McpJson::makeObject().set("where", "block").set("problem", b->error));

	std::vector<IniLine> lines;
	IniText::parseLines(b->text, lines);
	std::vector<std::string> nuggetTypes;
	IniText::getNuggets(b->text, nuggetTypes);
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type != IniLine::FIELD)
			continue;
		const FieldDef *def = nullptr;
		std::string where = "block";
		if (b->kind == PREVIEW_PARTICLE_SYSTEM)
		{
			def = FieldSchema::findParticleField(l.key.c_str());
		}
		else if (l.nugget >= 0 && l.nugget < (Int)nuggetTypes.size())
		{
			char buf[64];
			snprintf(buf, sizeof(buf), "nugget %d (%s)", l.nugget, nuggetTypes[l.nugget].c_str());
			where = buf;
			def = FieldSchema::findNuggetField(nuggetTypes[l.nugget], l.key.c_str());
		}
		if (def == nullptr)
		{
			problems.push(McpJson::makeObject().set("where", where).set("field", l.key).set("problem", "unknown field"));
			continue;
		}
		checkReference(problems, def, where, l.value);
	}
	if (b->kind == PREVIEW_FXLIST)
	{
		for (size_t n = 0; n < nuggetTypes.size(); ++n)
		{
			if (FieldSchema::nuggetFields(nuggetTypes[n]).empty())
			{
				char buf[64];
				snprintf(buf, sizeof(buf), "nugget %d", (int)n);
				problems.push(McpJson::makeObject().set("where", buf).set("problem", "unknown nugget type " + nuggetTypes[n]));
			}
		}
	}
	return problems;
}

//-------------------------------------------------------------------------------------------------
// Commands
//-------------------------------------------------------------------------------------------------

McpJson cmdStatus(const McpJson &)
{
	PreviewScene &scene = FXEditorApp::scene();
	IniDatabase &ini = FXEditorApp::ini();
	McpJson j = McpJson::makeObject()
		.set("ready", scene.isReady())
		.set("map", FXEditorApp::getPreviewMap().str())
		.set("map_key", FXEditorApp::getPreviewMapKey().str())
		.set("faction", FXEditorApp::getPreviewFaction().str())
		.set("map_loading", FXEditorApp::isMapLoading())
		.set("start_error", FXEditorApp::getStartError().str())
		.set("renderer", FXEditorApp::isDX12() ? "dx12" : "dx8")
		.set("frame", TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0u)
		.set("paused", scene.isPaused())
		.set("speed", scene.getSpeed())
		.set("ini_loaded", ini.isLoaded())
		.set("blocks", ini.getBlockCount())
		.set("center", coord(scene.getCenter()));
	if (scene.isPlaying())
		j.set("playing", McpJson::makeObject().set("kind", previewKindName(scene.getKind())).set("name", scene.getName().str()));
	McpJson dirty = McpJson::makeArray();
	for (Int i = 0; i < ini.getBlockCount(); ++i)
	{
		const IniBlock *b = ini.getBlock(i);
		if (b->isDirty())
			dirty.push(McpJson::makeObject().set("kind", previewKindName(b->kind)).set("name", b->name.str()).set("error", b->error));
	}
	j.set("unsaved", dirty);
	return j;
}

McpJson cmdList(const McpJson &args)
{
	requireReady();
	const PreviewKind kind = kindArg(args, "particle");
	const std::string contains = gameMcpArgString(args, "contains", "");
	const Int limit = gameMcpArgInt(args, "limit", 200);
	const Int offset = gameMcpArgInt(args, "offset", 0);

	std::vector<std::string> names;
	if (kind == PREVIEW_PARTICLE_SYSTEM)
	{
		for (ParticleSystemManager::TemplateMap::const_iterator it = TheParticleSystemManager->beginParticleSystemTemplate();
			it != TheParticleSystemManager->endParticleSystemTemplate(); ++it)
			names.push_back(it->first.str());
	}
	else if (kind == PREVIEW_FXLIST)
	{
		std::vector<AsciiString> list;
		TheFXListStore->getNames(list);
		for (size_t i = 0; i < list.size(); ++i)
			names.push_back(list[i].str());
	}
	else
	{
		const AudioEventInfoHash &events = TheAudio->getAllAudioEvents();
		for (AudioEventInfoHash::const_iterator it = events.begin(); it != events.end(); ++it)
			names.push_back(it->first.str());
	}
	std::sort(names.begin(), names.end());

	McpJson items = McpJson::makeArray();
	Int matched = 0;
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (!contains.empty() && !gameMcpContainsNoCase(names[i].c_str(), contains.c_str()))
			continue;
		if (matched++ < offset || (Int)items.size() >= limit)
			continue;
		McpJson item = McpJson::makeObject().set("name", names[i]);
		if (kind != PREVIEW_SOUND)
		{
			const Int block = FXEditorApp::ini().findBlock(kind, AsciiString(names[i].c_str()));
			item.set("has_text", block >= 0);
			if (block >= 0 && FXEditorApp::ini().getBlock(block)->isDirty())
				item.set("dirty", true);
		}
		items.push(item);
	}
	return McpJson::makeObject().set("kind", previewKindName(kind)).set("total", matched).set("items", items);
}

McpJson cmdGet(const McpJson &args)
{
	const Int block = blockArg(args);
	return describeBlock(block, gameMcpArgBool(args, "text", true));
}

McpJson cmdSchema(const McpJson &args)
{
	requireReady();
	const PreviewKind kind = kindArg(args, "particle");
	McpJson list = McpJson::makeArray();
	if (kind == PREVIEW_PARTICLE_SYSTEM)
	{
		const std::vector<FieldDef> &fields = FieldSchema::particleSystemFields();
		for (size_t i = 0; i < fields.size(); ++i)
			list.push(describeField(fields[i]));
		return McpJson::makeObject().set("kind", "particle").set("fields", list);
	}
	if (kind == PREVIEW_FXLIST)
	{
		const std::string type = gameMcpArgString(args, "nugget_type", "");
		McpJson nuggets = McpJson::makeObject();
		const std::vector<std::string> &types = FieldSchema::nuggetTypes();
		for (size_t t = 0; t < types.size(); ++t)
		{
			if (!type.empty() && _stricmp(type.c_str(), types[t].c_str()) != 0)
				continue;
			McpJson fields = McpJson::makeArray();
			const std::vector<FieldDef> &defs = FieldSchema::nuggetFields(types[t]);
			for (size_t i = 0; i < defs.size(); ++i)
				fields.push(describeField(defs[i]));
			nuggets.set(types[t].c_str(), fields);
		}
		return McpJson::makeObject().set("kind", "fxlist").set("nuggets", nuggets);
	}
	gameMcpFail("sounds have no schema here");
}

/// Applies a changed text as one undo step; fails with the game's parse error.
McpJson commit(Int block, const std::string &text, Bool keepOnError)
{
	AsciiString error;
	if (!FXEditorApp::ini().setText(block, text, error, keepOnError, TRUE))
		gameMcpFail("%s", error.str());
	FXEditorApp::onBlockChanged(block);
	return describeBlock(block, FALSE);
}

McpJson cmdSetFields(const McpJson &args)
{
	const Int block = blockArg(args);
	const Int nugget = gameMcpArgInt(args, "nugget", -1);
	const McpJson &fields = args.get("fields");
	if (!fields.isObject() || fields.numKeys() == 0)
		gameMcpFail("fields must be an object of field names and values (null removes a field)");
	IniBlock *b = FXEditorApp::ini().getBlock(block);
	if (nugget >= 0)
	{
		std::vector<std::string> types;
		IniText::getNuggets(b->text, types);
		if (nugget >= (Int)types.size())
			gameMcpFail("the FXList has %d nuggets", (int)types.size());
	}
	std::string text = b->text;
	for (size_t i = 0; i < fields.numKeys(); ++i)
	{
		const std::string &key = fields.keyAt(i);
		const McpJson &value = fields.valueAt(i);
		if (value.isNull())
		{
			text = IniText::removeField(text, nugget, key.c_str());
			continue;
		}
		std::string v;
		if (value.isString())
			v = value.asString();
		else if (value.isBool())
			v = value.asBool() ? "Yes" : "No";
		else if (value.isNumber())
			v = FieldSchema::formatReal((float)value.asNumber());
		else
			gameMcpFail("the value of %s must be a string, number, bool or null", key.c_str());
		text = IniText::setField(text, nugget, key.c_str(), v);
	}
	return commit(block, text, FALSE);
}

McpJson cmdSetText(const McpJson &args)
{
	const Int block = blockArg(args);
	return commit(block, gameMcpArgString(args, "text"), gameMcpArgBool(args, "keep_on_error", false));
}

McpJson cmdNugget(const McpJson &args)
{
	const Int block = blockArg(args);
	IniBlock *b = FXEditorApp::ini().getBlock(block);
	if (b->kind != PREVIEW_FXLIST)
		gameMcpFail("nuggets are parts of an FXList");
	const std::string action = gameMcpArgString(args, "action");
	const Int index = gameMcpArgInt(args, "index", -1);
	std::vector<std::string> types;
	IniText::getNuggets(b->text, types);
	if (action != "add" && (index < 0 || index >= (Int)types.size()))
		gameMcpFail("index must be 0..%d", (int)types.size() - 1);

	std::string text;
	if (action == "add")
	{
		const std::string type = gameMcpArgString(args, "type");
		if (FieldSchema::nuggetFields(type).empty())
			gameMcpFail("unknown nugget type '%s' (see fx.schema kind=fxlist)", type.c_str());
		text = IniText::addNugget(b->text, type.c_str(), index);
		// The new nugget gets its fields right away.
		const McpJson &fields = args.get("fields");
		const Int at = index >= 0 ? index : (Int)types.size();
		for (size_t i = 0; fields.isObject() && i < fields.numKeys(); ++i)
		{
			const McpJson &v = fields.valueAt(i);
			const std::string value = v.isString() ? v.asString() : (v.isBool() ? (v.asBool() ? "Yes" : "No") : FieldSchema::formatReal((float)v.asNumber()));
			text = IniText::setField(text, at, fields.keyAt(i).c_str(), value);
		}
	}
	else if (action == "remove")
		text = IniText::removeNugget(b->text, index);
	else if (action == "move")
		text = IniText::moveNugget(b->text, index, gameMcpArgInt(args, "offset"));
	else if (action == "duplicate")
		text = IniText::duplicateNugget(b->text, index);
	else
		gameMcpFail("action must be add, remove, move or duplicate");
	return commit(block, text, FALSE);
}

McpJson cmdCreate(const McpJson &args)
{
	requireReady();
	const PreviewKind kind = kindArg(args);
	if (kind == PREVIEW_SOUND)
		gameMcpFail("sounds cannot be created here");
	const std::string name = gameMcpArgString(args, "name");
	Int copyFrom = -1;
	if (args.has("copy_from"))
	{
		const std::string source = gameMcpArgString(args, "copy_from");
		copyFrom = FXEditorApp::ini().findBlock(kind, AsciiString(source.c_str()));
		if (copyFrom < 0)
			gameMcpFail("no INI text for %s '%s'", previewKindName(kind), source.c_str());
	}
	AsciiString error;
	const Int block = FXEditorApp::ini().createBlock(kind, AsciiString(name.c_str()), copyFrom, error);
	if (block < 0)
		gameMcpFail("%s", error.str());
	FXEditorApp::select(kind, AsciiString(name.c_str()));
	return describeBlock(block, TRUE);
}

McpJson cmdUndo(const McpJson &args)
{
	const Int block = blockArg(args);
	const std::string what = gameMcpArgString(args, "action", "undo");
	AsciiString error;
	Bool ok = FALSE;
	if (what == "undo")
		ok = FXEditorApp::ini().undo(block, error);
	else if (what == "redo")
		ok = FXEditorApp::ini().redo(block, error);
	else if (what == "revert")
		ok = FXEditorApp::ini().revert(block, error);
	else
		gameMcpFail("action must be undo, redo or revert");
	if (!ok)
		gameMcpFail("%s", error.str());
	FXEditorApp::onBlockChanged(block);
	return describeBlock(block, FALSE);
}

McpJson cmdValidate(const McpJson &args)
{
	const Int block = blockArg(args);
	const McpJson problems = validateBlock(block);
	return McpJson::makeObject().set("ok", problems.size() == 0).set("problems", problems);
}

McpJson cmdSave(const McpJson &args)
{
	requireReady();
	if (!gameMcpArgBool(args, "overwrite", false))
		gameMcpFail("saving writes into the mod's INI files: pass overwrite=true (a .fxeditor.bak copy is kept)");
	Int block = -1;
	if (args.has("name"))
		block = blockArg(args);
	AsciiString error;
	std::vector<AsciiString> written;
	if (!FXEditorApp::ini().save(block, error, &written))
		gameMcpFail("%s", error.str());
	McpJson files = McpJson::makeArray();
	for (size_t i = 0; i < written.size(); ++i)
	{
		files.push(written[i].str());
		FXEditorApp::log(FALSE, "Saved %s (MCP)", written[i].str());
	}
	return McpJson::makeObject().set("written", files);
}

McpJson cmdPlay(const McpJson &args)
{
	requireReady();
	const PreviewKind kind = kindArg(args, "particle");
	const std::string name = gameMcpArgString(args, "name");
	PreviewScene &scene = FXEditorApp::scene();
	PreviewSettings &s = scene.settings();
	s.height = (Real)gameMcpArgNumber(args, "height", s.height);
	s.repeat = gameMcpArgBool(args, "repeat", s.repeat);
	s.repeatSeconds = (Real)gameMcpArgNumber(args, "repeat_seconds", s.repeatSeconds);
	s.move = gameMcpArgBool(args, "move", s.move);
	s.moveRadius = (Real)gameMcpArgNumber(args, "move_radius", s.moveRadius);
	s.moveSpeed = (Real)gameMcpArgNumber(args, "move_speed", s.moveSpeed);
	s.angle = (Real)gameMcpArgNumber(args, "angle", s.angle);
	s.followCamera = gameMcpArgBool(args, "follow_camera", s.followCamera);
	if (args.has("attach_to"))
	{
		const std::string attach = gameMcpArgString(args, "attach_to");
		if (!attach.empty() && TheThingFactory->findTemplate(AsciiString(attach.c_str())) == nullptr)
			gameMcpFail("unknown object '%s'", attach.c_str());
		s.attachTo = attach.c_str();
	}
	if (args.has("x") || args.has("y"))
	{
		Coord3D center = scene.getCenter();
		center.x = (Real)gameMcpArgNumber(args, "x", center.x);
		center.y = (Real)gameMcpArgNumber(args, "y", center.y);
		scene.setCenter(center);
	}
	AsciiString error;
	if (!FXEditorApp::play(kind, AsciiString(name.c_str()), error))
		gameMcpFail("%s", error.str());
	return McpJson::makeObject().set("kind", previewKindName(kind)).set("name", name)
		.set("position", coord(scene.getEmitterPosition())).set("frame", (unsigned int)TheGameLogic->getFrame());
}

//-------------------------------------------------------------------------------------------------
// Direct3D 12: PostProcess and Material blocks
//-------------------------------------------------------------------------------------------------

/// Numbers of render blocks keep their precision (small biases, coverage fractions).
std::string renderNumber(float v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.6g", v);
	return buf;
}

Int renderBlockArg(const McpJson &args)
{
	requireReady();
	const std::string type = gameMcpArgString(args, "type");
	const std::string name = gameMcpArgString(args, "name", type.c_str());
	const Int block = FXEditorApp::renderIni().findTypedBlock(type.c_str(), AsciiString(name.c_str()));
	if (block < 0)
		gameMcpFail("no %s block '%s' (see fx.render_status)", type.c_str(), name.c_str());
	return block;
}

McpJson describeRenderBlock(Int index, Bool withText)
{
	IniDatabase &ini = FXEditorApp::renderIni();
	const IniBlock *b = ini.getBlock(index);
	std::vector<IniLine> lines;
	IniText::parseLines(b->text, lines);
	McpJson fields = McpJson::makeObject();
	McpJson params = McpJson::makeObject();
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const IniLine &l = lines[i];
		if (l.type != IniLine::FIELD || l.depth != 1)
			continue;
		if (_stricmp(l.key.c_str(), "Param") == 0)
			continue;
		if (!fields.has(l.key.c_str()))
			fields.set(l.key.c_str(), l.value);
	}
	std::vector<std::string> names;
	std::vector<std::string> values;
	IniText::getKeyedFields(b->text, "Param", names, values);
	for (size_t i = 0; i < names.size(); ++i)
		params.set(names[i].c_str(), values[i]);
	const IniSourceFile *file = ini.getFile(b->file);
	McpJson j = McpJson::makeObject().set("type", b->type).set("name", b->name.str())
		.set("file", file ? file->path.str() : "").set("dirty", b->isDirty()).set("error", b->error)
		.set("fields", fields).set("params", params);
	if (withText)
		j.set("text", b->text);
	return j;
}

McpJson cmdRenderStatus(const McpJson &args)
{
	requireReady();
	if (args.has("postprocess"))
		TheWritableGlobalData->m_postProcessing = gameMcpArgBool(args, "postprocess", true);
	if (args.has("materials") && TheW3DMaterials)
		TheW3DMaterials->setEnabled(gameMcpArgBool(args, "materials", true));

	McpJson j = McpJson::makeObject().set("dx12", FXEditorApp::isDX12());
	if (TheW3DPostProcess)
	{
		McpJson passes = McpJson::makeArray();
		for (size_t i = 0; i < TheW3DPostProcess->getPasses().size(); ++i)
			passes.push(TheW3DPostProcess->getPasses()[i].str());
		j.set("postprocess", McpJson::makeObject().set("enabled", TheGlobalData->m_postProcessing != 0)
			.set("running", TheW3DPostProcess->isRunning()).set("passes", passes).set("error", TheW3DPostProcess->getError().str()));
	}
	if (TheW3DMaterials)
	{
		std::vector<AsciiString> names;
		std::vector<Int> textures;
		std::vector<AsciiString> uses;
		TheW3DMaterials->getMaterials(names, textures, uses);
		McpJson materials = McpJson::makeArray();
		for (size_t i = 0; i < names.size(); ++i)
			materials.push(McpJson::makeObject().set("name", names[i].str()).set("use", uses[i].str()).set("textures", textures[i]));
		j.set("materials", McpJson::makeObject().set("enabled", TheW3DMaterials->isEnabled()).set("running", TheW3DMaterials->isRunning())
			.set("built", materials).set("error", TheW3DMaterials->getError().str()));
	}
	IniDatabase &ini = FXEditorApp::renderIni();
	McpJson blocks = McpJson::makeArray();
	for (Int i = 0; i < ini.getBlockCount(); ++i)
	{
		const IniBlock *b = ini.getBlock(i);
		if (ini.findTypedBlock(b->type.c_str(), b->name) != i)
			continue;	// overridden by a later definition
		blocks.push(McpJson::makeObject().set("type", b->type).set("name", b->name.str()).set("dirty", b->isDirty()).set("error", b->error));
	}
	j.set("blocks", blocks).set("unsaved", ini.getDirtyCount());
	return j;
}

McpJson cmdRenderGet(const McpJson &args)
{
	return describeRenderBlock(renderBlockArg(args), gameMcpArgBool(args, "text", true));
}

McpJson cmdRenderSet(const McpJson &args)
{
	const Int block = renderBlockArg(args);
	IniDatabase &ini = FXEditorApp::renderIni();
	std::string text = ini.getBlock(block)->text;
	if (args.has("text"))
		text = gameMcpArgString(args, "text");
	const McpJson &fields = args.get("fields");
	for (size_t i = 0; fields.isObject() && i < fields.numKeys(); ++i)
	{
		const std::string &key = fields.keyAt(i);
		const McpJson &value = fields.valueAt(i);
		if (value.isNull())
			text = IniText::removeField(text, -1, key.c_str());
		else if (value.isBool())
			text = IniText::setField(text, -1, key.c_str(), value.asBool() ? "Yes" : "No");
		else if (value.isNumber())
			text = IniText::setField(text, -1, key.c_str(), renderNumber((float)value.asNumber()));
		else
			text = IniText::setField(text, -1, key.c_str(), value.asString());
	}
	const McpJson &params = args.get("params");
	for (size_t i = 0; params.isObject() && i < params.numKeys(); ++i)
	{
		const std::string &name = params.keyAt(i);
		const McpJson &value = params.valueAt(i);
		if (value.isNull())
		{
			text = IniText::removeKeyedField(text, "Param", name.c_str());
			continue;
		}
		std::string v;
		if (value.isArray())
		{
			for (size_t k = 0; k < value.size(); ++k)
				v += (k ? " " : "") + renderNumber((float)value.at(k).asNumber());
		}
		else if (value.isNumber())
		{
			v = renderNumber((float)value.asNumber());
		}
		else
		{
			v = value.asString();
		}
		text = IniText::setKeyedField(text, "Param", name.c_str(), v);
	}
	AsciiString error;
	if (!ini.setText(block, text, error, FALSE, TRUE))
		gameMcpFail("%s", error.str());
	return describeRenderBlock(block, FALSE);
}

McpJson cmdRenderCreate(const McpJson &args)
{
	requireReady();
	const std::string type = gameMcpArgString(args, "type");
	const std::string name = gameMcpArgString(args, "name");
	IniDatabase &ini = FXEditorApp::renderIni();
	Int copyFrom = -1;
	if (args.has("copy_from"))
	{
		copyFrom = ini.findTypedBlock(type.c_str(), AsciiString(gameMcpArgString(args, "copy_from").c_str()));
		if (copyFrom < 0)
			gameMcpFail("no %s '%s' to copy", type.c_str(), gameMcpArgString(args, "copy_from").c_str());
	}
	AsciiString error;
	const Int block = ini.createTypedBlock(type.c_str(), AsciiString(name.c_str()), copyFrom, error);
	if (block < 0)
		gameMcpFail("%s", error.str());
	return describeRenderBlock(block, TRUE);
}

McpJson cmdRenderUndo(const McpJson &args)
{
	const Int block = renderBlockArg(args);
	IniDatabase &ini = FXEditorApp::renderIni();
	const std::string action = gameMcpArgString(args, "action", "undo");
	AsciiString error;
	Bool ok = FALSE;
	if (action == "undo")
		ok = ini.undo(block, error);
	else if (action == "redo")
		ok = ini.redo(block, error);
	else if (action == "revert")
		ok = ini.revert(block, error);
	else
		gameMcpFail("action must be undo, redo or revert");
	if (!ok)
		gameMcpFail("%s", error.str());
	return describeRenderBlock(block, FALSE);
}

McpJson cmdRenderSave(const McpJson &args)
{
	requireReady();
	if (!gameMcpArgBool(args, "overwrite", false))
		gameMcpFail("saving writes into the mod's PostProcess / Material INI files: pass overwrite=true (a .fxeditor.bak copy is kept)");
	AsciiString error;
	std::vector<AsciiString> written;
	if (!FXEditorApp::renderIni().save(-1, error, &written))
		gameMcpFail("%s", error.str());
	McpJson files = McpJson::makeArray();
	for (size_t i = 0; i < written.size(); ++i)
	{
		files.push(written[i].str());
		FXEditorApp::log(FALSE, "Saved %s (MCP)", written[i].str());
	}
	return McpJson::makeObject().set("written", files);
}

Int renderChainArg(const McpJson &args)
{
	requireReady();
	IniDatabase &ini = FXEditorApp::renderIni();
	const std::string name = gameMcpArgString(args, "chain", "Default");
	const Int block = _stricmp(name.c_str(), "Default") == 0 ? ini.findTypedBlock("PostProcess", AsciiString("PostProcess"))
		: ini.findTypedBlock("PostProcessChain", AsciiString(name.c_str()));
	if (block < 0)
		gameMcpFail("no chain '%s' (Default = the PostProcess block, or a PostProcessChain name)", name.c_str());
	return block;
}

McpJson describeChain(const PassChain &chain)
{
	const std::vector<AsciiString> *runningPasses = PassOrder::getRunningPasses(FXEditorApp::renderIni(), chain);
	McpJson rows = McpJson::makeArray();
	Int position = 0;
	for (size_t i = 0; i < chain.rows.size(); ++i)
	{
		const PassRow &row = chain.rows[i];
		McpJson inputs = McpJson::makeArray();
		for (size_t k = 0; k < row.inputs.size(); ++k)
			inputs.push(row.inputs[k].str());
		McpJson words = McpJson::makeArray();
		for (size_t k = 0; k < row.words.size(); ++k)
			words.push(row.words[k].str());
		McpJson j = McpJson::makeObject().set("name", row.name.str()).set("shader", row.shader.str()).set("reads", inputs)
			.set("inputs", words).set("role", PassOrder::getRoleName(row.role))
			.set("enabled", row.enabled != FALSE).set("listed", row.listed != FALSE).set("default_index", (int)row.defaultIndex)
			.set("moved", row.moved != FALSE);
		if (row.needsConnect())
		{
			McpJson wanted = McpJson::makeArray();
			for (size_t k = 0; k < row.wanted.size(); ++k)
				wanted.push(row.wanted[k].str());
			j.set("auto_inputs", wanted);
		}
		if (row.enabled)
			j.set("position", (int)++position);
		if (runningPasses != nullptr)
			j.set("runs", PassOrder::isRunning(runningPasses, row.name) != FALSE);
		if (row.block < 0)
			j.set("error", "no PostProcessPass of this name");
		rows.push(j);
	}
	McpJson debug = McpJson::makeArray();
	for (size_t i = 0; i < chain.debugRows.size(); ++i)
		debug.push(McpJson::makeObject().set("name", chain.debugRows[i].name.str()).set("enabled", chain.debugRows[i].enabled != FALSE));
	McpJson order = McpJson::makeArray();
	for (size_t i = 0; i < chain.defaultOrder.size(); ++i)
		order.push(chain.defaultOrder[i].str());
	McpJson running = McpJson::makeArray();
	if (TheW3DPostProcess)
		for (size_t i = 0; i < TheW3DPostProcess->getPasses().size(); ++i)
			running.push(TheW3DPostProcess->getPasses()[i].str());
	const IniBlock *block = FXEditorApp::renderIni().getBlock(chain.block);
	AsciiString orderError;
	PassOrder::checkOrder(chain.rows, orderError);
	return McpJson::makeObject().set("chain", block != nullptr && _stricmp(block->type.c_str(), "PostProcess") == 0 ? "Default" : block->name.str())
		.set("rows", rows).set("debug_views", debug).set("default_order", order).set("custom_order", chain.isCustomOrder() != FALSE)
		.set("moved_count", (int)chain.movedCount).set("order_error", orderError.str())
		.set("connect_count", (int)chain.countNeedsConnect()).set("running", running)
		.set("error", TheW3DPostProcess ? TheW3DPostProcess->getError().str() : "");
}

/** The passes of a chain as the Direct3D 12 window lists them, and the same changes: on/off, order, reset. */
McpJson cmdRenderChain(const McpJson &args)
{
	const Int block = renderChainArg(args);
	IniDatabase &ini = FXEditorApp::renderIni();
	PassChain chain;
	PassOrder::build(ini, block, chain);
	Bool changed = FALSE;
	const Bool connect = gameMcpArgBool(args, "connect", false);
	Bool autoInputs = gameMcpArgBool(args, "auto_inputs", false) || connect;

	if (gameMcpArgBool(args, "reset", false))
	{
		PassOrder::resetOrder(chain);
		// The default order goes by the stages; image passes that read each other in another order are rewired.
		AsciiString reason;
		if (!PassOrder::checkOrder(chain.rows, reason))
			autoInputs = TRUE;
		changed = TRUE;
	}
	const McpJson &order = args.get("order");
	if (order.isArray())
	{
		// The rows named, in that order; rows not named follow in their order.
		std::vector<PassRow> rows;
		for (size_t i = 0; i < order.size(); ++i)
		{
			const Int row = chain.findRow(AsciiString(order.at(i).asString().c_str()));
			if (row < 0)
				gameMcpFail("'%s' is no pass of the list (see rows)", order.at(i).asString().c_str());
			for (size_t k = 0; k < rows.size(); ++k)
				if (rows[k].name.compareNoCase(chain.rows[row].name) == 0)
					gameMcpFail("'%s' is named twice", order.at(i).asString().c_str());
			rows.push_back(chain.rows[row]);
		}
		for (size_t i = 0; i < chain.rows.size(); ++i)
		{
			Bool named = FALSE;
			for (size_t k = 0; k < rows.size(); ++k)
				named = named || rows[k].name.compareNoCase(chain.rows[i].name) == 0;
			if (!named)
				rows.push_back(chain.rows[i]);
		}
		AsciiString reason;
		if (!PassOrder::checkReorder(chain, rows, autoInputs, reason))
			gameMcpFail("%s", reason.str());
		chain.rows = rows;
		changed = TRUE;
	}
	const McpJson &move = args.get("move");
	if (move.isObject())
	{
		const std::string pass = gameMcpArgString(move, "pass");
		const Int from = chain.findRow(AsciiString(pass.c_str()));
		if (from < 0)
			gameMcpFail("'%s' is no pass of the list", pass.c_str());
		const Bool before = move.has("before");
		if (!before && !move.has("after"))
			gameMcpFail("move needs before or after");
		const std::string other = gameMcpArgString(move, before ? "before" : "after");
		const Int target = chain.findRow(AsciiString(other.c_str()));
		if (target < 0 || target == from)
			gameMcpFail("'%s' is no other pass of the list", other.c_str());
		// The index after the pass was taken out.
		Int to = before ? target : target + 1;
		if (from < to)
			--to;
		AsciiString reason;
		if (!PassOrder::canMove(chain, from, to, reason, autoInputs))
			gameMcpFail("%s", reason.str());
		PassOrder::move(chain, from, to);
		changed = TRUE;
	}
	const McpJson &enable = args.get("enable");
	for (size_t i = 0; enable.isObject() && i < enable.numKeys(); ++i)
	{
		const AsciiString name(enable.keyAt(i).c_str());
		const Bool on = enable.valueAt(i).asBool();
		Int row = chain.findRow(name);
		PassRow *target = row >= 0 ? &chain.rows[row] : nullptr;
		if (target == nullptr && (row = chain.findDebugRow(name)) >= 0)
			target = &chain.debugRows[row];
		if (target == nullptr || target->block < 0)
			gameMcpFail("'%s' is no pass of the list", name.str());
		target->enabled = on;
		target->listed = TRUE;
		changed = TRUE;
	}

	if (changed)
	{
		AsciiString error;
		if (!PassOrder::apply(ini, chain, autoInputs, error))
			gameMcpFail("%s", error.str());
	}
	else if (connect)
	{
		AsciiString error;
		if (!PassOrder::writeInputs(ini, chain, error))
			gameMcpFail("%s", error.str());
	}
	if (gameMcpArgBool(args, "run", false))
	{
		const IniBlock *b = ini.getBlock(block);
		ThePostProcessControl.useChain(_stricmp(b->type.c_str(), "PostProcess") == 0 ? AsciiString("Default") : b->name);
	}
	PassOrder::build(ini, block, chain);
	return describeChain(chain);
}

McpJson cmdRenderReload(const McpJson &)
{
	requireReady();
	FXEditorApp::reloadRenderFiles();
	return cmdRenderStatus(McpJson::makeObject());
}

McpJson cmdMaps(const McpJson &args)
{
	const std::string contains = gameMcpArgString(args, "contains", "");
	AsciiString needle = contains.c_str();
	needle.toLower();
	McpJson maps = McpJson::makeArray();
	for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		AsciiString display;
		display.translate(it->second.m_displayName);
		AsciiString lowerDisplay = display;
		lowerDisplay.toLower();
		if (!needle.isEmpty() && strstr(it->first.str(), needle.str()) == nullptr && strstr(lowerDisplay.str(), needle.str()) == nullptr)
			continue;
		maps.push(McpJson::makeObject().set("key", it->first.str()).set("name", display.str())
			.set("start_positions", FXEditorApp::countStartPositions(it->first)).set("official", it->second.m_isOfficial != FALSE));
	}
	std::vector<AsciiString> factions;
	FXEditorApp::getFactions(factions);
	McpJson factionList = McpJson::makeArray();
	for (size_t i = 0; i < factions.size(); ++i)
		factionList.push(factions[i].str());
	return McpJson::makeObject().set("maps", maps).set("factions", factionList);
}

McpJson cmdLoadMap(const McpJson &args)
{
	const std::string map = gameMcpArgString(args, "map");
	const std::string faction = gameMcpArgString(args, "faction", "FactionAmerica");
	AsciiString error;
	if (!FXEditorApp::loadMap(AsciiString(map.c_str()), AsciiString(faction.c_str()), error))
		gameMcpFail("%s", error.str());
	const AsciiString key = FXEditorApp::findMapKey(AsciiString(map.c_str()));
	const Bool skirmish = FXEditorApp::countStartPositions(key) > 0;
	return McpJson::makeObject().set("map_key", key.str()).set("mode", skirmish ? "skirmish" : "single_player")
		.set("faction", skirmish ? faction : std::string());
}

McpJson cmdSelect(const McpJson &args)
{
	const PreviewKind kind = kindArg(args, "particle");
	if (kind == PREVIEW_SOUND)
		gameMcpFail("sounds are not selected; play them with fx.play");
	const std::string name = gameMcpArgString(args, "name");
	FXEditorApp::select(kind, AsciiString(name.c_str()));
	PreviewScene &scene = FXEditorApp::scene();
	McpJson j = McpJson::makeObject().set("kind", previewKindName(kind)).set("name", name);
	if (scene.isPlaying())
		j.set("playing", McpJson::makeObject().set("kind", previewKindName(scene.getKind())).set("name", scene.getName().str()));
	return j;
}

McpJson cmdStop(const McpJson &args)
{
	PreviewScene &scene = FXEditorApp::scene();
	const std::string what = gameMcpArgString(args, "action", "stop");
	if (what == "stop")
		scene.stop();
	else if (what == "restart")
		scene.restart();
	else if (what == "clear")
		scene.clearAllParticles();
	else
		gameMcpFail("action must be stop, restart or clear");
	return McpJson::makeObject().set("playing", scene.isPlaying()).set("frame", TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0u);
}

McpJson cmdTime(const McpJson &args)
{
	requireReady();
	PreviewScene &scene = FXEditorApp::scene();
	if (args.has("speed"))
		scene.setSpeed((Real)gameMcpArgNumber(args, "speed"));
	if (args.has("paused"))
		scene.setPaused(gameMcpArgBool(args, "paused", false));
	if (args.has("step"))
		scene.step(gameMcpArgInt(args, "step"));
	return McpJson::makeObject().set("frame", (unsigned int)TheGameLogic->getFrame()).set("paused", scene.isPaused()).set("speed", scene.getSpeed());
}

McpJson cmdStats(const McpJson &args)
{
	requireReady();
	PreviewScene &scene = FXEditorApp::scene();
	PreviewStats st;
	scene.getStats(st);
	McpJson j = McpJson::makeObject()
		.set("frame", (unsigned int)TheGameLogic->getFrame())
		.set("particles", st.totalParticles)
		.set("max_particles", st.maxParticles)
		.set("field_particles", st.fieldParticles)
		.set("on_screen_particles", st.onScreenParticles)
		.set("systems", st.systems)
		.set("preview_particles", st.previewParticles)
		.set("preview_systems", st.previewSystems)
		.set("play_count", st.playCount)
		.set("frames_since_start", st.framesSinceStart)
		.set("sound_playing", scene.isSoundPlaying());
	if (gameMcpArgBool(args, "systems", true))
	{
		McpJson list = McpJson::makeArray();
		ParticleSystemManager::ParticleSystemList &all = TheParticleSystemManager->getAllParticleSystems();
		for (ParticleSystemManager::ParticleSystemListIt it = all.begin(); it != all.end() && list.size() < 100; ++it)
		{
			ParticleSystem *sys = *it;
			Coord3D pos;
			sys->getPosition(&pos);
			const ParticleSystemTemplate *t = sys->getTemplate();
			list.push(McpJson::makeObject()
				.set("id", (int)sys->getSystemID())
				.set("template", t ? t->getName().str() : "")
				.set("particles", sys->getParticleCount())
				.set("started_frame", sys->getStartFrame())
				.set("forever", sys->isSystemForever())
				.set("destroyed", sys->isDestroyed())
				.set("position", coord(pos)));
		}
		j.set("system_list", list);
	}
	return j;
}

McpJson cmdCapture(const McpJson &args)
{
	requireReady();
	std::string path = gameMcpArgString(args, "path", "");
	if (path.empty())
	{
		char temp[MAX_PATH];
		GetTempPathA(MAX_PATH, temp);
		char name[64];
		snprintf(name, sizeof(name), "fxeditor_%u.png", (unsigned int)TheGameLogic->getFrame());
		path = std::string(temp) + name;
	}
	AsciiString error;
	if (!FXEditorApp::captureScreenshot(path.c_str(), gameMcpArgBool(args, "with_interface", false), error))
		gameMcpFail("%s", error.str());
	return McpJson::makeObject().set("path", path).set("frame", (unsigned int)TheGameLogic->getFrame());
}

McpJson cmdCenter(const McpJson &args)
{
	requireReady();
	PreviewScene &scene = FXEditorApp::scene();
	if (args.has("x") || args.has("y"))
	{
		Coord3D c = scene.getCenter();
		c.x = (Real)gameMcpArgNumber(args, "x", c.x);
		c.y = (Real)gameMcpArgNumber(args, "y", c.y);
		scene.setCenter(c);
	}
	if (gameMcpArgBool(args, "look", true))
		scene.lookAtCenter();
	if (args.has("angle"))
		TheTacticalView->setAngle((Real)(gameMcpArgNumber(args, "angle") * PI / 180.0));
	if (args.has("pitch"))
		TheTacticalView->setPitch((Real)(gameMcpArgNumber(args, "pitch") * PI / 180.0));
	if (args.has("zoom"))
		TheTacticalView->setZoom((Real)gameMcpArgNumber(args, "zoom"));
	return McpJson::makeObject().set("center", coord(scene.getCenter()))
		.set("camera", McpJson::makeObject().set("position", coord(TheTacticalView->getPosition()))
			.set("angle", TheTacticalView->getAngle() * 180.0 / PI).set("pitch", TheTacticalView->getPitch() * 180.0 / PI)
			.set("zoom", TheTacticalView->getZoom()));
}

McpJson cmdLog(const McpJson &args)
{
	const UnsignedInt since = (UnsignedInt)gameMcpArgInt(args, "since", 0);
	Int count = 0;
	const FXEditorLogEntry *entries = FXEditorApp::getLog(count);
	McpJson list = McpJson::makeArray();
	UnsignedInt next = since;
	for (Int i = 0; i < count; ++i)
	{
		if (entries[i].serial <= since)
			continue;
		list.push(McpJson::makeObject().set("serial", entries[i].serial).set("error", entries[i].error).set("text", entries[i].text.str()));
		next = entries[i].serial;
	}
	return McpJson::makeObject().set("entries", list).set("next_serial", next);
}

McpJson cmdReload(const McpJson &)
{
	requireReady();
	if (FXEditorApp::ini().getDirtyCount() > 0)
		gameMcpFail("there are unsaved changes; save or revert them first");
	FXEditorApp::ini().load();
	return McpJson::makeObject().set("blocks", FXEditorApp::ini().getBlockCount()).set("files", FXEditorApp::ini().getFileCount());
}

} // namespace

//-------------------------------------------------------------------------------------------------
void FXEditorMcp::registerCommands()
{
	gameMcpRegisterCommand("fx.status", cmdStatus,
		"Editor state: preview game ready, map, renderer (dx8/dx12), frame, paused, speed, what plays, unsaved blocks.");
	gameMcpRegisterCommand("fx.list", cmdList,
		"{kind=particle|fxlist|sound, contains?, limit=200, offset=0} Names the game knows; has_text: the editor can edit it.");
	gameMcpRegisterCommand("fx.get", cmdGet,
		"{kind, name, text=true} A block: its INI text, file, fields (particle) or nuggets with fields (fxlist), dirty, error.");
	gameMcpRegisterCommand("fx.schema", cmdSchema,
		"{kind=particle|fxlist, nugget_type?} Field names, types, allowed values and help.");
	gameMcpRegisterCommand("fx.set_fields", cmdSetFields,
		"{kind, name, fields:{key:value|null}, nugget?} Sets fields (null removes one) as one undo step; the game parses it at once.");
	gameMcpRegisterCommand("fx.set_text", cmdSetText,
		"{kind, name, text, keep_on_error=false} Replaces the whole block text.");
	gameMcpRegisterCommand("fx.nugget", cmdNugget,
		"{kind=fxlist, name, action=add|remove|move|duplicate, index?, type? (add), fields? (add), offset? (move)} Edits the nuggets of an FXList.");
	gameMcpRegisterCommand("fx.create", cmdCreate,
		"{kind, name, copy_from?} A new block (a copy of another one); written to the source's file on save.");
	gameMcpRegisterCommand("fx.undo", cmdUndo, "{kind, name, action=undo|redo|revert} Undo, redo, or back to the text on disk.");
	gameMcpRegisterCommand("fx.validate", cmdValidate,
		"{kind, name} Unknown fields and references the game would not find (particle systems, FXLists, sounds, objects, textures).");
	gameMcpRegisterCommand("fx.save", cmdSave,
		"{overwrite=true, kind?, name?} Writes changed blocks (all, or that block's file) into the mod's INI files; keeps <file>.fxeditor.bak.");
	gameMcpRegisterCommand("fx.reload", cmdReload, "Reads the INI text from disk again (fails with unsaved changes).");
	gameMcpRegisterCommand("fx.play", cmdPlay,
		"{kind=particle|fxlist|sound, name, height?, repeat?, repeat_seconds?, move?, move_radius?, move_speed?, angle?, attach_to?, x?, y?} "
		"Plays at the preview point (or on a spawned object).");
	gameMcpRegisterCommand("fx.render_status", cmdRenderStatus,
		"{postprocess?, materials?} Direct3D 12: post-processing and materials on/off (not saved), what runs, errors, and the "
		"PostProcessPass / PostProcess / PostProcessChain / ShadowMap / Material blocks.");
	gameMcpRegisterCommand("fx.render_chain", cmdRenderChain,
		"{chain=Default, enable?={pass: bool}, order?=[pass, ...], move?={pass, before|after}, reset?, run?, auto_inputs?, connect?} The passes of a chain "
		"in the order they run, with the ones that are off at their place, the default order and what is moved from it; "
		"switches passes on or off (Disabled), reorders, or resets to the default order, as one undo step. run=true previews that chain. "
		"auto_inputs=true also gives the passes the Inputs of their place; connect=true only does that.");
	gameMcpRegisterCommand("fx.render_get", cmdRenderGet, "{type, name?, text=true} A render block: fields, params (Param lines by name), text.");
	gameMcpRegisterCommand("fx.render_set", cmdRenderSet,
		"{type, name?, fields?, params?, text?} Changes a render block (fields: value or null; params: number, list or null) and "
		"applies it at once; fails with the parse error.");
	gameMcpRegisterCommand("fx.render_create", cmdRenderCreate, "{type=PostProcessPass|PostProcessChain|Material, name, copy_from?} A new render block.");
	gameMcpRegisterCommand("fx.render_undo", cmdRenderUndo, "{type, name?, action=undo|redo|revert} Undo, redo, or back to the text on disk.");
	gameMcpRegisterCommand("fx.render_save", cmdRenderSave,
		"{overwrite=true} Writes the changed render blocks into the mod's PostProcess / Material INI files; keeps <file>.fxeditor.bak.");
	gameMcpRegisterCommand("fx.render_reload", cmdRenderReload, "Reads the shader and render INI files again; changes not saved stay.");
	gameMcpRegisterCommand("fx.maps", cmdMaps, "{contains?} Maps of the map cache (key, name, start positions) and the playable factions.");
	gameMcpRegisterCommand("fx.load_map", cmdLoadMap,
		"{map, faction=FactionAmerica} Loads a map (key, name or part of the path) at the next frame: a multiplayer map as a "
		"sandbox skirmish with that faction, otherwise as a single player game. Wait for fx.status map_loading=false.");
	gameMcpRegisterCommand("fx.select", cmdSelect,
		"{kind=particle|fxlist, name} Shows it in the inspector, as a click in the library; while an effect plays, it plays instead.");
	gameMcpRegisterCommand("fx.stop", cmdStop, "{action=stop|restart|clear} Stops or restarts what plays, or removes all particles.");
	gameMcpRegisterCommand("fx.time", cmdTime, "{paused?, step?, speed?} Pauses, steps logic frames, sets the speed (1 = normal).");
	gameMcpRegisterCommand("fx.stats", cmdStats, "{systems=true} Particle counts in total and of the preview, and the live systems.");
	gameMcpRegisterCommand("fx.capture", cmdCapture,
		"{path?, with_interface=false} Draws a frame and writes it to a .png/.jpg (default: temp folder).");
	gameMcpRegisterCommand("fx.center", cmdCenter, "{x?, y?, look=true, angle?, pitch?, zoom?} Moves the preview point and the camera.");
	gameMcpRegisterCommand("fx.log", cmdLog, "{since=0} Messages of the editor (parse errors, saves).");
}
