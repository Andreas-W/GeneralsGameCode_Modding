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
	m_wakeSpacing(8.0f),
	m_wakeHeight(0.25f),
	m_wakeFoam(0.3f),
	m_wakeRingRadius(0.0f),
	m_wakeRingTime(LOGICFRAMES_PER_SECOND * 2),
	m_wakeRingOffset(0.5f),
	m_bowWaveOffset(0.5f),
	m_trailMinSpeed(3.0f / LOGICFRAMES_PER_SECOND),
	m_maxHeightAboveWater(5.0f),
	m_maxDepth(10.0f),
	m_submergedHighlight(0.0f),
	m_highlightVisibleTo(HIGHLIGHT_VISIBLE_TO_ALLIES)
{
	m_submergedHighlightColor.setFromInt(0);
}

//-------------------------------------------------------------------------------------------------
static const char *const TheHighlightVisibilityNames[] = { "OWNER", "ALLIES", "ALL", nullptr };

//-------------------------------------------------------------------------------------------------
/** TrailParticleSystem = <bone> <particle system>, like ParticleSysBone; every entry adds one. */
//-------------------------------------------------------------------------------------------------
static void parseWaterTrail(INI* ini, void * /*instance*/, void *store, const void * /*userData*/)
{
	WaterTrailInfo info;
	info.boneName = ini->getNextAsciiString();
	info.boneName.toLower();
	info.particleSystem = ini->getNextAsciiString();
	((std::vector<WaterTrailInfo> *)store)->push_back(info);
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
		{ "WakeRingRadius",			INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeRingRadius) },
		{ "WakeRingTime",			INI::parseDurationUnsignedInt,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeRingTime) },
		{ "WakeRingOffset",			INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_wakeRingOffset) },
		{ "MaxHeightAboveWater",	INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_maxHeightAboveWater) },
		{ "MaxDepth",				INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_maxDepth) },
		{ "BowWaveParticleSystem",	INI::parseAsciiString,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_bowWaveParticleSystem) },
		{ "BowWaveOffset",			INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_bowWaveOffset) },
		{ "TrailParticleSystem",	parseWaterTrail,		nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_trails) },
		{ "TrailMinSpeed",			INI::parseVelocityReal,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_trailMinSpeed) },
		{ "IdleWavesParticleSystem",	INI::parseAsciiString,	nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_idleWavesParticleSystem) },
		{ "SubmergedHighlight",		INI::parseReal,			nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_submergedHighlight) },
		{ "SubmergedHighlightColor",	INI::parseRGBColor,		nullptr, offsetof(WaterInteractionClientUpdateModuleData, m_submergedHighlightColor) },
		{ "HighlightVisibleTo",		INI::parseIndexList,	TheHighlightVisibilityNames, offsetof(WaterInteractionClientUpdateModuleData, m_highlightVisibleTo) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
WaterInteractionClientUpdate::WaterInteractionClientUpdate( Thing *thing, const ModuleData* moduleData ) :
	ClientUpdateModule( thing, moduleData ),
	m_bowWaveSystem(INVALID_PARTICLE_SYSTEM_ID),
	m_idleWavesSystem(INVALID_PARTICLE_SYSTEM_ID),
	m_hasWakePosition(FALSE)
{
	m_lastWakePosition.zero();
}

//-------------------------------------------------------------------------------------------------
WaterInteractionClientUpdate::~WaterInteractionClientUpdate()
{
	destroyParticleSystem(m_bowWaveSystem);
	destroyParticleSystem(m_idleWavesSystem);
	for (size_t i = 0; i < m_trailSystems.size(); ++i)
		destroyParticleSystem(m_trailSystems[i]);
}

//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::destroyParticleSystem(ParticleSystemID& id)
{
	ParticleSystem *system = TheParticleSystemManager ? TheParticleSystemManager->findParticleSystem(id) : nullptr;
	if (system)
		system->destroy();
	id = INVALID_PARTICLE_SYSTEM_ID;
}

//-------------------------------------------------------------------------------------------------
/** A particle system attached to the drawable: made when first needed, emitting while active, and with its
	* particles' speed and count scaled by strength (0..1). The ID is looked up each time, as the system may be gone.
	* Returns the system while active, for the caller to place it in the drawable's space. */
//-------------------------------------------------------------------------------------------------
ParticleSystem *WaterInteractionClientUpdate::updateParticleSystem(ParticleSystemID& id, const AsciiString& name, Bool active,
	Real strength)
{
	if (name.isEmpty() || TheParticleSystemManager == nullptr)
		return nullptr;
	ParticleSystem *system = TheParticleSystemManager->findParticleSystem(id);
	if (system == nullptr)
	{
		id = INVALID_PARTICLE_SYSTEM_ID;
		if (!active)
			return nullptr;
		const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(name);
		if (tmpl == nullptr)
			return nullptr;
		system = TheParticleSystemManager->createParticleSystem(tmpl);
		if (system == nullptr)
			return nullptr;
		system->attachToDrawable(getDrawable());
		id = system->getSystemID();
	}
	if (!active)
	{
		system->stop();
		return nullptr;
	}
	const Coord3D speed = { strength, strength, strength };
	system->setVelocityMultiplier(&speed);
	system->setBurstCountMultiplier(strength);
	system->start();
	return system;
}

//-------------------------------------------------------------------------------------------------
/** While the object is wholly under the water and the local player may see it highlighted: a silhouette over
	* the water (drawn by the scene after the water, with any renderer), and with the Direct3D 12 water a clearer
	* view of it through the water (the highlight of the disturbance). Set every frame, cleared otherwise. */
//-------------------------------------------------------------------------------------------------
void WaterInteractionClientUpdate::updateHighlight(const Object *obj)
{
	const WaterInteractionClientUpdateModuleData *data = getWaterInteractionClientUpdateModuleData();
	Drawable *draw = getDrawable();
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	const Coord3D *pos = draw->getPosition();
	Real waterZ = 0.0f;
	Bool show = data->m_submergedHighlight > 0.0f && localPlayer != nullptr && !draw->isDrawableEffectivelyHidden()
		&& TheTerrainLogic->isUnderwater(pos->x, pos->y, &waterZ)
		&& pos->z + obj->getGeometryInfo().getMaxHeightAbovePosition() < waterZ - 1.0f
		&& obj->getShroudedStatus(localPlayer->getPlayerIndex()) < OBJECTSHROUD_FOGGED;
	if (show)
	{
		const Player *owner = obj->getControllingPlayer();
		switch (data->m_highlightVisibleTo)
		{
			case HIGHLIGHT_VISIBLE_TO_OWNER:
				show = owner == localPlayer;
				break;
			case HIGHLIGHT_VISIBLE_TO_ALLIES:
				show = owner == localPlayer || localPlayer->getRelationship(obj->getTeam()) == ALLIES;
				break;
			default:
				break;
		}
	}
	if (!show)
	{
		if (draw->getUnderwaterHighlight() != 0.0f)
			draw->setUnderwaterHighlight(0.0f, data->m_submergedHighlightColor);
		return;
	}

	RGBColor color = data->m_submergedHighlightColor;
	if (color.red == 0.0f && color.green == 0.0f && color.blue == 0.0f)
		color.setFromInt(obj->getIndicatorColor());
	draw->setUnderwaterHighlight(data->m_submergedHighlight, color);

	if (TheWaterDisturbance->isActive())
	{
		const GeometryInfo& geometry = obj->getGeometryInfo();
		const Real length = data->m_hullLength > 0.0f ? data->m_hullLength : 2.0f * geometry.getMajorRadius();
		const Real width = data->m_hullWidth > 0.0f ? data->m_hullWidth
			: 2.0f * (geometry.getGeomType() == GEOMETRY_BOX ? geometry.getMinorRadius() : geometry.getMajorRadius());
		Coord3D surface = *pos;
		surface.z = waterZ;
		TheWaterDisturbance->addStamp(surface, obj->getOrientation(), length, width, 0.0f, 0.0f, data->m_submergedHighlight,
			WaterDisturbance::SHAPE_HULL, TRUE);
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

	updateHighlight(obj);

	const PhysicsBehavior *physics = obj->getPhysics();
	const Real speed = physics ? fabs(physics->getForwardSpeed2D()) : 0.0f;
	const Bool moving = active && speed >= data->m_minSpeed;
	const Real strength = moving ? min(speed / max(data->m_maxSpeed, 0.001f), 1.0f) : 0.0f;

	// The hull's size: from the INI, else from the geometry (a box: its two radii; anything else: its radius).
	const GeometryInfo& geometry = obj->getGeometryInfo();
	Real length = data->m_hullLength;
	Real width = data->m_hullWidth;
	if (length <= 0.0f)
		length = 2.0f * geometry.getMajorRadius();
	if (width <= 0.0f)
		width = 2.0f * (geometry.getGeomType() == GEOMETRY_BOX ? geometry.getMinorRadius() : geometry.getMajorRadius());

	// The bow wave while moving, along the hull (the drawable's x points forward); the idle waves at the object's
	// position whenever the bow wave is off, on the water.
	ParticleSystem *bowWave = updateParticleSystem(m_bowWaveSystem, data->m_bowWaveParticleSystem, moving, strength);
	if (bowWave)
	{
		const Coord3D offset = { length * 0.5f * data->m_bowWaveOffset, 0.0f, 0.0f };
		bowWave->setPosition(&offset);
	}
	updateParticleSystem(m_idleWavesSystem, data->m_idleWavesParticleSystem, active && !moving, 1.0f);

	// The trails from their bones, from their own minimum speed on. Placed like ParticleSysBone does (the bone's
	// position and turn in the model), every frame, so they follow a changed model; a bone the current model
	// lacks (or NONE) puts the trail at the object's position.
	const Bool trailing = active && speed >= data->m_trailMinSpeed;
	const Real trailStrength = trailing ? min(speed / max(data->m_maxSpeed, 0.001f), 1.0f) : 0.0f;
	if (m_trailSystems.size() != data->m_trails.size())
		m_trailSystems.resize(data->m_trails.size(), INVALID_PARTICLE_SYSTEM_ID);
	for (size_t i = 0; i < data->m_trails.size(); ++i)
	{
		ParticleSystem *trail = updateParticleSystem(m_trailSystems[i], data->m_trails[i].particleSystem, trailing, trailStrength);
		if (trail == nullptr)
			continue;
		Coord3D bonePos;
		Matrix3D boneTransform(true);
		bonePos.zero();
		Matrix3D local(true);
		if (draw->getCurrentClientBonePositions(data->m_trails[i].boneName.str(), 0, &bonePos, &boneTransform, 1) > 0)
		{
			local.Rotate_Z(boneTransform.Get_Z_Rotation());
			local.Set_Translation(Vector3(bonePos.x, bonePos.y, bonePos.z));
		}
		trail->setLocalTransform(&local);
	}

	if (!active || !TheWaterDisturbance->isActive())
	{
		m_hasWakePosition = FALSE;
		return;
	}

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
		// A ring that spreads from a point along the hull (WakeRingOffset, towards the bow by default); the ship
		// moving on away from the rings makes the V of a wake, starting near the bow as on a real ship. Behind the
		// stern a patch of foam, which only fades.
		if (data->m_wakeRingTime > 0)
		{
			Coord3D origin = surface;
			origin.x += cosf(angle) * length * 0.5f * data->m_wakeRingOffset;
			origin.y += sinf(angle) * length * 0.5f * data->m_wakeRingOffset;
			const Real ringRadius = data->m_wakeRingRadius > 0.0f ? data->m_wakeRingRadius : max(width * 4.0f, 40.0f);
			TheWaterDisturbance->addRipple(origin, min(max(width * 0.5f, 8.0f), ringRadius), ringRadius, data->m_wakeHeight * strength,
				data->m_wakeFoam * strength * 0.25f, data->m_wakeRingTime);
		}
		TheWaterDisturbance->addStamp(stern, angle, width * 1.2f, width * 1.2f, data->m_wakeRingTime > 0 ? 0.0f : data->m_wakeHeight * strength,
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
	m_idleWavesSystem = INVALID_PARTICLE_SYSTEM_ID;
	m_trailSystems.clear();
	m_hasWakePosition = FALSE;
}
