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

// FILE: FireWeaponOnKillBehavior.cpp /////////////////////////////////////////////////////////////
// Desc:   Fires a weapon at the position of an object this object kills.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/FireWeaponOnKillBehavior.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
FireWeaponOnKillBehavior::FireWeaponOnKillBehavior( Thing *thing, const ModuleData* moduleData ) :
	BehaviorModule( thing, moduleData )
{
	if (getFireWeaponOnKillBehaviorModuleData()->m_initiallyActive)
	{
		giveSelfUpgrade();
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
FireWeaponOnKillBehavior::~FireWeaponOnKillBehavior( void )
{
}

//-------------------------------------------------------------------------------------------------
/** The kill callback. */
//-------------------------------------------------------------------------------------------------
void FireWeaponOnKillBehavior::onKilledObject( Object *victim, const DamageInfo *damageInfo )
{
	const FireWeaponOnKillBehaviorModuleData* d = getFireWeaponOnKillBehaviorModuleData();

	if (!isUpgradeActive())
		return;

	if (!d->m_killMuxData.isKillApplicable(getObject(), victim, damageInfo))
		return;

	if (d->m_killWeapon && victim)
	{
		TheWeaponStore->createAndFireTempWeapon(d->m_killWeapon, getObject(), victim->getPosition());
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void FireWeaponOnKillBehavior::crc( Xfer *xfer )
{
	// extend base class
	BehaviorModule::crc( xfer );

	// extend upgrade mux
	UpgradeMux::upgradeMuxCRC( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void FireWeaponOnKillBehavior::xfer( Xfer *xfer )
{
	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	BehaviorModule::xfer( xfer );

	// extend upgrade mux
	UpgradeMux::upgradeMuxXfer( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void FireWeaponOnKillBehavior::loadPostProcess( void )
{
	// extend base class
	BehaviorModule::loadPostProcess();

	// extend upgrade mux
	UpgradeMux::upgradeMuxLoadPostProcess();
}
