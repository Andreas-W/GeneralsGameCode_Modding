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

// The windows of the FX Editor interface.

#pragma once

namespace FXEditorPanels
{
	extern const char *const LIBRARY;
	extern const char *const INSPECTOR;
	extern const char *const PREVIEW;
	extern const char *const AUDIO;
	extern const char *const LOG;
	extern const char *const RENDER;

	void drawMainMenu();
	void drawLibrary();
	void drawInspector();
	void drawPreview();
	void drawAudio();
	void drawLog();
	/// Post-processing, materials and shadow maps of the Direct3D 12 renderer.
	void drawRender();
	/// Status over the game view while the preview game loads or when it could not start.
	void drawOverlayInfo();

	/// Releases the textures the inspector shows.
	void shutdown();
}
