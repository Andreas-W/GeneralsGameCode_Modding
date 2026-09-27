#define DEFINE_DEATH_NAMES

#include "GameLogic/Module/RiftSlowDeathUpdate.h"
#include "Common/GlobalData.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/FXList.h"

// A victim that has drifted a little past the rim is not dropped immediately; without this
// hysteresis an object orbiting the edge would capture and release every other frame, re-firing
// the one-shot setup and burning logic random values (which would desync).
static const Real RIFT_RELEASE_SLOP = 1.15f;

RiftSlowDeathBehaviorModuleData::RiftSlowDeathBehaviorModuleData(void)
{
	m_initialDelay = 0;
	m_rampupTime = 0;
	m_mainDuration = 0;
	m_fadeOutTime = 0;

	m_radius = 0.0f;
	m_eventHorizonRadius = 0.0f;
	m_liftHeight = 100.0f;

	m_pullAcceleration = 0.0f;
	m_swirlFactor = 0.0f;
	m_pullDamping = 0.12f;
	m_liftAcceleration = 0.0f;
	m_maxPullSpeed = 0.0f;
	m_initialLiftVelocity = 0.0f;
	m_cancelGravity = TRUE;
	m_spinRate = 0.0f;
	m_hugeVehiclePullScalar = 1.0f;

	m_damagePerSecond = 0.0f;
	m_damageInterval = LOGICFRAMES_PER_SECOND;
	m_damageType = DAMAGE_EXPLOSION;
	m_deathType = DEATH_NORMAL;
	m_consumeDeathType = DEATH_EXPLODED;
	m_consumeDeathRadius = 0.0f;

	m_finalPushForce = 0.0f;
	m_finalPushSpeed = 0.0f;
	m_finalPushDeathType = DEATH_SPLATTED;

	// things that have no business being dragged around by a gravity well
	m_noPullKindOf.set(KINDOF_PROJECTILE);
	m_noPullKindOf.set(KINDOF_SMALL_MISSILE);
	m_noPullKindOf.set(KINDOF_BALLISTIC_MISSILE);
	m_noPullKindOf.set(KINDOF_INERT);
	m_noPullKindOf.set(KINDOF_DRAWABLE_ONLY);
	m_noPullKindOf.set(KINDOF_OPTIMIZED_TREE);
	m_noPullKindOf.set(KINDOF_MINE);

	m_FXmain = NULL;
	m_FXcharge = NULL;
	m_FXfinal = NULL;
	m_FXconsume = NULL;
	m_OCLconsume = NULL;
	m_OCLstructureDebris = NULL;
	m_structureDebrisChance = 1.0f;
	m_structureDebrisCount = 1;
}  // end RiftSlowDeathBehaviorModuleData

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void RiftSlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	SlowDeathBehaviorModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "InitialDelay",					INI::parseDurationUnsignedInt,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_initialDelay) },
		{ "RampupTime",						INI::parseDurationUnsignedInt,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_rampupTime) },
		{ "MainDuration",					INI::parseDurationUnsignedInt,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_mainDuration) },
		{ "FadeOutTime",					INI::parseDurationUnsignedInt,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_fadeOutTime) },

		{ "Radius",								INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_radius) },
		{ "EventHorizonRadius",		INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_eventHorizonRadius) },
		{ "LiftHeight",						INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_liftHeight) },

		{ "PullAcceleration",			INI::parseAccelerationReal,			NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_pullAcceleration) },
		{ "SwirlFactor",					INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_swirlFactor) },
		{ "PullDamping",					INI::parsePercentToReal,				NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_pullDamping) },
		{ "LiftAcceleration",			INI::parseAccelerationReal,			NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_liftAcceleration) },
		{ "MaxPullSpeed",					INI::parseVelocityReal,					NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_maxPullSpeed) },
		{ "InitialLiftVelocity",	INI::parseVelocityReal,					NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_initialLiftVelocity) },
		{ "CancelGravity",				INI::parseBool,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_cancelGravity) },
		{ "SpinRate",							INI::parseAngularVelocityReal,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_spinRate) },
		{ "HugeVehiclePullScalar",INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_hugeVehiclePullScalar) },

		{ "DamagePerSecond",			INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_damagePerSecond) },
		{ "DamageInterval",				INI::parseDurationUnsignedInt,	NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_damageInterval) },
		{ "DamageType",						INI::parseIndexList,		DamageTypeFlags::s_bitNameList, offsetof(RiftSlowDeathBehaviorModuleData, m_damageType) },
		{ "DeathType",						INI::parseIndexList,		TheDeathNames,	offsetof(RiftSlowDeathBehaviorModuleData, m_deathType) },
		{ "ConsumeDeathType",			INI::parseIndexList,		TheDeathNames,	offsetof(RiftSlowDeathBehaviorModuleData, m_consumeDeathType) },
		{ "ConsumeDeathRadius",		INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_consumeDeathRadius) },

		{ "FinalPushForce",				INI::parseReal,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_finalPushForce) },
		{ "FinalPushSpeed",				INI::parseVelocityReal,					NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_finalPushSpeed) },
		{ "FinalPushDeathType",		INI::parseIndexList,		TheDeathNames,	offsetof(RiftSlowDeathBehaviorModuleData, m_finalPushDeathType) },

		{ "PullKindOf",						KindOfMaskType::parseFromINI,		NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_pullKindOf) },
		{ "NoPullKindOf",					KindOfMaskType::parseFromINI,		NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_noPullKindOf) },

		{ "FXMain",								INI::parseFXList,								NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXmain) },
		{ "FXCharge",							INI::parseFXList,								NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXcharge) },
		{ "FXFinal",							INI::parseFXList,								NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXfinal) },
		{ "FXConsume",						INI::parseFXList,								NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_FXconsume) },
		{ "OCLConsume",						INI::parseObjectCreationList,		NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_OCLconsume) },
		{ "StructureDebrisOCL",		INI::parseObjectCreationList,		NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_OCLstructureDebris) },
		{ "StructureDebrisChance",INI::parsePercentToReal,				NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_structureDebrisChance) },
		{ "StructureDebrisCount",	INI::parseInt,									NULL, offsetof(RiftSlowDeathBehaviorModuleData, m_structureDebrisCount) },

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
	m_chargeEndFrame = 0;
	m_rampEndFrame = 0;
	m_mainEndFrame = 0;
	m_fadeEndFrame = 0;
	m_nextDamageFrame = 0;
	m_riftPos.zero();
	m_phase = RIFTPHASE_INACTIVE;
}  // end RiftSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RiftSlowDeathBehavior::~RiftSlowDeathBehavior(void)
{

	// last line of defense: if we are torn down while still holding victims, hand them back
	if (TheGameLogic != NULL)
		releaseAllVictims();

}  // end ~RiftSlowDeathBehavior

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime RiftSlowDeathBehavior::update(void)
{
	// call the base class cause we're extending functionality
	SlowDeathBehavior::update();

	// get out of here if we're not activated yet
	if (isSlowDeathActivated() == FALSE)
		return UPDATE_SLEEP_NONE;

	UnsignedInt currFrame = TheGameLogic->getFrame();

	if (m_phase == RIFTPHASE_INACTIVE)
		onActivated(currFrame);

	Real strength = 0.0f;
	RiftPhaseType newPhase = computePhase(currFrame, &strength);
	if ((Int)newPhase != m_phase)
	{
		m_phase = newPhase;
		onPhaseEnter(newPhase);
	}

	if (m_phase == RIFTPHASE_RAMPUP || m_phase == RIFTPHASE_MAIN || m_phase == RIFTPHASE_FADEOUT)
		doRiftTick(strength, currFrame);

	return UPDATE_SLEEP_NONE;

}  // end update

// ------------------------------------------------------------------------------------------------
/** Lock in where the rift sits and when each phase ends. Everything downstream works off these
	* absolute frames, so the timings never drift and survive a save/load. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::onActivated(UnsignedInt currFrame)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();

	const Coord3D* pos = getObject()->getPosition();
	m_riftPos.x = pos->x;
	m_riftPos.y = pos->y;
	m_riftPos.z = TheTerrainLogic->getGroundHeight(m_riftPos.x, m_riftPos.y) + d->m_liftHeight;

	m_activationFrame = currFrame;
	m_chargeEndFrame = currFrame + d->m_initialDelay;
	m_rampEndFrame = m_chargeEndFrame + d->m_rampupTime;
	m_mainEndFrame = m_rampEndFrame + d->m_mainDuration;
	m_fadeEndFrame = m_mainEndFrame + d->m_fadeOutTime;
	m_nextDamageFrame = m_chargeEndFrame;

	m_phase = RIFTPHASE_CHARGE;
	onPhaseEnter(RIFTPHASE_CHARGE);

	// the base class destroys us on its own schedule; if that lands early the rift is cut short
	DEBUG_ASSERTCRASH(m_fadeEndFrame <= getDestructionFrame(),
		("RiftSlowDeathBehavior: DestructionDelay is shorter than the rift phases; the rift will be cut off"));

}  // end onActivated

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RiftPhaseType RiftSlowDeathBehavior::computePhase(UnsignedInt currFrame, Real* strength) const
{
	*strength = 0.0f;

	if (currFrame < m_chargeEndFrame)
		return RIFTPHASE_CHARGE;

	if (currFrame < m_rampEndFrame)
	{
		// guarded: a zero length phase would divide by zero and poison every position with a NaN
		UnsignedInt span = m_rampEndFrame - m_chargeEndFrame;
		if (span > 0)
			*strength = (Real)(currFrame - m_chargeEndFrame) / (Real)span;
		return RIFTPHASE_RAMPUP;
	}

	if (currFrame < m_mainEndFrame)
	{
		*strength = 1.0f;
		return RIFTPHASE_MAIN;
	}

	if (currFrame < m_fadeEndFrame)
	{
		UnsignedInt span = m_fadeEndFrame - m_mainEndFrame;
		if (span > 0)
			*strength = 1.0f - ((Real)(currFrame - m_mainEndFrame) / (Real)span);
		return RIFTPHASE_FADEOUT;
	}

	return RIFTPHASE_DONE;

}  // end computePhase

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::onPhaseEnter(RiftPhaseType phase)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();

	switch (phase)
	{
		case RIFTPHASE_CHARGE:
			FXList::doFXPos(d->m_FXcharge, &m_riftPos);
			break;

		case RIFTPHASE_RAMPUP:
			FXList::doFXPos(d->m_FXmain, &m_riftPos);
			break;

		case RIFTPHASE_DONE:
			doCollapse();
			break;

		default:
			break;
	}

}  // end onPhaseEnter

// ------------------------------------------------------------------------------------------------
/** One pass over everything in range: damage on its own cadence, then suction for whatever we are
	* allowed to move. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::doRiftTick(Real strength, UnsignedInt currFrame)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();
	Object* self = getObject();

	Bool damageThisFrame = FALSE;
	Real damageAmount = 0.0f;
	if (d->m_damagePerSecond > 0.0f && currFrame >= m_nextDamageFrame)
	{
		UnsignedInt interval = d->m_damageInterval > 0 ? d->m_damageInterval : 1;
		damageAmount = d->m_damagePerSecond * ((Real)interval / (Real)LOGICFRAMES_PER_SECOND) * strength;
		m_nextDamageFrame = currFrame + interval;
		damageThisFrame = TRUE;
	}

	DamageInfo damageInfo;
	damageInfo.in.m_damageType = d->m_damageType;
	damageInfo.in.m_deathType = d->m_deathType;
	damageInfo.in.m_sourceID = self->getID();
	damageInfo.in.m_amount = damageAmount;
	// no shockwave here: we move victims through physics directly, and attemptDamage refuses to
	// shove anything airborne anyway, which is exactly what we have lifted
	damageInfo.in.m_shockWaveAmount = 0.0f;
	damageInfo.in.m_shockWaveVector.zero();

	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter* filters[] = { &filterAlive, &filterOnMap, NULL };

	// 2D range, so Radius keeps meaning "the circle on the ground" that the targeting reticle shows
	ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(&m_riftPos,
		d->m_radius, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);

	// consuming kills objects, so collect them and deal with them once the iterator is released
	std::vector<ObjectID> consumed;
	std::vector<ObjectID> inRange;
	std::vector<ObjectID> debrisSources;

	for (Object* victim = iter->first(); victim != NULL; victim = iter->next())
	{
		if (victim == self)
			continue;

		if (victim->isEffectivelyDead())
			continue;

		// passengers ride along with whatever is carrying them
		if (victim->getContainedBy() != NULL)
			continue;

		Coord3D toRift;
		toRift.x = m_riftPos.x - victim->getPosition()->x;
		toRift.y = m_riftPos.y - victim->getPosition()->y;
		toRift.z = m_riftPos.z - victim->getPosition()->z;
		Real dist = toRift.length();

		Bool pullable = isPullable(victim);

		if (damageThisFrame)
		{
			// only things that can actually fall in may die the consume death
			damageInfo.in.m_deathType = (pullable && dist <= d->m_consumeDeathRadius) ? d->m_consumeDeathType : d->m_deathType;
			// the same DamageInfo is reused for every victim, so clear what the last one reported
			damageInfo.out = DamageInfoOutput();
			victim->attemptDamage(&damageInfo);

			if (d->m_OCLstructureDebris != NULL
					&& victim->isKindOf(KINDOF_STRUCTURE)
					&& !damageInfo.out.m_noEffect
					&& damageInfo.out.m_actualDamageDealt > 0.0f
					&& GameLogicRandomValueReal(0.0f, 1.0f) < d->m_structureDebrisChance)
				debrisSources.push_back(victim->getID());
		}

		if (!pullable)
			continue;

		if (d->m_eventHorizonRadius > 0.0f && dist <= d->m_eventHorizonRadius)
		{
			consumed.push_back(victim->getID());
			continue;
		}

		RiftVictim* held = findVictim(victim->getID());
		if (held == NULL)
		{
			captureVictim(victim);
			held = findVictim(victim->getID());
		}

		if (held != NULL)
		{
			inRange.push_back(victim->getID());
			steerVictim(*held, victim, toRift, dist, strength);
		}
	}

	// drop anything we were holding that is gone, dead, or has drifted well clear of the rim
	Real releaseRadiusSqr = (d->m_radius * RIFT_RELEASE_SLOP) * (d->m_radius * RIFT_RELEASE_SLOP);
	for (Int i = (Int)m_victims.size() - 1; i >= 0; --i)
	{
		RiftVictim& v = m_victims[i];

		Bool keep = FALSE;
		for (std::vector<ObjectID>::const_iterator it = inRange.begin(); it != inRange.end(); ++it)
		{
			if (*it == v.m_id)
			{
				keep = TRUE;
				break;
			}
		}
		if (keep)
			continue;

		Object* obj = TheGameLogic->findObjectByID(v.m_id);
		if (obj != NULL && !obj->isEffectivelyDead())
		{
			// still alive: only let go once it is properly clear, so we don't chatter at the rim
			Coord3D away;
			away.x = obj->getPosition()->x - m_riftPos.x;
			away.y = obj->getPosition()->y - m_riftPos.y;
			away.z = 0.0f;
			// but let go at once of anything that stopped being pullable, e.g. just turned invulnerable
			if (away.lengthSqr() <= releaseRadiusSqr && isPullable(obj))
				continue;

			releaseVictim(v, obj);
		}
		else if (obj != NULL)
		{
			// Dead but not yet destroyed. Anything that died close to the center keeps falling in
			// until its own death modules remove it; the alive filter hides it from the scan above,
			// so it is steered from here instead.
			Coord3D toRift;
			toRift.x = m_riftPos.x - obj->getPosition()->x;
			toRift.y = m_riftPos.y - obj->getPosition()->y;
			toRift.z = m_riftPos.z - obj->getPosition()->z;
			Real dist = toRift.length();

			// our debris has an InactiveBody, which counts as dead from birth, so it always lands here
			if ((v.m_savedFlags & RiftVictim::RIFTSAVE_DEBRIS) != 0)
			{
				if (d->m_eventHorizonRadius > 0.0f && dist <= d->m_eventHorizonRadius)
				{
					TheGameLogic->destroyObject(obj);
					m_victims.erase(m_victims.begin() + i);
					continue;
				}

				steerVictim(v, obj, toRift, dist, strength);
				continue;
			}

			if ((v.m_savedFlags & RiftVictim::RIFTSAVE_DYING) != 0 || dist <= d->m_consumeDeathRadius)
			{
				if ((v.m_savedFlags & RiftVictim::RIFTSAVE_DYING) == 0)
				{
					v.m_savedFlags |= RiftVictim::RIFTSAVE_DYING;

					// a dead flyer's AI no longer drives its locomotor, so take it over through physics
					if ((v.m_savedFlags & RiftVictim::RIFTSAVE_USES_LOCOMOTOR) != 0)
					{
						v.m_savedFlags &= ~RiftVictim::RIFTSAVE_USES_LOCOMOTOR;
						beginPhysicsCapture(v, obj);
					}
				}

				steerVictim(v, obj, toRift, dist, strength);
				continue;
			}
		}

		m_victims.erase(m_victims.begin() + i);
	}

	// now that the iterator is gone it is safe to create and destroy things
	for (std::vector<ObjectID>::const_iterator it = debrisSources.begin(); it != debrisSources.end(); ++it)
	{
		Object* structure = TheGameLogic->findObjectByID(*it);
		if (structure != NULL)
			spawnStructureDebris(structure);
	}

	for (std::vector<ObjectID>::const_iterator it = consumed.begin(); it != consumed.end(); ++it)
	{
		Object* victim = TheGameLogic->findObjectByID(*it);
		if (victim == NULL)
			continue;

		for (Int i = (Int)m_victims.size() - 1; i >= 0; --i)
		{
			if (m_victims[i].m_id == *it)
			{
				m_victims.erase(m_victims.begin() + i);
				break;
			}
		}

		FXList::doFXPos(d->m_FXconsume, victim->getPosition());
		ObjectCreationList::create(d->m_OCLconsume, self, victim->getPosition(), NULL, 0.0f);
		victim->kill(DAMAGE_UNRESISTABLE, d->m_consumeDeathType);
	}

}  // end doRiftTick

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool RiftSlowDeathBehavior::isPullable(const Object* victim, Bool isDebris) const
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();

	// anything nailed down only ever takes damage
	if (victim->isKindOf(KINDOF_IMMOBILE)
			|| victim->isKindOf(KINDOF_STRUCTURE)
			|| victim->isKindOf(KINDOF_BRIDGE)
			|| victim->isKindOf(KINDOF_BRIDGE_TOWER)
			|| victim->isKindOf(KINDOF_IMMUNE_TO_PULL))
		return FALSE;

	// invulnerable things are left alone. Our own debris is exempt: it usually has an InactiveBody,
	// which always reports itself as indestructible.
	if (!isDebris)
	{
		const BodyModuleInterface* body = victim->getBodyModule();
		if ((body != NULL && body->isIndestructible()) || victim->getIsUndetectedDefector())
			return FALSE;

		// these take damage but can never die, so the event horizon could never consume them
		static const NameKeyType key_ImmortalBody = NAMEKEY("ImmortalBody");
		static const NameKeyType key_HighlanderBody = NAMEKEY("HighlanderBody");
		for (BehaviorModule** m = victim->getBehaviorModules(); *m; ++m)
		{
			NameKeyType key = (*m)->getModuleNameKey();
			if (key == key_ImmortalBody || key == key_HighlanderBody)
				return FALSE;
		}
	}

	if (victim->isAnyKindOf(d->m_noPullKindOf))
		return FALSE;

	if (KINDOFMASK_ANY_SET(d->m_pullKindOf) && !victim->isAnyKindOf(d->m_pullKindOf))
		return FALSE;

	return TRUE;

}  // end isPullable

// ------------------------------------------------------------------------------------------------
/** Flyers whose locomotor template is flagged LocomotorWorksWhenDisabled keep steering themselves
	* even while disabled, and their handleBehaviorZ rewrites the z position every frame. Fighting
	* that is a losing game, so those victims get steered through their own locomotor instead. */
// ------------------------------------------------------------------------------------------------
Bool RiftSlowDeathBehavior::usesLocomotorCapture(const Object* victim) const
{
	const AIUpdateInterface* ai = victim->getAIUpdateInterface();
	if (ai == NULL)
		return FALSE;

	const Locomotor* loco = ai->getCurLocomotor();
	return loco != NULL && loco->getLocomotorWorksWhenDisabled();

}  // end usesLocomotorCapture

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
RiftVictim* RiftSlowDeathBehavior::findVictim(ObjectID id)
{
	for (std::vector<RiftVictim>::iterator it = m_victims.begin(); it != m_victims.end(); ++it)
	{
		if (it->m_id == id)
			return &(*it);
	}
	return NULL;

}  // end findVictim

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::captureVictim(Object* victim)
{
	RiftVictim v;
	v.m_id = victim->getID();
	v.m_kinematicVel.zero();
	v.m_spinSign = GameLogicRandomValue(0, 1) ? 1 : -1;
	v.m_savedFlags = 0;

	PhysicsBehavior* physics = victim->getPhysics();

	if (usesLocomotorCapture(victim))
	{
		// leave the flyer's AI running; steerVictim drives its preferred height instead
		v.m_savedFlags |= RiftVictim::RIFTSAVE_USES_LOCOMOTOR;
	}
	else
	{
		beginPhysicsCapture(v, victim);
	}

	if (physics != NULL)
	{
		// a parked object's physics is asleep and addVelocityTo does not wake it, so poke it with
		// a zero force, which does
		Coord3D zero;
		zero.zero();
		physics->applyForce(&zero);
	}

	m_victims.push_back(v);

}  // end captureVictim

// ------------------------------------------------------------------------------------------------
/** Take the victim away from its AI and hand it to physics, which steerVictim then drives. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::beginPhysicsCapture(RiftVictim& v, Object* victim)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();

	// DISABLED_FREEFALL is the only disable that suspends the AI while leaving physics running
	// (AIUpdate tolerates DISABLED_HELD only), so it is what hands the object over to us.
	if (victim->isDisabledByType(DISABLED_FREEFALL))
		v.m_savedFlags |= RiftVictim::RIFTSAVE_WAS_DISABLED_FREEFALL;
	victim->setDisabled(DISABLED_FREEFALL);

	PhysicsBehavior* physics = victim->getPhysics();
	if (physics == NULL)
		return;

	if (physics->getIsInFreeFall())
		v.m_savedFlags |= RiftVictim::RIFTSAVE_WAS_IN_FREEFALL;

	physics->setStickToGround(FALSE);
	physics->setAllowToFall(TRUE);		// stop physics clamping z to the terrain
	physics->setIsInFreeFall(TRUE);		// physics then re-asserts the disable for us each frame
	physics->setAllowBouncing(FALSE);
	physics->setAllowCollideForce(FALSE);
	physics->setImmuneToFallingDamage(TRUE);
	physics->setStunned(TRUE);
	victim->setModelConditionState(MODELCONDITION_STUNNED_FLAILING);

	if (d->m_initialLiftVelocity > 0.0f)
	{
		Coord3D kick;
		kick.x = 0.0f;
		kick.y = 0.0f;
		kick.z = d->m_initialLiftVelocity;
		physics->addVelocityTo(&kick);
	}

}  // end beginPhysicsCapture

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::steerVictim(RiftVictim& victim, Object* obj, const Coord3D& toRift,
																			 Real dist, Real strength)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();

	Real falloff = 1.0f;
	if (d->m_radius > 0.0f)
	{
		falloff = 1.0f - (dist / d->m_radius);
		if (falloff < 0.0f)
			falloff = 0.0f;
		else if (falloff > 1.0f)
			falloff = 1.0f;
	}

	Real scale = strength * falloff;
	if (obj->isKindOf(KINDOF_HUGE_VEHICLE))
		scale *= d->m_hugeVehiclePullScalar;

	// horizontal direction toward the rift, and the tangent that turns the fall into a spiral
	Coord3D inward;
	inward.x = toRift.x;
	inward.y = toRift.y;
	inward.z = 0.0f;
	Real dist2D = inward.length();
	if (dist2D > 0.001f)
	{
		inward.x /= dist2D;
		inward.y /= dist2D;
	}
	else
	{
		inward.zero();
	}

	// Bend the pull sideways rather than adding a separate tangential thrust. Rotating a
	// fixed-length vector cannot inject energy, so victims spiral in; a tangential force would
	// raise their orbit every frame and eventually fling them clear of the radius.
	// Fixed handedness: one coherent vortex reads as a single object, mixed directions read as noise.
	Coord3D dir;
	dir.x = inward.x - inward.y * d->m_swirlFactor;
	dir.y = inward.y + inward.x * d->m_swirlFactor;
	dir.z = 0.0f;
	Real dirLen = dir.length();
	if (dirLen > 0.001f)
	{
		dir.x /= dirLen;
		dir.y /= dirLen;
	}

	Coord3D accel;
	accel.x = dir.x * d->m_pullAcceleration * scale;
	accel.y = dir.y * d->m_pullAcceleration * scale;
	accel.z = 0.0f;

	// vertical: ease off as the victim reaches the rift plane, and push down on anything above it
	Real liftZone = d->m_liftHeight > 0.0f ? d->m_liftHeight : 1.0f;
	Real zSpring = toRift.z / liftZone;
	if (zSpring < -1.0f)
		zSpring = -1.0f;
	else if (zSpring > 1.0f)
		zSpring = 1.0f;
	accel.z = d->m_liftAcceleration * scale * zSpring;

	PhysicsBehavior* physics = obj->getPhysics();

	if ((victim.m_savedFlags & RiftVictim::RIFTSAVE_USES_LOCOMOTOR) != 0)
	{
		// hand the altitude to the flyer's own flight model; it will fly itself into the rift
		AIUpdateInterface* ai = obj->getAIUpdateInterface();
		Locomotor* loco = (ai != NULL) ? ai->getCurLocomotor() : NULL;
		if (loco != NULL)
		{
			// preferred height is relative to the surface for the locomotor modes flyers use
			Real surfaceHeight = TheTerrainLogic->getGroundHeight(obj->getPosition()->x, obj->getPosition()->y);
			Real wanted = m_riftPos.z - surfaceHeight;
			if (wanted < 0.0f)
				wanted = 0.0f;
			loco->setPreferredHeight(wanted);
		}
		// the flyer handles its own z, so only drag it sideways
		accel.z = 0.0f;
	}
	else if (physics != NULL && d->m_cancelGravity)
	{
		// gravity is added unconditionally every frame, so cancel it or LiftAcceleration has to
		// out-muscle it before any of the tuning numbers mean anything
		accel.z -= TheGlobalData->m_gravity * scale;
	}

	if (physics != NULL)
	{
		// addVelocityTo can only add, so work out the clamped delta ourselves
		Coord3D newVel = *physics->getVelocity();

		// Viscous drag first. This is what bounds the whole system: terminal speed settles at
		// roughly pullAcceleration/pullDamping instead of growing every frame, so a victim cannot
		// build up enough momentum to shoot through the middle and escape the far side.
		if (d->m_pullDamping > 0.0f)
		{
			Real drag = d->m_pullDamping * scale;
			if (drag > 1.0f)
				drag = 1.0f;
			newVel.x -= newVel.x * drag;
			newVel.y -= newVel.y * drag;
			// z needs it too: with gravity cancelled the lift term is otherwise a lossless spring,
			// so victims would bob around the rift plane forever instead of settling into it
			newVel.z -= newVel.z * drag;
		}

		newVel.add(accel);

		if (d->m_maxPullSpeed > 0.0f)
		{
			Real speed = newVel.length();
			if (speed > d->m_maxPullSpeed)
				newVel.scale(d->m_maxPullSpeed / speed);
		}

		Coord3D delta;
		delta.x = newVel.x - physics->getVelocity()->x;
		delta.y = newVel.y - physics->getVelocity()->y;
		delta.z = newVel.z - physics->getVelocity()->z;
		physics->addVelocityTo(&delta);

		if ((victim.m_savedFlags & RiftVictim::RIFTSAVE_USES_LOCOMOTOR) == 0)
		{
			// physics clears these when the victim touches down, so keep re-asserting them
			physics->setStickToGround(FALSE);
			physics->setAllowToFall(TRUE);
			physics->setIsInFreeFall(TRUE);
			obj->setDisabled(DISABLED_FREEFALL);

			if (d->m_spinRate > 0.0f)
			{
				Real spin = d->m_spinRate * scale * (Real)victim.m_spinSign;
				physics->setYawRate(spin);
				physics->setPitchRate(spin * 0.5f);
				physics->setRollRate(spin * 0.5f);
			}
		}
	}
	else
	{
		// no physics module, so integrate the victim ourselves
		if (d->m_pullDamping > 0.0f)
		{
			Real drag = d->m_pullDamping * scale;
			if (drag > 1.0f)
				drag = 1.0f;
			victim.m_kinematicVel.x -= victim.m_kinematicVel.x * drag;
			victim.m_kinematicVel.y -= victim.m_kinematicVel.y * drag;
			victim.m_kinematicVel.z -= victim.m_kinematicVel.z * drag;
		}

		victim.m_kinematicVel.add(accel);
		if (d->m_maxPullSpeed > 0.0f)
		{
			Real speed = victim.m_kinematicVel.length();
			if (speed > d->m_maxPullSpeed)
				victim.m_kinematicVel.scale(d->m_maxPullSpeed / speed);
		}

		// setPosition re-aligns KINDOF_STICK_TO_TERRAIN_SLOPE objects back onto the ground, so go
		// through the transform directly or props would never leave the terrain
		Matrix3D mtx = *obj->getTransformMatrix();
		mtx.Adjust_X_Translation(victim.m_kinematicVel.x);
		mtx.Adjust_Y_Translation(victim.m_kinematicVel.y);
		mtx.Adjust_Z_Translation(victim.m_kinematicVel.z);
		if (d->m_spinRate > 0.0f)
			mtx.Rotate_Z(d->m_spinRate * scale * (Real)victim.m_spinSign);
		obj->setTransformMatrix(&mtx);
	}

}  // end steerVictim

// ------------------------------------------------------------------------------------------------
/** Undo the parts of the capture that do not clean themselves up. Physics clears IS_IN_FREEFALL,
	* ALLOW_TO_FALL and the stun on its own once the victim lands. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::releaseVictim(const RiftVictim& victim, Object* obj)
{
	PhysicsBehavior* physics = obj->getPhysics();

	if ((victim.m_savedFlags & RiftVictim::RIFTSAVE_USES_LOCOMOTOR) != 0)
	{
		AIUpdateInterface* ai = obj->getAIUpdateInterface();
		Locomotor* loco = (ai != NULL) ? ai->getCurLocomotor() : NULL;
		if (loco != NULL)
			loco->restorePreferredHeightFromTemplate();
		return;
	}

	if (physics != NULL)
	{
		physics->setYawRate(0.0f);
		physics->setPitchRate(0.0f);
		physics->setRollRate(0.0f);
		physics->setAllowCollideForce(TRUE);
		physics->setAllowBouncing(TRUE);
		physics->setImmuneToFallingDamage(FALSE);
		physics->setIsInFreeFall((victim.m_savedFlags & RiftVictim::RIFTSAVE_WAS_IN_FREEFALL) != 0);
		// leave allowToFall set: it clears itself on landing, and clearing it now would snap the
		// victim straight back down to the terrain
		physics->setAllowToFall(TRUE);
	}

	if ((victim.m_savedFlags & RiftVictim::RIFTSAVE_WAS_DISABLED_FREEFALL) == 0)
		obj->clearDisabled(DISABLED_FREEFALL);

	// the victim is still carrying whatever order it had before we grabbed it
	AIUpdateInterface* ai = obj->getAIUpdateInterface();
	if (ai != NULL && !ai->isAiInDeadState())
		ai->aiIdle(CMD_FROM_AI);

}  // end releaseVictim

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::releaseAllVictims(void)
{
	for (std::vector<RiftVictim>::const_iterator it = m_victims.begin(); it != m_victims.end(); ++it)
	{
		Object* obj = TheGameLogic->findObjectByID(it->m_id);
		if (obj != NULL && (!obj->isEffectivelyDead() || (it->m_savedFlags & RiftVictim::RIFTSAVE_DEBRIS) != 0))
			releaseVictim(*it, obj);
	}
	m_victims.clear();

}  // end releaseAllVictims

// ------------------------------------------------------------------------------------------------
/** Fire the debris OCL at a random spot on the structure, so it looks like the rift is tearing
	* pieces out of it. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::spawnStructureDebris(Object* structure)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();
	const GeometryInfo& geom = structure->getGeometryInfo();

	Real angle = structure->getOrientation();
	Real c = Cos(angle);
	Real s = Sin(angle);

	for (Int n = 0; n < d->m_structureDebrisCount; ++n)
	{
		// the footprint offset is in the structure's local space
		Coord3D offset;
		geom.makeRandomOffsetWithinFootprint(offset);

		Coord3D pos = *structure->getPosition();
		pos.x += offset.x * c - offset.y * s;
		pos.y += offset.x * s + offset.y * c;
		pos.z += GameLogicRandomValueReal(0.0f, geom.getMaxHeightAbovePosition());

		Object* debris = ObjectCreationList::create(d->m_OCLstructureDebris, structure, &pos, NULL, 0.0f);

		// Debris usually has an InactiveBody and so counts as dead from birth, which hides it from
		// the scan in doRiftTick. We therefore grab it here and track it ourselves.
		if (debris == NULL || !isPullable(debris, TRUE) || findVictim(debris->getID()) != NULL)
			continue;

		captureVictim(debris);
		RiftVictim* held = findVictim(debris->getID());
		if (held != NULL)
			held->m_savedFlags |= RiftVictim::RIFTSAVE_DEBRIS;
	}

}  // end spawnStructureDebris

// ------------------------------------------------------------------------------------------------
/** The singularity collapses: everything we held is let go and thrown clear, and the whole radius
	* takes one outward blast. */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::doCollapse(void)
{
	const RiftSlowDeathBehaviorModuleData* d = getRiftSlowDeathBehaviorModuleData();
	Object* self = getObject();

	FXList::doFXPos(d->m_FXfinal, &m_riftPos);

	// throw the victims we were holding before letting go of them; attemptDamage will not shove
	// anything airborne, and airborne is exactly what these are
	if (d->m_finalPushSpeed > 0.0f)
	{
		for (std::vector<RiftVictim>::const_iterator it = m_victims.begin(); it != m_victims.end(); ++it)
		{
			Object* obj = TheGameLogic->findObjectByID(it->m_id);
			if (obj == NULL || (obj->isEffectivelyDead() && (it->m_savedFlags & RiftVictim::RIFTSAVE_DEBRIS) == 0))
				continue;

			PhysicsBehavior* physics = obj->getPhysics();
			if (physics == NULL)
				continue;

			Coord3D outward;
			outward.x = obj->getPosition()->x - m_riftPos.x;
			outward.y = obj->getPosition()->y - m_riftPos.y;
			outward.z = 0.0f;
			if (outward.lengthSqr() < 0.001f)
				outward.x = 1.0f;
			outward.normalize();
			outward.scale(d->m_finalPushSpeed);
			outward.z = d->m_finalPushSpeed * 0.5f;

			physics->addVelocityTo(&outward);
		}
	}

	releaseAllVictims();

	if (d->m_finalPushForce <= 0.0f)
		return;

	DamageInfo damageInfo;
	damageInfo.in.m_damageType = d->m_damageType;
	damageInfo.in.m_deathType = d->m_finalPushDeathType;
	damageInfo.in.m_sourceID = self->getID();
	damageInfo.in.m_amount = 1.0f;
	damageInfo.in.m_shockWaveAmount = d->m_finalPushForce;
	damageInfo.in.m_shockWaveRadius = d->m_radius;
	damageInfo.in.m_shockWaveTaperOff = 0.1f;

	PartitionFilterAlive filterAlive;
	PartitionFilterOnMap filterOnMap;
	PartitionFilter* filters[] = { &filterAlive, &filterOnMap, NULL };

	ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(&m_riftPos,
		d->m_radius, FROM_CENTER_2D, filters);
	MemoryPoolObjectHolder hold(iter);

	for (Object* victim = iter->first(); victim != NULL; victim = iter->next())
	{
		if (victim == self)
			continue;

		// the length of this vector is read as the distance from the blast center, and its z is
		// overwritten downstream, so build it flat
		Coord3D outward;
		outward.x = victim->getPosition()->x - m_riftPos.x;
		outward.y = victim->getPosition()->y - m_riftPos.y;
		outward.z = 0.0f;

		damageInfo.in.m_shockWaveVector = outward;
		victim->attemptDamage(&damageInfo);
	}

}  // end doCollapse

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
	* 1: Initial version
	* 2: Phase machine, damage cadence and held victim list */
	// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	SlowDeathBehavior::xfer(xfer);

	xfer->xferUnsignedInt(&m_activationFrame);
	xfer->xferCoord3D(&m_riftPos);

	xfer->xferUnsignedInt(&m_chargeEndFrame);
	xfer->xferUnsignedInt(&m_rampEndFrame);
	xfer->xferUnsignedInt(&m_mainEndFrame);
	xfer->xferUnsignedInt(&m_fadeEndFrame);
	xfer->xferUnsignedInt(&m_nextDamageFrame);
	xfer->xferInt(&m_phase);

	Int victimCount = (Int)m_victims.size();
	xfer->xferInt(&victimCount);
	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_victims.clear();
		m_victims.resize(victimCount);
	}
	for (Int i = 0; i < victimCount; ++i)
	{
		RiftVictim& v = m_victims[i];
		xfer->xferObjectID(&v.m_id);
		xfer->xferCoord3D(&v.m_kinematicVel);
		xfer->xferInt(&v.m_spinSign);
		xfer->xferUnsignedByte(&v.m_savedFlags);
	}

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void RiftSlowDeathBehavior::loadPostProcess(void)
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}  // end loadPostProcess
