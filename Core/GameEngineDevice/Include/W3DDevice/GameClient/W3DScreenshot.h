/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#pragma once

#include "GameClient/Display.h"

void W3D_TakeCompressedScreenshot(ScreenshotFormat format, Int jpegQuality);

#ifdef RTS_GAME_MCP
// Writes the current back buffer to a file right away, for the game's MCP bridge.
bool W3D_WriteScreenshotFile(const char* pathname, ScreenshotFormat format, Int jpegQuality);
#endif

// Called once per frame on the main thread to show messages for screenshots
// that the screenshot thread has finished writing.
void W3D_UpdateScreenshotMessages();

