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

// FILE: DieModule.cpp ////////////////////////////////////////////////////////////////////////////
// Author:
// Desc:
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_OBJECT_STATUS_NAMES
#include "Common/Xfer.h"
#include "Common/GlobalData.h"
#include "GameClient/Drawable.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Object.h"
#include <GameLogic/TerrainLogic.h>






//-------------------------------------------------------------------------------------------------
DieMuxData::DieMuxData() {
	m_deathTypes = DEATH_TYPE_FLAGS_ALL;
	m_veterancyLevels = VETERANCY_LEVEL_FLAGS_ALL;

	if (TheGlobalData) {
		m_deathTypes &= ~TheGlobalData->m_defaultExcludedDeathTypes;
	}
	m_minWaterDepth = 0.0f;
	m_maxWaterDepth = std::numeric_limits<Real>::infinity();
}

//-------------------------------------------------------------------------------------------------
const FieldParse* DieMuxData::getFieldParse()
{
	static const FieldParse dataFieldParse[] =
	{
		{ "DeathTypes",				INI::parseDeathTypeFlags,						nullptr, offsetof( DieMuxData, m_deathTypes ) },
		{ "VeterancyLevels",	INI::parseVeterancyLevelFlags,			nullptr, offsetof( DieMuxData, m_veterancyLevels ) },
		{ "ExemptStatus",			ObjectStatusMaskType::parseFromINI,	nullptr,	offsetof( DieMuxData, m_exemptStatus ) },
		{ "RequiredStatus",		ObjectStatusMaskType::parseFromINI, nullptr,	offsetof( DieMuxData, m_requiredStatus ) },
		{ "MinWaterDepth",    INI::parseReal,										nullptr, offsetof(DieMuxData, m_minWaterDepth )},
		{ "MaxWaterDepth",    INI::parseReal,										nullptr, offsetof(DieMuxData, m_maxWaterDepth )},
		{ nullptr, nullptr, nullptr, 0 } 
	};
  return dataFieldParse;
}

//-------------------------------------------------------------------------------------------------
Bool DieMuxData::isDieApplicable(const Object* obj, const DamageInfo *damageInfo) const
{
	// wrong death type? punt
	if (!getDeathTypeFlag(m_deathTypes, damageInfo->in.m_deathType))
		return false;

	// wrong vet level? punt
	if (!getVeterancyLevelFlag(m_veterancyLevels, obj->getVeterancyLevel()))
		return false;

	// all 'exempt' bits must be clear for us to run.
	if( m_exemptStatus.any() && obj->getStatusBits().testForAny( m_exemptStatus ) )
		return false;

	// all 'required' bits must be set for us to run.
	// But only if we have a required status to check
	if( m_requiredStatus.any()  &&  !obj->getStatusBits().testForAll( m_requiredStatus ) )
		return false;

	if ((m_minWaterDepth > 0.0f || m_maxWaterDepth < std::numeric_limits<Short>::infinity()) && obj != nullptr) {

		// if on bride and we need water -> not applicable
		if (obj->getLayer() > LAYER_GROUND && m_minWaterDepth > 0.0f) {
			return false;
		}

		// Water level restriction
		const Coord3D* pos = obj->getPosition();
		Real waterZ{ 0 }, terrainZ{ 0 };

		if (TheTerrainLogic->isUnderwater(pos->x, pos->y, &waterZ, &terrainZ)) {
			Real depth = waterZ - terrainZ;
			return depth >= m_minWaterDepth && depth < m_maxWaterDepth;
		}
		else {
			// we are over land
			if (m_minWaterDepth > 0.0f) return false;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DieModule::crc( Xfer *xfer )
{

	// extend base class
	BehaviorModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void DieModule::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// call base class
	BehaviorModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DieModule::loadPostProcess( void )
{

	// call base class
	BehaviorModule::loadPostProcess();

}
