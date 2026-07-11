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

// FILE: ChronoSphereUpdateModule.h /////////////////////////////////////////////////////////////////
// Desc:   Special power update module for a Red Alert 2 style chronosphere. The player selects two
//         locations (source and destination); the second selection can be canceled. This first
//         increment only captures the two points and wires the module - the teleport effect is TODO.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "Common/Science.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class SpecialPowerModuleInterface;
class FXList;
class ObjectCreationList;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class ChronoSphereUpdateModuleData : public ModuleData
{
public:
	SpecialPowerTemplate *m_specialPowerTemplate;

	UnsignedInt			m_teleportDelayFrames;	///< delay before the teleport happens
	KindOfMaskType	m_requiredKindOf;				///< whitelist: units must match to be affected
	KindOfMaskType	m_forbiddenKindOf;			///< blacklist: units matching are never affected
	Real						m_radius;								///< radius of the affected area at each point

	FXList					*m_sourceFX;						///< FX at the source area
	FXList					*m_targetFX;						///< FX at the destination area
	FXList					*m_unitSourceFX;				///< FX on each teleported unit at the source
	FXList					*m_unitTargetFX;				///< FX on each teleported unit at the destination

	const ObjectCreationList	*m_sourceOCL;	///< OCL fired at the source when the power activates (instant, ignores TeleportDelay)
	const ObjectCreationList	*m_targetOCL;	///< OCL fired at the destination when the power activates (instant)

	ChronoSphereUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class ChronoSphereUpdateModule : public SpecialPowerUpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ChronoSphereUpdateModule, "ChronoSphereUpdateModule" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ChronoSphereUpdateModule, ChronoSphereUpdateModuleData );

public:

	ChronoSphereUpdateModule( Thing *thing, const ModuleData* moduleData );
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

	// The chronosphere delivers its destination (the 2nd click) through the existing
	// overridable-destination channel - see Object::doSpecialPowerAtTwoLocations.
	virtual Bool doesSpecialPowerHaveOverridableDestinationActive() const { return m_active; }
	virtual Bool doesSpecialPowerHaveOverridableDestination() const { return true; }
	virtual void setSpecialPowerOverridableDestination( const Coord3D *loc );

	virtual void onObjectCreated();
	virtual UpdateSleepTime update();

	// termination conditions
	virtual DisabledMaskType getDisabledTypesToProcess() const { return MAKE_DISABLED_MASK4( DISABLED_SUBDUED, DISABLED_UNDERPOWERED, DISABLED_EMP, DISABLED_HACKED ); }

protected:

	void doChronoTeleport();	///< relocate all matching objects from source to destination, playing FX

	SpecialPowerModuleInterface*	m_specialPowerModule;	///< cached paired power module (recharge/cost/timer)

	Coord3D			m_sourceLocation;		///< first click - where things teleport FROM
	Coord3D			m_destLocation;			///< second click - where things teleport TO
	UnsignedInt	m_teleportFrame;		///< logic frame at which the teleport fires (activation + TeleportDelay)
	Bool				m_active;						///< TRUE while a teleport is pending (armed, not yet fired)
};
