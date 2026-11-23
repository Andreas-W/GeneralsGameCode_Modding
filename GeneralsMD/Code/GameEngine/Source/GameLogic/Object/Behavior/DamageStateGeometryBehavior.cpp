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

// FILE: DamageStateGeometryBehavior.cpp //////////////////////////////////////////////////////////
// Desc: Gives the object another geometry (primary and additional shapes) per body damage state
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef USE_ADVANCED_COLLISION
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/DamageStateGeometryBehavior.h"

//-------------------------------------------------------------------------------------------------
DamageStateGeometryBehaviorModuleData::DamageStateGeometryBehaviorModuleData() :
	m_damagedGeometry( GEOMETRY_SPHERE, FALSE, 0.0f, 0.0f, 0.0f ),
	m_reallyDamagedGeometry( GEOMETRY_SPHERE, FALSE, 0.0f, 0.0f, 0.0f )
{
	m_hasDamagedGeometry = FALSE;
	m_hasReallyDamagedGeometry = FALSE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DamageStateGeometryBehaviorModuleData::parseDamagedGeometry( INI* ini, void *instance, void *store, const void* userData )
{
	GeometryInfo::parseGeometryBlock( ini, instance, store, userData );
	((DamageStateGeometryBehaviorModuleData*)instance)->m_hasDamagedGeometry = TRUE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DamageStateGeometryBehaviorModuleData::parseReallyDamagedGeometry( INI* ini, void *instance, void *store, const void* userData )
{
	GeometryInfo::parseGeometryBlock( ini, instance, store, userData );
	((DamageStateGeometryBehaviorModuleData*)instance)->m_hasReallyDamagedGeometry = TRUE;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DamageStateGeometryBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	DamageModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "DamagedGeometry",				parseDamagedGeometry,				nullptr, offsetof( DamageStateGeometryBehaviorModuleData, m_damagedGeometry ) },
		{ "ReallyDamagedGeometry",	parseReallyDamagedGeometry,	nullptr, offsetof( DamageStateGeometryBehaviorModuleData, m_reallyDamagedGeometry ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DamageStateGeometryBehavior::DamageStateGeometryBehavior( Thing *thing, const ModuleData* moduleData ) :
	DamageModule( thing, moduleData ),
	m_undamagedGeometry( GEOMETRY_SPHERE, FALSE, 0.0f, 0.0f, 0.0f )
{
	m_usesDamageGeometry = FALSE;
}

//-------------------------------------------------------------------------------------------------
DamageStateGeometryBehavior::~DamageStateGeometryBehavior()
{
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DamageStateGeometryBehavior::changeObjectGeometry( Object *obj, const GeometryInfo& geom )
{
	// the old footprint leaves the pathfinder before the geometry changes, the new one enters after
	TheAI->pathfinder()->removeObjectFromPathfindMap( obj );
	obj->setGeometryInfo( geom );
	TheAI->pathfinder()->addObjectToPathfindMap( obj );
}

//-------------------------------------------------------------------------------------------------
const GeometryInfo *DamageStateGeometryBehavior::findGeometryFor( BodyDamageType state ) const
{
	const DamageStateGeometryBehaviorModuleData *data = getDamageStateGeometryBehaviorModuleData();
	if( state == BODY_REALLYDAMAGED && data->m_hasReallyDamagedGeometry )
		return &data->m_reallyDamagedGeometry;
	if( ( state == BODY_DAMAGED || state == BODY_REALLYDAMAGED ) && data->m_hasDamagedGeometry )
		return &data->m_damagedGeometry;
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
void DamageStateGeometryBehavior::onBodyDamageStateChange( const DamageInfo* /*damageInfo*/,
	BodyDamageType oldState, BodyDamageType newState )
{
	// rubble keeps the geometry it has; the body has already cut it down to rubble height
	if( newState == BODY_RUBBLE )
		return;

	Object *obj = getObject();
	const GeometryInfo *geom = findGeometryFor( newState );
	if( geom )
	{
		if( m_usesDamageGeometry && oldState != BODY_RUBBLE && geom == findGeometryFor( oldState ) )
			return;
		if( !m_usesDamageGeometry )
		{
			m_undamagedGeometry = obj->getGeometryInfo();
			m_usesDamageGeometry = TRUE;
		}
		changeObjectGeometry( obj, *geom );
	}
	else if( m_usesDamageGeometry )
	{
		m_usesDamageGeometry = FALSE;
		changeObjectGeometry( obj, m_undamagedGeometry );
	}
}

//-------------------------------------------------------------------------------------------------
void DamageStateGeometryBehavior::setUndamagedGeometry( const GeometryInfo& geom )
{
	if( m_usesDamageGeometry )
		m_undamagedGeometry = geom;
	else
		changeObjectGeometry( getObject(), geom );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DamageStateGeometryBehavior::crc( Xfer *xfer )
{

	// extend base class
	DamageModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void DamageStateGeometryBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DamageModule::xfer( xfer );

	// the geometry to go back to when healed
	xfer->xferBool( &m_usesDamageGeometry );
	if( m_usesDamageGeometry )
		xfer->xferSnapshot( &m_undamagedGeometry );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DamageStateGeometryBehavior::loadPostProcess()
{

	// extend base class
	DamageModule::loadPostProcess();

}
#endif // USE_ADVANCED_COLLISION
