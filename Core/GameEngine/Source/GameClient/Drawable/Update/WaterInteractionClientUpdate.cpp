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

// FILE: WaterInteractionClientUpdate.cpp /////////////////////////////////////
// See WaterInteractionClientUpdate.h.
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/Module/WaterInteractionClientUpdate.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/WaterDisturbance.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/PhysicsUpdate.h"

//-------------------------------------------------------------------------------------------------
WaterInteractionClientUpdateModuleData::WaterInteractionClientUpdateModuleData() :
	m_hullLength(0.0f),
	m_hullWidth(0.0f),
	m_minSpeed(3.0f / LOGICFRAMES_PER_SECOND),
	m_maxSpeed(40.0f / LOGICFRAMES_PER_SECOND),
	m_hullHeight(0.6f),
	m_hullFoam(0.5f),
	m_idleRipple(0.15f),
	m_wakeSpacing(10.0f),
	m_wakeHeight(0.25f),
	m_wakeFoam(0.3f),
	m_maxHeightAboveWater(5.0f),
	m_maxDepth(10.0f)
{
}

//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ClientUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "HullLength",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_hullLength) },
		{ "HullWidth",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_hullWidth) },
		{ "MinSpeed",				INI::parseVelocityReal,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_minSpeed) },
		{ "MaxSpeed",				INI::parseVelocityReal,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_maxSpeed) },
		{ "HullHeight",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_hullHeight) },
		{ "HullFoam",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_hullFoam) },
		{ "IdleRipple",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_idleRipple) },
		{ "WakeSpacing",			INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeSpacing) },
		{ "WakeHeight",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeHeight) },
		{ "WakeFoam",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeFoam) },
		{ "MaxHeightAboveWater",	INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_maxHeightAboveWater) },
		{ "MaxDepth",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_maxDepth) },
		{ "BowWaveParticleSystem",	INI::parseAsciiString,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_bowWaveParticleSystem) },
		{ "WakeParticleSystem",		INI::parseAsciiString,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeParticleSystem) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
WaterInteractionClientUpdate::WaterInteractionClientUpdate( Thing *thing, const ModuleData* moduleData ) :
	ClientUpdateModule( thing, moduleData ),
	m_bowWaveSystem(INVALID_PARTICLE_SYSTEM_ID),
	m_wakeSystem(INVALID_PARTICLE_SYSTEM_ID),
	m_hasWakePosition(FALSE)
{
	m_lastWakePosition.zero();
}

//-------------------------------------------------------------------------------------------------
WaterInteractionClientUpdate::~WaterInteractionClientUpdate()
{
	ParticleSystem *system = TheParticleSystemManager ? TheParticleSystemManager->findParticleSystem(m_bowWaveSystem) : nullptr;
	if (system)
		system->destroy();
	system = TheParticleSystemManager ? TheParticleSystemManager->findParticleSystem(m_wakeSystem) : nullptr;
	if (system)
		system->destroy();
}

//-------------------------------------------------------------------------------------------------
/** A particle system attached to the drawable: made when first needed, emitting while active, and with its
	* particles' speed and count scaled by strength (0..1). The ID is looked up each time, as the system may be gone. */
//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::updateParticleSystem(ParticleSystemID& id, const AsciiString& name, Bool active, Real strength)
{
	if (name.isEmpty() || TheParticleSystemManager == nullptr)
		return;
	ParticleSystem *system = TheParticleSystemManager->findParticleSystem(id);
	if (system == nullptr)
	{
		id = INVALID_PARTICLE_SYSTEM_ID;
		if (!active)
			return;
		const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(name);
		if (tmpl == nullptr)
			return;
		system = TheParticleSystemManager->createParticleSystem(tmpl);
		if (system == nullptr)
			return;
		system->attachToDrawable(getDrawable());
		id = system->getSystemID();
	}
	if (active)
	{
		const Coord3D speed = { strength, strength, strength };
		system->setVelocityMultiplier(&speed);
		system->setBurstCountMultiplier(strength);
		system->start();
	}
	else
	{
		system->stop();
	}
}

//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::clientUpdate()
{
	const WaterInteractionClientUpdateModuleData *data = getWaterInteractionClientUpdateModuleData();
	Drawable *draw = getDrawable();
	const Object *obj = draw ? draw->getObject() : nullptr;
	if (obj == nullptr || TheTerrainLogic == nullptr)
		return;

	// Only at the water's surface, and only where the local player sees it (ripples must not give away a hidden
	// or fogged enemy).
	const Coord3D *pos = draw->getPosition();
	Real waterZ = 0.0f;
	Bool active = TheTerrainLogic->isUnderwater(pos->x, pos->y, &waterZ)
		&& pos->z <= waterZ + data->m_maxHeightAboveWater && pos->z >= waterZ - data->m_maxDepth
		&& !draw->isDrawableEffectivelyHidden();
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (active && localPlayer && obj->getShroudedStatus(localPlayer->getPlayerIndex()) >= OBJECTSHROUD_FOGGED)
		active = FALSE;

	const PhysicsBehavior *physics = obj->getPhysics();
	const Real speed = physics ? fabs(physics->getForwardSpeed2D()) : 0.0f;
	const Bool moving = active && speed >= data->m_minSpeed;
	const Real strength = moving ? min(speed / max(data->m_maxSpeed, 0.001f), 1.0f) : 0.0f;

	updateParticleSystem(m_bowWaveSystem, data->m_bowWaveParticleSystem, moving, strength);
	updateParticleSystem(m_wakeSystem, data->m_wakeParticleSystem, moving, strength);

	if (!active || !TheWaterDisturbance->isActive())
	{
		m_hasWakePosition = FALSE;
		return;
	}

	// The hull's size: from the INI, else from the geometry (a box: its two radii; anything else: its radius).
	const GeometryInfo& geometry = obj->getGeometryInfo();
	Real length = data->m_hullLength;
	Real width = data->m_hullWidth;
	if (length <= 0.0f)
		length = 2.0f * geometry.getMajorRadius();
	if (width <= 0.0f)
		width = 2.0f * (geometry.getGeomType() == GEOMETRY_BOX ? geometry.getMinorRadius() : geometry.getMajorRadius());
	const Real angle = obj->getOrientation();
	Coord3D surface = *pos;
	surface.z = waterZ;

	const Real hull = moving ? data->m_idleRipple + (data->m_hullHeight - data->m_idleRipple) * strength : data->m_idleRipple;
	const Real foam = moving ? data->m_hullFoam * strength : data->m_idleRipple * 0.5f;
	TheWaterDisturbance->addStamp(surface, angle, length, width, hull, foam, 0.0f, WaterDisturbance::SHAPE_HULL, TRUE);

	// The wake: a splash behind the stern every WakeSpacing world units moved.
	if (!moving)
	{
		m_hasWakePosition = FALSE;
		return;
	}
	Coord3D stern = surface;
	stern.x -= cosf(angle) * length * 0.5f;
	stern.y -= sinf(angle) * length * 0.5f;
	if (!m_hasWakePosition)
	{
		m_lastWakePosition = stern;
		m_hasWakePosition = TRUE;
		return;
	}
	const Real dx = stern.x - m_lastWakePosition.x;
	const Real dy = stern.y - m_lastWakePosition.y;
	if (dx * dx + dy * dy >= data->m_wakeSpacing * data->m_wakeSpacing)
	{
		TheWaterDisturbance->addStamp(stern, angle, width * 1.2f, width * 1.2f, data->m_wakeHeight * strength,
			data->m_wakeFoam * strength, 0.0f, WaterDisturbance::SHAPE_DISC, FALSE);
		m_lastWakePosition = stern;
	}
}

//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::crc( Xfer *xfer )
{
	ClientUpdateModule::crc( xfer );
}

//-------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version; the particle systems are made again after a load */
//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	ClientUpdateModule::xfer( xfer );
}

//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::loadPostProcess()
{
	ClientUpdateModule::loadPostProcess();
	m_bowWaveSystem = INVALID_PARTICLE_SYSTEM_ID;
	m_wakeSystem = INVALID_PARTICLE_SYSTEM_ID;
	m_hasWakePosition = FALSE;
}
