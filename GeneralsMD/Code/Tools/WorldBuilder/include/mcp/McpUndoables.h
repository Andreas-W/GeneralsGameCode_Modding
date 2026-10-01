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

// McpUndoables.h
// Undo steps shared by several MCP bridge commands.

#pragma once

#include "CUndoable.h"
#include "WorldBuilderDoc.h"

#include <utility>
#include <vector>

/// Adds and removes waypoint links, which WorldBuilder itself does not record for undo.
class McpWaypointLinksUndoable : public Undoable
{
public:
	typedef std::pair<Int, Int> Link;
	McpWaypointLinksUndoable(CWorldBuilderDoc *doc) : m_doc(doc) {}

	std::vector<Link> added, removed;

	virtual void Do() override
	{
		for (size_t i = 0; i < removed.size(); i++) m_doc->removeWaypointLink(removed[i].first, removed[i].second);
		for (size_t i = 0; i < added.size(); i++) m_doc->addWaypointLink(added[i].first, added[i].second);
	}
	virtual void Undo() override
	{
		for (size_t i = 0; i < added.size(); i++) m_doc->removeWaypointLink(added[i].first, added[i].second);
		for (size_t i = 0; i < removed.size(); i++) m_doc->addWaypointLink(removed[i].first, removed[i].second);
	}

private:
	CWorldBuilderDoc *m_doc;
};
