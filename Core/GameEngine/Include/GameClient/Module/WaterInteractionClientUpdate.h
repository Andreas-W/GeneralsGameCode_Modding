/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Electronic Arts Inc.
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

// FILE: WaterInteractionClientUpdate.h ///////////////////////////////////////
// What a boat, a ship or an amphibious unit does to the water while it is at the surface, whatever draws
// it: its hull and a wake trail in the water disturbance (Direct3D 12 water), and particle systems scaled
// by its speed. Client side only.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/ClientUpdateModule.h"
#include "GameClient/ParticleSys.h"

//-------------------------------------------------------------------------------------------------
class WaterInteractionClientUpdateModuleData : public ClientUpdateModuleData
{
public:
	Real m_hullLength;			///< world units along the boat, 0 = from its geometry
	Real m_hullWidth;			///< world units across, 0 = from its geometry
	Real m_minSpeed;			///< per logic frame; slower counts as standing
	Real m_maxSpeed;			///< per logic frame; the hull and wake are at full strength from here
	Real m_hullHeight;			///< strength of the hull's crest and trough at full speed
	Real m_hullFoam;			///< strength of the foam around the hull at full speed
	Real m_idleRipple;			///< strength of the hull while standing
	Real m_wakeSpacing;			///< world units between the splashes of the wake trail
	Real m_wakeHeight;			///< strength of a splash of the wake at full speed
	Real m_wakeFoam;			///< strength of its foam at full speed
	Real m_maxHeightAboveWater;	///< higher above the water nothing happens
	Real m_maxDepth;			///< deeper under the water nothing happens
	AsciiString m_bowWaveParticleSystem;	///< attached while moving, scaled by speed
	AsciiString m_wakeParticleSystem;		///< attached while moving, scaled by speed

	WaterInteractionClientUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
class WaterInteractionClientUpdate : public ClientUpdateModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WaterInteractionClientUpdate, "WaterInteractionClientUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( WaterInteractionClientUpdate, WaterInteractionClientUpdateModuleData );

public:

	WaterInteractionClientUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void clientUpdate() override;

protected:

	void updateParticleSystem(ParticleSystemID& id, const AsciiString& name, Bool active, Real strength);

	ParticleSystemID m_bowWaveSystem;
	ParticleSystemID m_wakeSystem;
	Coord3D m_lastWakePosition;
	Bool m_hasWakePosition;
};
