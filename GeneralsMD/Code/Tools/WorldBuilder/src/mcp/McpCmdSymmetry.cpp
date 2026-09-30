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

// McpCmdSymmetry.cpp
// MCP bridge commands that mirror or rotate the map, or copy one part of it onto the others to make
// symmetric multiplayer maps. The feature set follows the map tools of Genesis (The CWC Team).
//
// All geometry works in a frame centered on the heightmap (including the border), measured in
// cells: vertex (x, y) is at (x - cx, y - cy), a cell at its center, and objects at their world
// position converted to cells.

#include "StdAfx.h"
#include "mcp/McpCommands.h"

#include "CUndoable.h"
#include "PointerTool.h"
#include "TerrainMaterial.h"
#include "WHeightMapEdit.h"
#include "WorldBuilderDoc.h"
#include "Common/WellKnownKeys.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"

#include <map>
#include <math.h>
#include <regex>
#include <set>
#include <vector>

namespace
{

/// Integer 2x2 matrix [a b; c d] for mirrors and quarter rotations. All of them are orthogonal.
struct Mat2
{
	Int a, b, c, d;

	void apply(double x, double y, double *ox, double *oy) const
	{
		*ox = a * x + b * y;
		*oy = c * x + d * y;
	}
	void applyDir(Int *dx, Int *dy) const
	{
		const Int x = *dx, y = *dy;
		*dx = a * x + b * y;
		*dy = c * x + d * y;
	}
	Mat2 inverse() const
	{
		Mat2 m = { a, c, b, d };
		return m;
	}
	Mat2 operator*(const Mat2 &o) const
	{
		Mat2 m = { a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d };
		return m;
	}
	bool swapsAxes() const { return a == 0; }
};

const Mat2 MAT_IDENTITY = { 1, 0, 0, 1 };
const Mat2 MAT_MIRROR_X = { -1, 0, 0, 1 };
const Mat2 MAT_MIRROR_Y = { 1, 0, 0, -1 };
const Mat2 MAT_ROTATE_90 = { 0, -1, 1, 0 };	// counterclockwise
const Mat2 MAT_ROTATE_180 = { -1, 0, 0, -1 };
const Mat2 MAT_ROTATE_270 = { 0, 1, -1, 0 };
const Mat2 MAT_MIRROR_DIAG = { 0, 1, 1, 0 };	// across the line y = x
const Mat2 MAT_MIRROR_ANTIDIAG = { 0, -1, -1, 0 };	// across the line y = -x

/// Converts between heightmap indices, world coordinates and the centered frame.
struct Frame
{
	double cx, cy;
	Int border;
	Int width, height;	///< vertex counts

	void init(WorldHeightMapEdit *map)
	{
		width = map->getXExtent();
		height = map->getYExtent();
		border = map->getBorderSize();
		cx = (width - 1) / 2.0;
		cy = (height - 1) / 2.0;
	}
	void worldToFrame(double wx, double wy, double *px, double *py) const
	{
		*px = wx / MAP_XY_FACTOR + border - cx;
		*py = wy / MAP_XY_FACTOR + border - cy;
	}
	void frameToWorld(double px, double py, double *wx, double *wy) const
	{
		*wx = (px + cx - border) * MAP_XY_FACTOR;
		*wy = (py + cy - border) * MAP_XY_FACTOR;
	}
};

Int roundToInt(double v)
{
	return (Int)floor(v + 0.5);
}

/// Which part of the map is kept and how it is copied onto the rest.
struct Symmetry
{
	std::string mode;
	std::string source;
	std::vector<Mat2> copies;	///< copies[0] is the identity; copies[k] maps the source onto copy k.

	void parse(const std::string &modeArg, const std::string &sourceArg, bool square)
	{
		mode = modeArg;
		source = sourceArg;
		copies.clear();
		copies.push_back(MAT_IDENTITY);
		if (mode == "mirror_x") {
			if (source != "west" && source != "east") mcpFail("mirror_x needs source west or east");
			copies.push_back(MAT_MIRROR_X);
		} else if (mode == "mirror_y") {
			if (source != "south" && source != "north") mcpFail("mirror_y needs source south or north");
			copies.push_back(MAT_MIRROR_Y);
		} else if (mode == "rotate_180") {
			if (source != "south" && source != "north" && source != "west" && source != "east") {
				mcpFail("rotate_180 needs source south, north, west or east");
			}
			copies.push_back(MAT_ROTATE_180);
		} else if (mode == "mirror_diag") {
			if (source != "se" && source != "nw") mcpFail("mirror_diag (across y = x) needs source se or nw");
			copies.push_back(MAT_MIRROR_DIAG);
		} else if (mode == "mirror_antidiag") {
			if (source != "sw" && source != "ne") mcpFail("mirror_antidiag (across y = -x) needs source sw or ne");
			copies.push_back(MAT_MIRROR_ANTIDIAG);
		} else if (mode == "rotate_quarters") {
			if (source != "sw" && source != "se" && source != "ne" && source != "nw") {
				mcpFail("rotate_quarters needs source sw, se, ne or nw");
			}
			copies.push_back(MAT_ROTATE_90);
			copies.push_back(MAT_ROTATE_180);
			copies.push_back(MAT_ROTATE_270);
		} else {
			mcpFail("mode must be mirror_x, mirror_y, rotate_180, mirror_diag, mirror_antidiag or rotate_quarters");
		}
		for (size_t k = 1; k < copies.size(); k++) {
			if (copies[k].swapsAxes() && !square) {
				mcpFail("%s needs a square map", mode.c_str());
			}
		}
	}

	/// Source region with tie rules chosen so the source and its copies never overlap.
	bool inSource(double px, double py) const
	{
		if (mode == "mirror_x") return source == "west" ? px < 0 : px > 0;
		if (mode == "mirror_y") return source == "south" ? py < 0 : py > 0;
		if (mode == "rotate_180") {
			if (source == "south") return py < 0 || (py == 0 && px < 0);
			if (source == "north") return py > 0 || (py == 0 && px > 0);
			if (source == "west") return px < 0 || (px == 0 && py < 0);
			return px > 0 || (px == 0 && py > 0);
		}
		if (mode == "mirror_diag") return source == "se" ? py < px : py > px;
		if (mode == "mirror_antidiag") return source == "sw" ? px + py < 0 : px + py > 0;
		// rotate_quarters: half-open quadrants that tile the plane under 90 degree rotations.
		if (source == "sw") return px < 0 && py <= 0;
		if (source == "se") return px >= 0 && py < 0;
		if (source == "ne") return px > 0 && py >= 0;
		return px <= 0 && py > 0;
	}

	/// For a point outside the source, finds the copy that covers it and the source point it comes from.
	bool findSource(double px, double py, double *sx, double *sy, Int *copy) const
	{
		if (inSource(px, py)) {
			return false;
		}
		for (size_t k = 1; k < copies.size(); k++) {
			copies[k].inverse().apply(px, py, sx, sy);
			if (inSource(*sx, *sy)) {
				*copy = (Int)k;
				return true;
			}
		}
		return false;
	}

	/// True for points on (or within tolerance of) an axis or center that maps onto itself.
	bool isOnAxis(double px, double py, double tolerance) const
	{
		for (size_t k = 1; k < copies.size(); k++) {
			double ix, iy;
			copies[k].apply(px, py, &ix, &iy);
			if (sqrt((ix - px) * (ix - px) + (iy - py) * (iy - py)) < 2 * tolerance) {
				return true;
			}
		}
		return false;
	}
};

/// Heightmap transform for map.symmetrize: fills everything outside the source from the source.
class SymmetryTransform : public HeightMapTransform
{
public:
	SymmetryTransform(const Frame &frame, const Symmetry &sym) : m_frame(frame), m_sym(sym) {}

	virtual Bool sourceVertex(Int x, Int y, Int *srcX, Int *srcY) const override
	{
		return map(x - m_frame.cx, y - m_frame.cy, 0, srcX, srcY, nullptr);
	}
	virtual Bool sourceCell(Int x, Int y, Int *srcX, Int *srcY) const override
	{
		return map(x + 0.5 - m_frame.cx, y + 0.5 - m_frame.cy, 0.5, srcX, srcY, nullptr);
	}
	virtual void mapDirection(Int x, Int y, Int *dx, Int *dy) const override
	{
		Int sx, sy, copy = 0;
		if (map(x + 0.5 - m_frame.cx, y + 0.5 - m_frame.cy, 0.5, &sx, &sy, &copy)) {
			m_sym.copies[copy].applyDir(dx, dy);
		}
	}

private:
	bool map(double px, double py, double offset, Int *srcX, Int *srcY, Int *copyOut) const
	{
		double sx, sy;
		Int copy;
		if (!m_sym.findSource(px, py, &sx, &sy, &copy)) {
			return false;
		}
		*srcX = roundToInt(sx + m_frame.cx - offset);
		*srcY = roundToInt(sy + m_frame.cy - offset);
		if (copyOut) *copyOut = copy;
		const Int maxX = m_frame.width - (offset > 0 ? 2 : 1);
		const Int maxY = m_frame.height - (offset > 0 ? 2 : 1);
		return *srcX >= 0 && *srcY >= 0 && *srcX <= maxX && *srcY <= maxY;
	}

	const Frame &m_frame;
	const Symmetry &m_sym;
};

/// Heightmap transform for map.transform: every destination comes from its preimage.
class WholeMapTransform : public HeightMapTransform
{
public:
	WholeMapTransform(const Frame &frame, const Mat2 &m) : m_frame(frame), m_m(m), m_inv(m.inverse()) {}

	virtual Bool sourceVertex(Int x, Int y, Int *srcX, Int *srcY) const override
	{
		double sx, sy;
		m_inv.apply(x - m_frame.cx, y - m_frame.cy, &sx, &sy);
		*srcX = roundToInt(sx + m_frame.cx);
		*srcY = roundToInt(sy + m_frame.cy);
		return *srcX >= 0 && *srcY >= 0 && *srcX < m_frame.width && *srcY < m_frame.height;
	}
	virtual Bool sourceCell(Int x, Int y, Int *srcX, Int *srcY) const override
	{
		double sx, sy;
		m_inv.apply(x + 0.5 - m_frame.cx, y + 0.5 - m_frame.cy, &sx, &sy);
		*srcX = roundToInt(sx + m_frame.cx - 0.5);
		*srcY = roundToInt(sy + m_frame.cy - 0.5);
		return *srcX >= 0 && *srcY >= 0 && *srcX < m_frame.width - 1 && *srcY < m_frame.height - 1;
	}
	virtual void mapDirection(Int, Int, Int *dx, Int *dy) const override
	{
		m_m.applyDir(dx, dy);
	}

private:
	const Frame &m_frame;
	Mat2 m_m;
	Mat2 m_inv;
};

void transformObject(MapObject *obj, const Frame &frame, const Mat2 &m, Coord3D *outLoc, Real *outAngle)
{
	Coord3D loc = *obj->getLocation();
	double px, py, qx, qy, wx, wy;
	frame.worldToFrame(loc.x, loc.y, &px, &py);
	m.apply(px, py, &qx, &qy);
	frame.frameToWorld(qx, qy, &wx, &wy);
	loc.x = (Real)wx;
	loc.y = (Real)wy;
	double dx, dy;
	m.apply(cos(obj->getAngle()), sin(obj->getAngle()), &dx, &dy);
	*outLoc = loc;
	*outAngle = (Real)atan2(dy, dx);
}

/// Applies the transform to a polygon point (integer world coordinates, z unchanged).
ICoord3D transformPoint(const ICoord3D &pt, const Frame &frame, const Mat2 &m)
{
	double px, py, qx, qy, wx, wy;
	frame.worldToFrame(pt.x, pt.y, &px, &py);
	m.apply(px, py, &qx, &qy);
	frame.frameToWorld(qx, qy, &wx, &wy);
	ICoord3D out = pt;
	out.x = roundToInt(wx);
	out.y = roundToInt(wy);
	return out;
}

bool isDefaultWater(PolygonTrigger *trig)
{
	std::string name = trig->getTriggerName().str();
	std::string norm;
	for (size_t i = 0; i < name.size(); i++) {
		if (name[i] != ' ' && name[i] != '_') norm += (char)tolower((unsigned char)name[i]);
	}
	return norm == "defaultwater";
}

void polygonCentroid(PolygonTrigger *trig, const Frame &frame, double *px, double *py)
{
	double sx = 0, sy = 0;
	const Int n = trig->getNumPoints();
	for (Int i = 0; i < n; i++) {
		sx += trig->getPoint(i)->x;
		sy += trig->getPoint(i)->y;
	}
	frame.worldToFrame(n ? sx / n : 0, n ? sy / n : 0, px, py);
}

//-------------------------------------------------------------------------------------------------
// Undoables
//-------------------------------------------------------------------------------------------------

/// Moves and rotates objects in place (map.transform).
class MoveObjectsUndoable : public Undoable
{
public:
	MoveObjectsUndoable(CWorldBuilderDoc *doc) : m_doc(doc), m_list(nullptr), m_tail(nullptr) {}
	virtual ~MoveObjectsUndoable() override { delete m_list; }

	void add(MapObject *obj, const Coord3D &loc, Real angle)
	{
		MoveInfo *info = new MoveInfo(obj);
		info->m_newLocation = loc;
		info->m_newAngle = angle;
		if (m_tail) m_tail->m_next = info; else m_list = info;
		m_tail = info;
	}
	virtual void Do() override { for (MoveInfo *i = m_list; i; i = i->m_next) i->DoMove(m_doc); }
	virtual void Undo() override { for (MoveInfo *i = m_list; i; i = i->m_next) i->UndoMove(m_doc); }

private:
	CWorldBuilderDoc *m_doc;
	MoveInfo *m_list;
	MoveInfo *m_tail;
};

/// Replaces all points of existing polygons (map.transform).
class PolygonPointsUndoable : public Undoable
{
public:
	void add(PolygonTrigger *trig, const std::vector<ICoord3D> &newPoints)
	{
		Entry e;
		e.trig = trig;
		e.newPoints = newPoints;
		for (Int i = 0; i < trig->getNumPoints(); i++) {
			e.oldPoints.push_back(*trig->getPoint(i));
		}
		m_entries.push_back(e);
	}
	virtual void Do() override { apply(true); }
	virtual void Undo() override { apply(false); }

private:
	struct Entry
	{
		PolygonTrigger *trig;
		std::vector<ICoord3D> oldPoints, newPoints;
	};
	void apply(bool forward)
	{
		for (size_t i = 0; i < m_entries.size(); i++) {
			const std::vector<ICoord3D> &pts = forward ? m_entries[i].newPoints : m_entries[i].oldPoints;
			for (size_t p = 0; p < pts.size(); p++) {
				m_entries[i].trig->setPoint(pts[p], (Int)p);
			}
		}
	}
	std::vector<Entry> m_entries;
};

/// Adds and removes waypoint links, which WorldBuilder itself does not record for undo.
class WaypointLinksUndoable : public Undoable
{
public:
	typedef std::pair<Int, Int> Link;
	WaypointLinksUndoable(CWorldBuilderDoc *doc) : m_doc(doc) {}

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

//-------------------------------------------------------------------------------------------------
// Renaming
//-------------------------------------------------------------------------------------------------

/// Renames copies: "auto" renumbers player numbers in known patterns, pairs replace substrings.
class Renamer
{
public:
	void parse(const McpJson &args, Int numCopies)
	{
		m_numCopies = numCopies;
		m_auto = true;
		const McpJson &rename = args.get("rename");
		if (rename.isString()) {
			if (rename.asString() == "none") m_auto = false;
			else if (rename.asString() != "auto") mcpFail("rename must be \"auto\", \"none\" or a list of [from, to] pairs");
		} else if (rename.isArray()) {
			m_auto = false;
			for (size_t i = 0; i < rename.size(); i++) {
				const McpJson &p = rename.at(i);
				if (!p.isArray() || p.size() != 2 || !p.at(0).isString() || !p.at(1).isString()) {
					mcpFail("rename pairs must be [from, to] strings");
				}
				m_pairs.push_back(std::make_pair(p.at(0).asString(), p.at(1).asString()));
			}
		} else if (!rename.isNull()) {
			mcpFail("rename must be \"auto\", \"none\" or a list of [from, to] pairs");
		}
		const McpJson &ownerMap = args.get("owner_map");
		if (ownerMap.isObject()) {
			for (size_t i = 0; i < ownerMap.numKeys(); i++) {
				if (!ownerMap.valueAt(i).isString()) mcpFail("owner_map values must be team names");
				m_ownerMap[ownerMap.keyAt(i)] = ownerMap.valueAt(i).asString();
			}
		}
	}

	std::string rename(const std::string &name, Int copy) const
	{
		if (name.empty()) return name;
		if (!m_auto) {
			std::string out = name;
			for (size_t i = 0; i < m_pairs.size(); i++) {
				size_t pos = 0;
				while (!m_pairs[i].first.empty() && (pos = out.find(m_pairs[i].first, pos)) != std::string::npos) {
					out.replace(pos, m_pairs[i].first.size(), m_pairs[i].second);
					pos += m_pairs[i].second.size();
				}
			}
			return out;
		}
		static const std::regex patterns[] = {
			std::regex("(Player_)(\\d+)", std::regex::icase),
			std::regex("^(P)(\\d+)(?=_)", std::regex::icase),
			std::regex("(Perimeter)(\\d+)", std::regex::icase),
			std::regex("(player)(\\d+)", std::regex::icase),
		};
		std::string out = name;
		for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); p++) {
			std::string result;
			std::string::const_iterator last = out.begin();
			for (std::sregex_iterator it(out.begin(), out.end(), patterns[p]), end; it != end; ++it) {
				const std::smatch &m = *it;
				result.append(last, m[0].first);
				result += m[1].str();
				result += renumber(m[2].str(), copy);
				last = m[0].second;
			}
			result.append(last, out.cend());
			out = result;
		}
		return out;
	}

	/// Owner team for a copy; falls back to the original when the renamed team does not exist.
	std::string owner(const std::string &original, Int copy, std::set<std::string> *unresolved) const
	{
		std::map<std::string, std::string>::const_iterator it = m_ownerMap.find(original);
		std::string mapped = it != m_ownerMap.end() ? it->second : rename(original, copy);
		if (mapped == original) return original;
		Int ndx;
		if (TheSidesList->findTeamInfo(AsciiString(mapped.c_str()), &ndx)) return mapped;
		unresolved->insert(mapped);
		return original;
	}

private:
	std::string renumber(const std::string &digits, Int copy) const
	{
		const Int n = atoi(digits.c_str());
		if (n < 1 || n > m_numCopies) return digits;
		const Int mapped = ((n - 1 + copy) % m_numCopies) + 1;
		char buf[32];
		sprintf(buf, "%0*d", (int)digits.size(), mapped);
		return buf;
	}

	Int m_numCopies;
	bool m_auto;
	std::vector<std::pair<std::string, std::string> > m_pairs;
	std::map<std::string, std::string> m_ownerMap;
};

std::string makeUnique(const std::string &name, std::set<std::string> &used)
{
	if (name.empty()) return name;
	std::string candidate = name;
	for (Int i = 2; used.count(candidate); i++) {
		char buf[16];
		sprintf(buf, "_%d", i);
		candidate = name + buf;
	}
	used.insert(candidate);
	return candidate;
}

struct Includes
{
	bool heights, textures, passability, objects, waypoints, areas;

	void parse(const McpJson &args)
	{
		const McpJson &inc = args.get("include");
		heights = textures = passability = objects = waypoints = areas = true;
		if (inc.isNull()) return;
		if (!inc.isObject()) mcpFail("include must be an object of booleans");
		heights = mcpArgBool(inc, "heights", true);
		textures = mcpArgBool(inc, "textures", true);
		passability = mcpArgBool(inc, "passability", true);
		objects = mcpArgBool(inc, "objects", true);
		waypoints = mcpArgBool(inc, "waypoints", true);
		areas = mcpArgBool(inc, "areas", true);
	}
};

/// Transforms the heightmap into a copy and adds it to the undo step. The copy is refreshed in the views.
void addHeightMapStep(CWorldBuilderDoc *doc, WorldHeightMapEdit *map, const HeightMapTransform &xf,
	const Includes &inc, MultipleUndoable *undo, WorldHeightMapEdit **outCopy)
{
	*outCopy = nullptr;
	if (!inc.heights && !inc.textures && !inc.passability) return;
	WorldHeightMapEdit *copy = map->duplicate();
	copy->copyTransformedFrom(map, xf, inc.heights, inc.textures, inc.passability);
	if (inc.textures) copy->optimizeTiles();
	IRegion2D all = { 0, 0, 0, 0 };
	doc->updateHeightMap(copy, false, all);
	undo->addUndoable(new WBDocUndoable(doc, copy));
	*outCopy = copy;
}

void finishHeightMapStep(CWorldBuilderDoc *doc, WorldHeightMapEdit *copy, const Includes &inc)
{
	if (copy == nullptr) return;
	REF_PTR_RELEASE(copy);
	if (inc.textures) TerrainMaterial::updateTextures(doc->GetHeightMap());
}

bool isRoadStart(MapObject *obj)
{
	return (obj->getFlag(FLAG_ROAD_POINT1) || obj->getFlag(FLAG_BRIDGE_POINT1)) && obj->getNext() != nullptr;
}

//-------------------------------------------------------------------------------------------------
// map.symmetrize
//-------------------------------------------------------------------------------------------------

McpJson cmdSymmetrize(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	Frame frame;
	frame.init(map);
	Symmetry sym;
	sym.parse(mcpArgString(args, "mode"), mcpArgString(args, "source"), frame.width == frame.height);
	Includes inc;
	inc.parse(args);
	Renamer renamer;
	renamer.parse(args, (Int)sym.copies.size());
	const double tolerance = mcpArgNumber(args, "axis_tolerance", 5.0) / MAP_XY_FACTOR;
	const bool skipDefaultWater = mcpArgBool(args, "skip_default_water", true);
	const Int numCopies = (Int)sym.copies.size();

	// Classify objects: source objects are copied, target objects deleted, axis objects kept.
	std::vector<MapObject *> toDelete;
	std::vector<std::vector<MapObject *> > sourceGroups;	// single objects or road pairs
	std::set<std::string> usedWaypointNames, usedObjectNames;
	std::set<Int> deletedWaypointIds, copiedWaypointIds;
	Int keptOnAxis = 0;
	for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
		std::vector<MapObject *> group(1, obj);
		double wx = obj->getLocation()->x, wy = obj->getLocation()->y;
		if (isRoadStart(obj)) {
			MapObject *second = obj->getNext();
			group.push_back(second);
			wx = (wx + second->getLocation()->x) / 2;
			wy = (wy + second->getLocation()->y) / 2;
			obj = second;
		}
		const bool isWaypoint = group[0]->isWaypoint();
		const bool enabled = isWaypoint ? inc.waypoints : inc.objects;
		double px, py;
		frame.worldToFrame(wx, wy, &px, &py);
		bool deleted = false;
		if (!enabled || sym.isOnAxis(px, py, tolerance)) {
			keptOnAxis += enabled ? 1 : 0;
		} else if (sym.inSource(px, py)) {
			sourceGroups.push_back(group);
			if (isWaypoint) copiedWaypointIds.insert(group[0]->getWaypointID());
		} else {
			for (size_t i = 0; i < group.size(); i++) toDelete.push_back(group[i]);
			if (isWaypoint) deletedWaypointIds.insert(group[0]->getWaypointID());
			deleted = true;
		}
		if (!deleted) {
			for (size_t i = 0; i < group.size(); i++) {
				if (group[i]->isWaypoint()) usedWaypointNames.insert(group[i]->getWaypointName().str());
				AsciiString name = group[i]->getProperties()->getAsciiString(TheKey_objectName);
				if (!name.isEmpty()) usedObjectNames.insert(name.str());
			}
		}
	}

	// Build the copies as one linked list, keeping road pairs adjacent.
	std::set<std::string> unresolvedOwners;
	McpJson renamed = McpJson::makeArray();
	std::vector<std::map<Int, Int> > waypointIdMap(numCopies);
	MapObject *head = nullptr, *tail = nullptr;
	Int objectsCopied = 0;
	for (Int k = 1; k < numCopies; k++) {
		for (size_t g = 0; g < sourceGroups.size(); g++) {
			for (size_t i = 0; i < sourceGroups[g].size(); i++) {
				MapObject *src = sourceGroups[g][i];
				MapObject *dup = src->duplicate();
				dup->setNextMap(nullptr);
				dup->setSelected(false);
				Coord3D loc;
				Real angle;
				transformObject(src, frame, sym.copies[k], &loc, &angle);
				dup->setLocation(&loc);
				dup->setAngle(angle);
				Dict *props = dup->getProperties();
				props->remove(TheKey_uniqueID);	// validate() assigns a fresh one.
				const std::string owner = props->getAsciiString(TheKey_originalOwner).str();
				props->setAsciiString(TheKey_originalOwner, AsciiString(renamer.owner(owner, k, &unresolvedOwners).c_str()));
				AsciiString objName = props->getAsciiString(TheKey_objectName);
				if (!objName.isEmpty()) {
					const std::string newName = makeUnique(renamer.rename(objName.str(), k), usedObjectNames);
					props->setAsciiString(TheKey_objectName, AsciiString(newName.c_str()));
				}
				if (src->isWaypoint()) {
					const Int newId = doc->getNextWaypointID();
					waypointIdMap[k][src->getWaypointID()] = newId;
					dup->setWaypointID(newId);
					const std::string oldName = src->getWaypointName().str();
					const std::string newName = makeUnique(renamer.rename(oldName, k), usedWaypointNames);
					dup->setWaypointName(AsciiString(newName.c_str()));
					if (renamed.size() < 100) {
						McpJson pair = McpJson::makeArray();
						pair.push(McpJson(oldName));
						pair.push(McpJson(newName));
						renamed.push(pair);
					}
				}
				if (tail) tail->setNextMap(dup); else head = dup;
				tail = dup;
				objectsCopied++;
			}
		}
	}

	// Waypoint links: drop links of deleted waypoints, recreate source links between the copies.
	WaypointLinksUndoable *links = new WaypointLinksUndoable(doc);
	for (Int i = 0; i < doc->getNumWaypointLinks(); i++) {
		Int a, b;
		doc->getWaypointLink(i, &a, &b);
		if (deletedWaypointIds.count(a) || deletedWaypointIds.count(b)) {
			links->removed.push_back(std::make_pair(a, b));
			continue;
		}
		if (!copiedWaypointIds.count(a) && !copiedWaypointIds.count(b)) continue;
		for (Int k = 1; k < numCopies; k++) {
			const Int ma = copiedWaypointIds.count(a) ? waypointIdMap[k][a] : a;
			const Int mb = copiedWaypointIds.count(b) ? waypointIdMap[k][b] : b;
			links->added.push_back(std::make_pair(ma, mb));
		}
	}

	// Areas.
	std::vector<PolygonTrigger *> polysToDelete, polysToAdd;
	std::set<std::string> usedPolyNames;
	if (inc.areas) {
		std::vector<PolygonTrigger *> sourcePolys;
		for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
			if (trig->getNumPoints() == 0 || (skipDefaultWater && isDefaultWater(trig))) {
				usedPolyNames.insert(trig->getTriggerName().str());
				continue;
			}
			double px, py;
			polygonCentroid(trig, frame, &px, &py);
			if (sym.isOnAxis(px, py, tolerance)) {
				usedPolyNames.insert(trig->getTriggerName().str());
			} else if (sym.inSource(px, py)) {
				sourcePolys.push_back(trig);
				usedPolyNames.insert(trig->getTriggerName().str());
			} else {
				polysToDelete.push_back(trig);
			}
		}
		for (Int k = 1; k < numCopies; k++) {
			for (size_t i = 0; i < sourcePolys.size(); i++) {
				PolygonTrigger *src = sourcePolys[i];
				PolygonTrigger *copy = newInstance(PolygonTrigger)(src->getNumPoints());
				for (Int p = 0; p < src->getNumPoints(); p++) {
					copy->addPoint(transformPoint(*src->getPoint(p), frame, sym.copies[k]));
				}
				copy->setTriggerName(AsciiString(makeUnique(renamer.rename(src->getTriggerName().str(), k), usedPolyNames).c_str()));
				copy->setLayerName(src->getLayerName());
				copy->setWaterArea(src->isWaterArea());
				copy->setRiver(src->isRiver());
				copy->setRiverStart(src->getRiverStart());
				copy->setDoExportWithScripts(src->doExportWithScripts());
				polysToAdd.push_back(copy);
			}
		}
	}

	// Assemble one undo step. MultipleUndoable runs the last added step first.
	MultipleUndoable *undo = new MultipleUndoable;
	undo->addUndoable(links);
	for (size_t i = 0; i < polysToAdd.size(); i++) undo->addUndoable(new AddPolygonUndoable(polysToAdd[i]));
	for (size_t i = 0; i < polysToDelete.size(); i++) undo->addUndoable(new DeletePolygonUndoable(polysToDelete[i]));
	if (head) undo->addUndoable(new AddObjectUndoable(doc, head));
	if (!toDelete.empty()) {
		PointerTool::clearSelection();
		for (size_t i = 0; i < toDelete.size(); i++) toDelete[i]->setSelected(true);
		undo->addUndoable(new DeleteObjectUndoable(doc));
		PointerTool::clearSelection();
	}
	SymmetryTransform xf(frame, sym);
	WorldHeightMapEdit *copy = nullptr;
	addHeightMapStep(doc, map, xf, inc, undo, &copy);
	mcpCommit(undo);
	finishHeightMapStep(doc, copy, inc);
	mcpResetObjectHandles();

	McpJson result = McpJson::makeObject();
	result.set("mode", sym.mode).set("source", sym.source).set("copies", numCopies - 1);
	result.set("objects_copied", objectsCopied).set("objects_deleted", (int)toDelete.size()).set("objects_kept_on_axis", keptOnAxis);
	result.set("waypoint_links_added", (int)links->added.size()).set("waypoint_links_removed", (int)links->removed.size());
	result.set("areas_copied", (int)polysToAdd.size()).set("areas_deleted", (int)polysToDelete.size());
	result.set("waypoints_renamed", renamed);
	McpJson unresolved = McpJson::makeArray();
	for (std::set<std::string>::const_iterator it = unresolvedOwners.begin(); it != unresolvedOwners.end(); ++it) {
		unresolved.push(McpJson(*it));
	}
	result.set("unresolved_owners", unresolved);
	return result;
}

//-------------------------------------------------------------------------------------------------
// map.transform
//-------------------------------------------------------------------------------------------------

McpJson cmdTransform(const McpJson &args)
{
	CWorldBuilderDoc *doc = mcpDoc();
	WorldHeightMapEdit *map = mcpHeightMap();
	Frame frame;
	frame.init(map);
	const std::string op = mcpArgString(args, "op");
	Mat2 m = MAT_IDENTITY;
	if (op == "mirror_x") m = MAT_MIRROR_X;
	else if (op == "mirror_y") m = MAT_MIRROR_Y;
	else if (op == "rotate_90") m = MAT_ROTATE_90;
	else if (op == "rotate_180") m = MAT_ROTATE_180;
	else if (op == "rotate_270") m = MAT_ROTATE_270;
	else if (op == "mirror_diag") m = MAT_MIRROR_DIAG;
	else if (op == "mirror_antidiag") m = MAT_MIRROR_ANTIDIAG;
	else mcpFail("op must be mirror_x, mirror_y, rotate_90, rotate_180, rotate_270, mirror_diag or mirror_antidiag");
	if (m.swapsAxes() && frame.width != frame.height) {
		mcpFail("%s needs a square map", op.c_str());
	}
	Includes inc;
	inc.parse(args);

	MultipleUndoable *undo = new MultipleUndoable;
	Int objectsMoved = 0, areasMoved = 0;
	if (inc.areas) {
		PolygonPointsUndoable *polys = new PolygonPointsUndoable;
		for (PolygonTrigger *trig = PolygonTrigger::getFirstPolygonTrigger(); trig; trig = trig->getNext()) {
			std::vector<ICoord3D> pts;
			for (Int i = 0; i < trig->getNumPoints(); i++) {
				pts.push_back(transformPoint(*trig->getPoint(i), frame, m));
			}
			polys->add(trig, pts);
			areasMoved++;
		}
		undo->addUndoable(polys);
	}
	if (inc.objects || inc.waypoints) {
		MoveObjectsUndoable *moves = new MoveObjectsUndoable(doc);
		for (MapObject *obj = MapObject::getFirstMapObject(); obj; obj = obj->getNext()) {
			if (obj->isWaypoint() ? !inc.waypoints : !inc.objects) continue;
			Coord3D loc;
			Real angle;
			transformObject(obj, frame, m, &loc, &angle);
			moves->add(obj, loc, angle);
			objectsMoved++;
		}
		undo->addUndoable(moves);
	}
	WholeMapTransform xf(frame, m);
	WorldHeightMapEdit *copy = nullptr;
	addHeightMapStep(doc, map, xf, inc, undo, &copy);
	mcpCommit(undo);
	finishHeightMapStep(doc, copy, inc);

	return McpJson::makeObject().set("op", op).set("objects_moved", objectsMoved).set("areas_moved", areasMoved);
}

} // namespace

void mcpRegisterSymmetryCommands()
{
	mcpRegisterCommand("map.transform", cmdTransform, "{op:mirror_x|mirror_y|rotate_90|rotate_180|rotate_270|mirror_diag|mirror_antidiag, include?} Mirrors or rotates the whole map.");
	mcpRegisterCommand("map.symmetrize", cmdSymmetrize, "{mode:mirror_x|mirror_y|rotate_180|mirror_diag|mirror_antidiag|rotate_quarters, source, rename?, owner_map?, include?, axis_tolerance?, skip_default_water?} Copies one part of the map onto the rest.");
}
