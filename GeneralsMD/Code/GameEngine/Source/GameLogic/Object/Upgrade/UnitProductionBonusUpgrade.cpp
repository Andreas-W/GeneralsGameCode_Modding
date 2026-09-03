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

// FILE: UnitProductionBonusUpgrade.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Aug 2002
//
//	Filename:  UnitProductionBonusUpgrade.cpp
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

// FILE: UnitProductionBonusUpgrade.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	June 2025
//
//	Filename: 	UnitProductionBonusUpgrade.h
//
//	author:		Andi W
//	
//	purpose:	Upgrade that modifies the cost or build time of a list of units
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/UnitProductionBonusUpgrade.h"
#include "GameLogic/Object.h"
#include "Common/BitFlagsIO.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UnitProductionBonusUpgradeModuleData::UnitProductionBonusUpgradeModuleData( void )
{
	m_templateNames.clear();
	m_costPercentage = 0.0f;
	m_timePercentage = 0.0f;
	m_isOneShot = FALSE;
	m_stackingType = NO_STACKING;

}  // end UnitProductionBonusUpgradeModuleData

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ void UnitProductionBonusUpgradeModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpgradeModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] = 
	{
		{ "CostModifierPercentage",			INI::parsePercentToReal, NULL, offsetof( UnitProductionBonusUpgradeModuleData, m_costPercentage ) },
		{ "BuildTimeModifierPercentage",			INI::parsePercentToReal, NULL, offsetof( UnitProductionBonusUpgradeModuleData, m_timePercentage ) },
		{ "IsOneShotUpgrade",		INI::parseBool, NULL, offsetof( UnitProductionBonusUpgradeModuleData, m_isOneShot) },
		{ "BonusStacksWith",		INI::parseIndexList, TheBonusStackingTypeNames, offsetof( UnitProductionBonusUpgradeModuleData, m_stackingType) },
		{ "UnitTemplateName", INI::parseAsciiStringVectorAppend, NULL, offsetof(UnitProductionBonusUpgradeModuleData, m_templateNames) },

		{ 0, 0, 0, 0 } 
	};
	p.add(dataFieldParse);

}  // end buildFieldParse

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UnitProductionBonusUpgrade::UnitProductionBonusUpgrade( Thing *thing, const ModuleData* moduleData ) : 
							UpgradeModule( thing, moduleData )
{

}  // end UnitProductionBonusUpgrade

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UnitProductionBonusUpgrade::~UnitProductionBonusUpgrade( void )
{

}  // end ~UnitProductionBonusUpgrade

//-------------------------------------------------------------------------------------------------
// Apply or remove this module's bonus on the given player (ref-counted, source-tracked).
//-------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::applyBonus( Player *player, Bool add )
{
	if (player == NULL)
		return;

	const UnitProductionBonusUpgradeModuleData * d = getUnitProductionBonusUpgradeModuleData();
	const Bool stackWithAny    = (d->m_stackingType == SAME_TYPE);
	const Bool stackUniqueType = (d->m_stackingType == OTHER_TYPE);
	const UnsignedInt sourceTemplateID = getObject()->getTemplate()->getTemplateID();

	for (std::vector<AsciiString>::const_iterator tempName = d->m_templateNames.begin();
		tempName != d->m_templateNames.end(); ++tempName)
	{
		if (d->m_costPercentage != 0.0f)
		{
			if (add)
				player->addProductionCostChangeStackable(*tempName, d->m_costPercentage, sourceTemplateID, stackUniqueType, stackWithAny);
			else
				player->removeProductionCostChangeStackable(*tempName, d->m_costPercentage, sourceTemplateID, stackUniqueType, stackWithAny);
		}

		if (d->m_timePercentage != 0.0f)
		{
			if (add)
				player->addProductionTimeChangeStackable(*tempName, d->m_timePercentage, sourceTemplateID, stackUniqueType, stackWithAny);
			else
				player->removeProductionTimeChangeStackable(*tempName, d->m_timePercentage, sourceTemplateID, stackUniqueType, stackWithAny);
		}
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::onDelete( void )
{
	const UnitProductionBonusUpgradeModuleData * d = getUnitProductionBonusUpgradeModuleData();

	// one-shot bonuses are permanent; nothing to clean up
	if (d->m_isOneShot)
		return;

	// nothing to remove if we never applied
	if (isAlreadyUpgraded() == FALSE)
		return;

	applyBonus( getObject()->getControllingPlayer(), FALSE );

	// this upgrade module is now "not upgraded"
	setUpgradeExecuted(FALSE);

}  // end onDelete

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::onCapture( Player *oldOwner, Player *newOwner )
{
	const UnitProductionBonusUpgradeModuleData * d = getUnitProductionBonusUpgradeModuleData();

	// one-shot bonuses stay with the original player; don't remove or transfer
	if (d->m_isOneShot)
		return;

	if (isAlreadyUpgraded() == FALSE)
		return;

	if (oldOwner)
	{
		applyBonus( oldOwner, FALSE );
		setUpgradeExecuted(FALSE);
	}
	if (newOwner)
	{
		applyBonus( newOwner, TRUE );
		setUpgradeExecuted(TRUE);
	}

}  // end onCapture

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::upgradeImplementation( void )
{
	applyBonus( getObject()->getControllingPlayer(), TRUE );

}  // end upgradeImplementation

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void UnitProductionBonusUpgrade::loadPostProcess( void )
{

	// extend base class
	UpgradeModule::loadPostProcess();

}  // end loadPostProcess
