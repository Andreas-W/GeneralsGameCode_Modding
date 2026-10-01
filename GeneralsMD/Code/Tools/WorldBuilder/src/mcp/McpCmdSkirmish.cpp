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

// McpCmdSkirmish.cpp
// MCP bridge commands that create and check the map data the skirmish AI looks up by name
// (idea from the SkirmishAIGenerator of Genesis, The CWC Team). For the player starting at
// Player_<N>_Start the engine uses:
//   - areas InnerPerimeter<N> and OuterPerimeter<N> (ScriptEngine::getQualifiedTriggerAreaByName),
//   - waypoint paths labeled Center<N>, Flank<N> and Backdoor<N>: approach paths that lead INTO
//     base N. Attackers join at the closest waypoint and follow the links
//     (ScriptActions::doTeamFollowSkirmishApproachPath); the defender places base defenses toward
//     its own paths (AISkirmishPlayer::buildAIBaseDefenseStructure).

#include "StdAfx.h"
#include "mcp/McpCommands.h"
#include "mcp/McpUndoables.h"

#include "PointerTool.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/Scripts.h"

#include <algorithm>
#include <math.h>
#include <set>
#include <vector>

namespace
{

const char *const COMBAT_ZONE_NAME = "CombatZone";
const char *const PATH_LABELS[3] = { SKIRMISH_CENTER, SKIRMISH_FLANK, SKIRMISH_BACKDOOR };

struct Start
{
	Int number;
	double x, y;
};

struct Vec2
{
	double x, y;
};

bool startBefore(const Start &a, const Start &b)
{
	return a.number < b.number;
}

std::vector<Start> findStarts()
{
	std::vector<Start> starts;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (!obj->isWaypoint()) continue;
		int number = 0;
		int consumed = 0;
		const AsciiString wayName = obj->getWaypointName();
		// %n is only reached when the whole pattern matched.
		if (sscanf(wayName.str(), "Player_%d_Start%n", &number, &consumed) == 1 && consumed == wayName.getLength()
			&& number >= 1 && number <= MAX_PLAYER_COUNT) {
			Start s;
			s.number = number;
			s.x = obj->getLocation()->x;
			s.y = obj->getLocation()->y;
			starts.push_back(s);
		}
	}
	std::sort(starts.begin(), starts.end(), startBefore);
	return starts;
}

/// Splits "Center2" into the skirmish label and its number. Returns false for other labels.
bool parsePathLabel(const AsciiString &label, Int *which, Int *number)
{
	for (Int i = 0; i < 3; i++) {
		const size_t len = strlen(PATH_LABELS[i]);
		if (_strnicmp(label.str(), PATH_LABELS[i], len) == 0) {
			const char *rest = label.str() + len;
			*which = i;
			*number = *rest ? atoi(rest) : 0;
			for (const char *p = rest; *p; p++) {
				if (*p < '0' || *p > '9') return false;
			}
			return true;
		}
	}
	return false;
}

/// True if any of the waypoint's three path labels is a skirmish approach label.
bool getSkirmishLabel(MapObject *waypoint, Int *which, Int *number)
{
	const NameKeyType keys[3] = { TheKey_waypointPathLabel1, TheKey_waypointPathLabel2, TheKey_waypointPathLabel3 };
	for (Int i = 0; i < 3; i++) {
		Bool exists = false;
		AsciiString label = waypoint->getProperties()->getAsciiString(keys[i], &exists);
		if (exists && !label.isEmpty() && parsePathLabel(label, which, number)) {
			return true;
		}
	}
	return false;
}

struct MapBounds
{
	WorldHeightMapEdit *map;
	double maxX, maxY;

	void init(WorldHeightMapEdit *m)
	{
		map = m;
		maxX = (m->getXExtent() - 1 - 2 * m->getBorderSize()) * MAP_XY_FACTOR;
		maxY = (m->getYExtent() - 1 - 2 * m->getBorderSize()) * MAP_XY_FACTOR;
	}
	void clamp(Vec2 *p, double margin) const
	{
		if (p->x < margin) p->x = margin;
		if (p->y < margin) p->y = margin;
		if (p->x > maxX - margin) p->x = maxX - margin;
		if (p->y > maxY - margin) p->y = maxY - margin;
	}
	bool isPassable(const Vec2 &p) const
	{
		const Int cx = (Int)floor(p.x / MAP_XY_FACTOR) + map->getBorderSize();
		const Int cy = (Int)floor(p.y / MAP_XY_FACTOR) + map->getBorderSize();
		if (cx < 0 || cy < 0 || cx >= map->getXExtent() - 1 || cy >= map->getYExtent() - 1) return false;
		return !map->getCliffState(cx, cy) && !mcpIsCellUnderWater(map, cx, cy);
	}
	/// Moves a point off cliffs and water to the nearest passable spot within reach.
	bool nudgeToPassable(Vec2 *p) const
	{
		if (isPassable(*p)) return false;
		for (double r = 20; r <= 300; r += 20) {
			for (Int a = 0; a < 16; a++) {
				Vec2 q = { p->x + r * cos(a * PI / 8), p->y + r * sin(a * PI / 8) };
				if (q.x < 10 || q.y < 10 || q.x > maxX - 10 || q.y > maxY - 10) continue;
				if (isPassable(q)) {
					*p = q;
					return true;
				}
			}
		}
		return false;
	}
	Int groundZ(const Vec2 &p) const
	{
		const Int ix = (Int)floor(p.x / MAP_XY_FACTOR + 0.5) + map->getBorderSize();
		const Int iy = (Int)floor(p.y / MAP_XY_FACTOR + 0.5) + map->getBorderSize();
		return (Int)floor(map->getHeight(ix, iy) * MAP_HEIGHT_SCALE + 0.5);
	}
};

/// A roughly circular trigger area, clipped to the playable area.
PolygonTrigger *makeCircleArea(const AsciiString &name, double cx, double cy, double radius, const MapBounds &bounds)
{
	const Int numPoints = 12;
	PolygonTrigger *trig = newInstance(PolygonTrigger)(numPoints);
	for (Int i = 0; i < numPoints; i++) {
		Vec2 p = { cx + radius * cos(i * 2 * PI / numPoints), cy + radius * sin(i * 2 * PI / numPoints) };
		bounds.clamp(&p, 0);
		ICoord3D pt;
		pt.x = (Int)floor(p.x + 0.5);
		pt.y = (Int)floor(p.y + 0.5);
		pt.z = bounds.groundZ(p);
		trig->addPoint(pt);
	}
	trig->setTriggerName(name);
	return trig;
}

PolygonTrigger *findArea(const AsciiString &name)
{
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		if (trig->getTriggerName().compareNoCase(name) == 0) return trig;
	}
	return nullptr;
}

Vec2 rotate(const Vec2 &v, double degrees)
{
	const double a = degrees * PI / 180.0;
	Vec2 r = { v.x * cos(a) - v.y * sin(a), v.x * sin(a) + v.y * cos(a) };
	return r;
}

//-------------------------------------------------------------------------------------------------
// ai.skirmish_check
//-------------------------------------------------------------------------------------------------

McpJson cmdSkirmishCheck(const McpJson &)
{
	mcpDoc();
	const std::vector<Start> starts = findStarts();
	McpJson players = McpJson::makeArray();
	McpJson problems = McpJson::makeArray();
	if (starts.size() < 2) {
		problems.push(McpJson("fewer than two Player_<N>_Start waypoints"));
	}

	// Count labeled waypoints per label and player; collect labels without a valid number.
	Int counts[3][MAX_PLAYER_COUNT + 1];
	memset(counts, 0, sizeof(counts));
	std::set<std::string> badLabels;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (!obj->isWaypoint()) continue;
		Int which, number;
		if (!getSkirmishLabel(obj, &which, &number)) continue;
		if (number >= 1 && number <= MAX_PLAYER_COUNT) {
			counts[which][number]++;
		} else {
			badLabels.insert(std::string(PATH_LABELS[which]) + (number ? " (bad number)" : " (no player number)"));
		}
	}
	for (std::set<std::string>::const_iterator it = badLabels.begin(); it != badLabels.end(); ++it) {
		problems.push(McpJson("path label " + *it + ": the AI only uses labels like Center1, Flank2, Backdoor1"));
	}

	for (size_t i = 0; i < starts.size(); i++) {
		const Int n = starts[i].number;
		AsciiString inner, outer;
		inner.format("%s%d", INNER_PERIMETER, n);
		outer.format("%s%d", OUTER_PERIMETER, n);
		McpJson p = McpJson::makeObject();
		p.set("player", n).set("start_x", starts[i].x).set("start_y", starts[i].y);
		p.set("inner_perimeter", findArea(inner) != nullptr).set("outer_perimeter", findArea(outer) != nullptr);
		for (Int l = 0; l < 3; l++) {
			static const char *const countKeys[3] = { "center_waypoints", "flank_waypoints", "backdoor_waypoints" };
			p.set(countKeys[l], counts[l][n]);
			if (counts[l][n] == 0) {
				AsciiString msg;
				msg.format("player %d has no %s%d approach path", n, PATH_LABELS[l], n);
				problems.push(McpJson(msg.str()));
			}
		}
		if (!findArea(inner)) { AsciiString m; m.format("missing area %s", inner.str()); problems.push(McpJson(m.str())); }
		if (!findArea(outer)) { AsciiString m; m.format("missing area %s", outer.str()); problems.push(McpJson(m.str())); }
		players.push(p);
	}
	McpJson j = McpJson::makeObject();
	j.set("players", players).set("combat_zone", findArea(COMBAT_ZONE_NAME) != nullptr);
	j.set("problems", problems).set("ready", problems.size() == 0);
	return j;
}

//-------------------------------------------------------------------------------------------------
// ai.skirmish_setup
//-------------------------------------------------------------------------------------------------

McpJson cmdSkirmishSetup(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	MapBounds bounds;
	bounds.init(mcpHeightMap());
	const std::vector<Start> starts = findStarts();
	if (starts.size() < 2) {
		mcpFail("need at least two start waypoints named Player_<N>_Start (found %d)", (int)starts.size());
	}
	const double innerRadius = mcpArgNumber(args, "inner_radius", 350);
	const double outerRadius = mcpArgNumber(args, "outer_radius", 600);
	const double flankAngle = mcpArgNumber(args, "flank_angle", 70);
	const double backdoorAngle = mcpArgNumber(args, "backdoor_angle", 70);
	const Int pathPoints = mcpArgInt(args, "path_points", 5);
	const bool replace = mcpArgBool(args, "replace", true);
	const bool makeCombatZone = mcpArgBool(args, "combat_zone", true);
	if (innerRadius < 50 || outerRadius <= innerRadius) mcpFail("need 50 <= inner_radius < outer_radius");
	if (pathPoints < 2 || pathPoints > 20) mcpFail("path_points must be 2..20");

	std::set<Int> numbers;
	for (size_t i = 0; i < starts.size(); i++) {
		if (!numbers.insert(starts[i].number).second) mcpFail("duplicate start waypoint Player_%d_Start", starts[i].number);
	}

	// Existing skirmish data for these players is replaced so the command can be re-run.
	std::vector<PolygonTrigger *> oldAreas;
	std::vector<MapObject *> oldWaypoints;
	std::set<Int> oldWaypointIds;
	std::set<std::string> usedNames;
	for (size_t i = 0; i < starts.size(); i++) {
		AsciiString name;
		name.format("%s%d", INNER_PERIMETER, starts[i].number);
		if (PolygonTrigger *t = findArea(name)) oldAreas.push_back(t);
		name.format("%s%d", OUTER_PERIMETER, starts[i].number);
		if (PolygonTrigger *t = findArea(name)) oldAreas.push_back(t);
	}
	if (makeCombatZone) {
		if (PolygonTrigger *t = findArea(COMBAT_ZONE_NAME)) oldAreas.push_back(t);
	}
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (!obj->isWaypoint()) continue;
		Int which, number;
		// Unnumbered labels are leftovers the AI never uses; replace them too.
		if (getSkirmishLabel(obj, &which, &number) && (number == 0 || numbers.count(number))) {
			oldWaypoints.push_back(obj);
			oldWaypointIds.insert(obj->getWaypointID());
		} else {
			usedNames.insert(obj->getWaypointName().str());
		}
	}
	if (!replace && (!oldAreas.empty() || !oldWaypoints.empty())) {
		mcpFail("the map already has %d skirmish areas and %d approach path waypoints; pass replace=true to rebuild them",
			(int)oldAreas.size(), (int)oldWaypoints.size());
	}

	// Build the new areas and paths.
	std::vector<PolygonTrigger *> newAreas;
	McpWaypointLinksUndoable *links = new McpWaypointLinksUndoable(doc);
	MapObject *head = nullptr, *tail = nullptr;
	McpJson players = McpJson::makeArray();
	Int nudged = 0;
	Vec2 centroid = { 0, 0 };
	for (size_t i = 0; i < starts.size(); i++) {
		centroid.x += starts[i].x / starts.size();
		centroid.y += starts[i].y / starts.size();
	}

	for (size_t i = 0; i < starts.size(); i++) {
		const Start &s = starts[i];
		AsciiString name;
		name.format("%s%d", INNER_PERIMETER, s.number);
		newAreas.push_back(makeCircleArea(name, s.x, s.y, innerRadius, bounds));
		name.format("%s%d", OUTER_PERIMETER, s.number);
		newAreas.push_back(makeCircleArea(name, s.x, s.y, outerRadius, bounds));

		// Approach paths start near the other players and lead into this base.
		Vec2 others = { 0, 0 };
		for (size_t k = 0; k < starts.size(); k++) {
			if (k == i) continue;
			others.x += starts[k].x / (starts.size() - 1);
			others.y += starts[k].y / (starts.size() - 1);
		}
		Vec2 dir = { others.x - s.x, others.y - s.y };
		double dist = sqrt(dir.x * dir.x + dir.y * dir.y);
		if (dist < 1) {
			dir.x = bounds.maxX / 2 - s.x;
			dir.y = bounds.maxY / 2 - s.y;
			dist = sqrt(dir.x * dir.x + dir.y * dir.y);
		}
		if (dist < 1) { dir.x = 1; dir.y = 0; dist = 1; }
		dir.x /= dist;
		dir.y /= dist;
		const double startDist = dist - outerRadius > dist * 0.5 ? dist - outerRadius : dist * 0.5;
		const Vec2 origin = { s.x + dir.x * startDist, s.y + dir.y * startDist };

		McpJson paths = McpJson::makeObject();
		const double angles[3] = { 0, flankAngle, -backdoorAngle };
		for (Int l = 0; l < 3; l++) {
			AsciiString label;
			label.format("%s%d", PATH_LABELS[l], s.number);
			const Vec2 side = rotate(dir, angles[l]);
			const Vec2 end = { s.x + side.x * innerRadius * 0.8, s.y + side.y * innerRadius * 0.8 };
			// Quadratic curve: flank and backdoor swing out and come in from their own side.
			const Vec2 control = { s.x + side.x * dist * 0.7, s.y + side.y * dist * 0.7 };
			McpJson names = McpJson::makeArray();
			Int prevId = 0;
			for (Int p = 0; p < pathPoints; p++) {
				const double t = (double)p / (pathPoints - 1);
				Vec2 pt = {
					(1 - t) * (1 - t) * origin.x + 2 * (1 - t) * t * control.x + t * t * end.x,
					(1 - t) * (1 - t) * origin.y + 2 * (1 - t) * t * control.y + t * t * end.y };
				bounds.clamp(&pt, 40);
				if (bounds.nudgeToPassable(&pt)) nudged++;

				Coord3D loc;
				loc.x = (Real)pt.x;
				loc.y = (Real)pt.y;
				loc.z = 0;
				MapObject *way = newInstance(MapObject)(loc, "*Waypoints/Waypoint", 0, 0, nullptr, nullptr);
				const Int id = doc->getNextWaypointID();
				AsciiString wayName;
				wayName.format("%s_%d", label.str(), p + 1);
				for (Int suffix = 2; usedNames.count(wayName.str()); suffix++) {
					wayName.format("%s_%d_%d", label.str(), p + 1, suffix);
				}
				usedNames.insert(wayName.str());
				way->setIsWaypoint();
				way->setWaypointID(id);
				way->setWaypointName(wayName);
				way->getProperties()->setAsciiString(TheKey_originalOwner, "team");
				way->getProperties()->setAsciiString(TheKey_waypointPathLabel1, label);
				if (tail) tail->setNextMap(way); else head = way;
				tail = way;
				if (prevId) links->added.push_back(std::make_pair(prevId, id));
				prevId = id;
				names.push(McpJson(wayName.str()));
			}
			paths.set(label.str(), names);
		}
		players.push(McpJson::makeObject().set("player", s.number).set("paths", paths));
	}

	if (makeCombatZone) {
		double nearest = 1e30;
		for (size_t i = 0; i < starts.size(); i++) {
			const double d = sqrt((starts[i].x - centroid.x) * (starts[i].x - centroid.x) + (starts[i].y - centroid.y) * (starts[i].y - centroid.y));
			if (d < nearest) nearest = d;
		}
		double radius = nearest - outerRadius;
		if (radius < 200) radius = nearest * 0.4;
		if (radius < 100) radius = 100;
		newAreas.push_back(makeCircleArea(COMBAT_ZONE_NAME, centroid.x, centroid.y, radius, bounds));
	}

	// Links of replaced waypoints go away with them.
	for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
		Int a, b;
		doc->getWaypointLink(i, &a, &b);
		if (oldWaypointIds.count(a) || oldWaypointIds.count(b)) links->removed.push_back(std::make_pair(a, b));
	}

	// One undo step; MultipleUndoable runs the last added step first.
	MultipleUndoable *undo = new MultipleUndoable;
	undo->addUndoable(links);
	if (head) undo->addUndoable(new AddObjectUndoable(doc, head));
	for (size_t i = 0; i < newAreas.size(); i++) undo->addUndoable(new AddPolygonUndoable(newAreas[i]));
	for (size_t i = 0; i < oldAreas.size(); i++) undo->addUndoable(new DeletePolygonUndoable(oldAreas[i]));
	if (!oldWaypoints.empty()) {
		PointerTool::clearSelection();
		for (size_t i = 0; i < oldWaypoints.size(); i++) oldWaypoints[i]->setSelected(true);
		undo->addUndoable(new DeleteObjectUndoable(doc));
		PointerTool::clearSelection();
	}
	mcpCommit(undo);
	mcpResetObjectHandles();

	McpJson j = McpJson::makeObject();
	j.set("players", players);
	j.set("areas_created", (int)newAreas.size()).set("areas_replaced", (int)oldAreas.size());
	j.set("waypoints_created", (int)(starts.size() * 3 * pathPoints)).set("waypoints_replaced", (int)oldWaypoints.size());
	j.set("waypoints_moved_off_obstacles", nudged);
	return j;
}

//-------------------------------------------------------------------------------------------------
// map.generate_starts
//-------------------------------------------------------------------------------------------------

/// Height of the highest water surface covering a world point, or a negative value if there is none.
double waterHeightAt(double x, double y)
{
	ICoord3D pt;
	pt.x = (Int)floor(x + 0.5);
	pt.y = (Int)floor(y + 0.5);
	pt.z = 0;
	double best = -1;
	for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
		if (!trig->isWaterArea() || trig->getNumPoints() < 3) continue;
		if (trig->pointInTrigger(pt) && trig->getPoint(0)->z > best) best = trig->getPoint(0)->z;
	}
	return best;
}

/// Places Player_<N>_Start waypoints at even angles around the map center, on a rectangle inset
/// from the map edges (Genesis StartingPositionGenerator), and levels each base area.
McpJson cmdGenerateStarts(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	MapBounds bounds;
	bounds.init(map);
	const Int numPlayers = mcpArgInt(args, "players");
	if (numPlayers < 2 || numPlayers > 8) mcpFail("players must be 2..8");
	const double baseRadius = mcpArgNumber(args, "base_radius", 300);
	const double margin = mcpArgNumber(args, "edge_margin", baseRadius + 50);
	const double distance = mcpArgNumber(args, "distance", 1.0);
	const bool flatten = mcpArgBool(args, "flatten", true);
	const double feather = mcpArgNumber(args, "flatten_feather", 150);
	const bool replace = mcpArgBool(args, "replace", true);
	if (baseRadius < 50) mcpFail("base_radius must be >= 50");
	if (distance <= 0 || distance > 1) mcpFail("distance must be in (0, 1]");

	const double cx = bounds.maxX / 2, cy = bounds.maxY / 2;
	const double hx = cx - margin, hy = cy - margin;
	if (hx <= 0 || hy <= 0) {
		mcpFail("the map is too small for edge_margin %.0f (playable size %.0f x %.0f)", margin, bounds.maxX, bounds.maxY);
	}
	// Default: the first player sits toward the south-west corner, which puts two players on a
	// diagonal and four players into the corners.
	const double firstAngle = args.has("angle_deg") ? mcpArgNumber(args, "angle_deg") * PI / 180.0 : atan2(-hy, -hx);

	std::vector<Vec2> positions;
	for (Int i = 0; i < numPlayers; i++) {
		const double a = firstAngle + i * 2 * PI / numPlayers;
		const double dx = cos(a), dy = sin(a);
		// Distance along the ray to the inset rectangle.
		const double tx = fabs(dx) > 1e-9 ? hx / fabs(dx) : 1e30;
		const double ty = fabs(dy) > 1e-9 ? hy / fabs(dy) : 1e30;
		const double t = (tx < ty ? tx : ty) * distance;
		Vec2 p = { cx + dx * t, cy + dy * t };
		positions.push_back(p);
	}
	double closest = 1e30;
	for (size_t i = 0; i < positions.size(); i++) {
		for (size_t k = i + 1; k < positions.size(); k++) {
			const double d = hypot(positions[i].x - positions[k].x, positions[i].y - positions[k].y);
			if (d < closest) closest = d;
		}
	}
	if (closest < 2 * baseRadius) {
		mcpFail("the starts would be only %.0f apart, less than two base radii (%.0f); use a larger map, fewer players or a smaller base_radius",
			closest, 2 * baseRadius);
	}

	// Existing start waypoints are replaced.
	std::vector<MapObject *> oldStarts;
	std::set<Int> oldIds;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		if (!obj->isWaypoint()) continue;
		int number = 0, consumed = 0;
		const AsciiString name = obj->getWaypointName();
		if (sscanf(name.str(), "Player_%d_Start%n", &number, &consumed) == 1 && consumed == name.getLength()) {
			oldStarts.push_back(obj);
			oldIds.insert(obj->getWaypointID());
		}
	}
	if (!replace && !oldStarts.empty()) {
		mcpFail("the map already has %d start waypoints; pass replace=true to replace them", (int)oldStarts.size());
	}

	MultipleUndoable *undo = new MultipleUndoable;
	McpWaypointLinksUndoable *links = new McpWaypointLinksUndoable(doc);
	for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
		Int a, b;
		doc->getWaypointLink(i, &a, &b);
		if (oldIds.count(a) || oldIds.count(b)) links->removed.push_back(std::make_pair(a, b));
	}
	undo->addUndoable(links);

	MapObject *head = nullptr, *tail = nullptr;
	McpJson starts = McpJson::makeArray();
	std::vector<double> targets(positions.size(), 0);
	const Int border = map->getBorderSize();
	for (size_t i = 0; i < positions.size(); i++) {
		// Level the base to the average height of the area, but never below the water surface.
		double sum = 0;
		Int count = 0;
		const Int r = (Int)ceil(baseRadius / MAP_XY_FACTOR);
		const Int vx = (Int)floor(positions[i].x / MAP_XY_FACTOR + 0.5) + border;
		const Int vy = (Int)floor(positions[i].y / MAP_XY_FACTOR + 0.5) + border;
		for (Int y = vy - r; y <= vy + r; y++) {
			for (Int x = vx - r; x <= vx + r; x++) {
				if (x < 0 || y < 0 || x >= map->getXExtent() || y >= map->getYExtent()) continue;
				if ((x - vx) * (x - vx) + (y - vy) * (y - vy) > r * r) continue;
				sum += map->getHeight(x, y);
				count++;
			}
		}
		double target = count ? sum / count : map->getHeight(vx, vy);
		const double water = waterHeightAt(positions[i].x, positions[i].y);
		if (water >= 0 && target * MAP_HEIGHT_SCALE <= water + 1) {
			target = (water + 2) / MAP_HEIGHT_SCALE;
		}
		targets[i] = floor(target + 0.5);

		Coord3D loc;
		loc.x = (Real)positions[i].x;
		loc.y = (Real)positions[i].y;
		loc.z = 0;
		MapObject *way = newInstance(MapObject)(loc, "*Waypoints/Waypoint", 0, 0, nullptr, nullptr);
		AsciiString name;
		name.format("Player_%d_Start", (Int)i + 1);
		way->setIsWaypoint();
		way->setWaypointID(doc->getNextWaypointID());
		way->setWaypointName(name);
		way->getProperties()->setAsciiString(TheKey_originalOwner, "team");
		if (tail) tail->setNextMap(way); else head = way;
		tail = way;
		McpJson s = McpJson::makeObject();
		s.set("player", (Int)i + 1).set("x", positions[i].x).set("y", positions[i].y);
		if (flatten) s.set("flattened_to_raw", targets[i]);
		starts.push(s);
	}
	undo->addUndoable(new AddObjectUndoable(doc, head));
	if (!oldStarts.empty()) {
		PointerTool::clearSelection();
		for (size_t i = 0; i < oldStarts.size(); i++) oldStarts[i]->setSelected(true);
		undo->addUndoable(new DeleteObjectUndoable(doc));
		PointerTool::clearSelection();
	}

	WorldHeightMapEdit *copy = nullptr;
	Int flattened = 0;
	if (flatten) {
		copy = map->duplicate();
		const double reach = baseRadius + feather;
		for (size_t i = 0; i < positions.size(); i++) {
			const Int r = (Int)ceil(reach / MAP_XY_FACTOR) + 1;
			const Int vx = (Int)floor(positions[i].x / MAP_XY_FACTOR + 0.5) + border;
			const Int vy = (Int)floor(positions[i].y / MAP_XY_FACTOR + 0.5) + border;
			for (Int y = vy - r; y <= vy + r; y++) {
				for (Int x = vx - r; x <= vx + r; x++) {
					if (x < 0 || y < 0 || x >= map->getXExtent() || y >= map->getYExtent()) continue;
					const double d = hypot((x - border) * MAP_XY_FACTOR - positions[i].x, (y - border) * MAP_XY_FACTOR - positions[i].y);
					if (d > reach) continue;
					const double w = d <= baseRadius ? 1.0 : (feather > 0 ? 1.0 - (d - baseRadius) / feather : 0);
					const double cur = copy->getHeight(x, y);
					double h = floor(cur + (targets[i] - cur) * w + 0.5);
					if (h < 0) h = 0;
					if (h > 255) h = 255;
					if ((Int)h != copy->getHeight(x, y)) {
						copy->setHeight(x, y, (UnsignedByte)h);
						flattened++;
					}
				}
			}
		}
		if (flattened > 0) {
			IRegion2D all = { 0, 0, 0, 0 };
			doc->updateHeightMap(copy, false, all);
			undo->addUndoable(new WBDocUndoable(doc, copy));
		}
	}
	mcpCommit(undo);
	if (copy) REF_PTR_RELEASE(copy);
	mcpResetObjectHandles();

	McpJson j = McpJson::makeObject();
	j.set("starts", starts).set("closest_distance", closest);
	j.set("starts_replaced", (int)oldStarts.size()).set("flattened_vertices", flattened);
	return j;
}

} // namespace

void mcpRegisterSkirmishCommands()
{
	mcpRegisterCommand("ai.skirmish_setup", cmdSkirmishSetup, "{inner_radius?=350, outer_radius?=600, flank_angle?=70, backdoor_angle?=70, path_points?=5, combat_zone?=true, replace?=true} Creates perimeters and Center/Flank/Backdoor approach paths for every Player_<N>_Start.");
	mcpRegisterCommand("map.generate_starts", cmdGenerateStarts, "{players, base_radius?=300, edge_margin?, angle_deg?, distance?=1, flatten?=true, flatten_feather?=150, replace?=true} Places Player_<N>_Start waypoints evenly around the map center and levels the bases.");
	mcpRegisterCommand("ai.skirmish_check", cmdSkirmishCheck, "Reports which skirmish AI areas and approach paths each start position has, and what is missing.");
}
