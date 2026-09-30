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

// McpServer.h
// Localhost TCP command server that lets an external MCP server drive WorldBuilder.
//
// Protocol: one JSON object per line in each direction.
//   request:  {"id":1,"cmd":"map.info","args":{}}
//   reply:    {"id":1,"ok":true,"result":{...}}  or  {"id":1,"ok":false,"error":"..."}
// The socket is serviced on a worker thread; every command runs on the UI thread.

#pragma once

namespace McpServer
{
	enum { DEFAULT_PORT = 47800 };

	/// Returns the port requested by "-mcp" / "-mcpport:N" or the WB_MCP_PORT environment variable, or 0 if not enabled.
	int getRequestedPort();

	/// Must be called on the UI thread after the main window exists.
	bool start(int port);
	void stop();
	bool isRunning();
}
