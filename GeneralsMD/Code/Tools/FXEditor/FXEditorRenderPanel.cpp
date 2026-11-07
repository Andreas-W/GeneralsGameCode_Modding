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

// The Direct3D 12 window of the FX Editor: post-processing passes and chains, materials and the shadow
// map, edited as INI text like the particle systems. Every edit is parsed into the game at once; the
// renderer builds the chain or the materials again. Param sliders change the running pass or material
// while they are dragged and write the text when they are let go.

#include "Precompiled/PreRTS.h"

#include "FXEditorPanels.h"
#include "FXEditorApp.h"
#include "IniDatabase.h"

#include "Common/GlobalData.h"
#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/W3DMaterials.h"
#include "W3DDevice/GameClient/W3DPostProcess.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <stdio.h>
#include <stdlib.h>

const char *const FXEditorPanels::RENDER = "Direct3D 12";

namespace
{

const ImVec4 COLOR_DIRTY(1.0f, 0.75f, 0.3f, 1.0f);
const ImVec4 COLOR_ERROR(1.0f, 0.4f, 0.4f, 1.0f);
const ImVec4 COLOR_DIM(0.6f, 0.6f, 0.6f, 1.0f);
const ImVec4 COLOR_ACTIVE(0.5f, 0.9f, 0.5f, 1.0f);

enum RenderFieldType { RF_BOOL, RF_INT, RF_REAL, RF_TEXT, RF_ENUM };

const char *const MATERIAL_USES[] = { "Meshes", "Particles", "Terrain", "Water", nullptr };

struct RenderField
{
	const char *blockType;
	const char *key;
	RenderFieldType type;
	const char *defaultValue;		///< shown (dimmed) while the block does not set the field
	float speed;					///< drag speed of numbers
	const char *help;
	const char *const *enumNames;
};

const RenderField FIELDS[] =
{
	{ "PostProcessPass", "Shader", RF_TEXT, "", 0, "Data\\Shaders\\PostProcess\\<Shader>.hlsl, else a built-in shader (PP_...)", nullptr },
	{ "PostProcessPass", "Inputs", RF_TEXT, "Scene", 0, "Input0..5: Scene, Depth, None, Previous, an earlier pass, or Texture:<file>", nullptr },
	{ "PostProcessPass", "Scale", RF_INT, "1", 0.05f, "output size = screen size / Scale; 0 = a single pixel", nullptr },
	{ "PostProcessPass", "HDR", RF_BOOL, "No", 0, "16 bit float output", nullptr },
	{ "PostProcess", "Passes", RF_TEXT, "", 0, "the passes that run, in order; the last draws into the screen", nullptr },
	{ "PostProcess", "HDRScene", RF_BOOL, "No", 0, "draw the scene in 16 bit float while the chain runs", nullptr },
	{ "PostProcessChain", "Passes", RF_TEXT, "", 0, "passes of this chain (POSTPROCESS_USE_CHAIN script action)", nullptr },
	{ "ShadowMap", "Enabled", RF_BOOL, "Yes", 0, "soft shadows from a depth map instead of stencil shadow volumes", nullptr },
	{ "ShadowMap", "Size", RF_INT, "2048", 16.0f, "texels of the square map that covers the visible area", nullptr },
	{ "ShadowMap", "Softness", RF_REAL, "1.5", 0.02f, "radius of the soft edge in texels", nullptr },
	{ "ShadowMap", "Bias", RF_REAL, "2", 0.05f, "world units a point is moved towards the sun before the test", nullptr },
	{ "ShadowMap", "DepthBias", RF_INT, "0", 0.2f, "rasterizer depth bias of the casters", nullptr },
	{ "ShadowMap", "SlopeBias", RF_REAL, "2", 0.05f, "rasterizer slope scaled depth bias of the casters", nullptr },
	{ "ShadowMap", "Darkness", RF_REAL, "1", 0.01f, "1 = the map's shadow color, higher = darker, 0 = none", nullptr },
	{ "ShadowMap", "MinSunElevation", RF_REAL, "0", 0.2f, "degrees; a lower sun is raised to this", nullptr },
	{ "ShadowMap", "Terrain", RF_BOOL, "Yes", 0, "hills and cliffs cast shadows too", nullptr },
	{ "ShadowMap", "Trees", RF_BOOL, "Yes", 0, "trees cast shadows into the map", nullptr },
	{ "ShadowMap", "MaxUpdateRate", RF_REAL, "60", 0.5f, "redraws per second while the view stands still; 0 = every frame", nullptr },
	{ "ShadowMap", "DetailCascade", RF_BOOL, "Yes", 0, "a second, sharper map around the middle of the view", nullptr },
	{ "ShadowMap", "DetailCoverage", RF_REAL, "0.3", 0.005f, "its width as a fraction of the main map's", nullptr },
	{ "ShadowMap", "DetailSize", RF_INT, "0", 16.0f, "its texels, 0 = Size", nullptr },
	{ "Material", "Use", RF_ENUM, "Meshes", 0, "what gets the material", MATERIAL_USES },
	{ "Material", "Textures", RF_TEXT, "", 0, "base textures without extension (Meshes, Particles)", nullptr },
	{ "Material", "Shader", RF_TEXT, "MAT_Standard", 0, "Data\\Shaders\\Material\\<Shader>.hlsl, else a built-in (MAT_Standard, MAT_Particle, MAT_Terrain, MAT_Water)", nullptr },
	{ "Material", "NormalMap", RF_TEXT, "_nrm", 0, "per texture <texture>_nrm; a file name; or None", nullptr },
	{ "Material", "SpecularMap", RF_TEXT, "_spec", 0, "the same; red = strength, green = gloss", nullptr },
	{ "Material", "EmissiveMap", RF_TEXT, "_emis", 0, "the same; glow color", nullptr },
	{ "Material", "ExtraMap", RF_TEXT, "None", 0, "a sixth texture for custom shaders", nullptr },
};

Int s_selectedPostProcess = -1;
Int s_selectedMaterial = -1;
Int s_gestureBlock = -1;
std::string s_gestureText;
std::string s_newParamName;
std::string s_newParamValue;
std::string s_newBlockName;

IniDatabase &db()
{
	return FXEditorApp::renderIni();
}

Bool isType(const IniBlock *b, const char *type)
{
	return b != nullptr && _stricmp(b->type.c_str(), type) == 0;
}

Bool applyText(Int block, const std::string &text, Bool recordUndo)
{
	AsciiString error;
	if (db().setText(block, text, error, FALSE, recordUndo))
		return TRUE;
	FXEditorApp::log(TRUE, "%s", error.str());
	return FALSE;
}

/// A drag of a field: applied live without undo steps; the text before it becomes one undo step at its end.
void applyDrag(Int block, const std::string &newText, Bool activated, Bool changed, Bool finished)
{
	IniBlock *b = db().getBlock(block);
	if (b == nullptr)
		return;
	if (activated)
	{
		s_gestureBlock = block;
		s_gestureText = b->text;
	}
	if (changed)
		applyText(block, newText, FALSE);
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

std::string formatReal(float v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.4g", v);
	return buf;
}

Int parseReals(const std::string &text, float *values, Int max)
{
	Int count = 0;
	const char *p = text.c_str();
	while (*p && count < max)
	{
		char *end = nullptr;
		const double v = strtod(p, &end);
		if (end == p)
			break;
		values[count++] = (float)v;
		p = end;
	}
	return count;
}

Bool containsWord(const std::string &list, const char *word)
{
	const char *p = list.c_str();
	const size_t len = strlen(word);
	while (*p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t')
			++p;
		if ((size_t)(p - start) == len && _strnicmp(start, word, len) == 0)
			return TRUE;
	}
	return FALSE;
}

std::string removeWord(const std::string &list, const char *word)
{
	std::string out;
	const char *p = list.c_str();
	const size_t len = strlen(word);
	while (*p)
	{
		while (*p == ' ' || *p == '\t')
			++p;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t')
			++p;
		if (p == start)
			break;
		if ((size_t)(p - start) == len && _strnicmp(start, word, len) == 0)
			continue;
		if (!out.empty())
			out += " ";
		out.append(start, p - start);
	}
	return out;
}

//-------------------------------------------------------------------------------------------------
void drawField(Int index, const RenderField &field)
{
	IniBlock *block = db().getBlock(index);
	std::string value;
	const Bool present = IniText::getField(block->text, -1, field.key, value) != FALSE;
	if (!present)
		value = field.defaultValue;

	ImGui::PushID(field.key);
	ImGui::AlignTextToFramePadding();
	if (present)
		ImGui::TextUnformatted(field.key);
	else
		ImGui::TextColored(COLOR_DIM, "%s", field.key);
	ImGui::SetItemTooltip("%s", field.help);
	ImGui::SameLine(140.0f);
	ImGui::SetNextItemWidth(-28.0f);

	switch (field.type)
	{
		case RF_BOOL:
		{
			bool v = _stricmp(value.c_str(), "Yes") == 0 || _stricmp(value.c_str(), "True") == 0 || value == "1";
			if (ImGui::Checkbox("##v", &v))
				applyText(index, IniText::setField(block->text, -1, field.key, v ? "Yes" : "No"), TRUE);
			break;
		}
		case RF_INT:
		{
			int v = atoi(value.c_str());
			const bool changed = ImGui::DragInt("##v", &v, field.speed);
			applyDrag(index, IniText::setField(block->text, -1, field.key, std::to_string(v)), ImGui::IsItemActivated(), changed,
				ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case RF_REAL:
		{
			float v = (float)atof(value.c_str());
			const bool changed = ImGui::DragFloat("##v", &v, field.speed, 0.0f, 0.0f, "%.3f");
			applyDrag(index, IniText::setField(block->text, -1, field.key, formatReal(v)), ImGui::IsItemActivated(), changed,
				ImGui::IsItemDeactivatedAfterEdit());
			break;
		}
		case RF_TEXT:
		{
			std::string edit = value;
			ImGui::InputText("##v", &edit);
			if (ImGui::IsItemDeactivatedAfterEdit() && edit != value)
				applyText(index, IniText::setField(block->text, -1, field.key, edit), TRUE);
			break;
		}
		case RF_ENUM:
		{
			if (ImGui::BeginCombo("##v", value.c_str()))
			{
				for (Int i = 0; field.enumNames[i]; ++i)
				{
					if (ImGui::Selectable(field.enumNames[i], _stricmp(field.enumNames[i], value.c_str()) == 0))
						applyText(index, IniText::setField(block->text, -1, field.key, field.enumNames[i]), TRUE);
				}
				ImGui::EndCombo();
			}
			break;
		}
	}

	if (present)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
			applyText(index, IniText::removeField(db().getBlock(index)->text, -1, field.key), TRUE);
		ImGui::SetItemTooltip("Remove %s (back to its default)", field.key);
	}
	ImGui::PopID();
}

//-------------------------------------------------------------------------------------------------
/** Param lines: a slider per value. While it is dragged the running pass or material gets the values
	directly (fast); the text is written when it is let go. Without a running one the text is written
	on every change. */
//-------------------------------------------------------------------------------------------------
void drawParams(Int index)
{
	IniBlock *block = db().getBlock(index);
	const Bool material = isType(block, "Material");
	const Bool pass = isType(block, "PostProcessPass");
	if (!material && !pass)
		return;

	ImGui::SeparatorText("Parameters");
	std::vector<std::string> names;
	std::vector<std::string> values;
	IniText::getKeyedFields(block->text, "Param", names, values);
	for (size_t i = 0; i < names.size(); ++i)
	{
		ImGui::PushID((int)i);
		float v[4] = { 0, 0, 0, 0 };
		Int count = parseReals(values[i], v, 4);
		if (count == 0)
			count = 1;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(names[i].c_str());
		ImGui::SameLine(140.0f);
		ImGui::SetNextItemWidth(-28.0f);
		bool changed = false;
		switch (count)
		{
			case 1: changed = ImGui::DragFloat("##p", v, 0.01f, 0.0f, 0.0f, "%.3f"); break;
			case 2: changed = ImGui::DragFloat2("##p", v, 0.01f, 0.0f, 0.0f, "%.3f"); break;
			case 3: changed = ImGui::DragFloat3("##p", v, 0.01f, 0.0f, 0.0f, "%.3f"); break;
			default: changed = ImGui::DragFloat4("##p", v, 0.01f, 0.0f, 0.0f, "%.3f"); break;
		}
		const Bool activated = ImGui::IsItemActivated();
		const Bool finished = ImGui::IsItemDeactivatedAfterEdit();
		const Bool active = ImGui::IsItemActive();
		std::string newValue;
		for (Int k = 0; k < count; ++k)
			newValue += (k ? " " : "") + formatReal(v[k]);
		const std::string newText = IniText::setKeyedField(block->text, "Param", names[i].c_str(), newValue);

		Bool live = FALSE;
		if (changed && active)
		{
			Real r[4] = { v[0], v[1], v[2], v[3] };
			if (pass && TheW3DPostProcess)
				live = TheW3DPostProcess->setParameter(block->name, AsciiString(names[i].c_str()), r, count);
			else if (material && TheW3DMaterials)
				live = TheW3DMaterials->setParameter(block->name, AsciiString(names[i].c_str()), r, count);
		}
		if (activated)
		{
			s_gestureBlock = index;
			s_gestureText = block->text;
		}
		if (changed && !live)
			applyText(index, newText, FALSE);
		else if (changed && live)
			block->text = newText;	// parsed when the drag ends
		if (finished && s_gestureBlock == index)
		{
			// The text as dragged goes through the parser once, as one undo step.
			const std::string finalText = block->text;
			block->text = s_gestureText;
			if (applyText(index, finalText, TRUE) == FALSE)
				block->text = s_gestureText;
			s_gestureBlock = -1;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
			applyText(index, IniText::removeKeyedField(db().getBlock(index)->text, "Param", names[i].c_str()), TRUE);
		ImGui::SetItemTooltip("Remove the parameter");
		ImGui::PopID();
	}

	ImGui::SetNextItemWidth(130.0f);
	ImGui::InputTextWithHint("##newname", "name", &s_newParamName);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##newvalue", "1 to 4 values", &s_newParamValue);
	ImGui::SameLine();
	ImGui::BeginDisabled(s_newParamName.empty() || s_newParamName.find(' ') != std::string::npos);
	if (ImGui::Button("Add parameter"))
	{
		const std::string value = s_newParamValue.empty() ? "0" : s_newParamValue;
		if (applyText(index, IniText::setKeyedField(db().getBlock(index)->text, "Param", s_newParamName.c_str(), value), TRUE))
		{
			s_newParamName.clear();
			s_newParamValue.clear();
		}
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("A variable of the shader's Params cbuffer. Time of day variants (ParamNight ...) are edited in the text.");
}

//-------------------------------------------------------------------------------------------------
void drawTextEditor(Int index)
{
	static std::string edit;
	static Int editBlock = -1;
	static std::string editBase;
	IniBlock *block = db().getBlock(index);
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
		if (!db().setText(index, edit, err, TRUE, TRUE))
			FXEditorApp::log(TRUE, "%s", err.str());
		editBase = db().getBlock(index)->text;
	}
}

//-------------------------------------------------------------------------------------------------
/** The editor of one block: its fields and parameters, or its text. */
//-------------------------------------------------------------------------------------------------
void drawBlockEditor(Int index)
{
	IniBlock *block = db().getBlock(index);
	if (block == nullptr)
	{
		ImGui::TextColored(COLOR_DIM, "Pick a block above.");
		return;
	}

	ImGui::Text("%s %s", block->type.c_str(), block->name.compareNoCase(block->type.c_str()) == 0 ? "" : block->name.str());
	if (block->isDirty())
	{
		ImGui::SameLine();
		ImGui::TextColored(COLOR_DIRTY, "(changed)");
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(block->undo.empty());
	if (ImGui::SmallButton("Undo"))
	{
		AsciiString err;
		db().undo(index, err);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(block->redo.empty());
	if (ImGui::SmallButton("Redo"))
	{
		AsciiString err;
		db().redo(index, err);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!block->isDirty() || block->file < 0);
	if (ImGui::SmallButton("Revert"))
	{
		AsciiString err;
		if (!db().revert(index, err))
			FXEditorApp::log(TRUE, "%s", err.str());
	}
	ImGui::EndDisabled();
	if (const IniSourceFile *file = db().getFile(block->file))
		ImGui::TextColored(COLOR_DIM, "%s", file->path.str());
	if (!block->error.empty())
		ImGui::TextColored(COLOR_ERROR, "%s", block->error.c_str());

	// A pass can be put into or taken out of the PostProcess chain from here.
	if (isType(block, "PostProcessPass"))
	{
		const Int chain = db().findTypedBlock("PostProcess", AsciiString("PostProcess"));
		if (const IniBlock *chainBlock = db().getBlock(chain))
		{
			std::string passes;
			IniText::getField(chainBlock->text, -1, "Passes", passes);
			const Bool inChain = containsWord(passes, block->name.str());
			const AsciiString name = block->name;
			if (ImGui::SmallButton(inChain ? "Take out of the chain" : "Add to the end of the chain"))
			{
				const std::string newPasses = inChain ? removeWord(passes, name.str()) : (passes.empty() ? std::string(name.str()) : passes + " " + name.str());
				applyText(chain, IniText::setField(chainBlock->text, -1, "Passes", newPasses), TRUE);
			}
			ImGui::SetItemTooltip("Changes Passes of the PostProcess block");
		}
	}

	if (ImGui::BeginTabBar("##blocktabs"))
	{
		if (ImGui::BeginTabItem("Fields"))
		{
			for (size_t i = 0; i < ARRAY_SIZE(FIELDS); ++i)
			{
				if (isType(db().getBlock(index), FIELDS[i].blockType))
					drawField(index, FIELDS[i]);
			}
			drawParams(index);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Text"))
		{
			drawTextEditor(index);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

//-------------------------------------------------------------------------------------------------
void drawNewBlock(const char *type, Int copyFrom, Int &selected)
{
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##newblock", "new name", &s_newBlockName);
	ImGui::SameLine();
	ImGui::BeginDisabled(s_newBlockName.empty());
	char label[64];
	snprintf(label, sizeof(label), copyFrom >= 0 ? "Copy as new %s" : "New %s", type);
	if (ImGui::Button(label))
	{
		AsciiString error;
		const Int created = db().createTypedBlock(type, AsciiString(s_newBlockName.c_str()), copyFrom, error);
		if (created >= 0)
		{
			selected = created;
			FXEditorApp::log(FALSE, "Created %s %s (saved with the next save)", type, s_newBlockName.c_str());
			s_newBlockName.clear();
		}
		else
		{
			FXEditorApp::log(TRUE, "Cannot create %s: %s", s_newBlockName.c_str(), error.str());
		}
	}
	ImGui::EndDisabled();
}

//-------------------------------------------------------------------------------------------------
void drawPostProcessTab()
{
	bool on = TheGlobalData->m_postProcessing != 0;
	if (ImGui::Checkbox("Post-processing", &on))
		TheWritableGlobalData->m_postProcessing = on;
	ImGui::SetItemTooltip("Turns the chain on or off (not saved)");
	ImGui::SameLine();
	const Bool running = TheW3DPostProcess && TheW3DPostProcess->isRunning();
	ImGui::TextColored(running ? COLOR_ACTIVE : COLOR_DIM, running ? "running" : "not running");
	if (TheW3DPostProcess && !TheW3DPostProcess->getError().isEmpty())
		ImGui::TextColored(COLOR_ERROR, "%s", TheW3DPostProcess->getError().str());

	std::string chainPasses;
	if (const IniBlock *chainBlock = db().getBlock(db().findTypedBlock("PostProcess", AsciiString("PostProcess"))))
		IniText::getField(chainBlock->text, -1, "Passes", chainPasses);

	if (ImGui::BeginChild("##ppblocks", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.35f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
	{
		for (Int i = 0; i < db().getBlockCount(); ++i)
		{
			const IniBlock *b = db().getBlock(i);
			if (!isType(b, "PostProcess") && !isType(b, "PostProcessPass") && !isType(b, "PostProcessChain"))
				continue;
			// Earlier definitions of the same block (the defaults the mod overrides) are not edited.
			if (db().findTypedBlock(b->type.c_str(), b->name) != i)
				continue;
			char label[160];
			const Bool inChain = isType(b, "PostProcessPass") && containsWord(chainPasses, b->name.str());
			if (isType(b, "PostProcessPass"))
				snprintf(label, sizeof(label), "%s %s%s", inChain ? "*" : " ", b->name.str(), b->isDirty() ? "  (changed)" : "");
			else
				snprintf(label, sizeof(label), "%s%s%s", b->type.c_str(), isType(b, "PostProcessChain") ? (std::string(" ") + b->name.str()).c_str() : "",
					b->isDirty() ? "  (changed)" : "");
			if (!b->error.empty())
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
			else if (b->isDirty())
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIRTY);
			else if (isType(b, "PostProcessPass") && !inChain)
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIM);
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
			ImGui::PushID(i);
			if (ImGui::Selectable(label, s_selectedPostProcess == i))
				s_selectedPostProcess = i;
			ImGui::PopID();
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::TextColored(COLOR_DIM, "* in the PostProcess chain");
	const IniBlock *selected = db().getBlock(s_selectedPostProcess);
	drawNewBlock("PostProcessPass", isType(selected, "PostProcessPass") ? s_selectedPostProcess : -1, s_selectedPostProcess);
	ImGui::Separator();
	drawBlockEditor(s_selectedPostProcess);
}

//-------------------------------------------------------------------------------------------------
void drawMaterialTab()
{
	if (TheW3DMaterials)
	{
		bool on = TheW3DMaterials->isEnabled() != FALSE;
		if (ImGui::Checkbox("Materials", &on))
			TheW3DMaterials->setEnabled(on);
		ImGui::SetItemTooltip("Turns the HLSL materials on or off (not saved)");
		ImGui::SameLine();
		const Bool running = TheW3DMaterials->isRunning();
		ImGui::TextColored(running ? COLOR_ACTIVE : COLOR_DIM, running ? "running" : "not running");
		if (!TheW3DMaterials->getError().isEmpty())
			ImGui::TextColored(COLOR_ERROR, "%s", TheW3DMaterials->getError().str());
	}

	// How many textures each material got so far, as the renderer counts them.
	std::vector<AsciiString> names;
	std::vector<Int> textures;
	std::vector<AsciiString> uses;
	if (TheW3DMaterials)
		TheW3DMaterials->getMaterials(names, textures, uses);

	if (ImGui::BeginChild("##matblocks", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.35f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
	{
		for (Int i = 0; i < db().getBlockCount(); ++i)
		{
			const IniBlock *b = db().getBlock(i);
			if (!isType(b, "Material") || db().findTypedBlock("Material", b->name) != i)
				continue;
			std::string use = "Meshes";
			IniText::getField(b->text, -1, "Use", use);
			Int count = -1;
			for (size_t k = 0; k < names.size(); ++k)
			{
				if (names[k].compareNoCase(b->name) == 0)
					count = textures[k];
			}
			char label[200];
			if (count >= 0 && (_stricmp(use.c_str(), "Meshes") == 0 || _stricmp(use.c_str(), "Particles") == 0))
				snprintf(label, sizeof(label), "%s  (%s, %d textures)%s", b->name.str(), use.c_str(), count, b->isDirty() ? "  (changed)" : "");
			else
				snprintf(label, sizeof(label), "%s  (%s)%s", b->name.str(), use.c_str(), b->isDirty() ? "  (changed)" : "");
			if (!b->error.empty())
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_ERROR);
			else if (b->isDirty())
				ImGui::PushStyleColor(ImGuiCol_Text, COLOR_DIRTY);
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
			ImGui::PushID(i);
			if (ImGui::Selectable(label, s_selectedMaterial == i))
				s_selectedMaterial = i;
			ImGui::PopID();
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::TextColored(COLOR_DIM, "Textures: base textures drawn with the material so far");
	drawNewBlock("Material", isType(db().getBlock(s_selectedMaterial), "Material") ? s_selectedMaterial : -1, s_selectedMaterial);
	ImGui::Separator();
	drawBlockEditor(s_selectedMaterial);
}

//-------------------------------------------------------------------------------------------------
void drawShadowTab()
{
	const Int index = db().findTypedBlock("ShadowMap", AsciiString("ShadowMap"));
	if (index < 0)
	{
		ImGui::TextWrapped("No ShadowMap block in the PostProcess INI files: the game draws stencil shadow volumes. "
			"Add a ShadowMap block to Data\\INI\\PostProcess.ini and use Reload shaders to edit it here.");
		return;
	}
	ImGui::TextColored(COLOR_DIM, "Needs the 3D shadows option. Changes apply at once.");
	drawBlockEditor(index);
}

} // namespace

//-------------------------------------------------------------------------------------------------
void FXEditorPanels::drawRender()
{
	// A layout saved before this window existed has no place for it: it joins the inspector.
	if (ImGuiWindow *inspector = ImGui::FindWindowByName(INSPECTOR))
	{
		if (inspector->DockId != 0)
			ImGui::SetNextWindowDockID(inspector->DockId, ImGuiCond_FirstUseEver);
	}
	if (!ImGui::Begin(RENDER))
	{
		ImGui::End();
		return;
	}
	IniDatabase &ini = db();
	if (!ini.isLoaded())
	{
		ImGui::TextColored(COLOR_DIM, "Waiting for the preview game...");
		ImGui::End();
		return;
	}

	char label[64];
	snprintf(label, sizeof(label), "Save (%d changed)", ini.getDirtyCount());
	ImGui::BeginDisabled(ini.getDirtyCount() == 0);
	if (ImGui::Button(label))
	{
		AsciiString error;
		std::vector<AsciiString> written;
		if (ini.save(-1, error, &written))
		{
			for (size_t i = 0; i < written.size(); ++i)
				FXEditorApp::log(FALSE, "Saved %s", written[i].str());
		}
		else
		{
			FXEditorApp::log(TRUE, "Save failed: %s", error.str());
		}
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Writes the changed PostProcess and Material blocks into the mod's INI files (keeps <file>.fxeditor.bak)");
	ImGui::SameLine();
	if (ImGui::Button("Reload shaders"))
		FXEditorApp::reloadRenderFiles();
	ImGui::SetItemTooltip("Reads the HLSL shader files (and the INI files) again; changes not saved stay");

	if (ImGui::BeginTabBar("##rendertabs"))
	{
		if (ImGui::BeginTabItem("Post-processing"))
		{
			drawPostProcessTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Materials"))
		{
			drawMaterialTab();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Shadows"))
		{
			drawShadowTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
