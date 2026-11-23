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

// FILE: Geometry.h ///////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:	 Geometry descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/RandomValue.h"
#include "Common/Snapshot.h"

class INI;

//-------------------------------------------------------------------------------------------------
/** Geometry type descriptions, keep this in the same order as GeometryNames[] below
	*
	* NOTE: Do *NOT* change the order of these defines unless you update the
	* partition manager ... in particular theCollideTestProcs depend on the
	* order of this geometry and the fact that the values start at 1
	*/
//-------------------------------------------------------------------------------------------------
enum GeometryType CPP_11(: Int)
{
	GEOMETRY_SPHERE,			///< partition/collision testing as sphere. (majorRadius = radius)
	GEOMETRY_CYLINDER,		///< partition/collision testing as cylinder. (majorRadius = radius, height = height)
	GEOMETRY_BOX,					///< partition/collision testing as rectangular box (majorRadius = half len in forward dir; minorRadius = half len in side dir; height = height)

	GEOMETRY_NUM_TYPES,
	GEOMETRY_FIRST = 0
};

#ifdef DEFINE_GEOMETRY_NAMES
static const char *const GeometryNames[] =
{
	"SPHERE",
	"CYLINDER",
	"BOX",
	nullptr
};
static_assert(ARRAY_SIZE(GeometryNames) == GEOMETRY_NUM_TYPES + 1, "Incorrect array size");
#endif  // end DEFINE_GEOMETRY_NAMES

//-------------------------------------------------------------------------------------------------
#if defined(RTS_DEBUG)
enum ExtentModType CPP_11(: Int)
{
	EXTENTMOD_INVALID = 0,
	EXTENTMOD_TYPE = 1,
	EXTENTMOD_MAJOR = 2,
	EXTENTMOD_MINOR = 3,
	EXTENTMOD_HEIGHT = 4,
};
static const Real EXTENT_BIG_CHANGE = 10.0f;
#endif

#ifdef USE_ADVANCED_COLLISION
//-------------------------------------------------------------------------------------------------
/** Max number of primitive shapes in one geometry, including the primary shape. */
//-------------------------------------------------------------------------------------------------
enum { MAX_GEOMETRY_SHAPES = 8 };

//-------------------------------------------------------------------------------------------------
/** An additional primitive shape of a compound geometry, in the object's local frame
	* (x = forward, y = left, z = up from the object position). */
//-------------------------------------------------------------------------------------------------
struct GeometryShape
{
	GeometryType m_type;
	Real m_height;
	Real m_majorRadius;
	Real m_minorRadius;
	Coord3D m_offset;
	Real m_angle;							///< yaw relative to the object, radians

	void clear();
	void normalize();					///< sphere/cylinder: minor = major (sphere: height = major), like GeometryInfo::set
	Real getBoundingCircleRadius() const;
	Real getBoundingSphereRadius() const;
	Real getZDeltaToCenterPosition() const { return (m_type == GEOMETRY_SPHERE) ? 0.0f : (m_height * 0.5f); }
	Real getMaxHeightAbovePosition() const { return (m_type == GEOMETRY_SPHERE) ? m_majorRadius : m_height; }
	Real getMaxHeightBelowPosition() const { return (m_type == GEOMETRY_SPHERE) ? m_majorRadius : 0.0f; }
};

//-------------------------------------------------------------------------------------------------
/** One primitive shape of a geometry, placed in the world. 'position' is where an object with
	* only this shape would stand (bottom center; center for a sphere). */
//-------------------------------------------------------------------------------------------------
struct GeometryPrimitive
{
	GeometryType type;
	Real height;
	Real majorRadius;
	Real minorRadius;
	Coord3D position;
	Real angle;

	Real getMaxHeightAbovePosition() const { return (type == GEOMETRY_SPHERE) ? majorRadius : height; }
	Real getMaxHeightBelowPosition() const { return (type == GEOMETRY_SPHERE) ? majorRadius : 0.0f; }
	Real getBoundingCircleRadius() const;
	Bool isPointInFootprint2D(const Coord3D& pt) const;	///< respects the angle
	Real getDistanceSqToPoint2D(const Coord3D& pt) const;	///< 0 inside
	Real getDistanceSqToPoint3D(const Coord3D& pt) const;	///< 0 inside
};

//-------------------------------------------------------------------------------------------------
/** The additional shapes of a compound geometry. Shared by every copy of a GeometryInfo (the
	* template and all of its objects) and never changed while shared: a change makes a new list. */
//-------------------------------------------------------------------------------------------------
struct GeometryShapeList
{
	Int m_refCount;
	Int m_count;
	GeometryShape m_shapes[MAX_GEOMETRY_SHAPES - 1];
};

#endif
//-------------------------------------------------------------------------------------------------
/** Geometry information */
//-------------------------------------------------------------------------------------------------
class GeometryInfo : public Snapshot
{
private:
	GeometryType m_type;
	Bool m_isSmall;						///< if true, geometry is assumed to fit in a single partition cell
	Real m_height;
	Real m_majorRadius;
	Real m_minorRadius;

	Real m_boundingCircleRadius;	///< not in INI file -- size of bounding circle (2d)
	Real m_boundingSphereRadius;	///< not in INI -- size of bounding sphere (3d)

#ifdef USE_ADVANCED_COLLISION
	// Additional shapes of a compound geometry (null: none). The scalars above stay the primary
	// shape (offset 0, angle 0), so code that only knows one shape keeps working with it.
	GeometryShapeList *m_extraShapes;

#endif
	void calcBoundingStuff();
#ifdef USE_ADVANCED_COLLISION
	Int getExtraShapeCount() const { return m_extraShapes ? m_extraShapes->m_count : 0; }
	void setExtraShapes(GeometryShapeList *list);	///< takes a reference on list, releases the old one
	void setLoadedExtraShapes(const GeometryShape *shapes, Int count);
#endif

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

public:

	static void parseGeometryType( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryIsSmall( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryHeight( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryMajorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );
	static void parseGeometryMinorRadius( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );

#ifdef USE_ADVANCED_COLLISION
	/// parses an "AdditionalGeometry ... End" block into a GeometryShape (store)
	static void parseGeometryShape( INI* ini, GeometryShape& shape );

	/// parses an "AdditionalGeometry ... End" block and adds it to the GeometryInfo (store)
	static void parseAdditionalGeometryInto( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );

	/// parses a whole geometry (Geometry, GeometryMajorRadius, ..., AdditionalGeometry blocks) up to
	/// "End" into the GeometryInfo (store), replacing what it held; for geometries in module blocks
	static void parseGeometryBlock( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ );

#endif
	GeometryInfo(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius)
	{
		m_boundingCircleRadius = 0.0f;
		m_boundingSphereRadius = 0.0f;
#ifdef USE_ADVANCED_COLLISION
		m_extraShapes = nullptr;
#endif
		set(type, isSmall, height, majorRadius, minorRadius);
	}

#ifdef USE_ADVANCED_COLLISION
	GeometryInfo(const GeometryInfo& that) : Snapshot(that), m_extraShapes(nullptr) { *this = that; }
	GeometryInfo& operator=(const GeometryInfo& that);
	~GeometryInfo() { setExtraShapes(nullptr); }

	/// sets the primary shape; additional shapes are kept
#endif
	void set(GeometryType type, Bool isSmall, Real height, Real majorRadius, Real minorRadius);

#ifdef USE_ADVANCED_COLLISION
	// compound geometry
	Bool isCompound() const { return m_extraShapes != nullptr; }
	Int getShapeCount() const { return 1 + getExtraShapeCount(); }
	const GeometryShape& getExtraShape(Int i) const { return m_extraShapes->m_shapes[i]; }
	Bool addShape(const GeometryShape& shape);	///< false if full
	void clearExtraShapes();
	/// the additional shapes of 'base', offsets scaled per object axis and sizes scaled with them
	void setScaledExtraShapes(const GeometryInfo& base, Real scaleX, Real scaleY, Real scaleZ);
	/// shape i (0 = primary) of an object with this geom at 'pos' facing 'angle', in world space
	void getWorldPrimitive(Int i, const Coord3D& pos, Real angle, GeometryPrimitive& out) const;
	/// the primary shape in world space (inline: used by every collision test)
	void getPrimaryPrimitive(const Coord3D& pos, Real angle, GeometryPrimitive& out) const
	{
		out.type = m_type;
		out.height = m_height;
		out.majorRadius = m_majorRadius;
		out.minorRadius = m_minorRadius;
		out.position = pos;
		out.angle = angle;
	}
	/// all shapes at once into out[MAX_GEOMETRY_SHAPES]; returns getShapeCount()
	Int getWorldPrimitives(const Coord3D& pos, Real angle, GeometryPrimitive *out) const;
	/// squared 2d/3d distance from 'pt' to the nearest shape of an object at 'pos' facing 'angle' (0 inside)
	Real getDistanceSqToPoint2D(const Coord3D& pos, Real angle, const Coord3D& pt) const;
	Real getDistanceSqToPoint3D(const Coord3D& pos, Real angle, const Coord3D& pt) const;

#endif
	// bleah, icky but needed for legacy code
	void setMajorRadius(Real majorRadius)
	{
		m_majorRadius = majorRadius;
		calcBoundingStuff();
	}

	// bleah, icky but needed for legacy code
	void setMinorRadius(Real minorRadius)
	{
		m_minorRadius = minorRadius;
		calcBoundingStuff();
	}

	GeometryType getGeomType() const { return m_type; }
#ifdef USE_ADVANCED_COLLISION
	Bool getIsSmall() const { return m_isSmall && m_extraShapes == nullptr; }	///< a compound geometry is never small
#else
	Bool getIsSmall() const { return m_isSmall; }
#endif
	Real getMajorRadius() const { return m_majorRadius; }	// x-axis
	Real getMinorRadius() const { return m_minorRadius; }	// y-axis

	// this has been removed and should never need to be called...
	// you should generally call getMaxHeightAbovePosition() instead. (srj)
	//inline Real getGeomHeight() const { return m_height; }				// z-axis

	Real getBoundingCircleRadius() const { return m_boundingCircleRadius; }
	Real getBoundingSphereRadius() const { return m_boundingSphereRadius; }

	Bool isIntersectedByLineSegment(const Coord3D& loc, const Coord3D& from, const Coord3D& to) const;

	Real getFootprintArea() const;

	// given an object with this geom, how far above the object's canonical position does its max z extend?
	Real getMaxHeightAbovePosition() const;

	// given an object with this geom, how far below the object's canonical position does its max z extend?
	Real getMaxHeightBelowPosition() const;

	// given an object with this geom, how far above/below the object's canonical position is its center?
	Real getZDeltaToCenterPosition() const;

	// given an object with this geom, located at 'pos', where is the "center" of the geometry?
	void getCenterPosition(const Coord3D& pos, Coord3D& center) const;

	// given an object with this geom, located at 'pos', and another obj with the given
	// pos & geom, calc the min and max pitches from this to that.
	void calcPitches(const Coord3D& thisPos, const GeometryInfo& that, const Coord3D& thatPos,
		Real& minPitch, Real& maxPitch) const;

	/// expand the 2d footprint
	void expandFootprint(Real radius);

	/// get the 2d bounding box
	void get2DBounds(const Coord3D& geomCenter, Real angle, Region2D& bounds	) const;

	void makeRandomOffsetWithinFootprint(Coord3D& pt, const RandomValueClass& random = LogicRandomValueClass()) const;
	void makeRandomOffsetOnPerimeter(Coord3D& pt) const; ///< Chooses a random point on the extent border. Uses game logic random!

	void clipPointToFootprint(const Coord3D& geomCenter, Coord3D& ptToClip) const;

	Bool isPointInFootprint(const Coord3D& geomCenter, const Coord3D& pt) const;

	// given an object with this geom, SET how far above the object's canonical position its max z should extend.
#ifdef USE_ADVANCED_COLLISION
	// Additional shapes reaching higher than z are cut down to it.
#endif
	void setMaxHeightAbovePosition(Real z);

#if defined(RTS_DEBUG)
	void tweakExtents(ExtentModType extentModType, Real extentModAmount);
	AsciiString getDescriptiveString() const;
#endif

};
