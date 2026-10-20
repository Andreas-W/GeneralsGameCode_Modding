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

// FILE: PointDefenseLaserUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Author: Kris Morness, August 2002
// Desc:   Update module to handle independent targeting of point defense laser.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class ThingTemplate;
class WeaponTemplate;


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class PointDefenseLaserUpdateModuleData : public ModuleData
{
public:
	UpgradeMuxData	m_upgradeMuxData;
	Bool						m_initiallyActive;	///< only matters with TriggeredBy; without it the laser is always active
	WeaponTemplate	*m_weaponTemplate;
	KindOfMaskType	m_primaryTargetKindOf;
	KindOfMaskType  m_secondaryTargetKindOf;
	UnsignedInt			m_scanFrames;
	Real						m_scanRange;
	Real						m_velocityFactor;

	PointDefenseLaserUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
/** The default	update module */
//-------------------------------------------------------------------------------------------------
class PointDefenseLaserUpdate : public UpdateModule, public UpgradeMux
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( PointDefenseLaserUpdate, "PointDefenseLaserUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( PointDefenseLaserUpdate, PointDefenseLaserUpdateModuleData );

public:

	PointDefenseLaserUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// module methods
	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | MODULEINTERFACE_UPGRADE; }

	// BehaviorModule
	virtual UpgradeModuleInterface* getUpgrade() override { return this; }

	virtual void onObjectCreated() override;
	virtual UpdateSleepTime update() override;

	// Removing the trigger upgrade switches the laser off again.
	virtual Bool resetUpgrade( const UpgradeMaskType& keyMask ) override;

	Object* scanClosestTarget();
	void fireWhenReady();

protected:

	virtual void upgradeImplementation() override;
	virtual void getUpgradeActivationMasks(UpgradeMaskType& activation, UpgradeMaskType& conflicting) const override
	{
		getPointDefenseLaserUpdateModuleData()->m_upgradeMuxData.getUpgradeActivationMasks(activation, conflicting);
	}
	virtual void performUpgradeFX() override
	{
		getPointDefenseLaserUpdateModuleData()->m_upgradeMuxData.performUpgradeFX(getObject());
	}
	virtual void processUpgradeRemoval() override
	{
		getPointDefenseLaserUpdateModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval(getObject());
	}
	virtual Bool requiresAllActivationUpgrades() const override
	{
		return getPointDefenseLaserUpdateModuleData()->m_upgradeMuxData.m_requiresAllTriggers;
	}
	virtual Bool isSubObjectsUpgrade() override { return false; }

	/// Without TriggeredBy the laser needs no upgrade.
	Bool isUpgradeActive() const { return getPointDefenseLaserUpdateModuleData()->m_upgradeMuxData.m_activationUpgradeNames.empty() || isAlreadyUpgraded(); }

	ObjectID m_bestTargetID;
	Bool m_inRange;
	Int m_nextScanFrames;
	Int m_nextShotAvailableInFrames;
};
