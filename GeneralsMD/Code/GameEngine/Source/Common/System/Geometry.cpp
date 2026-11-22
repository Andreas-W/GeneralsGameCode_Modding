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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: Geometry.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, Aug 2002
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_GEOMETRY_NAMES

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/Geometry.h"
#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryType( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_type = (GeometryType)INI::scanIndexList(ini->getNextToken(), GeometryNames);
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryIsSmall( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_isSmall = INI::scanBool(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryHeight( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_height = INI::scanReal(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryMajorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_majorRadius = INI::scanReal(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryMinorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	((GeometryInfo*)store)->m_minorRadius = INI::scanReal(ini->getNextToken());
	((GeometryInfo*)store)->calcBoundingStuff();
}

//=============================================================================
static const FieldParse s_geometryShapeFieldParse[] =
{
	{ "Type",					INI::parseIndexList,	GeometryNames,	offsetof( GeometryShape, m_type ) },
	{ "MajorRadius",	INI::parseReal,				nullptr,				offsetof( GeometryShape, m_majorRadius ) },
	{ "MinorRadius",	INI::parseReal,				nullptr,				offsetof( GeometryShape, m_minorRadius ) },
	{ "Height",				INI::parseReal,				nullptr,				offsetof( GeometryShape, m_height ) },
	{ "Offset",				INI::parseCoord3D,		nullptr,				offsetof( GeometryShape, m_offset ) },
	{ "Angle",				INI::parseAngleReal,	nullptr,				offsetof( GeometryShape, m_angle ) },
	{ nullptr, nullptr, nullptr, 0 }
};

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryShape( INI* ini, GeometryShape& shape )
{
	static_assert(sizeof(GeometryType) == sizeof(Int), "parseIndexList stores an Int");
	shape.clear();
	ini->initFromINI(&shape, s_geometryShapeFieldParse);
	shape.normalize();
}

//=============================================================================
/*static*/ void GeometryInfo::parseAdditionalGeometryInto( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	GeometryShape shape;
	parseGeometryShape(ini, shape);
	if (!((GeometryInfo*)store)->addShape(shape))
	{
		DEBUG_CRASH(("A geometry has more than %d shapes", (Int)MAX_GEOMETRY_SHAPES));
		throw INI_INVALID_DATA;
	}
}

//=============================================================================
static const FieldParse s_geometryBlockFieldParse[] =
{
	{ "Geometry",							GeometryInfo::parseGeometryType,						nullptr, 0 },
	{ "GeometryMajorRadius",	GeometryInfo::parseGeometryMajorRadius,			nullptr, 0 },
	{ "GeometryMinorRadius",	GeometryInfo::parseGeometryMinorRadius,			nullptr, 0 },
	{ "GeometryHeight",				GeometryInfo::parseGeometryHeight,					nullptr, 0 },
	{ "GeometryIsSmall",			GeometryInfo::parseGeometryIsSmall,					nullptr, 0 },
	{ "AdditionalGeometry",		GeometryInfo::parseAdditionalGeometryInto,	nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

//=============================================================================
/*static*/ void GeometryInfo::parseGeometryBlock( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	GeometryInfo* geom = (GeometryInfo*)store;
	geom->clearExtraShapes();
	geom->set(GEOMETRY_SPHERE, FALSE, 0.0f, 0.0f, 0.0f);
	ini->initFromINI(geom, s_geometryBlockFieldParse);
}

//=============================================================================
void GeometryShape::clear()
{
	m_type = GEOMETRY_BOX;
	m_height = 0.0f;
	m_majorRadius = 0.0f;
	m_minorRadius = 0.0f;
	m_offset.zero();
	m_angle = 0.0f;
}

//=============================================================================
void GeometryShape::normalize()
{
	switch (m_type)
	{
		case GEOMETRY_SPHERE:
			m_minorRadius = m_majorRadius;
			m_height = m_majorRadius;
			break;
		case GEOMETRY_CYLINDER:
			m_minorRadius = m_majorRadius;
			break;
		default:
			break;
	}
}

//=============================================================================
Real GeometryShape::getBoundingCircleRadius() const
{
	if (m_type == GEOMETRY_BOX)
		return sqrt(sqr(m_majorRadius) + sqr(m_minorRadius));
	return m_majorRadius;
}

//=============================================================================
Real GeometryShape::getBoundingSphereRadius() const
{
	switch (m_type)
	{
		case GEOMETRY_SPHERE:
			return m_majorRadius;
		case GEOMETRY_CYLINDER:
			return (m_height * 0.5f > m_majorRadius) ? m_height * 0.5f : m_majorRadius;
		default:
			return sqrt(sqr(m_majorRadius) + sqr(m_minorRadius) + sqr(m_height * 0.5f));
	}
}

//=============================================================================
Real GeometryPrimitive::getBoundingCircleRadius() const
{
	if (type == GEOMETRY_BOX)
		return sqrt(sqr(majorRadius) + sqr(minorRadius));
	return majorRadius;
}

//=============================================================================
// squared distance in the xy plane from 'pt' to the footprint, 0 inside
Real GeometryPrimitive::getDistanceSqToPoint2D(const Coord3D& pt) const
{
	Real dx = pt.x - position.x;
	Real dy = pt.y - position.y;
	if (type == GEOMETRY_BOX)
	{
		Real c = (Real)Cos(-angle);
		Real s = (Real)Sin(-angle);
		Real lx = (Real)fabs(dx*c - dy*s) - majorRadius;
		Real ly = (Real)fabs(dx*s + dy*c) - minorRadius;
		if (lx < 0.0f) lx = 0.0f;
		if (ly < 0.0f) ly = 0.0f;
		return sqr(lx) + sqr(ly);
	}
	Real d = sqrt(sqr(dx) + sqr(dy)) - majorRadius;
	return (d > 0.0f) ? sqr(d) : 0.0f;
}

//=============================================================================
Real GeometryPrimitive::getDistanceSqToPoint3D(const Coord3D& pt) const
{
	if (type == GEOMETRY_SPHERE)
	{
		Real d = sqrt(sqr(pt.x - position.x) + sqr(pt.y - position.y) + sqr(pt.z - position.z)) - majorRadius;
		return (d > 0.0f) ? sqr(d) : 0.0f;
	}
	Real dz = 0.0f;
	if (pt.z < position.z)
		dz = position.z - pt.z;
	else if (pt.z > position.z + height)
		dz = pt.z - (position.z + height);
	return getDistanceSqToPoint2D(pt) + sqr(dz);
}

//=============================================================================
Bool GeometryPrimitive::isPointInFootprint2D(const Coord3D& pt) const
{
	return getDistanceSqToPoint2D(pt) <= 0.0f;
}

//=============================================================================
void GeometryInfo::setExtraShapes(GeometryShapeList *list)
{
	if (list)
		++list->m_refCount;
	if (m_extraShapes && --m_extraShapes->m_refCount == 0)
		delete m_extraShapes;
	m_extraShapes = list;
}

//=============================================================================
GeometryInfo& GeometryInfo::operator=(const GeometryInfo& that)
{
	m_type = that.m_type;
	m_isSmall = that.m_isSmall;
	m_height = that.m_height;
	m_majorRadius = that.m_majorRadius;
	m_minorRadius = that.m_minorRadius;
	m_boundingCircleRadius = that.m_boundingCircleRadius;
	m_boundingSphereRadius = that.m_boundingSphereRadius;
	setExtraShapes(that.m_extraShapes);
	return *this;
}

//=============================================================================
Bool GeometryInfo::addShape(const GeometryShape& shape)
{
	Int count = getExtraShapeCount();
	if (count >= MAX_GEOMETRY_SHAPES - 1)
		return false;

	// the list may be shared, so the new shape goes into a new list
	GeometryShapeList *list = NEW GeometryShapeList;
	list->m_refCount = 0;
	list->m_count = count + 1;
	for (Int i = 0; i < count; ++i)
		list->m_shapes[i] = m_extraShapes->m_shapes[i];
	list->m_shapes[count] = shape;
	setExtraShapes(list);

	// a compound geometry covers more than one partition cell
	m_isSmall = false;
	calcBoundingStuff();
	return true;
}

//=============================================================================
void GeometryInfo::clearExtraShapes()
{
	setExtraShapes(nullptr);
	calcBoundingStuff();
}

//=============================================================================
/** Offsets are scaled per object axis. A box keeps its edges on the object axes when its angle is
	* a multiple of 90 degrees (so x/y scale its radii, swapped at 90 and 270); other boxes and the
	* radius of spheres and cylinders scale by sqrt(scaleX * scaleY). Heights scale by scaleZ. */
void GeometryInfo::setScaledExtraShapes(const GeometryInfo& base, Real scaleX, Real scaleY, Real scaleZ)
{
	Int count = base.getExtraShapeCount();
	if (count == 0)
	{
		clearExtraShapes();
		return;
	}

	Real scaleXY = (Real)sqrt(fabs(scaleX * scaleY));
	GeometryShape shapes[MAX_GEOMETRY_SHAPES - 1];
	for (Int i = 0; i < count; ++i)
	{
		GeometryShape& s = shapes[i];
		s = base.m_extraShapes->m_shapes[i];
		s.m_offset.x *= scaleX;
		s.m_offset.y *= scaleY;
		s.m_offset.z *= scaleZ;
		s.m_height *= scaleZ;
		if (s.m_type == GEOMETRY_BOX)
		{
			Real c = (Real)fabs(Cos(s.m_angle));
			Real sn = (Real)fabs(Sin(s.m_angle));
			if (sn < 0.01f)
			{
				s.m_majorRadius *= scaleX;
				s.m_minorRadius *= scaleY;
			}
			else if (c < 0.01f)
			{
				s.m_majorRadius *= scaleY;
				s.m_minorRadius *= scaleX;
			}
			else
			{
				s.m_majorRadius *= scaleXY;
				s.m_minorRadius *= scaleXY;
			}
		}
		else
		{
			s.m_majorRadius *= scaleXY;
		}
		s.normalize();
	}

	setLoadedExtraShapes(shapes, count);
	m_isSmall = false;
	calcBoundingStuff();
}

//=============================================================================
Int GeometryInfo::getWorldPrimitives(const Coord3D& pos, Real angle, GeometryPrimitive *out) const
{
	getPrimaryPrimitive(pos, angle, out[0]);
	Int count = getExtraShapeCount();
	if (count == 0)
		return 1;

	Real c = (Real)Cos(angle);
	Real s = (Real)Sin(angle);
	for (Int i = 0; i < count; ++i)
	{
		const GeometryShape& shape = m_extraShapes->m_shapes[i];
		GeometryPrimitive& prim = out[i + 1];
		prim.type = shape.m_type;
		prim.height = shape.m_height;
		prim.majorRadius = shape.m_majorRadius;
		prim.minorRadius = shape.m_minorRadius;
		prim.position.x = pos.x + shape.m_offset.x*c - shape.m_offset.y*s;
		prim.position.y = pos.y + shape.m_offset.x*s + shape.m_offset.y*c;
		prim.position.z = pos.z + shape.m_offset.z;
		prim.angle = angle + shape.m_angle;
	}
	return count + 1;
}

//=============================================================================
void GeometryInfo::getWorldPrimitive(Int i, const Coord3D& pos, Real angle, GeometryPrimitive& out) const
{
	if (i <= 0)
	{
		getPrimaryPrimitive(pos, angle, out);
		return;
	}

	DEBUG_ASSERTCRASH(i <= getExtraShapeCount(), ("geometry shape index out of range"));
	const GeometryShape& shape = m_extraShapes->m_shapes[i - 1];
	Real c = (Real)Cos(angle);
	Real s = (Real)Sin(angle);
	out.type = shape.m_type;
	out.height = shape.m_height;
	out.majorRadius = shape.m_majorRadius;
	out.minorRadius = shape.m_minorRadius;
	out.position.x = pos.x + shape.m_offset.x*c - shape.m_offset.y*s;
	out.position.y = pos.y + shape.m_offset.x*s + shape.m_offset.y*c;
	out.position.z = pos.z + shape.m_offset.z;
	out.angle = angle + shape.m_angle;
}

//=============================================================================
Real GeometryInfo::getDistanceSqToPoint2D(const Coord3D& pos, Real angle, const Coord3D& pt) const
{
	GeometryPrimitive prims[MAX_GEOMETRY_SHAPES];
	Int count = getWorldPrimitives(pos, angle, prims);
	Real best = 0.0f;
	for (Int i = 0; i < count; ++i)
	{
		Real d = prims[i].getDistanceSqToPoint2D(pt);
		if (i == 0 || d < best)
			best = d;
	}
	return best;
}

//=============================================================================
Real GeometryInfo::getDistanceSqToPoint3D(const Coord3D& pos, Real angle, const Coord3D& pt) const
{
	GeometryPrimitive prims[MAX_GEOMETRY_SHAPES];
	Int count = getWorldPrimitives(pos, angle, prims);
	Real best = 0.0f;
	for (Int i = 0; i < count; ++i)
	{
		Real d = prims[i].getDistanceSqToPoint3D(pt);
		if (i == 0 || d < best)
			best = d;
	}
	return best;
}

//-----------------------------------------------------------------------------
void GeometryInfo::set(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius)
{
	m_type = type;
	m_isSmall = isSmall;

	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			m_majorRadius = majorRadius;
			m_minorRadius = majorRadius;
			m_height = majorRadius;
			break;

		case GEOMETRY_CYLINDER:
			m_majorRadius = majorRadius;
			m_minorRadius = majorRadius;
			m_height = height;
			break;

		case GEOMETRY_BOX:
			m_majorRadius = majorRadius;
			m_minorRadius = minorRadius;
			m_height = height;
			break;
	};

	if (isCompound())
		m_isSmall = false;

	calcBoundingStuff();
}

//-----------------------------------------------------------------------------
static Real calcDotProduct(const Coord3D& a, const Coord3D& b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

//-----------------------------------------------------------------------------
static Real calcDistSquared(const Coord3D& a, const Coord3D& b)
{
	return sqr(a.x - b.x) + sqr(a.y - b.y) + sqr(a.z - b.z);
}

//-----------------------------------------------------------------------------
static Real calcPointToLineDistSquared(const Coord3D& pt, const Coord3D& lineStart, const Coord3D& lineEnd)
{
	Coord3D line, lineToPt, closest;

	line.x = lineEnd.x - lineStart.x;
	line.y = lineEnd.y - lineStart.y;
	line.z = lineEnd.z - lineStart.z;

	lineToPt.x = pt.x - lineStart.x;
	lineToPt.y = pt.y - lineStart.y;
	lineToPt.z = pt.z - lineStart.z;

	Real dot = calcDotProduct(lineToPt, line);
	if (dot <= 0.0f)
	{
		// angle is obtuse
		return calcDistSquared(pt, lineStart);
	}

	Real lineLenSqr = calcDistSquared(lineStart, lineEnd);
	DEBUG_ASSERTCRASH(lineLenSqr==calcDotProduct(line,line),("hmm"));
	if (lineLenSqr <= dot)
	{
		return calcDistSquared(pt, lineEnd);
	}

  Real tmp = dot / lineLenSqr;

	closest.x = lineStart.x + tmp * line.x;
	closest.y = lineStart.y + tmp * line.y;
	closest.z = lineStart.z + tmp * line.z;

  return calcDistSquared(pt, closest);
}

//=============================================================================
Bool GeometryInfo::isIntersectedByLineSegment(const Coord3D& loc, const Coord3D& from, const Coord3D& to) const
{
	DEBUG_CRASH(("this call does not work properly for nonspheres yet. use with caution."));

	/// @todo srj -- treats everything as a sphere for now. fix.
	Real distSquared = calcPointToLineDistSquared(loc, from, to);
	return distSquared <= sqr(getBoundingSphereRadius());
}

//=============================================================================
// given an object with this geom, located at 'pos', and another obj with the given
// pos & geom, calc the min and max pitches from this to that.
void GeometryInfo::calcPitches(const Coord3D& thisPos, const GeometryInfo& that, const Coord3D& thatPos,
	Real& minPitch, Real& maxPitch) const
{
	Coord3D thisCenter;
	getCenterPosition(thisPos, thisCenter);

	Real dxy = sqrt(sqr(thatPos.x - thisCenter.x) + sqr(thatPos.y - thisCenter.y));

	Real dz;

	/** @todo srj -- this could be better, by calcing it for all the corners, not just top-center
		and bottom-center... oh well */
	dz = (thatPos.z + that.getMaxHeightAbovePosition()) - thisCenter.z;
	maxPitch = atan2(dz, dxy);

	dz = (thatPos.z - that.getMaxHeightBelowPosition()) - thisCenter.z;
	minPitch = atan2(dz, dxy);
}

//=============================================================================
// given an object with this geom, SET how far above the object's canonical position its max z should extend.
void GeometryInfo::setMaxHeightAbovePosition(Real z)
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			m_majorRadius = z;
			break;

		case GEOMETRY_BOX:
		case GEOMETRY_CYLINDER:
			m_height = z;
			break;
	};

	if (m_extraShapes)
	{
		Int count = m_extraShapes->m_count;
		GeometryShape shapes[MAX_GEOMETRY_SHAPES - 1];
		for (Int i = 0; i < count; ++i)
		{
			GeometryShape& s = shapes[i];
			s = m_extraShapes->m_shapes[i];
			if (s.m_offset.z + s.getMaxHeightAbovePosition() > z)
			{
				Real h = z - s.m_offset.z;
				if (h < 0.0f)
					h = 0.0f;
				if (s.m_type == GEOMETRY_SPHERE)
					s.m_majorRadius = h;
				else
					s.m_height = h;
				s.normalize();
			}
		}
		// keeps the list when nothing was cut
		setLoadedExtraShapes(shapes, count);
	}

	calcBoundingStuff();
}

//=============================================================================
// given an object with this geom, how far above the object's canonical position does its max z extend?
Real GeometryInfo::getMaxHeightAbovePosition() const
{
	Real result = 0.0f;
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			result = m_majorRadius;
			break;

		case GEOMETRY_BOX:
		case GEOMETRY_CYLINDER:
			result = m_height;
			break;
	};

	for (Int i = 0; i < getExtraShapeCount(); ++i)
	{
		const GeometryShape& shape = m_extraShapes->m_shapes[i];
		Real z = shape.m_offset.z + shape.getMaxHeightAbovePosition();
		if (z > result)
			result = z;
	}

	return result;
}

//=============================================================================
// given an object with this geom, how far below the object's canonical position does its max z extend?
Real GeometryInfo::getMaxHeightBelowPosition() const
{
	Real result = 0.0f;
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
			result = m_majorRadius;
			break;

		case GEOMETRY_BOX:
		case GEOMETRY_CYLINDER:
			result = 0.0f;
			break;
	};

	for (Int i = 0; i < getExtraShapeCount(); ++i)
	{
		const GeometryShape& shape = m_extraShapes->m_shapes[i];
		Real z = shape.getMaxHeightBelowPosition() - shape.m_offset.z;
		if (z > result)
			result = z;
	}

	return result;
}

//=============================================================================
// given an object with this geom, located at 'pos', where is the "center" of the geometry?
Real GeometryInfo::getZDeltaToCenterPosition() const
{
	return (m_type == GEOMETRY_SPHERE) ? 0.0f : (m_height * 0.5f);
}

//=============================================================================
// given an object with this geom, located at 'pos', where is the "center" of the geometry?
void GeometryInfo::getCenterPosition(const Coord3D& pos, Coord3D& center) const
{
	center = pos;
	center.z += getZDeltaToCenterPosition();
}

//=============================================================================
void GeometryInfo::expandFootprint(Real radius)
{
	m_majorRadius += radius;
	m_minorRadius += radius;
	calcBoundingStuff();
}

//=============================================================================
static void calcPrimitive2DBounds(GeometryType m_type, Real m_majorRadius, Real m_minorRadius,
	const Coord3D& geomCenter, Real angle, Region2D& bounds)
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			bounds.lo.x = geomCenter.x - m_majorRadius;
			bounds.lo.y = geomCenter.y - m_majorRadius;
			bounds.hi.x = geomCenter.x + m_majorRadius;
			bounds.hi.y = geomCenter.y + m_majorRadius;
			break;
		}

		case GEOMETRY_BOX:
		{
			Real c = (Real)cos(angle);
			Real s = (Real)sin(angle);
			Real exc = m_majorRadius*c;
			Real eyc = m_minorRadius*c;
			Real exs = m_majorRadius*s;
			Real eys = m_minorRadius*s;
			Real x,y;
			x = geomCenter.x - exc - eys;
			y = geomCenter.y + eyc - exs;
			bounds.lo.x = x;
			bounds.lo.y = y;
			bounds.hi.x = x;
			bounds.hi.y = y;

			x = geomCenter.x + exc - eys;
			y = geomCenter.y + eyc + exs;
			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			x = geomCenter.x + exc + eys;
			y = geomCenter.y - eyc + exs;
			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			x = geomCenter.x - exc + eys;
			y = geomCenter.y - eyc - exs;
 			if (bounds.lo.x > x) bounds.lo.x = x;
			if (bounds.lo.y > y) bounds.lo.y = y;
			if (bounds.hi.x < x) bounds.hi.x = x;
			if (bounds.hi.y < y) bounds.hi.y = y;

			break;
		}
	}
}

//=============================================================================
void GeometryInfo::get2DBounds(const Coord3D& geomCenter, Real angle, Region2D& bounds) const
{
	calcPrimitive2DBounds(m_type, m_majorRadius, m_minorRadius, geomCenter, angle, bounds);

	if (!isCompound())
		return;
	GeometryPrimitive prims[MAX_GEOMETRY_SHAPES];
	Int count = getWorldPrimitives(geomCenter, angle, prims);
	for (Int i = 1; i < count; ++i)
	{
		const GeometryPrimitive& prim = prims[i];
		Region2D b;
		calcPrimitive2DBounds(prim.type, prim.majorRadius, prim.minorRadius, prim.position, prim.angle, b);
		if (b.lo.x < bounds.lo.x) bounds.lo.x = b.lo.x;
		if (b.lo.y < bounds.lo.y) bounds.lo.y = b.lo.y;
		if (b.hi.x > bounds.hi.x) bounds.hi.x = b.hi.x;
		if (b.hi.y > bounds.hi.y) bounds.hi.y = b.hi.y;
	}
}

//=============================================================================
void GeometryInfo::clipPointToFootprint(const Coord3D& geomCenter, Coord3D& ptToClip) const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			Real dx = ptToClip.x - geomCenter.x;
			Real dy = ptToClip.y - geomCenter.y;
			Real radius = sqrt(sqr(dx) + sqr(dy));
			if (radius > m_majorRadius)
			{
				Real ratio = m_majorRadius / radius;
				ptToClip.x = geomCenter.x + dx * ratio;
				ptToClip.y = geomCenter.y + dy * ratio;
			}
			break;
		}

		case GEOMETRY_BOX:
		{
			ptToClip.x = clamp(geomCenter.x - m_majorRadius, ptToClip.x, geomCenter.x + m_majorRadius);
			ptToClip.y = clamp(geomCenter.y - m_minorRadius, ptToClip.y, geomCenter.y + m_minorRadius);
			break;
		}
	};
}

//=============================================================================
inline Bool isWithin(Real a, Real b, Real c) { return a<=b && b<=c; }

//=============================================================================
Bool GeometryInfo::isPointInFootprint(const Coord3D& geomCenter, const Coord3D& pt) const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			Real dx = pt.x - geomCenter.x;
			Real dy = pt.y - geomCenter.y;
			Real radius = sqrt(sqr(dx) + sqr(dy));
			return (radius <= m_majorRadius);
			break;
		}

		case GEOMETRY_BOX:
		{
			return isWithin(geomCenter.x - m_majorRadius, pt.x, geomCenter.x + m_majorRadius) &&
							isWithin(geomCenter.y - m_minorRadius, pt.y, geomCenter.y + m_minorRadius);
		}
	};
	return false;
}

//=============================================================================
void GeometryInfo::makeRandomOffsetWithinFootprint(Coord3D& pt, const RandomValueClass& random) const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
#if 1
			// this is a better technique than the more obvious radius-and-angle
			// one, below, because the latter tends to clump more towards the center.
			Real maxDistSqr = sqr(m_majorRadius);
			Real distSqr;
			do
			{
				pt.x = RandomValueReal(random, -m_majorRadius, m_majorRadius);
				pt.y = RandomValueReal(random, -m_majorRadius, m_majorRadius);
				pt.z = 0.0f;
				distSqr = sqr(pt.x) + sqr(pt.y);
			} while (distSqr > maxDistSqr);
#else
			Real radius = RandomValueReal(random, 0.0f, m_boundingCircleRadius);
			Real angle = RandomValueReal(random, -PI, PI);
			pt.x = radius * Cos(angle);
			pt.y = radius * Sin(angle);
			pt.z = 0.0f;
#endif
			break;
		}

		case GEOMETRY_BOX:
		{
			pt.x = RandomValueReal(random, -m_majorRadius, m_majorRadius);
			pt.y = RandomValueReal(random, -m_minorRadius, m_minorRadius);
			pt.z = 0.0f;
			break;
		}
	};
}

//=============================================================================
void GeometryInfo::makeRandomOffsetOnPerimeter(Coord3D& pt) const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			DEBUG_CRASH( ("GeometryInfo::makeRandomOffsetOnPerimeter() not implemented for SPHERE or CYLINDER extents. Using position.") );

			//Kris: Did not have time nor need to support non-box extents. I added this feature for script placement
			//      of boobytraps.
			pt.x = 0.0f;
			pt.y = 0.0f;
			break;
		}

		case GEOMETRY_BOX:
		{
			if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
			{
				//Pick random point on x axis.
				pt.x = GameLogicRandomValueReal(-m_majorRadius, m_majorRadius);

				//Min or max the y axis value
				if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
					pt.y = -m_minorRadius;
				else
					pt.y = m_minorRadius;
			}
			else
			{
				//Pick random point on y axis.
				pt.y = GameLogicRandomValueReal(-m_minorRadius, m_minorRadius);

				//Min or max the x axis value
				if( GameLogicRandomValueReal( 0.0f, 1.0f ) < 0.5f )
					pt.x = -m_majorRadius;
				else
					pt.x = m_majorRadius;
			}
			pt.z = 0.0f;
			break;
		}
	};
}

//=============================================================================
Real GeometryInfo::getFootprintArea() const
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		case GEOMETRY_CYLINDER:
		{
			return PI * sqr(m_boundingCircleRadius);
		}

		case GEOMETRY_BOX:
		{
			return 4.0f * m_majorRadius * m_minorRadius;
		}
	};

	DEBUG_CRASH(("should never get here"));
	return 0.0f;
}

//=============================================================================
void GeometryInfo::calcBoundingStuff()
{
	switch(m_type)
	{
		case GEOMETRY_SPHERE:
		{
			m_boundingSphereRadius = m_majorRadius;
			m_boundingCircleRadius = m_majorRadius;
			break;
		}
		case GEOMETRY_CYLINDER:
		{
			m_boundingCircleRadius = m_majorRadius;

			m_boundingSphereRadius = m_height*0.5;
			if (m_boundingSphereRadius < m_majorRadius)
				m_boundingSphereRadius = m_majorRadius;
			break;
		}

		case GEOMETRY_BOX:
		{
			m_boundingCircleRadius = sqrt(sqr(m_majorRadius) + sqr(m_minorRadius));
			m_boundingSphereRadius = sqrt(sqr(m_majorRadius) + sqr(m_minorRadius) + sqr(m_height*0.5));
			break;
		}
	};

	// grow the bounds around the object position (circle) and the primary center (sphere)
	// so that they enclose the additional shapes too
	if (isCompound())
	{
		Real primaryCenterZ = getZDeltaToCenterPosition();
		for (Int i = 0; i < getExtraShapeCount(); ++i)
		{
			const GeometryShape& shape = m_extraShapes->m_shapes[i];
			Real circle = sqrt(sqr(shape.m_offset.x) + sqr(shape.m_offset.y)) + shape.getBoundingCircleRadius();
			if (circle > m_boundingCircleRadius)
				m_boundingCircleRadius = circle;

			Real dz = shape.m_offset.z + shape.getZDeltaToCenterPosition() - primaryCenterZ;
			Real sphere = sqrt(sqr(shape.m_offset.x) + sqr(shape.m_offset.y) + sqr(dz)) + shape.getBoundingSphereRadius();
			if (sphere > m_boundingSphereRadius)
				m_boundingSphereRadius = sphere;
		}
	}
}

#if defined(RTS_DEBUG)
//=============================================================================
void GeometryInfo::tweakExtents(ExtentModType extentModType, Real extentModAmount)
{
	switch(extentModType)
	{
		case EXTENTMOD_HEIGHT:
			m_height += extentModAmount;
			break;
		case EXTENTMOD_MAJOR:
			m_majorRadius += extentModAmount;
			break;
		case EXTENTMOD_MINOR:
			m_minorRadius += extentModAmount;
			break;
		case EXTENTMOD_TYPE:
			m_type = (GeometryType)((m_type + ((extentModType == EXTENTMOD_TYPE)?1:0)) % GEOMETRY_NUM_TYPES);
			break;
	}
	m_isSmall = false;
	calcBoundingStuff();
}
#endif

#if defined(RTS_DEBUG)
//=============================================================================
AsciiString GeometryInfo::getDescriptiveString() const
{
	AsciiString msg;
	msg.format("%d/%d(%g %g %g)", (Int)m_type, (Int)m_isSmall, m_majorRadius, m_minorRadius, m_height);
	for (Int i = 0; i < getExtraShapeCount(); ++i)
	{
		const GeometryShape& shape = m_extraShapes->m_shapes[i];
		AsciiString tmp;
		tmp.format(" +%d(%g %g %g @%g,%g,%g %g)", (Int)shape.m_type, shape.m_majorRadius, shape.m_minorRadius, shape.m_height,
			shape.m_offset.x, shape.m_offset.y, shape.m_offset.z, shape.m_angle);
		msg.concat(tmp);
	}
	return msg;
}
#endif

// ------------------------------------------------------------------------------------------------
static Bool isSameShape( const GeometryShape& a, const GeometryShape& b )
{
	return a.m_type == b.m_type && a.m_height == b.m_height && a.m_majorRadius == b.m_majorRadius &&
		a.m_minorRadius == b.m_minorRadius && a.m_offset.x == b.m_offset.x && a.m_offset.y == b.m_offset.y &&
		a.m_offset.z == b.m_offset.z && a.m_angle == b.m_angle;
}

// ------------------------------------------------------------------------------------------------
/** After loading: keeps the shared list when the saved shapes are the ones this geometry already
	* has (an object made from its template), so loaded objects do not each get a copy. */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::setLoadedExtraShapes( const GeometryShape *shapes, Int count )
{
	if( count == 0 )
	{
		setExtraShapes( nullptr );
		return;
	}

	if( getExtraShapeCount() == count )
	{
		Bool same = TRUE;
		for( Int i = 0; i < count && same; ++i )
			same = isSameShape( shapes[ i ], m_extraShapes->m_shapes[ i ] );
		if( same )
			return;
	}

	GeometryShapeList *list = NEW GeometryShapeList;
	list->m_refCount = 0;
	list->m_count = count;
	for( Int i = 0; i < count; ++i )
		list->m_shapes[ i ] = shapes[ i ];
	setExtraShapes( list );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Additional shapes of a compound geometry */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// type
	xfer->xferUser( &m_type, sizeof( GeometryType ) );

	// is small
	xfer->xferBool( &m_isSmall );

	// height
	xfer->xferReal( &m_height );

	// major radius
	xfer->xferReal( &m_majorRadius );

	// minor radius
	xfer->xferReal( &m_minorRadius );

	// bouncing circle radius
	xfer->xferReal( &m_boundingCircleRadius );

	// bounding sphere radius
	xfer->xferReal( &m_boundingSphereRadius );

	// additional shapes
	if( version >= 2 )
	{
		Int count = getExtraShapeCount();
		xfer->xferInt( &count );
		if( count < 0 || count > MAX_GEOMETRY_SHAPES - 1 )
		{
			DEBUG_CRASH(( "GeometryInfo::xfer - invalid shape count %d", count ));
			throw SC_INVALID_DATA;
		}

		GeometryShape shapes[ MAX_GEOMETRY_SHAPES - 1 ];
		for( Int i = 0; i < count; ++i )
		{
			GeometryShape& shape = shapes[ i ];
			if( xfer->getXferMode() != XFER_LOAD )
				shape = m_extraShapes->m_shapes[ i ];
			xfer->xferUser( &shape.m_type, sizeof( GeometryType ) );
			xfer->xferReal( &shape.m_height );
			xfer->xferReal( &shape.m_majorRadius );
			xfer->xferReal( &shape.m_minorRadius );
			xfer->xferCoord3D( &shape.m_offset );
			xfer->xferReal( &shape.m_angle );
		}

		if( xfer->getXferMode() == XFER_LOAD )
			setLoadedExtraShapes( shapes, count );
	}
	else
	{
		setExtraShapes( nullptr );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GeometryInfo::loadPostProcess()
{

}
