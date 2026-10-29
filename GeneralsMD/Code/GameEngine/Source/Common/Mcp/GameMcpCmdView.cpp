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

// GameMcpCmdView.cpp
// Camera, screenshot and display mode commands of the MCP bridge. They need the window, so they fail headless.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/GameMcp.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/PostProcess.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

namespace
{

void requireWindow()
{
	if (TheGlobalData->m_headless || TheTacticalView == nullptr) {
		gameMcpFail("the game runs headless: there is no view (start it without -headless)");
	}
}

McpJson describeCamera()
{
	return McpJson::makeObject().set("position", gameMcpCoord(TheTacticalView->getPosition()))
		.set("angle", TheTacticalView->getAngle() * 180.0 / PI).set("pitch", TheTacticalView->getPitch() * 180.0 / PI)
		.set("zoom", TheTacticalView->getZoom());
}

McpJson cmdCamera(const McpJson &args)
{
	requireWindow();
	if (args.has("id")) {
		const Object *object = gameMcpRequireObject(gameMcpArgInt(args, "id"));
		TheTacticalView->lookAt(object->getPosition());
	} else if (args.has("x") || args.has("y")) {
		Coord3D pos = TheTacticalView->getPosition();
		pos.x = (Real)gameMcpArgNumber(args, "x", pos.x);
		pos.y = (Real)gameMcpArgNumber(args, "y", pos.y);
		TheTacticalView->lookAt(&pos);
	}
	if (args.has("angle")) {
		TheTacticalView->setAngle((Real)(gameMcpArgNumber(args, "angle") * PI / 180.0));
	}
	if (args.has("pitch")) {
		TheTacticalView->setPitch((Real)(gameMcpArgNumber(args, "pitch") * PI / 180.0));
	}
	if (args.has("zoom")) {
		TheTacticalView->setZoom((Real)gameMcpArgNumber(args, "zoom"));
	}
	if (gameMcpArgBool(args, "reset", false)) {
		TheTacticalView->setAngleToDefault();
		TheTacticalView->setPitchToDefault();
		TheTacticalView->setZoomToDefault();
	}
	return describeCamera();
}

McpJson cmdScreenshot(const McpJson &args)
{
	requireWindow();
	if (TheGameMcpScreenshot == nullptr) {
		gameMcpFail("the display cannot take screenshots");
	}
	const std::string path = gameMcpArgString(args, "path");
	const bool png = path.size() > 4 && _stricmp(path.c_str() + path.size() - 4, ".png") == 0;
	// What is on the back buffer is the last frame drawn.
	if (!TheGameMcpScreenshot(path.c_str(), png ? SCREENSHOT_PNG : SCREENSHOT_JPEG, gameMcpArgInt(args, "quality", 85))) {
		gameMcpFail("could not write the screenshot to '%s'", path.c_str());
	}
	return McpJson::makeObject().set("path", path).set("frame", TheGameLogic->getFrame()).set("camera", describeCamera());
}

McpJson describeDisplay()
{
	return McpJson::makeObject().set("width", (Int)TheDisplay->getWidth()).set("height", (Int)TheDisplay->getHeight())
		.set("windowed", TheDisplay->getWindowed() != 0);
}

// Changes the resolution like the options menu, without saving it to Options.ini. The device is
// reset, which is what this is for: testing what a reset does to the renderer.
McpJson cmdDisplayMode(const McpJson &args)
{
	requireWindow();
	const Int width = gameMcpArgInt(args, "width", TheDisplay->getWidth());
	const Int height = gameMcpArgInt(args, "height", TheDisplay->getHeight());
	const Bool windowed = gameMcpArgBool(args, "windowed", TheDisplay->getWindowed() != 0);
	if (width == (Int)TheDisplay->getWidth() && height == (Int)TheDisplay->getHeight() && windowed == TheDisplay->getWindowed()) {
		return describeDisplay();
	}
	if (!TheDisplay->setDisplayMode(width, height, TheDisplay->getBitDepth(), windowed)) {
		gameMcpFail("the display mode %dx%d is not available", width, height);
	}
	TheWritableGlobalData->m_xResolution = width;
	TheWritableGlobalData->m_yResolution = height;
	TheHeaderTemplateManager->onResolutionChanged();
	TheMouse->onResolutionChanged();
	TheShell->recreateWindowLayouts();
	TheInGameUI->recreateControlBar();
	TheInGameUI->refreshCustomUiResources();
	return describeDisplay();
}

// Turns the post-processing chain on or off (not saved), reloads its INI and shaders, or changes a
// parameter of a pass while the game runs.
McpJson cmdPostProcess(const McpJson &args)
{
	requireWindow();
	if (TheGameMcpPostProcess.isAvailable == nullptr) {
		gameMcpFail("the display has no post-processing");
	}
	if (args.has("enabled")) {
		TheWritableGlobalData->m_postProcessing = gameMcpArgBool(args, "enabled", true);
	}
	if (gameMcpArgBool(args, "reload", false)) {
		TheGameMcpPostProcess.reload();
	}
	if (args.has("param")) {
		const std::string pass = gameMcpArgString(args, "pass");
		const std::string param = gameMcpArgString(args, "param");
		const McpJson &list = args.get("values");
		Real values[4] = { 0, 0, 0, 0 };
		Int count = 0;
		if (list.isArray()) {
			for (size_t i = 0; i < list.size() && count < 4; ++i)
				values[count++] = (Real)list.at(i).asNumber();
		} else {
			values[count++] = (Real)gameMcpArgNumber(args, "values");
		}
		if (!TheGameMcpPostProcess.setParameter(pass.c_str(), param.c_str(), values, count)) {
			gameMcpFail("no running pass '%s' with a parameter '%s'", pass.c_str(), param.c_str());
		}
	}

	std::vector<AsciiString> passes;
	AsciiString error;
	TheGameMcpPostProcess.getPasses(passes, error);
	McpJson names = McpJson::makeArray();
	for (size_t i = 0; i < passes.size(); ++i)
		names.push(passes[i].str());
	return McpJson::makeObject().set("available", TheGameMcpPostProcess.isAvailable())
		.set("enabled", TheGlobalData->m_postProcessing != 0).set("running", TheGameMcpPostProcess.isRunning())
		.set("passes", names).set("error", error.str());
}

// Shadow maps: changes the ShadowMap settings while the game runs (not saved) and returns the state.
McpJson cmdShadows(const McpJson &args)
{
	requireWindow();
	if (TheGameMcpShadowMap.isAvailable == nullptr) {
		gameMcpFail("the display has no shadow maps");
	}
	static const char *const settingNames[] = { "enabled", "size", "softness", "bias", "depth_bias", "slope_bias", "darkness",
		"min_sun_elevation" };
	for (size_t i = 0; i < ARRAY_SIZE(settingNames); ++i) {
		if (args.has(settingNames[i]) && TheShadowMapSettings == nullptr) {
			TheShadowMapSettings = new ShadowMapSettings;
		}
	}
	if (TheShadowMapSettings != nullptr) {
		ShadowMapSettings &settings = *TheShadowMapSettings;
		settings.m_enabled = gameMcpArgBool(args, "enabled", settings.m_enabled != FALSE);
		settings.m_size = gameMcpArgInt(args, "size", settings.m_size);
		settings.m_softness = (Real)gameMcpArgNumber(args, "softness", settings.m_softness);
		settings.m_bias = (Real)gameMcpArgNumber(args, "bias", settings.m_bias);
		settings.m_depthBias = gameMcpArgInt(args, "depth_bias", settings.m_depthBias);
		settings.m_slopeBias = (Real)gameMcpArgNumber(args, "slope_bias", settings.m_slopeBias);
		settings.m_darkness = (Real)gameMcpArgNumber(args, "darkness", settings.m_darkness);
		settings.m_minSunElevation = (Real)gameMcpArgNumber(args, "min_sun_elevation", settings.m_minSunElevation);
	}
	if (args.has("debug")) {
		TheGameMcpShadowMap.setDebug(gameMcpArgBool(args, "debug", false));
	}

	bool active = false;
	bool rendered = false;
	Int casters = 0;
	Real coverage = 0.0f;
	Real sunElevation = 0.0f;
	bool debug = false;
	TheGameMcpShadowMap.getState(active, rendered, casters, coverage, sunElevation, debug);
	McpJson result = McpJson::makeObject().set("available", TheGameMcpShadowMap.isAvailable())
		.set("configured", TheShadowMapSettings != nullptr).set("shadow_volumes_option", TheGlobalData->m_useShadowVolumes != 0)
		.set("active", active).set("rendered", rendered).set("casters", (Int)casters).set("coverage", (Real)coverage)
		.set("sun_elevation", (Real)sunElevation).set("debug", debug);
	if (TheShadowMapSettings != nullptr) {
		const ShadowMapSettings &settings = *TheShadowMapSettings;
		result.set("enabled", settings.m_enabled != 0).set("size", (Int)settings.m_size).set("softness", (Real)settings.m_softness)
			.set("bias", (Real)settings.m_bias).set("depth_bias", (Int)settings.m_depthBias).set("slope_bias", (Real)settings.m_slopeBias)
			.set("darkness", (Real)settings.m_darkness).set("min_sun_elevation", (Real)settings.m_minSunElevation);
	}
	return result;
}

// Materials: turns them on or off (not saved), reloads the Material INI and shaders, sets a parameter of a
// material, or lists the base textures drawn since the last call (to find names for Textures = ...).
McpJson cmdMaterials(const McpJson &args)
{
	requireWindow();
	if (TheGameMcpMaterials.isAvailable == nullptr) {
		gameMcpFail("the display has no materials");
	}
	if (args.has("enabled")) {
		TheGameMcpMaterials.setEnabled(gameMcpArgBool(args, "enabled", true));
	}
	if (gameMcpArgBool(args, "reload", false)) {
		TheGameMcpMaterials.reload();
	}
	if (args.has("param")) {
		const std::string material = gameMcpArgString(args, "material");
		const std::string param = gameMcpArgString(args, "param");
		const McpJson &list = args.get("values");
		Real values[4] = { 0, 0, 0, 0 };
		Int count = 0;
		if (list.isArray()) {
			for (size_t i = 0; i < list.size() && count < 4; ++i)
				values[count++] = (Real)list.at(i).asNumber();
		} else {
			values[count++] = (Real)gameMcpArgNumber(args, "values");
		}
		if (!TheGameMcpMaterials.setParameter(material.c_str(), param.c_str(), values, count)) {
			gameMcpFail("no running material '%s' with a parameter '%s'", material.c_str(), param.c_str());
		}
	}

	std::vector<AsciiString> names;
	std::vector<Int> meshTextures;
	AsciiString error;
	TheGameMcpMaterials.getState(names, meshTextures, error);
	McpJson materials = McpJson::makeArray();
	for (size_t i = 0; i < names.size(); ++i)
		materials.push(McpJson::makeObject().set("name", names[i].str()).set("textures_used", (Int)meshTextures[i]));
	McpJson result = McpJson::makeObject().set("available", TheGameMcpMaterials.isAvailable())
		.set("running", TheGameMcpMaterials.isRunning()).set("materials", materials).set("error", error.str());
	if (gameMcpArgBool(args, "textures", false)) {
		std::vector<AsciiString> textures;
		TheGameMcpMaterials.collectTextures(textures);
		McpJson list = McpJson::makeArray();
		for (size_t i = 0; i < textures.size(); ++i)
			list.push(textures[i].str());
		result.set("textures", list);
	}
	return result;
}

} // namespace

void gameMcpRegisterViewCommands()
{
	gameMcpRegisterCommand("view.camera", cmdCamera,
		"{id? | x?, y?, angle?, pitch?, zoom?, reset?} Moves the camera (to an object or a position) and returns where it is.");
	gameMcpRegisterCommand("view.screenshot", cmdScreenshot, "{path, quality=85} Writes what the window shows to a .jpg or .png file.");
	gameMcpRegisterCommand("view.display_mode", cmdDisplayMode,
		"{width?, height?, windowed?} Changes the resolution like the options menu (not saved) and returns the display mode.");
	gameMcpRegisterCommand("render.postprocess", cmdPostProcess,
		"{enabled?, reload?, pass?, param?, values?} Post-processing of the Direct3D 12 renderer: turns it on or off (not saved), "
		"reloads the PostProcess INI and shaders, or sets a parameter of a running pass; returns its state.");
	gameMcpRegisterCommand("render.materials", cmdMaterials,
		"{enabled?, reload?, material?, param?, values?, textures?} HLSL materials of the Direct3D 12 renderer: turns them on or off "
		"(not saved), reloads the Material INI and shaders, sets a parameter of a material, or (textures=true) returns the base "
		"textures drawn since the last such call; returns the materials and how many textures each got.");
	gameMcpRegisterCommand("render.shadows", cmdShadows,
		"{enabled?, size?, softness?, bias?, depth_bias?, slope_bias?, darkness?, min_sun_elevation?, debug?} Shadow maps of the Direct3D 12 renderer: "
		"changes the ShadowMap settings (not saved; debug shows the amount of shadow as grey) and returns the state.");
}

#endif // RTS_GAME_MCP
