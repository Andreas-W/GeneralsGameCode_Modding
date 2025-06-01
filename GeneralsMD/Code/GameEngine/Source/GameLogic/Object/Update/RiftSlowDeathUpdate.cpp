#include "GameLogic/Module/RiftSlowDeathUpdate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/ToppleUpdate.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/FXList.h"
#include "GameLogic/Module/PhysicsUpdate.h"

RiftSlowDeathBehaviorModuleData::RiftSlowDeathBehaviorModuleData(void)
{
	m_initialDelay = 0;
	m_rampupTime = 0;
	m_mainDuration = 0;
	m_fadeOutTime = 0;
	m_radius = 0.0f;
	m_damage = 0.0f;
	m_FXmain = NULL;
	m_FXcharge = NULL;
	m_FXfinal = NULL;
}  // end RiftSlowDeathBehaviorModuleData

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void RiftSlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	SlowDeathBehaviorModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "InitialDelay", INI::parseDurationUnsignedInt, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_initialDelay) },
		{ "RampupTime", INI::parseDurationUnsignedInt, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_rampupTime) },
		{ "MainDuration", INI::parseDurationUnsignedInt, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_mainDuration) },
		{ "FadeOutTime", INI::parseDurationUnsignedInt, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_fadeOutTime) },
		{ "Radius", INI::parseReal, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_radius) },
		{ "Damage", INI::parseReal, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_damage) },
		{ "FXMain",					INI::parseFXList, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXmain) },
		{ "FXCharge",					INI::parseFXList, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXcharge) },
		{ "FXFinal",					INI::parseFXList, NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXfinal) },
		{ 0, 0, 0, 0 }
	};

	p.add(dataFieldParse);

}  // end buildFieldParse

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RiftSlowDeathBehavior::RiftSlowDeathBehavior(Thing* thing, const ModuleData* moduleData)
	: SlowDeathBehavior(thing, moduleData)
{
	m_activationFrame = 0;
	m_riftPos.x = 0.0f;
	m_riftPos.y = 0.0f;
	m_riftPos.z = 0.0f;
	m_finalPush = false;
	m_playedMainFX = false;
}  // end RiftSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RiftSlowDeathBehavior::~RiftSlowDeathBehavior(void)
{

}  // end ~RiftSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime RiftSlowDeathBehavior::update(void)
{
	/// @todo srj use SLEEPY_UPDATE here
		// get the module data
	const RiftSlowDeathBehaviorModuleData* modData = getRiftSlowDeathBehaviorModuleData();

	// call the base class cause we're extending functionality
	SlowDeathBehavior::update();

	// get out of here if we're not activated yet
	if (isSlowDeathActivated() == FALSE)
		return UPDATE_SLEEP_NONE;

	// get the current frame
	UnsignedInt currFrame = TheGameLogic->getFrame();

	// when we become activated we want to do a few things
	if (m_activationFrame == 0)
	{
		const Coord3D* projectilePos = getObject()->getPosition();

		// Lock in position when activated
		m_riftPos.x = projectilePos->x;
		m_riftPos.y = projectilePos->y;
		m_riftPos.z = TheTerrainLogic->getGroundHeight(m_riftPos.x, m_riftPos.y) + 100.0f;

		// record the frame
		m_activationFrame = currFrame;

		FXList::doFXPos(modData->m_FXcharge, &m_riftPos);
		//DEBUG_LOG(("Rift Position(%f, %f, %f)\n", m_riftPos.x, m_riftPos.y, m_riftPos.z));
		//DEBUG_LOG(("Rift Frame Start: %d\n", m_activationFrame));
	}  // end if

	UnsignedInt frameProgress = currFrame - m_activationFrame;

	if (frameProgress < modData->m_initialDelay) {
	// Charging Up

		//DEBUG_LOG(("RIFT Initial: %d\n", frameProgress));
	}
	else if (frameProgress < modData->m_initialDelay + modData->m_rampupTime) {
		// rampup phase
		float rampup = static_cast<float>(frameProgress - modData->m_initialDelay) / static_cast<float>(modData->m_rampupTime);
		
		if (!m_playedMainFX) {
			FXList::doFXPos(modData->m_FXmain, &m_riftPos);
			m_playedMainFX = true;
		}

		applyPull(rampup);
		//DEBUG_LOG(("RIFT Charge: %d\n", frameProgress));
	}
	else if (frameProgress < modData->m_initialDelay + modData->m_rampupTime + modData->m_mainDuration) {
		// main damage phase
		
		if (!m_playedMainFX) {
			FXList::doFXPos(modData->m_FXmain, &m_riftPos);
			m_playedMainFX = true;
		}

		applyPull(1.0f);
		//DEBUG_LOG(("RIFT Main: %d\n", frameProgress));
	}
	else if (frameProgress < modData->m_initialDelay + modData->m_rampupTime + modData->m_mainDuration + modData->m_fadeOutTime) {
		// fade out phase
		float fadeout = 1.0f - (static_cast<float>(frameProgress - (modData->m_initialDelay + modData->m_rampupTime + modData->m_mainDuration)) / static_cast<float>(modData->m_fadeOutTime));

		applyPull(fadeout);
		//DEBUG_LOG(("RIFT Fade: %d\n", frameProgress));
	}
	else {
		// do final push,
		if (!m_finalPush) {
			FXList::doFXPos(modData->m_FXfinal, &m_riftPos);
			doFinalPush();
			m_finalPush = true;
		}
		//DEBUG_LOG(("RIFT FINAL: %d\n", frameProgress));
	}

	
	return UPDATE_SLEEP_NONE;

}  // end update

void RiftSlowDeathBehavior::applyPull(float magnitude_multiplier) const
{
	const RiftSlowDeathBehaviorModuleData* data = getRiftSlowDeathBehaviorModuleData();

	DamageInfo damageInfo;
	damageInfo.in.m_damageType = DAMAGE_EXPLOSION;
	damageInfo.in.m_deathType = DEATH_EXPLODED;
	damageInfo.in.m_sourceID = getObject()->getID();
	damageInfo.in.m_amount = data->m_damage * magnitude_multiplier;
	damageInfo.in.m_shockWaveVector.zero();
	damageInfo.in.m_shockWaveAmount = 0.5f;
	damageInfo.in.m_shockWaveRadius = data->m_radius;
	damageInfo.in.m_shockWaveTaperOff = 1.0f;

	// scan objects in our region
	ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(&m_riftPos,
		data->m_radius,
		FROM_CENTER_2D, NULL);

	MemoryPoolObjectHolder hold(iter);

	for (Object* currentObj = iter->first(); currentObj != NULL; currentObj = iter->next())
	{
		// get other position
		const Coord3D * objPos = currentObj->getPosition();

		// compute vector from the object to the rift center
		Coord3D forceVector;
		forceVector.x = m_riftPos.x - objPos->x;
		forceVector.y = m_riftPos.y - objPos->y;
		forceVector.z = m_riftPos.z - objPos->z;
				
		/*if (objPos->z > m_riftPos.z) {
			// avoid overshooting
			forceVector.z = -1.0f;
		}*/
		damageInfo.in.m_shockWaveVector = forceVector;
		//Real distance = forceVector.length();
		//Real pull_strength = (distance / data->m_radius) * 0.5f + 0.5f;

		if (!currentObj->isKindOf(KINDOF_IMMOBILE) && !currentObj->isKindOf(KINDOF_PROJECTILE) && !currentObj->isKindOf(KINDOF_INERT) && objPos->length() > 3.0f*magnitude_multiplier) {
			Coord3D offset = forceVector;
			offset.normalize();
			offset.scale(3.0f * magnitude_multiplier);
			offset.x += objPos->x;
			offset.y += objPos->y;
			offset.z += objPos->z;
			currentObj->setPosition(&offset);
		}
		/*PhysicsBehavior* phys = currentObj->getPhysics();
		Real phys_mult = 0.1f;
		if (phys != NULL) {
			phys_mult = phys->getMass();

			const Coord3D* current_velocity = phys->getVelocity();
			Coord3D minus_vel;
			minus_vel.x = -current_velocity->x;
			minus_vel.y = -current_velocity->y;
			minus_vel.z = -current_velocity->z;
			phys->applyForce(&minus_vel);
		}*/
		//pull_strength *= magnitude_multiplier * phys_mult;
		//currentObj->topple(&forceVector, pull_strength * 10.0f * magnitude_multiplier, TOPPLE_OPTIONS_NO_BOUNCE | TOPPLE_OPTIONS_NO_FX);

		//damageInfo.in.m_shockWaveAmount = pull_strength * 0.2f;
		currentObj->attemptDamage(&damageInfo);
	}
}

void RiftSlowDeathBehavior::doFinalPush() const
{
	const RiftSlowDeathBehaviorModuleData* data = getRiftSlowDeathBehaviorModuleData();

	DamageInfo damageInfo;
	damageInfo.in.m_damageType = DAMAGE_EXPLOSION;
	damageInfo.in.m_deathType = DEATH_SPLATTED;
	damageInfo.in.m_sourceID = getObject()->getID();
	damageInfo.in.m_amount = 1.0f;
	damageInfo.in.m_shockWaveVector.zero();
	damageInfo.in.m_shockWaveAmount = 50.0f;
	damageInfo.in.m_shockWaveRadius = data->m_radius;
	damageInfo.in.m_shockWaveTaperOff = 0.1f;

	// scan objects in our region
	ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(&m_riftPos,
		data->m_radius,
		FROM_CENTER_2D, NULL);

	MemoryPoolObjectHolder hold(iter);

	for (Object* currentObj = iter->first(); currentObj != NULL; currentObj = iter->next())
	{
		// get other position
		const Coord3D* objPos = currentObj->getPosition();

		// compute vector from the object to the rift center
		Coord3D forceVector;
		forceVector.x = objPos->x - m_riftPos.x;
		forceVector.y = objPos->y - m_riftPos.y;
		forceVector.z = 0.0f;

		damageInfo.in.m_shockWaveVector = forceVector;

		currentObj->attemptDamage(&damageInfo);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::crc(Xfer* xfer)
{

	// extend base class
	SlowDeathBehavior::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
	// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	SlowDeathBehavior::xfer(xfer);

	// activation frame
	xfer->xferUnsignedInt(&m_activationFrame);

	xfer->xferCoord3D(&m_riftPos);

	xfer->xferBool(&m_finalPush);

	xfer->xferBool(&m_playedMainFX);
	
}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::loadPostProcess(void)
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}  // end loadPostProcess