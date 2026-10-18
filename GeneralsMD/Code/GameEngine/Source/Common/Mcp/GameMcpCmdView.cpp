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
// Camera and screenshot commands of the MCP bridge. They need the window, so they fail headless.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/GameMcp.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
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

} // namespace

void gameMcpRegisterViewCommands()
{
	gameMcpRegisterCommand("view.camera", cmdCamera,
		"{id? | x?, y?, angle?, pitch?, zoom?, reset?} Moves the camera (to an object or a position) and returns where it is.");
	gameMcpRegisterCommand("view.screenshot", cmdScreenshot, "{path, quality=85} Writes what the window shows to a .jpg or .png file.");
}

#endif // RTS_GAME_MCP
