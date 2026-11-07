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

#include "FXEditorPanels.h"
#include "FXEditorApp.h"
#include "FieldSchema.h"
#include "IniDatabase.h"

#include "Common/AudioAffect.h"
#include "Common/AudioEventInfo.h"
#include "Common/FileSystem.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameMcp.h"
#include "Common/GlobalData.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameClient/MapUtil.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DAssetManagerExposed.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/texture.h"

#include "GameMcpCommands.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <map>
#include <stdio.h>

const char *const FXEditorPanels::LIBRARY = "Library";
const char *const FXEditorPanels::INSPECTOR = "Inspector";
const char *const FXEditorPanels::PREVIEW = "Preview";
const char *const FXEditorPanels::AUDIO = "Audio";
const char *const FXEditorPanels::LOG = "Log";

namespace
{

const ImVec4 COLOR_DIRTY(1.0f, 0.75f, 0.3f, 1.0f);
const ImVec4 COLOR_ERROR(1.0f, 0.4f, 0.4f, 1.0f);
const ImVec4 COLOR_DIM(0.6f, 0.6f, 0.6f, 1.0f);

Bool s_showLibrary = TRUE;
Bool s_showInspector = TRUE;
Bool s_showPreview = TRUE;
Bool s_showAudio = TRUE;
Bool s_showLog = TRUE;
Bool s_showDemo = FALSE;
Bool s_showAllFields = FALSE;
Bool s_showMapDialog = FALSE;

// The map dialog: the maps of the map cache, the picked one and the faction for multiplayer maps.
struct MapEntry
{
	AsciiString key;
	std::string label;
	Int startPositions;
};
std::vector<MapEntry> s_maps;
std::vector<AsciiString> s_factions;
std::string s_mapFilter;
AsciiString s_mapPicked;
AsciiString s_factionPicked;	// FactionAmerica until one is picked

// Undo of drags: the text before the drag is recorded once when it ends.
Int s_gestureBlock = -1;
std::string s_gestureText;

std::vector<float> s_particleHistory;

typedef std::map<std::string, TextureClass *> TextureCache;
TextureCache s_textures;

//-------------------------------------------------------------------------------------------------
// Name lists for the pickers
//-------------------------------------------------------------------------------------------------

Bool lessNoCase(const std::string &a, const std::string &b)
{
	return _stricmp(a.c_str(), b.c_str()) < 0;
}

void sortNames(std::vector<std::string> &names)
{
	std::sort(names.begin(), names.end(), lessNoCase);
}

const std::vector<std::string> &particleSystemNames()
{
	static std::vector<std::string> names;
	static size_t lastCount = 0;
	size_t count = 0;
	for (ParticleSystemManager::TemplateMap::const_iterator it = TheParticleSystemManager->beginParticleSystemTemplate();
		it != TheParticleSystemManager->endParticleSystemTemplate(); ++it)
		++count;
	if (count != lastCount)
	{
		names.clear();
		for (ParticleSystemManager::TemplateMap::const_iterator it = TheParticleSystemManager->beginParticleSystemTemplate();
			it != TheParticleSystemManager->endParticleSystemTemplate(); ++it)
			names.push_back(it->first.str());
		sortNames(names);
		lastCount = count;
	}
	return names;
}

const std::vector<std::string> &fxListNames()
{
	static std::vector<std::string> names;
	static Int lastBlocks = -1;
	const Int blocks = FXEditorApp::ini().getBlockCount();
	if (blocks != lastBlocks)
	{
		std::vector<AsciiString> list;
		TheFXListStore->getNames(list);
		names.clear();
		for (size_t i = 0; i < list.size(); ++i)
			names.push_back(list[i].str());
		// FXLists made in the editor have their name as written
		for (Int i = 0; i < blocks; ++i)
		{
			const IniBlock *b = FXEditorApp::ini().getBlock(i);
			if (b->kind != PREVIEW_FXLIST)
				continue;
			Bool found = FALSE;
			for (size_t j = 0; j < names.size() && !found; ++j)
				found = _stricmp(names[j].c_str(), b->name.str()) == 0;
			if (!found)
				names.push_back(b->name.str());
		}
		sortNames(names);
		lastBlocks = blocks;
	}
	return names;
}

const std::vector<std::string> &soundNames()
{
	static std::vector<std::string> names;
	if (names.empty() && TheAudio)
	{
		const AudioEventInfoHash &events = TheAudio->getAllAudioEvents();
		for (AudioEventInfoHash::const_iterator it = events.begin(); it != events.end(); ++it)
		{
			if (it->second && it->second->m_soundType == AT_SoundEffect)
				names.push_back(it->first.str());
		}
		sortNames(names);
	}
	return names;
}

const std::vector<std::string> &objectNames()
{
	static std::vector<std::string> names;
	if (names.empty() && TheThingFactory)
	{
		for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate())
			names.push_back(t->getName().str());
		sortNames(names);
	}
	return names;
}

const std::vector<std::string> &textureNames()
{
	static std::vector<std::string> names;
	if (names.empty() && TheFileSystem)
	{
		const char *const patterns[] = { "*.tga", "*.dds" };
		for (size_t p = 0; p < ARRAY_SIZE(patterns); ++p)
		{
			FilenameList list;
			TheFileSystem->getFileListInDirectory("Art\\Textures\\", patterns[p], list, TRUE);
			for (FilenameList::const_iterator it = list.begin(); it != list.end(); ++it)
			{
				const char *full = it->str();
				const char *slash = strrchr(full, '\\');
				std::string name = slash ? slash + 1 : full;
				// The game asks for .tga and finds the .dds too.
				if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".dds") == 0)
					name = name.substr(0, name.size() - 4) + ".tga";
				names.push_back(name);
			}
		}
		sortNames(names);
		names.erase(std::unique(names.begin(), names.end()), names.end());
	}
	return names;
}

const std::vector<std::string> *namesFor(FieldType type)
{
	switch (type)
	{
		case FIELD_PARTICLE_SYSTEM: return &particleSystemNames();
		case FIELD_FXLIST: return &fxListNames();
		case FIELD_SOUND: return &soundNames();
		case FIELD_OBJECT: return &objectNames();
		case FIELD_TEXTURE: return &textureNames();
		default: return nullptr;
	}
}

Bool containsNoCase(const std::string &haystack, const char *needle)
{
	if (needle == nullptr || *needle == 0)
		return TRUE;
	return gameMcpContainsNoCase(haystack.c_str(), needle);
}

/// A filtered list in a popup; returns true when a name was picked.
Bool namePicker(const char *popupId, const std::vector<std::string> &names, std::string &picked)
{
	Bool result = FALSE;
	ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_Appearing);
	if (ImGui::BeginPopup(popupId))
	{
		static char filter[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			filter[0] = 0;
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));
		std::vector<Int> shown;
		for (size_t i = 0; i < names.size(); ++i)
		{
			if (containsNoCase(names[i], filter))
				shown.push_back((Int)i);
		}
		ImGui::BeginChild("##names", ImVec2(0, 360), ImGuiChildFlags_Borders);
		ImGuiListClipper clipper;
		clipper.Begin((int)shown.size());
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				if (ImGui::Selectable(names[shown[row]].c_str()))
				{
					picked = names[shown[row]];
					result = TRUE;
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
	return result;
}

//-------------------------------------------------------------------------------------------------
TextureClass *getTexture(const std::string &name)
{
	TextureCache::iterator it = s_textures.find(name);
	if (it != s_textures.end())
		return it->second;
	TextureClass *tex = nullptr;
	if (WW3DAssetManager::Get_Instance() && !name.empty())
		tex = WW3DAssetManager::Get_Instance()->Get_Texture(name.c_str());
	if (tex && !tex->Is_Initialized())
		tex->Init();
	s_textures[name] = tex;
	return tex;
}

void drawTexturePreview(const std::string &name, float size)
{
	TextureClass *tex = getTexture(name);
	IDirect3DTexture8 *d3d = tex ? tex->Peek_D3D_Texture() : nullptr;
	if (d3d)
	{
		ImGui::Image((ImTextureID)(intptr_t)d3d, ImVec2(size, size));
	}
	else
	{
		ImGui::Dummy(ImVec2(size, size));
		ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(120, 120, 120, 255));
	}
}

/// What follows the two numbers of a random variable ("UNIFORM", "GAUSSIAN"), kept when the numbers change.
std::string randomSuffix(const std::string &value)
{
	const char *p = value.c_str();
	for (int i = 0; i < 2; ++i)
	{
		char *end = nullptr;
		strtod(p, &end);
		if (end == p)
			return std::string();
		p = end;
	}
	while (*p == ' ' || *p == '	')
		++p;
	return *p ? std::string(" ") + p : std::string();
}

//-------------------------------------------------------------------------------------------------
// Applying edits
//-------------------------------------------------------------------------------------------------

/// A discrete edit: one undo step.
void applyField(Int block, Int nugget, const char *key, const std::string &value)
{
	AsciiString error;
	if (FXEditorApp::ini().setField(block, nugget, key, value, error))
		FXEditorApp::onBlockChanged(block);
	else
		FXEditorApp::log(TRUE, "%s", error.str());
}

void removeField(Int block, Int nugget, const char *key)
{
	AsciiString error;
	if (FXEditorApp::ini().removeField(block, nugget, key, error))
		FXEditorApp::onBlockChanged(block);
	else
		FXEditorApp::log(TRUE, "%s", error.str());
}

void applyText(Int block, const std::string &text, Bool recordUndo)
{
	AsciiString error;
	if (FXEditorApp::ini().setText(block, text, error, FALSE, recordUndo))
		FXEditorApp::onBlockChanged(block);
	else
		FXEditorApp::log(TRUE, "%s", error.str());
}

/// A drag: applied live without undo steps; the text before it becomes one undo step at its end.
void applyDrag(Int block, Int nugget, const char *key, const std::string &value, Bool activated, Bool changed, Bool finished)
{
	IniBlock *b = FXEditorApp::ini().getBlock(block);
	if (b == nullptr)
		return;
	if (activated)
	{
		s_gestureBlock = block;
		s_gestureText = b->text;
	}
	if (changed)
		applyText(block, IniText::setField(b->text, nugget, key, value), FALSE);
	if (finished && s_gestureBlock == block)
	{
		if (b->text != s_gestureText)
		{
			b->undo.push_back(s_gestureText);
			b->redo.clear();
		}
		s_gestureBlock = -1;
	}
}

//-------------------------------------------------------------------------------------------------
/** One field row: label, control and a button to remove the field from the block. */
//-------------------------------------------------------------------------------------------------
void drawField(Int block, Int nugget, const FieldDef &field, const std::string &text)
{
	std::string value;
	const Bool present = IniText::getField(text, nugget, field.name.c_str(), value);

	ImGui::PushID(field.name.c_str());
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	if (present)
		ImGui::TextUnformatted(field.name.c_str());
	else
		ImGui::TextColored(COLOR_DIM, "%s", field.name.c_str());
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("%s  (%s)", field.name.c_str(), FieldSchema::typeName(field.type));
		if (!field.help.empty())
			ImGui::TextUnformatted(field.help.c_str());
		if (!present)
			ImGui::TextColored(COLOR_DIM, "Not in the block: the default is used.");
		ImGui::EndTooltip();
	}

	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-30.0f);
	const char *key = field.name.c_str();
	// A field that is not in the block shows a neutral value, not what the game uses: dim it.
	if (!present)
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);

	switch (field.type)
	{
		case FIELD_BOOL:
		{
			bool v = present && (_stricmp(value.c_str(), "Yes") == 0 || _stricmp(value.c_str(), "True") == 0 || value == "1");
			if (ImGui::Checkbox("##v", &v))
				applyField(block, nugget, key, v ? "Yes" : "No");
			break;
		}
		case FIELD_ENUM:
		{
			if (ImGui::BeginCombo("##v", present ? value.c_str() : "(default)"))
			{
				for (const char *const *e = field.enumNames; e && *e; ++e)
				{
					if (ImGui::Selectable(*e, _stricmp(*e, value.c_str()) == 0))
						applyField(block, nugget, key, *e);
				}
				ImGui::EndCombo();
			}
			break;
		}
		case FIELD_REAL:
		case FIELD_ANGLE:
		case FIELD_PERCENT:
		{
			float v = present ? (float)atof(value.c_str()) : 0.0f;
			const Bool changed = ImGui::DragFloat("##v", &v, 0.05f, 0.0f, 0.0f, "%.3f");
			std::string out = FieldSchema::formatReal(v);
			if (field.type == FIELD_PERCENT)
				out += "%";
			applyDrag(block, nugget, key, out, ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case FIELD_INT:
		case FIELD_UINT:
		case FIELD_DURATION:
		{
			int v = present ? atoi(value.c_str()) : 0;
			const Bool changed = ImGui::DragInt("##v", &v, 0.2f, field.type == FIELD_INT ? INT_MIN : 0, INT_MAX);
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", v);
			applyDrag(block, nugget, key, buf, ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case FIELD_RANDOM:
		{
			float v[2] = { 0.0f, 0.0f };
			if (present)
				FieldSchema::parseFloats(value, v, 2);
			const Bool changed = ImGui::DragFloat2("##v", v, 0.05f, 0.0f, 0.0f, "%.2f");
			applyDrag(block, nugget, key, FieldSchema::formatRandom(v[0], v[1]) + randomSuffix(value), ImGui::IsItemActivated(), changed,
				ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case FIELD_COORD3D:
		{
			float v[3] = { 0.0f, 0.0f, 0.0f };
			if (present)
				FieldSchema::parseLabeled(value, "XYZ", v);
			const Bool changed = ImGui::DragFloat3("##v", v, 0.05f, 0.0f, 0.0f, "%.2f");
			applyDrag(block, nugget, key, FieldSchema::formatCoord(v), ImGui::IsItemActivated(), changed,
				ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case FIELD_RGB:
		{
			float v[3] = { 255.0f, 255.0f, 255.0f };
			if (present)
				FieldSchema::parseLabeled(value, "RGB", v);
			float c[3] = { v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f };
			const Bool changed = ImGui::ColorEdit3("##v", c, ImGuiColorEditFlags_Uint8);
			const float out[3] = { c[0] * 255.0f, c[1] * 255.0f, c[2] * 255.0f };
			applyDrag(block, nugget, key, FieldSchema::formatRGB(out), ImGui::IsItemActivated(), changed,
				ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case FIELD_ALPHA_KEY:
		{
			float v[3] = { 0.0f, 0.0f, 0.0f };
			if (present)
				FieldSchema::parseFloats(value, v, 3);
			float ab[2] = { v[0], v[1] };
			int frame = (int)v[2];
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f - 30.0f);
			Bool changed = ImGui::DragFloat2("##ab", ab, 0.01f, 0.0f, 1.0f, "%.2f");
			Bool activated = ImGui::IsItemActivated();
			Bool finished = ImGui::IsItemDeactivatedAfterEdit();
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-30.0f);
			changed |= ImGui::DragInt("##f", &frame, 0.5f, 0, 100000, "frame %d");
			activated |= ImGui::IsItemActivated();
			finished |= ImGui::IsItemDeactivatedAfterEdit();
			applyDrag(block, nugget, key, FieldSchema::formatAlphaKey(ab[0], ab[1], frame), activated, changed, finished);
			break;
		}
		case FIELD_COLOR_KEY:
		{
			float v[3] = { 0.0f, 0.0f, 0.0f };
			int frame = 0;
			if (present)
			{
				FieldSchema::parseLabeled(value, "RGB", v);
				const size_t b = value.find("B:");
				if (b != std::string::npos)
				{
					const size_t sp = value.find(' ', b);
					if (sp != std::string::npos)
						frame = atoi(value.c_str() + sp + 1);
				}
			}
			float c[3] = { v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f };
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f - 30.0f);
			Bool changed = ImGui::ColorEdit3("##c", c, ImGuiColorEditFlags_Uint8);
			Bool activated = ImGui::IsItemActivated();
			Bool finished = ImGui::IsItemDeactivatedAfterEdit();
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-30.0f);
			changed |= ImGui::DragInt("##f", &frame, 0.5f, 0, 100000, "frame %d");
			activated |= ImGui::IsItemActivated();
			finished |= ImGui::IsItemDeactivatedAfterEdit();
			const float out[3] = { c[0] * 255.0f, c[1] * 255.0f, c[2] * 255.0f };
			applyDrag(block, nugget, key, FieldSchema::formatColorKey(out, frame), activated, changed, finished);
			break;
		}
		case FIELD_TEXTURE:
		case FIELD_PARTICLE_SYSTEM:
		case FIELD_FXLIST:
		case FIELD_SOUND:
		case FIELD_OBJECT:
		{
			std::string edit = value;
			ImGui::SetNextItemWidth(-(30.0f + 30.0f + (field.type == FIELD_PARTICLE_SYSTEM || field.type == FIELD_FXLIST ? 30.0f : 0.0f)));
			if (ImGui::InputText("##v", &edit, ImGuiInputTextFlags_EnterReturnsTrue))
				applyField(block, nugget, key, edit);
			ImGui::SameLine();
			if (ImGui::Button("..."))
				ImGui::OpenPopup("pick");
			const std::vector<std::string> *names = namesFor(field.type);
			std::string picked;
			if (names && namePicker("pick", *names, picked))
				applyField(block, nugget, key, picked);
			if ((field.type == FIELD_PARTICLE_SYSTEM || field.type == FIELD_FXLIST) && present && !value.empty())
			{
				ImGui::SameLine();
				if (ImGui::ArrowButton("##go", ImGuiDir_Right))
					FXEditorApp::select(field.type == FIELD_FXLIST ? PREVIEW_FXLIST : PREVIEW_PARTICLE_SYSTEM, AsciiString(value.c_str()));
				ImGui::SetItemTooltip("Open %s", value.c_str());
			}
			if (field.type == FIELD_TEXTURE && present)
			{
				drawTexturePreview(value, 64.0f);
			}
			break;
		}
		case FIELD_STRING:
		default:
		{
			std::string edit = value;
			if (ImGui::InputText("##v", &edit, ImGuiInputTextFlags_EnterReturnsTrue))
				applyField(block, nugget, key, edit);
			break;
		}
	}

	if (!present)
		ImGui::PopStyleColor();
	ImGui::SameLine();
	if (present)
	{
		if (ImGui::SmallButton("x"))
			removeField(block, nugget, key);
		ImGui::SetItemTooltip("Remove the field (the default is used)");
	}
	else
	{
		ImGui::Dummy(ImVec2(ImGui::GetFrameHeight() * 0.6f, 1.0f));
	}
	ImGui::PopID();
}

//-------------------------------------------------------------------------------------------------
/** Alpha over the lifetime of a particle, with the random range as a band, and the colour keys. */
//-------------------------------------------------------------------------------------------------
void drawKeyframeCurves(const std::string &text)
{
	struct AlphaKey { float low, high; int frame; };
	struct ColorKey { float rgb[3]; int frame; };
	std::vector<AlphaKey> alpha;
	std::vector<ColorKey> color;
	for (int i = 1; i <= 8; ++i)
	{
		char key[16];
		std::string value;
		snprintf(key, sizeof(key), "Alpha%d", i);
		if (IniText::getField(text, -1, key, value))
		{
			float v[3];
			if (FieldSchema::parseFloats(value, v, 3))
			{
				AlphaKey k = { v[0], v[1], (int)v[2] };
				alpha.push_back(k);
			}
		}
		snprintf(key, sizeof(key), "Color%d", i);
		if (IniText::getField(text, -1, key, value))
		{
			ColorKey k;
			FieldSchema::parseLabeled(value, "RGB", k.rgb);
			k.frame = 0;
			const size_t b = value.find("B:");
			const size_t sp = b == std::string::npos ? std::string::npos : value.find(' ', b);
			if (sp != std::string::npos)
				k.frame = atoi(value.c_str() + sp + 1);
			color.push_back(k);
		}
	}
	std::string lifetimeText;
	float lifetime[2] = { 30.0f, 30.0f };
	if (IniText::getField(text, -1, "Lifetime", lifetimeText))
		FieldSchema::parseFloats(lifetimeText, lifetime, 2);
	int maxFrame = (int)std::max(lifetime[0], lifetime[1]);
	for (size_t i = 0; i < alpha.size(); ++i)
		maxFrame = std::max(maxFrame, alpha[i].frame);
	for (size_t i = 0; i < color.size(); ++i)
		maxFrame = std::max(maxFrame, color[i].frame);
	if (maxFrame < 1)
		maxFrame = 1;

	const float width = ImGui::GetContentRegionAvail().x;
	const float height = 70.0f;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(width, height));
	dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), IM_COL32(25, 25, 30, 255));
	// lifetime range
	const float lifeLo = p.x + width * std::min(lifetime[0], lifetime[1]) / maxFrame;
	const float lifeHi = p.x + width * std::max(lifetime[0], lifetime[1]) / maxFrame;
	dl->AddRectFilled(ImVec2(lifeLo, p.y), ImVec2(lifeHi, p.y + height), IM_COL32(60, 60, 80, 120));
	dl->AddLine(ImVec2(lifeLo, p.y), ImVec2(lifeLo, p.y + height), IM_COL32(120, 120, 160, 255));
	for (size_t i = 0; i + 1 < alpha.size(); ++i)
	{
		const float x0 = p.x + width * alpha[i].frame / maxFrame;
		const float x1 = p.x + width * alpha[i + 1].frame / maxFrame;
		const float lo0 = p.y + height * (1.0f - alpha[i].low);
		const float hi0 = p.y + height * (1.0f - alpha[i].high);
		const float lo1 = p.y + height * (1.0f - alpha[i + 1].low);
		const float hi1 = p.y + height * (1.0f - alpha[i + 1].high);
		dl->AddQuadFilled(ImVec2(x0, lo0), ImVec2(x0, hi0), ImVec2(x1, hi1), ImVec2(x1, lo1), IM_COL32(200, 200, 255, 60));
		dl->AddLine(ImVec2(x0, (lo0 + hi0) * 0.5f), ImVec2(x1, (lo1 + hi1) * 0.5f), IM_COL32(220, 220, 255, 255), 2.0f);
	}
	for (size_t i = 0; i < alpha.size(); ++i)
	{
		const float x = p.x + width * alpha[i].frame / maxFrame;
		dl->AddCircleFilled(ImVec2(x, p.y + height * (1.0f - (alpha[i].low + alpha[i].high) * 0.5f)), 3.0f, IM_COL32(255, 255, 255, 255));
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Alpha over the particle's life (0..%d frames); band: random range, shaded: Lifetime", maxFrame);

	// colour gradient
	p = ImGui::GetCursorScreenPos();
	const float barH = 16.0f;
	ImGui::Dummy(ImVec2(width, barH));
	if (color.empty())
	{
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + barH), IM_COL32(255, 255, 255, 255));
	}
	else
	{
		ImU32 first = IM_COL32((int)color[0].rgb[0], (int)color[0].rgb[1], (int)color[0].rgb[2], 255);
		const float x0 = p.x + width * color[0].frame / maxFrame;
		dl->AddRectFilled(p, ImVec2(x0, p.y + barH), first);
		for (size_t i = 0; i + 1 < color.size(); ++i)
		{
			const ImU32 a = IM_COL32((int)color[i].rgb[0], (int)color[i].rgb[1], (int)color[i].rgb[2], 255);
			const ImU32 b = IM_COL32((int)color[i + 1].rgb[0], (int)color[i + 1].rgb[1], (int)color[i + 1].rgb[2], 255);
			const float xa = p.x + width * color[i].frame / maxFrame;
			const float xb = p.x + width * color[i + 1].frame / maxFrame;
			dl->AddRectFilledMultiColor(ImVec2(xa, p.y), ImVec2(xb, p.y + barH), a, b, b, a);
		}
		const ColorKey &last = color.back();
		const float xl = p.x + width * last.frame / maxFrame;
		dl->AddRectFilled(ImVec2(xl, p.y), ImVec2(p.x + width, p.y + barH), IM_COL32((int)last.rgb[0], (int)last.rgb[1], (int)last.rgb[2], 255));
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Colour over the particle's life (ColorScale is added every frame on top)");
}

//-------------------------------------------------------------------------------------------------
void drawParticleProperties(Int blockIndex, const IniBlock &block)
{
	const std::vector<FieldDef> &fields = FieldSchema::particleSystemFields();
	std::string velocityType, volumeType, windMotion;
	IniText::getField(block.text, -1, "VelocityType", velocityType);
	IniText::getField(block.text, -1, "VolumeType", volumeType);
	IniText::getField(block.text, -1, "WindMotion", windMotion);

	ImGui::Checkbox("Show fields of other velocity / volume / wind types", (bool *)&s_showAllFields);
	drawKeyframeCurves(block.text);

	// Sections in the order their first field appears.
	std::vector<std::string> groups;
	for (size_t i = 0; i < fields.size(); ++i)
	{
		if (std::find(groups.begin(), groups.end(), fields[i].group) == groups.end())
			groups.push_back(fields[i].group);
	}
	for (size_t g = 0; g < groups.size(); ++g)
	{
		const Bool open = ImGui::CollapsingHeader(groups[g].c_str(),
			groups[g] == "Alpha keyframes" || groups[g] == "Color keyframes" || groups[g] == "Wind" ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
		if (!open)
			continue;
		if (ImGui::BeginTable(groups[g].c_str(), 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			for (size_t i = 0; i < fields.size(); ++i)
			{
				if (fields[i].group != groups[g])
					continue;
				if (!s_showAllFields && !FieldSchema::isParticleFieldRelevant(fields[i], velocityType, volumeType, windMotion))
					continue;
				drawField(blockIndex, -1, fields[i], block.text);
			}
			ImGui::EndTable();
		}
	}
}

//-------------------------------------------------------------------------------------------------
void drawFXListProperties(Int blockIndex, const IniBlock &block)
{
	std::vector<std::string> nuggets;
	IniText::getNuggets(block.text, nuggets);

	for (Int n = 0; n < (Int)nuggets.size(); ++n)
	{
		ImGui::PushID(n);
		char title[128];
		std::string name;
		IniText::getField(block.text, n, "Name", name);
		snprintf(title, sizeof(title), "%d  %s%s%s###nugget", n + 1, nuggets[n].c_str(), name.empty() ? "" : "  -  ", name.c_str());
		const Bool open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
		// buttons on the header line
		const float buttons = 4 * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons);
		if (ImGui::ArrowButton("##up", ImGuiDir_Up))
			applyText(blockIndex, IniText::moveNugget(block.text, n, -1), TRUE);
		ImGui::SameLine();
		if (ImGui::ArrowButton("##down", ImGuiDir_Down))
			applyText(blockIndex, IniText::moveNugget(block.text, n, 1), TRUE);
		ImGui::SameLine();
		if (ImGui::SmallButton("+"))
			applyText(blockIndex, IniText::duplicateNugget(block.text, n), TRUE);
		ImGui::SetItemTooltip("Duplicate");
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			applyText(blockIndex, IniText::removeNugget(block.text, n), TRUE);
			ImGui::PopID();
			break;
		}
		ImGui::SetItemTooltip("Remove");

		if (open)
		{
			const std::vector<FieldDef> &fields = FieldSchema::nuggetFields(nuggets[n]);
			if (fields.empty())
			{
				ImGui::TextColored(COLOR_DIM, "Unknown nugget type: edit it in the text tab.");
			}
			else if (ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
				ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
				for (size_t i = 0; i < fields.size(); ++i)
					drawField(blockIndex, n, fields[i], block.text);
				ImGui::EndTable();
			}
		}
		ImGui::PopID();
	}

	ImGui::Separator();
	static int addType = 0;
	const std::vector<std::string> &types = FieldSchema::nuggetTypes();
	ImGui::SetNextItemWidth(200.0f);
	if (ImGui::BeginCombo("##addtype", types[addType].c_str()))
	{
		for (size_t i = 0; i < types.size(); ++i)
		{
			if (ImGui::Selectable(types[i].c_str(), (int)i == addType))
				addType = (int)i;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add nugget"))
		applyText(blockIndex, IniText::addNugget(block.text, types[addType].c_str()), TRUE);
}

//-------------------------------------------------------------------------------------------------
void drawNewBlockPopup(const char *id, PreviewKind kind, Int copyFrom)
{
	if (ImGui::BeginPopup(id))
	{
		static char name[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			const IniBlock *src = FXEditorApp::ini().getBlock(copyFrom);
			snprintf(name, sizeof(name), "%s%s", src ? src->name.str() : "New", src ? "_Copy" : (kind == PREVIEW_FXLIST ? "FXList" : "ParticleSystem"));
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::Text(copyFrom >= 0 ? "Name of the copy:" : "Name of the new %s:", kind == PREVIEW_FXLIST ? "FXList" : "particle system");
		const Bool enter = ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue);
		if (enter || ImGui::Button("Create"))
		{
			AsciiString error;
			const Int created = FXEditorApp::ini().createBlock(kind, AsciiString(name), copyFrom, error);
			if (created >= 0)
			{
				FXEditorApp::select(kind, AsciiString(name));
				FXEditorApp::log(FALSE, "Created %s (saved with the next save)", name);
			}
			else
			{
				FXEditorApp::log(TRUE, "Cannot create %s: %s", name, error.str());
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void saveBlock(Int block)
{
	AsciiString error;
	std::vector<AsciiString> written;
	if (FXEditorApp::ini().save(block, error, &written))
	{
		for (size_t i = 0; i < written.size(); ++i)
			FXEditorApp::log(FALSE, "Saved %s", written[i].str());
		if (written.empty())
			FXEditorApp::log(FALSE, "Nothing to save");
	}
	else
	{
		FXEditorApp::log(TRUE, "Save failed: %s", error.str());
	}
}

//-------------------------------------------------------------------------------------------------
// Map dialog
//-------------------------------------------------------------------------------------------------

const char *const DEFAULT_PREVIEW_MAP = "FXEditorPreview";

void loadMap(const AsciiString &map, const AsciiString &faction)
{
	AsciiString error;
	if (!FXEditorApp::loadMap(map, faction, error))
		FXEditorApp::log(TRUE, "Cannot load the map: %s", error.str());
}

Bool lessMapLabel(const MapEntry &a, const MapEntry &b)
{
	return _stricmp(a.label.c_str(), b.label.c_str()) < 0;
}

void openMapDialog()
{
	s_maps.clear();
	if (TheMapCache)
	{
		for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
		{
			if (!it->second.m_doesExist)
				continue;
			MapEntry entry;
			entry.key = it->first;
			entry.startPositions = FXEditorApp::countStartPositions(it->first);
			AsciiString display;
			display.translate(it->second.m_displayName);
			if (display.isEmpty())
				display = it->first.reverseFind('\\') ? it->first.reverseFind('\\') + 1 : it->first.str();
			char label[300];
			if (entry.startPositions > 0)
				snprintf(label, sizeof(label), "%s  (%d players)", display.str(), entry.startPositions);
			else
				snprintf(label, sizeof(label), "%s  (scenario)", display.str());
			entry.label = label;
			s_maps.push_back(entry);
		}
	}
	std::sort(s_maps.begin(), s_maps.end(), lessMapLabel);
	FXEditorApp::getFactions(s_factions);
	if (!FXEditorApp::getPreviewFaction().isEmpty())
		s_factionPicked = FXEditorApp::getPreviewFaction();
	else if (s_factionPicked.isEmpty())
		s_factionPicked = "FactionAmerica";
	s_mapPicked = FXEditorApp::getPreviewMapKey();
	s_showMapDialog = TRUE;
}

void drawMapDialog()
{
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(560.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Load map", (bool *)&s_showMapDialog, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##mapfilter", "Filter (name or path)", &s_mapFilter);

	Bool load = FALSE;
	const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
	if (ImGui::BeginChild("##maps", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders))
	{
		for (size_t i = 0; i < s_maps.size(); ++i)
		{
			const MapEntry &entry = s_maps[i];
			if (!s_mapFilter.empty() && !containsNoCase(entry.label, s_mapFilter.c_str())
				&& !containsNoCase(std::string(entry.key.str()), s_mapFilter.c_str()))
				continue;
			ImGui::PushID((int)i);
			if (ImGui::Selectable(entry.label.c_str(), entry.key == s_mapPicked, ImGuiSelectableFlags_AllowDoubleClick))
			{
				s_mapPicked = entry.key;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					load = TRUE;
			}
			ImGui::SetItemTooltip("%s", entry.key.str());
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	const MapEntry *picked = nullptr;
	for (size_t i = 0; i < s_maps.size(); ++i)
	{
		if (s_maps[i].key == s_mapPicked)
			picked = &s_maps[i];
	}

	// The faction counts only on multiplayer maps; other maps bring their own player.
	const Bool skirmish = picked && picked->startPositions > 0;
	ImGui::BeginDisabled(!skirmish);
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("Faction", skirmish ? s_factionPicked.str() : "the map's own player"))
	{
		for (size_t i = 0; i < s_factions.size(); ++i)
		{
			if (ImGui::Selectable(s_factions[i].str(), s_factions[i] == s_factionPicked))
				s_factionPicked = s_factions[i];
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Maps with start positions run as a sandbox skirmish with a player of this faction.\n"
		"Other maps run as a single player game with the map's own player.");

	ImGui::BeginDisabled(picked == nullptr);
	if (ImGui::Button("Load", ImVec2(120.0f, 0.0f)))
		load = TRUE;
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		s_showMapDialog = FALSE;

	if (load && picked)
	{
		loadMap(picked->key, skirmish ? s_factionPicked : AsciiString::TheEmptyString);
		s_showMapDialog = FALSE;
	}
	ImGui::End();
}

} // namespace

//=================================================================================================

void FXEditorPanels::drawMainMenu()
{
	if (!ImGui::BeginMainMenuBar())
		return;
	IniDatabase &ini = FXEditorApp::ini();
	if (ImGui::BeginMenu("File"))
	{
		char label[64];
		snprintf(label, sizeof(label), "Save all (%d changed)", ini.getDirtyCount());
		if (ImGui::MenuItem(label, "Ctrl+S", false, ini.getDirtyCount() > 0))
			saveBlock(-1);
		if (ImGui::MenuItem("Reload INI text from disk", nullptr, false, ini.isLoaded()))
		{
			ini.load();
			FXEditorApp::log(FALSE, "Read the INI text again (the game keeps what it has parsed)");
		}
		if (ImGui::MenuItem("Reload textures"))
		{
			ReloadAllTextures();
			FXEditorPanels::shutdown();
			FXEditorApp::log(FALSE, "Textures reloaded");
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Quit"))
			FXEditorApp::requestQuit();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Preview"))
	{
		PreviewScene &scene = FXEditorApp::scene();
		if (ImGui::MenuItem(scene.isPaused() ? "Resume" : "Pause", "Space"))
			scene.setPaused(!scene.isPaused());
		if (ImGui::MenuItem("Step 1 frame", "."))
			scene.step(1);
		if (ImGui::MenuItem("Restart", "R", false, scene.isPlaying()))
			scene.restart();
		if (ImGui::MenuItem("Stop", nullptr, false, scene.isPlaying()))
			scene.stop();
		if (ImGui::MenuItem("Remove all particles"))
			scene.clearAllParticles();
		ImGui::Separator();
		if (ImGui::MenuItem("Look at the preview point", "Home"))
			scene.lookAtCenter();
		if (ImGui::MenuItem("Preview point to the camera's target"))
			scene.setCenter(TheTacticalView->getPosition());
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Map"))
	{
		if (ImGui::MenuItem("Load map..."))
			openMapDialog();
		if (ImGui::MenuItem("Reload map", nullptr, false, !FXEditorApp::getPreviewMapKey().isEmpty()))
			loadMap(FXEditorApp::getPreviewMapKey(), FXEditorApp::getPreviewFaction());
		if (ImGui::MenuItem("Empty preview map"))
			loadMap(AsciiString(DEFAULT_PREVIEW_MAP), AsciiString::TheEmptyString);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem(LIBRARY, nullptr, (bool *)&s_showLibrary);
		ImGui::MenuItem(INSPECTOR, nullptr, (bool *)&s_showInspector);
		ImGui::MenuItem(PREVIEW, nullptr, (bool *)&s_showPreview);
		ImGui::MenuItem(AUDIO, nullptr, (bool *)&s_showAudio);
		ImGui::MenuItem(LOG, nullptr, (bool *)&s_showLog);
		ImGui::Separator();
		ImGui::MenuItem("ImGui demo", nullptr, (bool *)&s_showDemo);
		ImGui::EndMenu();
	}
	char status[256];
	snprintf(status, sizeof(status), "%s%s%s | %s | %.0f fps | MCP port %d", FXEditorApp::getPreviewMap().str(),
		FXEditorApp::getPreviewFaction().isEmpty() ? "" : " as ", FXEditorApp::getPreviewFaction().str(),
		FXEditorApp::isDX12() ? "D3D12" : "D3D8", ImGui::GetIO().Framerate, TheGameMcp ? TheGameMcp->getPort() : 0);
	ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status).x - 20.0f);
	ImGui::TextColored(COLOR_DIM, "%s", status);
	ImGui::EndMainMenuBar();

	if (s_showMapDialog)
		drawMapDialog();

	// Shortcuts that work while the view, not a text field, has the keyboard.
	ImGuiIO &io = ImGui::GetIO();
	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && ini.getDirtyCount() > 0)
		saveBlock(-1);
	if (!io.WantTextInput)
	{
		PreviewScene &scene = FXEditorApp::scene();
		if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
			scene.setPaused(!scene.isPaused());
		if (ImGui::IsKeyPressed(ImGuiKey_Period))
			scene.step(1);
		if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !io.KeyCtrl)
			scene.restart();
		if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
			scene.lookAtCenter();
		const Int block = FXEditorApp::getSelectedBlock();
		AsciiString error;
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && block >= 0 && ini.undo(block, error))
			FXEditorApp::onBlockChanged(block);
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && block >= 0 && ini.redo(block, error))
			FXEditorApp::onBlockChanged(block);
	}
	if (s_showDemo)
		ImGui::ShowDemoWindow((bool *)&s_showDemo);
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawLibrary()
{
	if (!s_showLibrary)
		return;
	if (!ImGui::Begin(LIBRARY, (bool *)&s_showLibrary))
	{
		ImGui::End();
		return;
	}
	if (!FXEditorApp::scene().isReady() || TheParticleSystemManager == nullptr)
	{
		ImGui::TextColored(COLOR_DIM, "Waiting for the preview game...");
		ImGui::End();
		return;
	}

	static char filter[128] = "";
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##filter", "Filter", filter, sizeof(filter));

	IniDatabase &ini = FXEditorApp::ini();
	if (ImGui::BeginTabBar("kinds"))
	{
		for (int k = 0; k < 2; ++k)
		{
			const PreviewKind kind = k == 0 ? PREVIEW_PARTICLE_SYSTEM : PREVIEW_FXLIST;
			if (!ImGui::BeginTabItem(k == 0 ? "Particle systems" : "FXLists"))
				continue;

			if (ImGui::Button("New"))
				ImGui::OpenPopup("new");
			drawNewBlockPopup("new", kind, -1);
			ImGui::SameLine();
			const Int selectedBlock = FXEditorApp::getSelectedKind() == kind ? FXEditorApp::getSelectedBlock() : -1;
			ImGui::BeginDisabled(selectedBlock < 0);
			if (ImGui::Button("Duplicate"))
				ImGui::OpenPopup("dup");
			ImGui::EndDisabled();
			drawNewBlockPopup("dup", kind, selectedBlock);

			const std::vector<std::string> &names = k == 0 ? particleSystemNames() : fxListNames();
			std::vector<Int> shown;
			for (size_t i = 0; i < names.size(); ++i)
			{
				if (containsNoCase(names[i], filter))
					shown.push_back((Int)i);
			}
			ImGui::TextColored(COLOR_DIM, "%d of %d", (int)shown.size(), (int)names.size());

			ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_None);
			ImGuiListClipper clipper;
			clipper.Begin((int)shown.size());
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::string &name = names[shown[row]];
					const AsciiString aname(name.c_str());
					const Int block = ini.findBlock(kind, aname);
					const IniBlock *b = ini.getBlock(block);
					const Bool selected = FXEditorApp::getSelectedKind() == kind && FXEditorApp::getSelectedName().compareNoCase(aname) == 0;
					const Bool dirty = b && b->isDirty();
					const Bool error = b && !b->error.empty();
					if (error)
						ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
					else if (dirty)
						ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIRTY);
					else if (b == nullptr)
						ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
					char label[256];
					snprintf(label, sizeof(label), "%s%s", name.c_str(), dirty ? " *" : "");
					if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick))
					{
						FXEditorApp::select(kind, aname);
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							AsciiString err;
							if (!FXEditorApp::play(kind, aname, err))
								FXEditorApp::log(TRUE, "%s", err.str());
						}
					}
					ImGui::PopStyleColor(error || dirty || b == nullptr ? 1 : 0);
					if (b == nullptr && ImGui::IsItemHovered())
						ImGui::SetTooltip("No INI text found for it: it can be played but not edited.");
					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::MenuItem("Play"))
						{
							AsciiString err;
							if (!FXEditorApp::play(kind, aname, err))
								FXEditorApp::log(TRUE, "%s", err.str());
						}
						if (ImGui::MenuItem("Copy name"))
							ImGui::SetClipboardText(name.c_str());
						ImGui::EndPopup();
					}
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawInspector()
{
	if (!s_showInspector)
		return;
	if (!ImGui::Begin(INSPECTOR, (bool *)&s_showInspector))
	{
		ImGui::End();
		return;
	}
	IniDatabase &ini = FXEditorApp::ini();
	const Int index = FXEditorApp::getSelectedBlock();
	IniBlock *block = ini.getBlock(index);
	if (block == nullptr)
	{
		if (FXEditorApp::getSelectedName().isEmpty())
			ImGui::TextColored(COLOR_DIM, "Pick a particle system or FXList in the library.");
		else
			ImGui::TextColored(COLOR_DIM, "No INI text found for %s.", FXEditorApp::getSelectedName().str());
		ImGui::End();
		return;
	}

	// Header: name, file, actions.
	ImGui::Text("%s %s", block->kind == PREVIEW_FXLIST ? "FXList" : "ParticleSystem", block->name.str());
	const IniSourceFile *file = ini.getFile(block->file);
	if (file)
		ImGui::TextColored(COLOR_DIM, "%s%s", file->path.str(), file->loose ? "" : "  (in a .big archive; saving writes a loose copy)");
	else
		ImGui::TextColored(COLOR_DIRTY, "New, not saved yet");

	if (ImGui::Button("Play"))
	{
		AsciiString err;
		if (!FXEditorApp::play(block->kind, block->name, err))
			FXEditorApp::log(TRUE, "%s", err.str());
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!block->isDirty());
	if (ImGui::Button("Save"))
		saveBlock(index);
	ImGui::SameLine();
	if (ImGui::Button("Revert"))
	{
		AsciiString err;
		if (ini.revert(index, err))
			FXEditorApp::onBlockChanged(index);
		else
			FXEditorApp::log(TRUE, "%s", err.str());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(block->undo.empty());
	if (ImGui::Button("Undo"))
	{
		AsciiString err;
		if (ini.undo(index, err))
			FXEditorApp::onBlockChanged(index);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(block->redo.empty());
	if (ImGui::Button("Redo"))
	{
		AsciiString err;
		if (ini.redo(index, err))
			FXEditorApp::onBlockChanged(index);
	}
	ImGui::EndDisabled();

	if (!block->error.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
		ImGui::TextWrapped("The game cannot parse this text and keeps the last good one: %s", block->error.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginTabBar("views"))
	{
		if (ImGui::BeginTabItem("Properties"))
		{
			ImGui::BeginChild("##props");
			// The block may change (or move) while it is drawn; draw a copy.
			const IniBlock copy = *block;
			if (copy.kind == PREVIEW_PARTICLE_SYSTEM)
				drawParticleProperties(index, copy);
			else
				drawFXListProperties(index, copy);
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Text"))
		{
			static std::string edit;
			static Int editBlock = -1;
			static std::string editBase;
			if (editBlock != index || editBase != block->text)
			{
				edit = block->text;
				editBlock = index;
				editBase = block->text;
			}
			const Bool apply = ImGui::Button("Apply (Ctrl+Enter)");
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
				edit = block->text;
			ImGui::InputTextMultiline("##text", &edit, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput);
			const Bool ctrlEnter = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
			if (apply || ctrlEnter)
			{
				AsciiString err;
				if (ini.setText(index, edit, err, TRUE, TRUE))
					FXEditorApp::onBlockChanged(index);
				else
					FXEditorApp::log(TRUE, "%s", err.str());
				editBase = ini.getBlock(index)->text;
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawPreview()
{
	PreviewScene &scene = FXEditorApp::scene();
	PreviewStats stats;
	scene.getStats(stats);
	s_particleHistory.push_back((float)stats.previewParticles);
	if (s_particleHistory.size() > 300)
		s_particleHistory.erase(s_particleHistory.begin());

	if (!s_showPreview)
		return;
	if (!ImGui::Begin(PREVIEW, (bool *)&s_showPreview))
	{
		ImGui::End();
		return;
	}

	if (scene.isPlaying())
		ImGui::Text("Playing %s %s", previewKindName(scene.getKind()), scene.getName().str());
	else
		ImGui::TextColored(COLOR_DIM, "Nothing plays. Double-click an entry in the library.");

	if (ImGui::Button(scene.isPaused() ? "Resume" : "Pause"))
		scene.setPaused(!scene.isPaused());
	ImGui::SameLine();
	if (ImGui::Button("Step"))
		scene.step(1);
	ImGui::SameLine();
	if (ImGui::Button("+10"))
		scene.step(10);
	ImGui::SameLine();
	ImGui::BeginDisabled(!scene.isPlaying());
	if (ImGui::Button("Restart"))
		scene.restart();
	ImGui::SameLine();
	if (ImGui::Button("Stop"))
		scene.stop();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Clear particles"))
		scene.clearAllParticles();
	ImGui::SetItemTooltip("Removes all particles and scorch marks.");
	ImGui::SameLine();
	float speed = scene.getSpeed();
	ImGui::SetNextItemWidth(150.0f);
	if (ImGui::SliderFloat("Speed", &speed, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
		scene.setSpeed(speed);
	ImGui::SameLine();
	ImGui::TextColored(COLOR_DIM, "frame %u", TheGameLogic ? TheGameLogic->getFrame() : 0);

	if (ImGui::BeginTable("previewcols", 2, ImGuiTableFlags_Resizable))
	{
		ImGui::TableNextColumn();
		PreviewSettings &s = scene.settings();
		ImGui::Checkbox("Repeat every", (bool *)&s.repeat);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("s##repeat", &s.repeatSeconds, 0.05f, 0.1f, 60.0f, "%.1f");
		ImGui::SetNextItemWidth(150.0f);
		ImGui::DragFloat("Height above ground", &s.height, 0.5f, -50.0f, 500.0f, "%.0f");
		ImGui::SetNextItemWidth(150.0f);
		ImGui::DragFloat("Direction", &s.angle, 1.0f, -360.0f, 360.0f, "%.0f deg");
		ImGui::Checkbox("Follow the camera", (bool *)&s.followCamera);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Each start of the effect (play, restart, repeat) happens where the camera looks; what already runs stays");
		ImGui::Checkbox("Move in a circle", (bool *)&s.move);
		if (s.move)
		{
			ImGui::SetNextItemWidth(150.0f);
			ImGui::DragFloat("Radius", &s.moveRadius, 1.0f, 5.0f, 500.0f, "%.0f");
			ImGui::SetNextItemWidth(150.0f);
			ImGui::DragFloat("Speed (units/s)", &s.moveSpeed, 1.0f, 0.0f, 1000.0f, "%.0f");
		}
		std::string attach = s.attachTo.str();
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::InputTextWithHint("##attach", "(on the point)", &attach, ImGuiInputTextFlags_EnterReturnsTrue))
			s.attachTo = attach.c_str();
		ImGui::SameLine();
		if (ImGui::Button("...##attachpick"))
			ImGui::OpenPopup("attachpick");
		std::string picked;
		if (namePicker("attachpick", objectNames(), picked))
			s.attachTo = picked.c_str();
		ImGui::SameLine();
		if (ImGui::SmallButton("x##attachclear"))
			s.attachTo.clear();
		ImGui::SameLine();
		ImGui::TextUnformatted("Play on object");

		ImGui::TableNextColumn();
		ImGui::Text("Particles: %u of max %u (field %u)", stats.totalParticles, stats.maxParticles, stats.fieldParticles);
		ImGui::Text("Systems: %u", stats.systems);
		ImGui::Text("Preview: %u particles in %u systems", stats.previewParticles, stats.previewSystems);
		ImGui::Text("Started %u times, last %.1f s ago", stats.playCount, stats.framesSinceStart / (float)LOGICFRAMES_PER_SECOND);
		float maxValue = 1.0f;
		for (size_t i = 0; i < s_particleHistory.size(); ++i)
			maxValue = std::max(maxValue, s_particleHistory[i]);
		ImGui::PlotLines("##hist", s_particleHistory.data(), (int)s_particleHistory.size(), 0, "preview particles", 0.0f,
			maxValue * 1.1f, ImVec2(-1.0f, 60.0f));
		ImGui::EndTable();
	}
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawAudio()
{
	if (!s_showAudio)
		return;
	if (!ImGui::Begin(AUDIO, (bool *)&s_showAudio))
	{
		ImGui::End();
		return;
	}
	if (TheAudio == nullptr || !FXEditorApp::scene().isReady())
	{
		ImGui::TextColored(COLOR_DIM, "Waiting for the preview game...");
		ImGui::End();
		return;
	}

	static float soundVolume = -1.0f;
	static float musicVolume = -1.0f;
	if (soundVolume < 0.0f)
	{
		soundVolume = TheAudio->getVolume(AudioAffect_Sound3D);
		// Music only gets in the way of judging effects.
		musicVolume = 0.0f;
		TheAudio->setVolume(0.0f, AudioAffect_Music);
	}
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::SliderFloat("Effects", &soundVolume, 0.0f, 1.0f, "%.2f"))
	{
		TheAudio->setVolume(soundVolume, (AudioAffect)(AudioAffect_Sound | AudioAffect_Sound3D));
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::SliderFloat("Music", &musicVolume, 0.0f, 1.0f, "%.2f"))
		TheAudio->setVolume(musicVolume, AudioAffect_Music);

	static char filter[128] = "";
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##soundfilter", "Filter sounds", filter, sizeof(filter));
	const std::vector<std::string> &names = soundNames();
	std::vector<Int> shown;
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (containsNoCase(names[i], filter))
			shown.push_back((Int)i);
	}
	ImGui::BeginChild("##sounds");
	ImGuiListClipper clipper;
	clipper.Begin((int)shown.size());
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			const std::string &name = names[shown[row]];
			if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				AsciiString err;
				if (!FXEditorApp::scene().play(PREVIEW_SOUND, AsciiString(name.c_str()), err))
					FXEditorApp::log(TRUE, "%s", err.str());
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Play at the preview point"))
				{
					AsciiString err;
					if (!FXEditorApp::scene().play(PREVIEW_SOUND, AsciiString(name.c_str()), err))
						FXEditorApp::log(TRUE, "%s", err.str());
				}
				if (ImGui::MenuItem("Copy name"))
					ImGui::SetClipboardText(name.c_str());
				ImGui::EndPopup();
			}
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawLog()
{
	if (!s_showLog)
		return;
	if (!ImGui::Begin(LOG, (bool *)&s_showLog))
	{
		ImGui::End();
		return;
	}
	Int count = 0;
	const FXEditorLogEntry *entries = FXEditorApp::getLog(count);
	static UnsignedInt lastSerial = 0;
	ImGui::BeginChild("##log");
	for (Int i = 0; i < count; ++i)
	{
		if (entries[i].error)
			ImGui::TextColored(COLOR_ERROR, "%s", entries[i].text.str());
		else
			ImGui::TextUnformatted(entries[i].text.str());
	}
	if (count > 0 && entries[count - 1].serial != lastSerial)
	{
		ImGui::SetScrollHereY(1.0f);
		lastSerial = entries[count - 1].serial;
	}
	ImGui::EndChild();
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawOverlayInfo()
{
	if (FXEditorApp::scene().isReady())
		return;
	const AsciiString &error = FXEditorApp::getStartError();
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.4f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing);
	if (!error.isEmpty())
	{
		ImGui::TextColored(COLOR_ERROR, "The preview game did not start: %s", error.str());
		ImGui::TextWrapped("Put the preview map into the Maps folder or start the editor with -map <map name or .map path>.");
	}
	else
	{
		ImGui::Text("Loading the preview map '%s'...", FXEditorApp::getPreviewMap().str());
	}
	ImGui::End();
}

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::shutdown()
{
	for (TextureCache::iterator it = s_textures.begin(); it != s_textures.end(); ++it)
	{
		if (it->second)
			it->second->Release_Ref();
	}
	s_textures.clear();
}
