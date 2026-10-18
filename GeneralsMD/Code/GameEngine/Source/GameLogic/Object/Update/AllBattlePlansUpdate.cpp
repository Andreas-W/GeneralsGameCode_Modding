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

// FILE: AllBattlePlansUpdate.cpp /////////////////////////////////////////////////////////////////
// Desc:   Grants the controlling player the army bonuses of all battle plans while this object
//         exists.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AllBattlePlansUpdate.h"
#include "GameLogic/Object.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AllBattlePlansUpdate::AllBattlePlansUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_valid = FALSE;
	m_applied = FALSE;

	// Take the values from the strategy center so the temporary plans match the permanent ones exactly.
	// Removing a plan inverts the scalars of whichever source drops the count to 0, so they must be equal.
	const AllBattlePlansUpdateModuleData *data = getAllBattlePlansUpdateModuleData();
	const ThingTemplate *centerTemplate = TheThingFactory->findTemplate( data->m_strategyCenterTemplateName );
	const BattlePlanUpdateModuleData *planData = nullptr;
	if( centerTemplate )
	{
		const ModuleInfo& mi = centerTemplate->getBehaviorModuleInfo();
		for( Int i = 0; i < mi.getCount(); ++i )
		{
			if( mi.getNthName( i ).compareNoCase( "BattlePlanUpdate" ) == 0 )
			{
				planData = (const BattlePlanUpdateModuleData *)mi.getNthData( i );
				break;
			}
		}
	}

	if( !planData )
	{
		DEBUG_CRASH( ("%s object's AllBattlePlansUpdate needs a StrategyCenterTemplate with a BattlePlanUpdate (got '%s').",
			getObject()->getTemplate()->getName().str(), data->m_strategyCenterTemplateName.str()) );
		return;
	}

	for( Int i = 0; i < PLAN_SOURCE_COUNT; ++i )
	{
		m_bonuses[i].m_validKindOf = planData->m_validMemberKindOf;
		m_bonuses[i].m_invalidKindOf = planData->m_invalidMemberKindOf;
	}
	m_bonuses[PLANSTATUS_BOMBARDMENT - 1].m_bombardment = 1;
	m_bonuses[PLANSTATUS_HOLDTHELINE - 1].m_holdTheLine = 1;
	m_bonuses[PLANSTATUS_HOLDTHELINE - 1].m_armorScalar = planData->m_holdTheLineArmorDamageScalar;
	m_bonuses[PLANSTATUS_SEARCHANDDESTROY - 1].m_searchAndDestroy = 1;
	m_bonuses[PLANSTATUS_SEARCHANDDESTROY - 1].m_sightRangeScalar = planData->m_searchAndDestroySightRangeScalar;
	m_valid = TRUE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AllBattlePlansUpdate::~AllBattlePlansUpdate()
{
}

//-------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::changeAllPlans( Player *player, Int delta )
{
	DEBUG_LOG(( "AllBattlePlansUpdate frame %d: %s all battle plans for player %d (object %d)",
		TheGameLogic->getFrame(), delta > 0 ? "granting" : "removing", player->getPlayerIndex(), getObject()->getID() ));

	player->changeBattlePlan( PLANSTATUS_BOMBARDMENT, delta, &m_bonuses[PLANSTATUS_BOMBARDMENT - 1] );
	player->changeBattlePlan( PLANSTATUS_HOLDTHELINE, delta, &m_bonuses[PLANSTATUS_HOLDTHELINE - 1] );
	player->changeBattlePlan( PLANSTATUS_SEARCHANDDESTROY, delta, &m_bonuses[PLANSTATUS_SEARCHANDDESTROY - 1] );
}

//-------------------------------------------------------------------------------------------------
// Applied on the first update rather than on creation: a loaded save constructs the object again,
// but restores m_applied before any update runs, so the plans are not counted twice.
//-------------------------------------------------------------------------------------------------
UpdateSleepTime AllBattlePlansUpdate::update()
{
	if( m_valid && !m_applied )
	{
		Player *player = getObject()->getControllingPlayer();
		if( player )
		{
			changeAllPlans( player, 1 );
			m_applied = TRUE;
		}
	}
	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::onDelete()
{
	UpdateModule::onDelete();

	// Player can be null during game reset cycles, where removing the bonuses no longer matters.
	Player *player = getObject()->getControllingPlayer();
	if( player && m_applied )
	{
		changeAllPlans( player, -1 );
	}
	m_applied = FALSE;
}

//-------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::onCapture( Player *oldOwner, Player *newOwner )
{
	if( m_applied )
	{
		if( oldOwner )
			changeAllPlans( oldOwner, -1 );
		if( newOwner )
			changeAllPlans( newOwner, 1 );
		else
			m_applied = FALSE;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// applied
	xfer->xferBool( &m_applied );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AllBattlePlansUpdate::loadPostProcess()
{

	// extend base class
	UpdateModule::loadPostProcess();

}
