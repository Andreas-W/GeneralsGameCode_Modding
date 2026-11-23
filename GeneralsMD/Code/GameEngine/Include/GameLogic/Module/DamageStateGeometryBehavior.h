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

// FILE: DamageStateGeometryBehavior.h ////////////////////////////////////////////////////////////
// Desc: Gives the object another geometry (primary and additional shapes) per body damage state
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef USE_ADVANCED_COLLISION
// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/Geometry.h"
#include "GameLogic/Module/DamageModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;
class Object;

//-------------------------------------------------------------------------------------------------
class DamageStateGeometryBehaviorModuleData : public DamageModuleData
{
public:

	DamageStateGeometryBehaviorModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseDamagedGeometry( INI* ini, void *instance, void *store, const void* /*userData*/ );
	static void parseReallyDamagedGeometry( INI* ini, void *instance, void *store, const void* /*userData*/ );

	GeometryInfo m_damagedGeometry;
	GeometryInfo m_reallyDamagedGeometry;
	Bool m_hasDamagedGeometry;
	Bool m_hasReallyDamagedGeometry;
};

//-------------------------------------------------------------------------------------------------
class DamageStateGeometryBehavior : public DamageModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( DamageStateGeometryBehavior, "DamageStateGeometryBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( DamageStateGeometryBehavior, DamageStateGeometryBehaviorModuleData )

public:

	DamageStateGeometryBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// DamageModuleInterface
	virtual void onDamage( DamageInfo *damageInfo ) override { }
	virtual void onHealing( DamageInfo *damageInfo ) override { }
	virtual void onBodyDamageStateChange( const DamageInfo* damageInfo, BodyDamageType oldState, BodyDamageType newState ) override;

	/// the geometry for the undamaged state (from a GeometryUpgrade); applied now unless a damage state geometry is in use
	void setUndamagedGeometry( const GeometryInfo& geom );

	/// sets the geometry and moves the object's footprint in the pathfinder with it
	static void changeObjectGeometry( Object *obj, const GeometryInfo& geom );

protected:

	const GeometryInfo *findGeometryFor( BodyDamageType state ) const;	///< null: the undamaged one

	GeometryInfo m_undamagedGeometry;		///< while m_usesDamageGeometry: the geometry to go back to
	Bool m_usesDamageGeometry;					///< the object has a damage state geometry now

};
#endif // USE_ADVANCED_COLLISION
