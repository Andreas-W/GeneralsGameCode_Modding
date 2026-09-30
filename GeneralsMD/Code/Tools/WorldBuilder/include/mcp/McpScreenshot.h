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

// McpScreenshot.h

#pragma once

/// Sets how the next capture is written. maxWidth <= 0 keeps the full size; jpegQuality <= 0 writes a PNG.
void mcpSetScreenshotOptions(int maxWidth, int jpegQuality);

/// Writes the current D3D back buffer to an image file. Must be called between WW3D::Begin_Render and End_Render.
bool mcpWriteBackBufferImage(const char *path, int *outWidth, int *outHeight);
