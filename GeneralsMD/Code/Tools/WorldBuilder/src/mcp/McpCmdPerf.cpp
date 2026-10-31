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

// McpCmdPerf.cpp
// MCP bridge command that times the interactive height tools. It drives the real tool code along a
// straight path, so the same call can be compared before and after an engine change.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "BrushTool.h"
#include "FeatherTool.h"
#include "MoundTool.h"
#include "WHeightMapEdit.h"
#include "WorldBuilder.h"
#include "WorldBuilderDoc.h"
#include "wbview3d.h"

#include <string>

namespace
{

double nowMs()
{
	static LARGE_INTEGER frequency = { 0 };
	if (frequency.QuadPart == 0) {
		::QueryPerformanceFrequency(&frequency);
	}
	LARGE_INTEGER counter;
	::QueryPerformanceCounter(&counter);
	return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

/// Collects the durations of one kind of work.
struct TimeStats
{
	TimeStats() : count(0), total(0), maxValue(0) {}

	void add(double ms)
	{
		count++;
		total += ms;
		if (ms > maxValue) maxValue = ms;
	}
	McpJson toJson() const
	{
		McpJson json = McpJson::makeObject();
		json.set("avg_ms", count > 0 ? total / count : 0.0);
		json.set("max_ms", maxValue);
		json.set("total_ms", total);
		return json;
	}

	Int count;
	double total;
	double maxValue;
};

/// The tool constructors and the benchmark change the static settings shown in the options panels.
struct SavedToolSettings
{
	SavedToolSettings() :
		brushWidth(BrushTool::getWidth()), brushFeather(BrushTool::getFeather()), brushHeight(BrushTool::getHeight()),
		moundWidth(MoundTool::getWidth()), moundFeather(MoundTool::getFeather()), moundHeight(MoundTool::getMoundHeight()),
		feather(FeatherTool::getFeather()), featherRate(FeatherTool::getRate()), featherRadius(FeatherTool::getRadius())
	{
	}
	void restore() const
	{
		BrushTool::setWidth(brushWidth);
		BrushTool::setFeather(brushFeather);
		BrushTool::setHeight(brushHeight);
		MoundTool::setWidth(moundWidth);
		MoundTool::setFeather(moundFeather);
		MoundTool::setMoundHeight(moundHeight);
		FeatherTool::setFeather(feather);
		FeatherTool::setRate(featherRate);
		FeatherTool::setRadius(featherRadius);
	}

	Int brushWidth, brushFeather, brushHeight;
	Int moundWidth, moundFeather, moundHeight;
	Int feather, featherRate, featherRadius;
};

CPoint strokePoint(WbView3d *view, WorldHeightMapEdit *map, double x, double y)
{
	Coord3D loc;
	loc.x = (Real)x;
	loc.y = (Real)y;
	loc.z = map->getHeight(mcpWorldToIndexX(x), mcpWorldToIndexY(y)) * MAP_HEIGHT_SCALE;
	CPoint pt;
	CRect client;
	view->GetClientRect(&client);
	if (!view->docToViewCoords(loc, &pt) || !client.PtInRect(pt)) {
		mcpFail("stroke point (%g, %g) is not on screen; move the camera with view.set_camera first", x, y);
	}
	return pt;
}

McpJson cmdStrokeBench(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	WbView3d *view = CWorldBuilderDoc::GetActive3DView();
	if (view == nullptr) {
		mcpFail("no 3D view is open");
	}
	const std::string toolName = mcpArgString(args, "tool", "height");
	const double x0 = mcpArgNumber(args, "x0");
	const double y0 = mcpArgNumber(args, "y0");
	const double x1 = mcpArgNumber(args, "x1");
	const double y1 = mcpArgNumber(args, "y1");
	const Int steps = mcpArgInt(args, "steps", 40);
	const Int width = mcpArgInt(args, "width", 10);
	const Int feather = mcpArgInt(args, "feather", 3);
	const Int amount = mcpArgInt(args, "amount", 30);
	if (steps < 1) {
		mcpFail("steps must be >= 1");
	}

	const bool isMound = toolName == "mound" || toolName == "dig";
	if (!isMound && toolName != "height" && toolName != "feather") {
		mcpFail("tool must be height, mound, dig or feather");
	}
	// The mound tool ignores moves closer together than its minimum delay.
	const Int paceMs = mcpArgInt(args, "pace_ms", isMound ? 70 : 0);

	// Check the path before anything is edited.
	for (Int i = 0; i <= steps; i++) {
		const double t = (double)i / steps;
		strokePoint(view, map, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}

	// Window messages take the way of a real mouse: the view's tracking and status bar code run as well.
	const bool viaMessages = mcpArgBool(args, "via_messages", false);
	Tool *const previousTool = WbApp()->getCurTool();
	if (viaMessages && toolName != "height") {
		mcpFail("via_messages supports only the height tool");
	}
	const SavedToolSettings saved;
	// Activating a tool loads the settings of its options panel, so this comes before the settings below.
	if (viaMessages) {
		WbApp()->selectBrushTool();
	}
	BrushTool brushTool;
	FeatherTool featherTool;
	MoundTool moundTool;
	DigTool digTool;
	Tool *tool = &brushTool;
	if (toolName == "height") {
		BrushTool::setWidth(width);
		BrushTool::setFeather(feather);
		BrushTool::setHeight(amount);
	} else if (toolName == "feather") {
		tool = &featherTool;
		FeatherTool::setFeather(width);
		FeatherTool::setRate(amount);
		FeatherTool::setRadius(feather);
	} else {
		tool = toolName == "mound" ? (Tool *)&moundTool : (Tool *)&digTool;
		MoundTool::setWidth(width);
		MoundTool::setFeather(feather);
		MoundTool::setMoundHeight(amount);
	}

	TimeStats stepTime;
	TimeStats frameTime;
	TimeStats idleTime;
	CPoint pt = strokePoint(view, map, x0, y0);
	double start = nowMs();
	if (viaMessages) {
		view->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
	} else {
		tool->mouseDown(TRACK_L, pt, view, doc);
	}
	const double downMs = nowMs() - start;
	view->UpdateWindow();

	for (Int i = 1; i <= steps; i++) {
		if (paceMs > 0) {
			::Sleep(paceMs);
		}
		const double t = (double)i / steps;
		pt = strokePoint(view, map, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
		start = nowMs();
		if (viaMessages) {
			view->SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
		} else {
			tool->mouseMoved(TRACK_L, pt, view, doc);
		}
		const double afterStep = nowMs();
		// Paints only if the step left the window invalid, so a step that already painted is not counted twice.
		view->UpdateWindow();
		stepTime.add(afterStep - start);
		frameTime.add(nowMs() - afterStep);
		if (viaMessages) {
			// What the message loop does whenever the queue runs empty.
			const double beforeIdle = nowMs();
			for (LONG idleCount = 0; AfxGetApp()->OnIdle(idleCount); idleCount++) {
			}
			idleTime.add(nowMs() - beforeIdle);
		}
		if (i == steps / 2 && args.has("mid_screenshot")) {
			McpJson shotArgs = McpJson::makeObject();
			shotArgs.set("path", mcpArgString(args, "mid_screenshot"));
			shotArgs.set("format", std::string("png"));
			mcpRunCommand("view.screenshot", shotArgs);
		}
	}

	pt = strokePoint(view, map, x1, y1);
	start = nowMs();
	if (viaMessages) {
		view->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
	} else {
		tool->mouseUp(TRACK_L, pt, view, doc);
	}
	const double upMs = nowMs() - start;
	start = nowMs();
	view->Invalidate(false);
	view->UpdateWindow();
	const double firstFrameMs = nowMs() - start;
	start = nowMs();
	view->Invalidate(false);
	view->UpdateWindow();
	const double idleFrameMs = nowMs() - start;

	saved.restore();
	if (viaMessages) {
		WbApp()->setActiveTool(previousTool);
	}
	if (mcpArgBool(args, "undo", true)) {
		mcpRunCommand("edit.undo", McpJson::makeObject());
	}

	McpJson result = McpJson::makeObject();
	result.set("tool", toolName);
	result.set("steps", steps);
	result.set("mouse_down_ms", downMs);
	result.set("step", stepTime.toJson());
	result.set("frame", frameTime.toJson());
	result.set("message_loop_idle", idleTime.toJson());
	result.set("per_move_avg_ms", (stepTime.total + frameTime.total + idleTime.total) / steps);
	result.set("mouse_up_ms", upMs);
	result.set("frame_after_up_ms", firstFrameMs);
	result.set("idle_frame_ms", idleFrameMs);
	return result;
}

}	// namespace

void mcpRegisterPerfCommands()
{
	mcpRegisterCommand("perf.stroke_bench", cmdStrokeBench, "{tool:height|mound|dig|feather, x0,y0,x1,y1, steps?, width?, feather?, amount?, pace_ms?, undo?, mid_screenshot?, via_messages?} Runs a height tool along a path and reports how long each mouse move and frame took. Undoes the stroke unless undo=false. mid_screenshot is a PNG path for a picture taken halfway through the stroke. via_messages sends window messages like a real mouse, so the view's own code is timed too (height tool only). For feather: feather=radius, amount=rate.");
}
