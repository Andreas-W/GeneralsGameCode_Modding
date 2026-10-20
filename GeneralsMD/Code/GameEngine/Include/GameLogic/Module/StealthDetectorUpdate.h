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

// FILE: StealthDetectorUpdate.h //////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, May 2002
// Desc:	 An update that checks for a status bit to stealth the owning object
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;

//-------------------------------------------------------------------------------------------------
class StealthDetectorUpdateModuleData : public UpdateModuleData
{
public:
	UpgradeMuxData									m_upgradeMuxData;
	Bool														m_initiallyActive;				///< only matters with TriggeredBy; without it the detector is always active
	UnsignedInt											m_updateRate;
	Real														m_detectionRange;
	Bool														m_initiallyDisabled;
	AudioEventRTS										m_pingSound;
	AudioEventRTS										m_loudPingSound;
	const ParticleSystemTemplate*   m_IRBeaconParticleSysTmpl;
	const ParticleSystemTemplate*   m_IRParticleSysTmpl;
	const ParticleSystemTemplate*   m_IRBrightParticleSysTmpl;
	const ParticleSystemTemplate*   m_IRGridParticleSysTmpl;
	AsciiString											m_IRParticleSysBone;
	KindOfMaskType									m_extraDetectKindof;			///< units must match any kindof bits set here, in order to be detected
	KindOfMaskType									m_extraDetectKindofNot;		///< units must NOT match any kindof bits set here, in order to be detected
	Bool														m_canDetectWhileGarrisoned;
	Bool														m_canDetectWhileTransported;
	Bool														m_requiresPlayerOwner;		///< inactive while owned by a non-playable side (neutral, civilian)

	StealthDetectorUpdateModuleData()
	{
		m_initiallyActive = false;
		m_updateRate = 1;
		m_detectionRange = 0.0f;
		m_initiallyDisabled = false;
		m_IRBeaconParticleSysTmpl = nullptr;
		m_IRParticleSysTmpl = nullptr;
		m_IRBrightParticleSysTmpl = nullptr;
		m_IRGridParticleSysTmpl = nullptr;
		m_extraDetectKindof.clear();
		m_extraDetectKindofNot.clear();
		m_canDetectWhileGarrisoned = false;
		m_canDetectWhileTransported = false;
		m_requiresPlayerOwner = false;
	}

	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
class StealthDetectorUpdate : public UpdateModule, public UpgradeMux
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( StealthDetectorUpdate, "StealthDetectorUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( StealthDetectorUpdate, StealthDetectorUpdateModuleData );

public:

	StealthDetectorUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// module methods
	static Int getInterfaceMask() { return UpdateModule::getInterfaceMask() | MODULEINTERFACE_UPGRADE; }

	// BehaviorModule
	virtual UpgradeModuleInterface* getUpgrade() override { return this; }

	Bool isSDEnabled() const { return m_enabled; }
	void setSDEnabled( Bool enabled );
	virtual UpdateSleepTime update() override;
	virtual DisabledMaskType getDisabledTypesToProcess() const override { return MAKE_DISABLED_MASK( DISABLED_HELD ); }
	virtual void onCapture( Player *oldOwner, Player *newOwner ) override;

	// Removing the trigger upgrade switches the detector off again.
	virtual Bool resetUpgrade( const UpgradeMaskType& keyMask ) override;

protected:

	virtual void upgradeImplementation() override;
	virtual void getUpgradeActivationMasks(UpgradeMaskType& activation, UpgradeMaskType& conflicting) const override
	{
		getStealthDetectorUpdateModuleData()->m_upgradeMuxData.getUpgradeActivationMasks(activation, conflicting);
	}
	virtual void performUpgradeFX() override
	{
		getStealthDetectorUpdateModuleData()->m_upgradeMuxData.performUpgradeFX(getObject());
	}
	virtual void processUpgradeRemoval() override
	{
		getStealthDetectorUpdateModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval(getObject());
	}
	virtual Bool requiresAllActivationUpgrades() const override
	{
		return getStealthDetectorUpdateModuleData()->m_upgradeMuxData.m_requiresAllTriggers;
	}
	virtual Bool isSubObjectsUpgrade() override { return false; }

	/// Without TriggeredBy the detector needs no upgrade.
	Bool isUpgradeActive() const { return getStealthDetectorUpdateModuleData()->m_upgradeMuxData.m_activationUpgradeNames.empty() || isAlreadyUpgraded(); }

private:
	Bool m_enabled;

};
