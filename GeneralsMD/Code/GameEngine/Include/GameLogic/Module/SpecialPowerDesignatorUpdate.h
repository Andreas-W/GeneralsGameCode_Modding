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

// FILE: SpecialPowerDesignatorUpdate.h /////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SpecialPowerDesignatorUpdate_H_
#define __SpecialPowerDesignatorUpdate_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/RadiusDecalBehavior.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////
class SpecialPowerTemplate;
class Thing;

//-------------------------------------------------------------------------------------------------
class SpecialPowerDesignatorUpdateModuleData : public RadiusDecalBehaviorModuleData
{
public:
	SpecialPowerTemplate* m_specialPowerTemplate;
	Real m_designatorRadius;
	Bool m_alwaysShowDecal;
	ObjectStatusTypes m_triggerStatusType;
	UnsignedInt m_triggerStatusTime;

	SpecialPowerDesignatorUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerDesignatorUpdate : public RadiusDecalBehavior
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SpecialPowerDesignatorUpdate, "SpecialPowerDesignatorUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SpecialPowerDesignatorUpdate, SpecialPowerDesignatorUpdateModuleData )

public:

	SpecialPowerDesignatorUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	//void createRadiusDecal( void );
	//void killRadiusDecal( void );

	// UpdateModuleInterface
	virtual UpdateSleepTime update();

	//virtual DisabledMaskType getDisabledTypesToProcess() const { return MAKE_DISABLED_MASK(DISABLED_HELD); }

	Real getDesignatorRadius() { return getSpecialPowerDesignatorUpdateModuleData()->m_designatorRadius; }

	Bool isValidDesignatorForSpecialPower(const SpecialPowerTemplate* templ);

	void setActive(bool status);

	void triggerSpecialPower();

protected:

	//virtual void upgradeImplementation()
	//{
	//	createRadiusDecal();
	//	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	//}

	//virtual void getUpgradeActivationMasks(UpgradeMaskType& activation, UpgradeMaskType& conflicting) const
	//{
	//	getSpecialPowerDesignatorUpdateModuleData()->m_upgradeMuxData.getUpgradeActivationMasks(activation, conflicting);
	//}

	//virtual void performUpgradeFX()
	//{
	//	getSpecialPowerDesignatorUpdateModuleData()->m_upgradeMuxData.performUpgradeFX(getObject());
	//}

	//virtual void processUpgradeRemoval()
	//{
	//	// I can't take it any more.  Let the record show that I think the UpgradeMux multiple inheritence is CRAP.
	//	getSpecialPowerDesignatorUpdateModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval(getObject());
	//}

	//virtual Bool requiresAllActivationUpgrades() const
	//{
	//	return getSpecialPowerDesignatorUpdateModuleData()->m_upgradeMuxData.m_requiresAllTriggers;
	//}

	//inline Bool isUpgradeActive() const { return isAlreadyUpgraded(); }

	//virtual Bool isSubObjectsUpgrade() { return false; }

private:

	Bool m_targetingActive;  // We are currently looking for targets

	UnsignedInt m_statusClearFrame;

};

#endif // __SpecialPowerDesignatorUpdate_H_

