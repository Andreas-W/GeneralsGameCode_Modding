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

// FILE: GeometryUpgrade.cpp //////////////////////////////////////////////////////////////////////
// Desc: Replaces the object's whole geometry (primary and additional shapes) when upgraded
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef USE_ADVANCED_COLLISION
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/DamageStateGeometryBehavior.h"
#include "GameLogic/Module/GeometryUpgrade.h"

//-------------------------------------------------------------------------------------------------
GeometryUpgradeModuleData::GeometryUpgradeModuleData() :
	m_geometry( GEOMETRY_SPHERE, FALSE, 0.0f, 0.0f, 0.0f )
{
}

//-------------------------------------------------------------------------------------------------
/*static*/ void GeometryUpgradeModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpgradeModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "Geometry", GeometryInfo::parseGeometryBlock, nullptr, offsetof( GeometryUpgradeModuleData, m_geometry ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
GeometryUpgrade::GeometryUpgrade( Thing *thing, const ModuleData* moduleData ) : UpgradeModule( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
GeometryUpgrade::~GeometryUpgrade()
{
}

//-------------------------------------------------------------------------------------------------
void GeometryUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	const GeometryInfo& geom = getGeometryUpgradeModuleData()->m_geometry;

	// while a damage state has its own geometry, that one stays and this becomes the undamaged one
	static NameKeyType key_damageStateGeometry = NAMEKEY( "DamageStateGeometryBehavior" );
	DamageStateGeometryBehavior *damageGeometry = (DamageStateGeometryBehavior *)obj->findDamageModule( key_damageStateGeometry );
	if( damageGeometry )
		damageGeometry->setUndamagedGeometry( geom );
	else
		DamageStateGeometryBehavior::changeObjectGeometry( obj, geom );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void GeometryUpgrade::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void GeometryUpgrade::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void GeometryUpgrade::loadPostProcess()
{

	// extend base class
	UpgradeModule::loadPostProcess();

}
#endif // USE_ADVANCED_COLLISION
