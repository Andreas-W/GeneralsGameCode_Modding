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

	// What the panels and the MCP commands share.
	static PreviewScene &scene();
	static IniDatabase &ini();
	static Bool isDX12();

	static void log(Bool error, const char *format, ...);
	static const FXEditorLogEntry *getLog(Int &count);

	/// Shows a ParticleSystem or FXList in the inspector (sounds are only played).
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
