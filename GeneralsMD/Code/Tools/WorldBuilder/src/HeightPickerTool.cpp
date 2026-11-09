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

// HeightPickerTool.cpp
// Height selection tool for worldbuilder.

#include "StdAfx.h"
#include "resource.h"

#include "HeightPickerTool.h"
#include "BrushTool.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "WorldBuilderView.h"
#include "MainFrm.h"
#include "DrawObject.h"

//
// HeightPickerTool class.
//
/// Constructor
HeightPickerTool::HeightPickerTool() :
	Tool(ID_HEIGHT_PICKER_TOOL, IDC_EYEDROPPER)
{
}

/// Destructor
HeightPickerTool::~HeightPickerTool()
{
}

/// Shows the height brush options panel.
void HeightPickerTool::activate()
{
	CMainFrame::GetMainFrame()->showOptionsDialog(IDD_BRUSH_OPTIONS);
	DrawObject::setDoBrushFeedback(false);
}

/// Picks the height on mouse down.
void HeightPickerTool::mouseDown(TTrackingMode m, CPoint viewPt, WbView* pView, CWorldBuilderDoc *pDoc)
{
	if (m != TRACK_L) return;
	pickHeight(viewPt, pView, pDoc);
}

/// Keeps picking while the mouse button is held, so the height can be found by dragging.
void HeightPickerTool::mouseMoved(TTrackingMode m, CPoint viewPt, WbView* pView, CWorldBuilderDoc *pDoc)
{
	if (m != TRACK_L) return;
	pickHeight(viewPt, pView, pDoc);
}

/// Sets the height brush height to the height of the vertex nearest to the cursor.
void HeightPickerTool::pickHeight(CPoint viewPt, WbView* pView, CWorldBuilderDoc *pDoc)
{
	Coord3D cpt;
	pView->viewToDocCoords(viewPt, &cpt);

	// An even width centers on the nearest vertex instead of the cell.
	CPoint ndx(-1, -1);
	getCenterIndex(&cpt, 2, &ndx, pDoc);

	WorldHeightMapEdit *pMap = pDoc->GetHeightMap();
	if (ndx.x < 0 || ndx.y < 0 || ndx.x >= pMap->getXExtent() || ndx.y >= pMap->getYExtent())
		return;

	BrushTool::setHeight(pMap->getHeight(ndx.x, ndx.y));
}
