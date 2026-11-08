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

// The FX Editor itself: its ImGui interface on top of the game view, the preview scene, the INI
// text that is edited, and what the panels and the MCP commands share.

#pragma once

#include <windows.h>
#include <vector>

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"
#include "PreviewScene.h"

class IniDatabase;

struct FXEditorLogEntry
{
	UnsignedInt serial;
	Bool error;
	AsciiString text;
};

class FXEditorApp
{
public:
	enum { DEFAULT_MCP_PORT = 47802 };

	// Hooks for the window procedure and the engine.
	static void setPreviewMap(const char *map);
	static const AsciiString &getPreviewMap();
	static void setStartError(const char *error);
	static const AsciiString &getStartError(); ///< why the preview game did not start, or empty
	static Bool handleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	static Bool wantsMouse();
	static Bool wantsKeyboard();
	static void onWindowResized(Int width, Int height, Bool minimized);
	static void init();
	static void shutdown();
	static void update();

	// The map the preview runs on.
	/// The map cache key of a map key, display name or part of a map's path; empty if none matches.
	static AsciiString findMapKey(const AsciiString &map);
	/// Start positions of a multiplayer map (two or more, as the game counts them); 0 for other maps,
	/// for which the map cache cannot tell one start position from none.
	static Int countStartPositions(const AsciiString &mapKey);
	/// Factions the local player can have on a multiplayer map.
	static void getFactions(std::vector<AsciiString> &factions);
	/// Loads another map at the start of the next frame. Multiplayer maps run as a sandbox
	/// skirmish with the local player of that faction (starting units removed); other maps as a
	/// single player game with the map's own player. What plays is played again on the new map.
	/// False with a reason when the map or faction is unknown.
	static Bool loadMap(const AsciiString &map, const AsciiString &faction, AsciiString &error);
	static const AsciiString &getPreviewMapKey(); ///< map cache key of the current map, or empty
	static const AsciiString &getPreviewFaction(); ///< faction asked for on a multiplayer map, or empty
	static Bool isMapLoading();

	// What the panels and the MCP commands share.
	static PreviewScene &scene();
	static IniDatabase &ini();
	/// PostProcess and Material blocks, edited in the Direct3D 12 window.
	static IniDatabase &renderIni();
	/// The renderer reads its INI and shader files again; changes not saved are applied on top.
	static void reloadRenderFiles();
	static Bool isDX12();

	static void log(Bool error, const char *format, ...);
	static const FXEditorLogEntry *getLog(Int &count);

	/// Shows a ParticleSystem or FXList in the inspector (sounds are only played). While an effect
	/// plays, the newly selected one plays instead.
	static void select(PreviewKind kind, const AsciiString &name);
	static PreviewKind getSelectedKind();
	static const AsciiString &getSelectedName();
	/// Index of the selected block in the INI database, or -1.
	static Int getSelectedBlock();

	/// Plays something in the preview and selects it.
	static Bool play(PreviewKind kind, const AsciiString &name, AsciiString &error);

	/// A block's text changed: what plays is started again so the change shows.
	static void onBlockChanged(Int block);

	/// Draws a frame and writes it to a file (.png or .jpg); without the editor interface unless asked.
	static Bool captureScreenshot(const char *path, Bool withInterface, AsciiString &error);

	/// Asks before quitting when there are changes that are not saved.
	static void requestQuit();
};
