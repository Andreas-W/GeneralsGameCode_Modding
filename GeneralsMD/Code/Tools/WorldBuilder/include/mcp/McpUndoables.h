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
#include "wbview3d.h"

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

/// Moves, rotates, renames or re-templates objects. Set the new values on the MoveInfo returned by
/// add(); they are applied in Do().
class McpModifyObjectsUndoable : public Undoable
{
public:
	McpModifyObjectsUndoable(CWorldBuilderDoc *doc) : m_doc(doc), m_list(nullptr), m_tail(nullptr), m_templateChanged(false) {}
	virtual ~McpModifyObjectsUndoable() override { delete m_list; }

	MoveInfo *add(MapObject *obj)
	{
		MoveInfo *info = new MoveInfo(obj);
		if (m_tail) m_tail->m_next = info; else m_list = info;
		m_tail = info;
		return info;
	}
	void setTemplateChanged() { m_templateChanged = true; }

	virtual void Do() override { apply(true); }
	virtual void Undo() override { apply(false); }

private:
	void apply(bool forward)
	{
		for (MoveInfo *info = m_list; info; info = info->m_next) {
			if (forward) info->DoMove(m_doc); else info->UndoMove(m_doc);
		}
		if (m_templateChanged) {
			WbView3d *view = m_doc->GetActive3DView();
			if (view) {
				view->resetRenderObjects();
				view->invalObjectInView(nullptr);
			}
		}
	}

	CWorldBuilderDoc *m_doc;
	MoveInfo *m_list;
	MoveInfo *m_tail;
	bool m_templateChanged;
};
