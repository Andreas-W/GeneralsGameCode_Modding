// FILE: DroneCarrierAIUpdate.cpp ///////////////////////////////////////////////////////////////////
// Desc: Handles aircraft movement and parking behavior for aircraft carriers.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CRCDebug.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#include "GameLogic/Module/DroneCarrierAIUpdate.h"
#include "GameLogic/Module/JetAIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/ContainModule.h"


DroneCarrierAIUpdateModuleData::DroneCarrierAIUpdateModuleData()
{
	m_respawn_time = 0;
	m_spawnTemplateNameData.clear();
}
//-------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	AIUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "RespawnTime",			INI::parseDurationUnsignedInt,		NULL, offsetof(DroneCarrierAIUpdateModuleData, m_respawn_time) },
		{ "DroneTemplateName",INI::parseAsciiStringVectorAppend,NULL, offsetof(DroneCarrierAIUpdateModuleData, m_spawnTemplateNameData) },
		{ "Slots",						INI::parseInt,										NULL, offsetof(DroneCarrierAIUpdateModuleData, m_slots) },

		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

DroneCarrierAIUpdate::DroneCarrierAIUpdate(Thing* thing, const ModuleData* moduleData) : AIUpdateInterface(thing, moduleData)
{
	const DroneCarrierAIUpdateModuleData* md = getDroneCarrierAIUpdateModuleData();

	m_templateNameIterator = md->m_spawnTemplateNameData.begin();
	m_spawnTemplate = TheThingFactory->findTemplate(*m_templateNameIterator);
	m_rebuild_time = 0;
	m_active = true;
	m_initial_spawns = false;

	m_designatedTarget = INVALID_ID;
	m_designatedCommand = AICMD_NO_COMMAND;
	m_designatedPosition.zero();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DroneCarrierAIUpdate::~DroneCarrierAIUpdate(void)
{
}

Bool DroneCarrierAIUpdate::shouldTryToSpawn()
{
	return false; // TODO
}

Bool DroneCarrierAIUpdate::createSpawn()
{
	Object* parent = getObject();
	const DroneCarrierAIUpdateModuleData* md = getDroneCarrierAIUpdateModuleData();
	ContainModuleInterface* contain = parent->getContain();
	if (contain == nullptr) {
		DEBUG_CRASH(("DroneCarrierAIUpdate requires unit to have a contain module!"));
		return false;
	}
	if (contain->getContainMax() < md->m_slots) {
		DEBUG_CRASH(("DroneCarrierAIUpdate requires unit to have a contain module with equal or more slots!"));
		return false;
	}

	Object* newSpawn = NULL;

	m_spawnTemplate = TheThingFactory->findTemplate(*m_templateNameIterator);

	newSpawn = TheThingFactory->newObject(m_spawnTemplate, parent->getTeam()); // just a little worried about this...

	// Count this unit towards our score.
	newSpawn->getControllingPlayer()->onUnitCreated(parent, newSpawn);

	++m_templateNameIterator;
	if (m_templateNameIterator == md->m_spawnTemplateNameData.end())
	{
		m_templateNameIterator = md->m_spawnTemplateNameData.begin();
	}

	newSpawn->setProducer(parent);

	// If they have a SlavedUpdate, then I have to tell them who their daddy is from now on.
	for (BehaviorModule** update = newSpawn->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != NULL)
		{
			sdu->onEnslave(parent);
			break;
		}
	}
	m_spawnIDs.push_back(newSpawn->getID());

	contain->addToContain(newSpawn);
	return TRUE;
}

Bool DroneCarrierAIUpdate::is_full() {
	const DroneCarrierAIUpdateModuleData* md = getDroneCarrierAIUpdateModuleData();
	return m_spawnIDs.size() >= md->m_slots;
}

Object* DroneCarrierAIUpdate::getClosestSlave(const Coord3D* pos)
{
	Object* closest = NULL;
	Real closestDistance;

	for (const ObjectID & spawn_id : m_spawnIDs) {
		Object* obj = TheGameLogic->findObjectByID(spawn_id);
		if (obj)
		{
			Real distance = ThePartitionManager->getDistanceSquared(obj, pos, FROM_CENTER_2D);

			if (!closest || closestDistance > distance)
			{
				closest = obj;
				closestDistance = distance;
			}
		}
	}
	return closest; //Could be null!
}

void DroneCarrierAIUpdate::onSpawnDeath(ObjectID deadSpawn, DamageInfo* damageInfo)
{
	auto it = std::find(m_spawnIDs.begin(), m_spawnIDs.end(), deadSpawn);

	// If the iterator is at the end, we didn't find deadSpawn, so bail out.
	// Otherwise, bad crash stuff will happen.
	if (it == m_spawnIDs.end())
		return;

	//When one dies, you push (now + delay) as the time a new one should be made
	const DroneCarrierAIUpdateModuleData* md = getDroneCarrierAIUpdateModuleData();

	//trigger rebuilt timer when not active, carrier must have open slot if one died
	if (m_rebuild_time == 0) {
		m_rebuild_time = TheGameLogic->getFrame() + md->m_respawn_time;
	}

	m_spawnIDs.erase(it);
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::orderSlavesToAttackTarget(Object* target, Int maxShotsToFire, CommandSourceType cmdSource)
{
	for (const auto & slave_id: m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			AIUpdateInterface* ai = obj->getAI();
			if (ai)
			{
				ai->aiForceAttackObject(target, maxShotsToFire, cmdSource);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::orderSlavesToAttackPosition(const Coord3D* pos, Int maxShotsToFire, CommandSourceType cmdSource)
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			AIUpdateInterface* ai = obj->getAI();
			if (ai)
			{
				ai->aiAttackPosition(pos, maxShotsToFire, cmdSource);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::orderSlavesToGoIdle(CommandSourceType cmdSource)
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			AIUpdateInterface* ai = obj->getAI();
			if (ai)
			{
				ai->aiIdle(cmdSource);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::orderSlavesDisabledUntil(DisabledType type, UnsignedInt frame)
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			AIUpdateInterface* ai = obj->getAI();
			if (ai)
			{
				ai->aiIdle(CMD_FROM_AI);
			}
			obj->setDisabledUntil(type, frame);
		}
	}
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::orderSlavesToClearDisabled(DisabledType type)
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			obj->clearDisabled(type);
		}
	}
}

// ------------------------------------------------------------------------------------------------
CanAttackResult DroneCarrierAIUpdate::getCanAnySlavesAttackSpecificTarget(AbleToAttackType attackType, const Object* target, CommandSourceType cmdSource)
{
	Bool invalidShot = FALSE;
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			CanAttackResult result = obj->getAbleToAttackSpecificObject(attackType, target, cmdSource);

			switch (result)
			{
			case ATTACKRESULT_POSSIBLE:
			case ATTACKRESULT_POSSIBLE_AFTER_MOVING:
				return result;

			case ATTACKRESULT_NOT_POSSIBLE:
				break;

			case ATTACKRESULT_INVALID_SHOT:
				invalidShot = TRUE;
				break;

			default:
				DEBUG_CRASH(("DroneCarrierAIUpdate::getCanAnySlavesAttackSpecificTarget encountered unhandled CanAttackResult of %d. Treating as not possible...", result));
				break;
			}
		}
	}
	//Prioritize the reasonings!
	if (invalidShot)
	{
		return ATTACKRESULT_INVALID_SHOT;
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

// ------------------------------------------------------------------------------------------------
CanAttackResult DroneCarrierAIUpdate::getCanAnySlavesUseWeaponAgainstTarget(AbleToAttackType attackType, const Object* victim, const Coord3D* pos, CommandSourceType cmdSource)
{
	Bool invalidShot = FALSE;
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			CanAttackResult result = obj->getAbleToUseWeaponAgainstTarget(attackType, victim, pos, cmdSource);

			switch (result)
			{
			case ATTACKRESULT_POSSIBLE:
			case ATTACKRESULT_POSSIBLE_AFTER_MOVING:
				return result;

			case ATTACKRESULT_NOT_POSSIBLE:
				break;

			case ATTACKRESULT_INVALID_SHOT:
				invalidShot = TRUE;
				break;

			default:
				DEBUG_CRASH(("DroneCarrierAIUpdate::getCanAnySlavesUseWeaponAgainstTarget encountered unhandled CanAttackResult of %d. Treating as not possible...", result));
				break;
			}
		}
	}
	//Prioritize the reasonings!
	if (invalidShot)
	{
		return ATTACKRESULT_INVALID_SHOT;
	}
	return ATTACKRESULT_NOT_POSSIBLE;
}

// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::giveSlavesStealthUpgrade(Bool grantStealth)
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			obj->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_CAN_STEALTH), grantStealth);
		}
	}
}

Bool DroneCarrierAIUpdate::canAnySlavesAttack()
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj)
		{
			if (obj->isAbleToAttack())
			{
				return true;
			}
		}
	}
	return false;
}
// ------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
Bool DroneCarrierAIUpdate::areAllSlavesStealthed() const
{
	Object* currentSpawn;

	for (const auto& slave_id : m_spawnIDs)
	{
		currentSpawn = TheGameLogic->findObjectByID((slave_id));
		if (currentSpawn)
		{
			const StealthUpdate* stealthUpdate = currentSpawn->getStealth();
			if (!stealthUpdate || !stealthUpdate->allowedToStealth(currentSpawn))
			{
				return FALSE;
			}
		}
	}

	return TRUE; //0 or more spawns are ALL stealthed... I suppose if you have NO spawns, then they are considered stealthed ;)
}

//-------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::revealSlaves()
{
	Object* currentSpawn;

	for (const auto& slave_id : m_spawnIDs)
	{
		currentSpawn = TheGameLogic->findObjectByID((slave_id));
		if (currentSpawn)
		{
			StealthUpdate* stealthUpdate = currentSpawn->getStealth();
			if (stealthUpdate)
			{
				stealthUpdate->markAsDetected();
			}
		}
	}
}

/*************************************************/
/* DIE */
//-------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::onDie(const DamageInfo* damageInfo)
{
	// kill all drone slaves
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj == NULL || obj->isEffectivelyDead())
			continue;

		//TODO config option for disabled effect?
		obj->setDisabled(DISABLED_UNMANNED);
		if (obj->getAI())
			obj->getAI()->aiIdle(CMD_FROM_AI);
		//obj->kill();
	}	
}

/****************************************/
void DroneCarrierAIUpdate::deployDrones()
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* carrier = getObject();
		Object* obj = TheGameLogic->findObjectByID(slave_id);
		if (obj == NULL || obj->isEffectivelyDead())
			continue;

		if (obj->isContained()) {
			AIUpdateInterface* ai = obj->getAI();
			if (ai != nullptr) {
				ai->aiExit(carrier, CMD_FROM_AI);
			}
		}
	}
}

void DroneCarrierAIUpdate::retrieveDrones()
{
	//Order all outside drones back inside!
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* member = TheGameLogic->findObjectByID(slave_id);
		AIUpdateInterface* ai = member ? member->getAI() : NULL;
		if (member && ai)
		{
			Bool contained = member->isContained();
			if (!contained)
			{
				if (ai->getAIStateType() != AI_ENTER) {
					ai->aiEnter(getObject(), CMD_FROM_AI);
				}
			}
		}
	}
}

void DroneCarrierAIUpdate::propagateOrderToSpecificDrone(Object* drone)
{
	if (drone != nullptr)
	{
		AIUpdateInterface* ai = drone->getAI();
		if (ai != nullptr)
		{
			Object* target = TheGameLogic->findObjectByID(m_designatedTarget);
			bool contained = drone->isContained();
			switch (m_designatedCommand)
			{
			case AICMD_GUARD_POSITION:
				if (!contained) {
					ai->aiGuardPosition(&m_designatedPosition, GUARDMODE_NORMAL, CMD_FROM_AI);
				}
				else {
					ai->aiExit(getObject(), CMD_FROM_AI);
				}
				break;
			case AICMD_ATTACK_POSITION:
				if (!contained) {
					ai->aiAttackPosition(&m_designatedPosition, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
				}
				else {
					ai->aiExit(getObject(), CMD_FROM_AI);
				}
				break;
			case AICMD_FORCE_ATTACK_OBJECT:
			case AICMD_ATTACK_OBJECT:
				if (!contained) {
					ai->aiForceAttackObject(target, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER);
				}
				else {
					ai->aiExit(getObject(), CMD_FROM_AI);
				}
				break;
			case AICMD_ATTACKMOVE_TO_POSITION:
				if (!contained) {
					ai->aiAttackMoveToPosition(&m_designatedPosition, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI);
				}
				else
				{
					ai->aiExit(getObject(), CMD_FROM_AI);
				}
				break;
			case AICMD_IDLE:
				if (!contained) {
					ai->aiEnter(getObject(), CMD_FROM_AI);
				}
				else {
					ai->aiIdle(CMD_FROM_AI);
				}
				break;
			}
		}
	}
}

bool DroneCarrierAIUpdate::positionInRange(const Coord3D* loc)
{
	Object* carrier = getObject();
	Weapon* primaryWeapon = carrier->getWeaponInWeaponSlot(PRIMARY_WEAPON);

	// set a limit on how far drones are send out
	if (primaryWeapon == nullptr) {
		return true;
	}

	Real range = primaryWeapon->getAttackRange(carrier);
	Coord3D pos = *carrier->getPosition();
	pos.sub(loc);
	return pos.lengthSqr() <= range * range;
}

//-------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::propagateOrdersToDrones()
{
	for (const auto& slave_id : m_spawnIDs)
	{
		Object* drone = TheGameLogic->findObjectByID(slave_id);
		if (drone != nullptr) {
			propagateOrderToSpecificDrone(drone);
		}
	}
}

/**********************************************/
/* AI Update */
//-------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::aiDoCommand(const AICommandParms* parms)
{
	//forward commands to drones
  if (parms->m_cmdSource != CMD_FROM_AI)
	{
		//Now the only time we care about anything is if we were ordered to attack something or attack move.
		switch (parms->m_cmd)
		{
		case AICMD_GUARD_POSITION:
			m_designatedTarget = INVALID_ID;
			m_designatedPosition.set(&parms->m_pos);
			m_designatedCommand = parms->m_cmd;
			//if (positionInRange(&m_designatedPosition))
			//	propagateOrdersToDrones();
			break;
		case AICMD_ATTACK_POSITION:
			m_designatedTarget = INVALID_ID;
			m_designatedPosition.set(&parms->m_pos);
			m_designatedCommand = parms->m_cmd;
			//if (positionInRange(&m_designatedPosition))
			//	propagateOrdersToDrones();
			break;
		case AICMD_FORCE_ATTACK_OBJECT:
		case AICMD_ATTACK_OBJECT:
			m_designatedTarget = parms->m_obj ? parms->m_obj->getID() : INVALID_ID;
			m_designatedPosition.zero();
			m_designatedCommand = parms->m_cmd;
			//if (parms->m_obj != nullptr && positionInRange(parms->m_obj->getPosition())) 
			//	propagateOrdersToDrones();
			break;
		case AICMD_ATTACKMOVE_TO_POSITION:
			m_designatedTarget = INVALID_ID;
			m_designatedPosition.set(&parms->m_pos);
			m_designatedCommand = parms->m_cmd;
			//if (positionInRange(&m_designatedPosition))
			//	propagateOrdersToDrones();
			break;
		case AICMD_IDLE:
			m_designatedTarget = INVALID_ID;
			m_designatedPosition.zero();
			m_designatedCommand = parms->m_cmd;
			//propagateOrdersToDrones();
			break;
		default:
			m_designatedCommand = AICMD_NO_COMMAND;
			break;
		}
	}

	AIUpdateInterface::aiDoCommand( parms );
}


UpdateSleepTime DroneCarrierAIUpdate::update()
{
	const DroneCarrierAIUpdateModuleData* data = getDroneCarrierAIUpdateModuleData();

	// initially fill
	if (!m_initial_spawns) {
		for (size_t i = 0; i < data->m_slots; i++) {
			createSpawn();
		}
		m_initial_spawns = true;
	}

	UnsignedInt now = TheGameLogic->getFrame();

	if (m_rebuild_time != 0 && m_rebuild_time <= now && !is_full())
	{
		if (createSpawn()) {
			if (is_full()) {
				m_rebuild_time = 0;
			}
			else {
				m_rebuild_time = now + data->m_respawn_time;
			}
		}
	}

	Object* my_target = getCurrentVictim();
	if (my_target != nullptr) {

		if (positionInRange(my_target->getPosition())) {
			// send out contained drones
			deployDrones();

			//update orders
			if (now % 5 == 0) { //do every 5th frame
				switch (m_designatedCommand) {
				case AICMD_GUARD_POSITION:
				case AICMD_ATTACK_POSITION:
				case AICMD_FORCE_ATTACK_OBJECT:
				case AICMD_ATTACK_OBJECT:
				case AICMD_ATTACKMOVE_TO_POSITION:
					propagateOrdersToDrones();
					break;
				default:
					break;
				}
			}
		}
	}

	return AIUpdateInterface::update();
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::crc(Xfer* xfer)
{

	// extend base class
	AIUpdateInterface::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added m_initialBurstTimesInited to the save. jba.
*/
// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::xfer(Xfer* xfer)
{
	AsciiString name;

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	AIUpdateInterface::xfer(xfer);


}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DroneCarrierAIUpdate::loadPostProcess(void)
{

	// extend base class
	AIUpdateInterface::loadPostProcess();

}  // end loadPostProcess
