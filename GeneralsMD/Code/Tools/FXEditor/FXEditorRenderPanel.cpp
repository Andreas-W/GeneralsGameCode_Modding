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
#include "PassOrder.h"

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
	{ "PostProcess", "Disabled", RF_TEXT, "", 0, "passes of Passes that are switched off; they keep their place", nullptr },
	{ "PostProcess", "HDRScene", RF_BOOL, "No", 0, "draw the scene in 16 bit float while the chain runs", nullptr },
	{ "PostProcessChain", "Passes", RF_TEXT, "", 0, "passes of this chain (POSTPROCESS_USE_CHAIN script action)", nullptr },
	{ "PostProcessChain", "Disabled", RF_TEXT, "", 0, "passes of Passes that are switched off; they keep their place", nullptr },
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
Int s_selectedChain = -1;		///< the PostProcess or PostProcessChain block whose passes are listed
bool s_reorder = false;			///< the order of the passes can be changed
bool s_autoInputs = true;		///< changes of the list also give the passes the inputs of their place
AsciiString s_dropReason;		///< why the row under a dragged pass cannot take it, shown by the drag
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
/** Output size and format of a pass, short: "1/2 HDR". */
//-------------------------------------------------------------------------------------------------
std::string passOutput(Int index)
{
	const IniBlock *b = db().getBlock(index);
	if (b == nullptr)
		return "";
	std::string scale = "1";
	std::string hdr;
	IniText::getField(b->text, -1, "Scale", scale);
	IniText::getField(b->text, -1, "HDR", hdr);
	const Int s = atoi(scale.c_str());
	char out[32];
	if (s == 0)
		snprintf(out, sizeof(out), "1 px");
	else if (s == 1)
		snprintf(out, sizeof(out), "full");
	else
		snprintf(out, sizeof(out), "1/%d", s);
	std::string text = out;
	if (_stricmp(hdr.c_str(), "Yes") == 0 || _stricmp(hdr.c_str(), "True") == 0 || hdr == "1")
		text += " HDR";
	return text;
}

//-------------------------------------------------------------------------------------------------
void writeChain(PassChain &chain, Bool autoInputs)
{
	AsciiString error;
	if (!PassOrder::apply(db(), chain, autoInputs, error))
		FXEditorApp::log(TRUE, "%s", error.str());
}

void writeChain(PassChain &chain)
{
	writeChain(chain, s_autoInputs);
}

std::string passInputsTooltip(const PassRow &row)
{
	std::string text = "Inputs = ";
	for (size_t k = 0; k < row.words.size(); ++k)
		text += (k > 0 ? " " : "") + std::string(row.words[k].str());
	if (row.needsConnect())
	{
		text += "\nAuto inputs: ";
		for (size_t k = 0; k < row.wanted.size(); ++k)
			text += (k > 0 ? " " : "") + std::string(row.wanted[k].str());
	}
	if (row.role == PASS_ROLE_NONE)
		text += "\nAuto inputs leave it alone";
	else
		text += std::string("\nPart of ") + PassOrder::getRoleName(row.role);
	return text;
}

//-------------------------------------------------------------------------------------------------
/** The chain to list: the one picked, else the PostProcess block. Picking a chain also runs it. */
//-------------------------------------------------------------------------------------------------
void drawChainPicker(const std::vector<Int> &chains)
{
	Bool known = FALSE;
	for (size_t i = 0; i < chains.size(); ++i)
		known = known || chains[i] == s_selectedChain;
	if (!known)
		s_selectedChain = chains.empty() ? -1 : chains[0];

	const IniBlock *current = db().getBlock(s_selectedChain);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Chain");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo("##chain", current == nullptr ? "" : isType(current, "PostProcess") ? "Default" : current->name.str()))
	{
		for (size_t i = 0; i < chains.size(); ++i)
		{
			const IniBlock *b = db().getBlock(chains[i]);
			const Bool main = isType(b, "PostProcess");
			if (ImGui::Selectable(main ? "Default" : b->name.str(), chains[i] == s_selectedChain))
			{
				s_selectedChain = chains[i];
				ThePostProcessControl.useChain(main ? AsciiString("Default") : b->name);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Default = the PostProcess block; the others are PostProcessChain blocks (POSTPROCESS_USE_CHAIN). The preview runs the chain picked here.");
	ImGui::SameLine();
	ImGui::BeginDisabled(current == nullptr);
	if (ImGui::Button("Edit chain block"))
		s_selectedPostProcess = s_selectedChain;
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("HDRScene and the text of the chain block, below");
}

//-------------------------------------------------------------------------------------------------
/** The passes of the chain in the order they run, with the ones that are off at their place. Changes are
	collected while drawing and written as one undo step of the chain block afterwards. */
//-------------------------------------------------------------------------------------------------
void drawPassTable(PassChain &chain)
{
	Int toggleRow = -1;
	Int moveFrom = -1;
	Int moveTo = -1;
	Int lastEnabled = -1;
	for (size_t i = 0; i < chain.rows.size(); ++i)
		if (chain.rows[i].enabled)
			lastEnabled = (Int)i;
	Bool debugOn = FALSE;
	for (size_t i = 0; i < chain.debugRows.size(); ++i)
		debugOn = debugOn || chain.debugRows[i].enabled;
	const std::vector<AsciiString> *running = PassOrder::getRunningPasses(db(), chain);

	const Int columns = s_reorder ? 7 : 6;
	if (ImGui::BeginTable("##passes", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		if (s_reorder)
			ImGui::TableSetupColumn("Move");
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("On");
		ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Shader");
		ImGui::TableSetupColumn("Output");
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();

		Int runIndex = 0;
		for (Int i = 0; i < (Int)chain.rows.size(); ++i)
		{
			const PassRow &row = chain.rows[i];
			const IniBlock *block = db().getBlock(row.block);
			ImGui::PushID(i);
			ImGui::TableNextRow();

			if (s_reorder)
			{
				ImGui::TableNextColumn();
				AsciiString reason;
				const Bool up = i > 0 && PassOrder::canMove(chain, i, i - 1, reason, s_autoInputs);
				ImGui::BeginDisabled(!up);
				if (ImGui::ArrowButton("##up", ImGuiDir_Up))
				{
					moveFrom = i;
					moveTo = i - 1;
				}
				ImGui::EndDisabled();
				if (!up && i > 0)
					ImGui::SetItemTooltip("%s", reason.str());
				ImGui::SameLine();
				reason.clear();
				const Bool down = i + 1 < (Int)chain.rows.size() && PassOrder::canMove(chain, i, i + 1, reason, s_autoInputs);
				ImGui::BeginDisabled(!down);
				if (ImGui::ArrowButton("##down", ImGuiDir_Down))
				{
					moveFrom = i;
					moveTo = i + 1;
				}
				ImGui::EndDisabled();
				if (!down && i + 1 < (Int)chain.rows.size())
					ImGui::SetItemTooltip("%s", reason.str());
			}

			ImGui::TableNextColumn();
			if (row.enabled)
				ImGui::Text("%d", ++runIndex);
			else
				ImGui::TextColored(COLOR_DIM, "-");

			ImGui::TableNextColumn();
			bool on = row.enabled != FALSE;
			ImGui::BeginDisabled(row.block < 0);
			if (ImGui::Checkbox("##on", &on))
				toggleRow = i;
			ImGui::EndDisabled();
			if (!row.listed)
				ImGui::SetItemTooltip("Not in Passes yet: switching it on puts it there, at this place");

			ImGui::TableNextColumn();
			ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			if (row.block < 0 || (block != nullptr && !block->error.empty()))
				color = COLOR_ERROR;
			else if (block != nullptr && block->isDirty())
				color = COLOR_DIRTY;
			else if (!row.enabled)
				color = COLOR_DIM;
			char label[160];
			snprintf(label, sizeof(label), "%s%s", row.name.str(), block != nullptr && block->isDirty() ? "  (changed)" : "");
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (ImGui::Selectable(label, row.block >= 0 && s_selectedPostProcess == row.block) && row.block >= 0)
				s_selectedPostProcess = row.block;
			ImGui::PopStyleColor();
			if (row.block < 0)
				ImGui::SetItemTooltip("No PostProcessPass named %s", row.name.str());
			else if (!ImGui::GetDragDropPayload())
				ImGui::SetItemTooltip("%s", passInputsTooltip(row).c_str());

			if (s_reorder)
			{
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("FX_PP_PASS", &i, sizeof(i));
					ImGui::Text("Move %s", row.name.str());
					if (s_dropReason.isNotEmpty())
						ImGui::TextColored(COLOR_ERROR, "%s", s_dropReason.str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FX_PP_PASS",
						ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
					if (payload != nullptr)
					{
						const Int from = *(const Int *)payload->Data;
						AsciiString reason;
						const Bool valid = PassOrder::canMove(chain, from, i, reason, s_autoInputs);
						s_dropReason = valid ? AsciiString::TheEmptyString : reason;
						const ImVec2 min = ImGui::GetItemRectMin();
						const ImVec2 max = ImGui::GetItemRectMax();
						// The line where the pass lands: above a row it moves up to, below a row it moves down to.
						const float y = from > i ? min.y : max.y;
						ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, y), ImVec2(max.x, y),
							ImGui::GetColorU32(valid ? COLOR_ACTIVE : COLOR_ERROR), 2.0f);
						if (payload->IsDelivery() && valid && from != i)
						{
							moveFrom = from;
							moveTo = i;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			ImGui::TableNextColumn();
			ImGui::TextColored(COLOR_DIM, "%s", row.shader.str());
			ImGui::TableNextColumn();
			ImGui::TextColored(COLOR_DIM, "%s", passOutput(row.block).c_str());

			ImGui::TableNextColumn();
			if (row.moved)
			{
				ImGui::TextColored(COLOR_DIRTY, "moved");
				const Int before = row.defaultIndex - 1;
				if (before >= 0)
					ImGui::SetItemTooltip("Default place: after %s", chain.defaultOrder[before].str());
				else
					ImGui::SetItemTooltip("Default place: first");
				ImGui::SameLine();
			}
			if (row.needsConnect())
			{
				ImGui::TextColored(COLOR_DIRTY, "inputs");
				ImGui::SetItemTooltip("Reads other inputs than Auto inputs would give it here (Connect inputs fixes it)\n%s", passInputsTooltip(row).c_str());
				ImGui::SameLine();
			}
			if (row.enabled && running != nullptr && !PassOrder::isRunning(running, row.name))
			{
				ImGui::TextColored(COLOR_DIM, "unused");
				ImGui::SetItemTooltip("On, but it does not run: no pass on the way to the screen reads it, or the pass it feeds is off or gets None\ninstead (for example the blurs of an occlusion pass that is off). Auto inputs connect what can be connected.");
				ImGui::SameLine();
			}
			if (i == lastEnabled && !debugOn)
			{
				ImGui::TextColored(COLOR_ACTIVE, "-> screen");
				ImGui::SetItemTooltip("The last pass that is on draws into the screen");
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (!ImGui::GetDragDropPayload())
		s_dropReason.clear();

	if (toggleRow >= 0)
	{
		PassRow &row = chain.rows[toggleRow];
		row.enabled = !row.enabled;
		row.listed = TRUE;
		writeChain(chain);
	}
	else if (moveFrom >= 0)
	{
		PassOrder::move(chain, moveFrom, moveTo);
		writeChain(chain);
	}
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

	std::vector<Int> chains;
	PassOrder::getChainBlocks(db(), chains);
	if (chains.empty())
	{
		ImGui::TextWrapped("No PostProcess block in the PostProcess INI files. Add one to Data\\INI\\PostProcess.ini and use Reload shaders.");
	}
	else
	{
		drawChainPicker(chains);

		PassChain chain;
		PassOrder::build(db(), s_selectedChain, chain);

		ImGui::Checkbox("Reorder", &s_reorder);
		ImGui::SetItemTooltip(s_autoInputs ? "Unlocks the order: arrows and drag and drop. The image chain follows the new order; a branch\n(occlusion, bloom, auto exposure) stays together and must run before the ColorGrade that reads it."
			: "Unlocks the order: arrows and drag and drop. Every pass must run after the passes it reads.");
		ImGui::SameLine();
		ImGui::Checkbox("Auto inputs", &s_autoInputs);
		ImGui::SetItemTooltip("Switching, moving and resetting also set the Inputs of the passes for their place, as if every pass were on\n"
			"(the game skips the ones that are off): the image chain reads the image pass before it, a branch starts from\n"
			"Scene or Depth, and ColorGrade reads the bloom, occlusion and auto exposure results before it.\n"
			"Shaders of the mod's own keep their inputs, except the image they work on. Inputs typed by hand are replaced.");
		ImGui::SameLine();
		if (chain.isCustomOrder())
			ImGui::TextColored(COLOR_DIRTY, "Custom order (%d moved)", chain.movedCount);
		else
			ImGui::TextColored(COLOR_DIM, "Default order");
		ImGui::SameLine();
		ImGui::BeginDisabled(!chain.isCustomOrder());
		if (ImGui::Button("Reset to default order"))
		{
			PassOrder::resetOrder(chain);
			// The default order goes by the stages; image passes that read each other in another order are rewired.
			AsciiString reason;
			writeChain(chain, s_autoInputs || !PassOrder::checkOrder(chain.rows, reason));
			PassOrder::build(db(), s_selectedChain, chain);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Puts the passes into the default order; what is on or off stays");
		AsciiString orderError;
		if (!PassOrder::checkOrder(chain.rows, orderError))
			ImGui::TextColored(COLOR_ERROR, "%s", orderError.str());
		const Int rewire = chain.countNeedsConnect();
		if (rewire > 0)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(COLOR_DIRTY, rewire == 1 ? "1 pass reads other inputs than Auto inputs would give it" : "%d passes read other inputs than Auto inputs would give them", rewire);
			ImGui::SameLine();
			if (ImGui::Button("Connect inputs"))
			{
				AsciiString error;
				if (!PassOrder::writeInputs(db(), chain, error))
					FXEditorApp::log(TRUE, "%s", error.str());
				PassOrder::build(db(), s_selectedChain, chain);
			}
			ImGui::SetItemTooltip("Sets the Inputs of the passes marked \"inputs\" (one undo step per pass)");
		}

		if (ImGui::BeginChild("##ppblocks", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.4f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
			drawPassTable(chain);
		ImGui::EndChild();

		if (!chain.debugRows.empty() && ImGui::CollapsingHeader("Debug views"))
		{
			PassOrder::build(db(), s_selectedChain, chain);
			for (size_t i = 0; i < chain.debugRows.size(); ++i)
			{
				PassRow &row = chain.debugRows[i];
				ImGui::PushID((int)i);
				bool view = row.enabled != FALSE;
				if (ImGui::Checkbox(row.name.str(), &view))
				{
					row.enabled = view;
					writeChain(chain);
					ImGui::PopID();
					break;
				}
				ImGui::SetItemTooltip("%s: runs after the chain and replaces the picture", row.shader.str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Edit"))
					s_selectedPostProcess = row.block;
				ImGui::PopID();
			}
		}
	}

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
