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

// FILE: MultiLocationSpecialPowerUpdate.h //////////////////////////////////////////////////////////
// Desc:   Generic special power update module driven by a configurable number of clicked target
//         points (NEED_N_TARGET_POS on the command button, NumberOfTargets = N). When all points
//         are captured they arrive together via setSpecialPowerMultiLocations. This foundation
//         increment captures + logs the points and starts the cooldown; no gameplay effect yet.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/Science.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"
#include "GameLogic/Module/OCLSpecialPower.h"	// OCLCreateLocType

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class SpecialPowerModuleInterface;
class ObjectCreationList;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class MultiLocationSpecialPowerUpdateModuleData : public ModuleData
{
public:

	// science -> OCL override, mirrors OCLSpecialPowerModuleData::Upgrades
	struct Upgrades
	{
		ScienceType									m_science;
		const ObjectCreationList*		m_ocl;

		Upgrades() : m_science(SCIENCE_INVALID), m_ocl(nullptr) {}
	};

	SpecialPowerTemplate *m_specialPowerTemplate;

	std::vector<Upgrades>			m_upgradeOCL;			///< per-science OCL overrides
	const ObjectCreationList*	m_defaultOCL;			///< OCL fired at each captured point
	OCLCreateLocType					m_createLoc;			///< where the OCL is created
	UnsignedInt								m_initialDelay;		///< delay before the first OCL fires (INI in ms, stored as frames)
	UnsignedInt								m_delay;				///< delay between successive OCLs (INI in ms, stored as frames)

	MultiLocationSpecialPowerUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class MultiLocationSpecialPowerUpdate : public SpecialPowerUpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( MultiLocationSpecialPowerUpdate, "MultiLocationSpecialPowerUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( MultiLocationSpecialPowerUpdate, MultiLocationSpecialPowerUpdateModuleData );

public:

	MultiLocationSpecialPowerUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// SpecialPowerUpdateInterface
	virtual Bool initiateIntentToDoSpecialPower(const SpecialPowerTemplate *specialPowerTemplate, const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions );
	virtual Bool isSpecialAbility() const { return false; }
	virtual Bool isSpecialPower() const { return true; }
	virtual Bool isActive() const { return m_active; }
	virtual SpecialPowerUpdateInterface* getSpecialPowerUpdateInterface() { return this; }
	virtual CommandOption getCommandOption() const { return (CommandOption)0; }
	virtual Bool isPowerCurrentlyInUse( const CommandButton *command = nullptr ) const { return m_active; }
	virtual ScienceType getExtraRequiredScience() const { return SCIENCE_INVALID; }

	// All N captured target points arrive together through the multi-location channel - see
	// Object::doSpecialPowerAtMultipleLocations. doesSpecialPowerHaveOverridableDestination stays
	// true so the finder in Object reaches this module.
	// This power delivers all points at once and never redirects in flight, so it must NOT report an
	// active overridable destination - otherwise the client keeps the targeting cursor alive for the
	// whole spawn sequence (canOverrideSpecialPowerDestination). Only the non-active variant stays true
	// so the delivery finder (findSpecialPowerWithOverridableDestination) still reaches this module.
	virtual Bool doesSpecialPowerHaveOverridableDestinationActive() const { return FALSE; }
	virtual Bool doesSpecialPowerHaveOverridableDestination() const { return true; }
	virtual void setSpecialPowerOverridableDestination( const Coord3D *loc ) {}	///< unused: delivery goes through setSpecialPowerMultiLocations
	virtual void setSpecialPowerMultiLocations( const std::vector<Coord3D>& locs );

	virtual void onObjectCreated();
	virtual UpdateSleepTime update();

	// termination conditions
	virtual DisabledMaskType getDisabledTypesToProcess() const { return MAKE_DISABLED_MASK4( DISABLED_SUBDUED, DISABLED_UNDERPOWERED, DISABLED_EMP, DISABLED_HACKED ); }

protected:

	const ObjectCreationList* findOCL() const;					///< science-upgraded OCL, else the default
	void fireOclAtLocation( const Coord3D *loc );				///< spawn the OCL at one captured point, honoring CreateLocation

	SpecialPowerModuleInterface*	m_specialPowerModule;	///< cached paired power module (recharge/cost/timer)

	std::vector<Coord3D>	m_locations;	///< all captured target points (in click order)
	Bool									m_active;			///< TRUE while the OCL spawn sequence is running
	Int										m_spawnIndex;	///< index of the next point to spawn an OCL at
};
