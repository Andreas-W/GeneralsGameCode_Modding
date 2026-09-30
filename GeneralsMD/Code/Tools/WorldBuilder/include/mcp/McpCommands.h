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

// McpCommands.h
// Command table and shared helpers for the WorldBuilder MCP command bridge.
// All handlers run on the MFC UI thread.

#pragma once

#include "mcp/McpJson.h"

class CWorldBuilderDoc;
class WorldHeightMapEdit;
class MapObject;
class PolygonTrigger;
class Undoable;

/// Thrown by command handlers to report a user-facing error.
struct McpError
{
	std::string message;
};

typedef McpJson (*McpHandler)(const McpJson &args);

void mcpRegisterCommand(const char *name, McpHandler handler, const char *help);

/// Parses one request line, runs the command and returns the reply line (without newline).
std::string mcpDispatch(const std::string &requestLine);

// Per-area registration, called once from mcpDispatch.
void mcpRegisterMapCommands();
void mcpRegisterTerrainCommands();
void mcpRegisterObjectCommands();
void mcpRegisterWaypointCommands();

//-------------------------------------------------------------------------------------------------
// Helpers
//-------------------------------------------------------------------------------------------------

[[noreturn]] void mcpFail(const char *fmt, ...);

double mcpArgNumber(const McpJson &args, const char *key);
double mcpArgNumber(const McpJson &args, const char *key, double defaultValue);
int mcpArgInt(const McpJson &args, const char *key);
int mcpArgInt(const McpJson &args, const char *key, int defaultValue);
bool mcpArgBool(const McpJson &args, const char *key, bool defaultValue);
std::string mcpArgString(const McpJson &args, const char *key);
std::string mcpArgString(const McpJson &args, const char *key, const char *defaultValue);
const McpJson &mcpArgArray(const McpJson &args, const char *key);

CWorldBuilderDoc *mcpDoc();
WorldHeightMapEdit *mcpHeightMap();

/// Commits an undoable to the active document, which takes ownership.
void mcpCommit(Undoable *undo);

/// Heightmap vertex index for a world coordinate (nearest vertex, not clamped).
int mcpWorldToIndexX(double worldX);
int mcpWorldToIndexY(double worldY);
double mcpIndexToWorldX(int ndx);
double mcpIndexToWorldY(int ndx);

/// Case-insensitive substring test used by list filters.
bool mcpContainsNoCase(const char *haystack, const char *needle);

/// Session-stable integer handles for map objects and polygon triggers.
int mcpObjectHandle(MapObject *obj);
MapObject *mcpFindObject(int handle);		///< Returns null if the object is no longer in the map.
MapObject *mcpRequireObject(int handle);
void mcpResetObjectHandles();

PolygonTrigger *mcpRequirePolygon(int id);

/// Common JSON description of a map object.
McpJson mcpDescribeObject(MapObject *obj);

/// Converts between engine property dictionaries and JSON objects. When writing, the type of a key
/// that already exists in the dict is kept; new keys get their type from the JSON value.
class Dict;
McpJson mcpDictToJson(const Dict &dict);
void mcpJsonToDict(const McpJson &props, Dict &dict);
