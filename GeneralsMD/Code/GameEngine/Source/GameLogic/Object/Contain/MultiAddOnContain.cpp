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

///////////////////////////////////////////////////////////////////////////////////////////////////
//
//  FILE: MultiAddOnContain.cpp ////////////////////////////////////////////////////////////////////////
//  Author: Mark Lorenzen, April, 2003
//
//  Desc:
//
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/MultiAddOnContain.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Weapon.h"




// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
MultiAddOnContainModuleData::MultiAddOnContainModuleData()
{
  //	m_initialPayload.count = 0;
  m_drawPips = TRUE;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void MultiAddOnContainModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  TransportContainModuleData::buildFieldParse(p);

  static const FieldParse dataFieldParse[] =
  {
    { "PayloadTemplateName",  INI::parseAsciiStringVectorAppend, NULL, offsetof(MultiAddOnContainModuleData, m_payloadTemplateNameData) },
    {"ShouldDrawPips",  INI::parseBool, NULL, offsetof(MultiAddOnContainModuleData, m_drawPips) },

    { 0, 0, 0, 0 }
  };
  p.add(dataFieldParse);
}
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
//void MultiAddOnContainModuleData::parseInitialPayload( INI* ini, void *instance, void *store, const void* /*userData*/ )
//{
//	MultiAddOnContainModuleData* self = (MultiAddOnContainModuleData*)instance;
//	const char* name = ini->getNextToken();
//	const char* countStr = ini->getNextTokenOrNull();
//	Int count = countStr ? INI::scanInt(countStr) : 1;

//	self->m_initialPayload.name.set(name);
//	self->m_initialPayload.count = count;
//}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
MultiAddOnContain::MultiAddOnContain(Thing* thing, const ModuleData* moduleData) :
  TransportContain(thing, moduleData)
{

  m_payloadCreated = FALSE;
  m_portableStructureID = INVALID_ID;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MultiAddOnContain::~MultiAddOnContain(void)
{

}


void MultiAddOnContain::onObjectCreated(void)
{
  MultiAddOnContain::createPayload();
}



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime MultiAddOnContain::update()
{

  Object* portable = getPortableStructure();
  if (portable)
  {
    portable->setPosition(getObject()->getPosition());
    portable->setOrientation(getObject()->getOrientation());
  }

  return TransportContain::update(); // extend base
}


void MultiAddOnContain::redeployOccupants(void)
{
  // Removed by AndiW: This restores proper firebones, if the parent vehicle model has them

  // Coord3D firePos = *getObject()->getPosition();
  // firePos.z += 8;
  // for (ContainedItemsList::iterator it = m_containList.begin(); it != m_containList.end(); ++it)
  // {
  //   Object* rider = *it;
  //   if (rider)
  //     rider->setPosition( &firePos );
  // }
  OpenContain::redeployOccupants();
}


//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void MultiAddOnContain::createPayload()
{
  MultiAddOnContainModuleData* self = (MultiAddOnContainModuleData*)getMultiAddOnContainModuleData();


  // Any number of different passengers can be loaded here at init time
  Object* object = getObject();
  ContainModuleInterface* contain = object->getContain();
  if (contain)
  {
    contain->enableLoadSounds(FALSE);

    TemplateNameList list = self->m_payloadTemplateNameData;
    TemplateNameIterator iter = list.begin();
    while (iter != list.end())
    {
      const ThingTemplate* temp = TheThingFactory->findTemplate(*iter);
      if (temp)
      {
        Object* payload = TheThingFactory->newObject(temp, object->getTeam());

        if (contain->isValidContainerFor(payload, true))
        {
          contain->addToContain(payload);
        }
        else
        {
          DEBUG_CRASH(("MultiAddOnContain::createPayload: %s is full, or not valid for the payload %s!", object->getName().str(), self->m_initialPayload.name.str()));
        }

      }

      ++iter;
    }

    contain->enableLoadSounds(TRUE);

  } // endif contain

  m_payloadCreated = TRUE;

}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::onBodyDamageStateChange(const DamageInfo* damageInfo,
  BodyDamageType oldState,
  BodyDamageType newState)  ///< state change callback
{
  // Need to apply state change to the portable structure
  Object* portable = getPortableStructure();
  if (newState != BODY_RUBBLE && portable)
  {
    portable->getBodyModule()->setDamageState(newState);
  }

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
VecObjectPtr& MultiAddOnContain::getPortableStructures(void)
{
	VecObjectPtr portableStructures;
  for (VecObjectIDIt it = m_portableStructureIDs.begin(); it != m_portableStructureIDs.end(); ) {
    Object* obj = TheGameLogic->findObjectByID(*it);
    if (obj)
      portableStructures.push_back(obj);
  }
  return portableStructures;
}



//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::onDie(const DamageInfo* damageInfo)
{
  VecObjectPtr portable = getPortableStructures();
  for (VecObjectPtrIt it = portable.begin(); it != portable.end(); ) {
    if (*it)
      (*it)->kill();
  }

  TransportContain::onDie(damageInfo);//extend base class
}

//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::onDelete(void)
{
  VecObjectPtr portable = getPortableStructures();
  for (VecObjectPtrIt it = portable.begin(); it != portable.end(); ) {
    if (*it)
      TheGameLogic->destroyObject(*it);
  }
    
  TransportContain::onDelete();
}

// ------------------------------------------------------------------------------------------------
void MultiAddOnContain::onCapture(Player* oldOwner, Player* newOwner)
{
  //  Need to setteam() the portable structure, that's all;

  VecObjectPtr portable = getPortableStructures();
  for (VecObjectPtrIt it = portable.begin(); it != portable.end(); ) {
    if (*it)
      (*it)->setTeam(newOwner->getDefaultTeam());
  }
}

//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::addToContainList(Object* obj)
{
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE))
  {
    // This is probably redundant and should happen in addToContain instead
    //m_portableStructureIDs.push_back(obj->getID());
    //obj->friend_setContainedBy(getObject());//fool portable into thinking my object is his container

  }
  else {
    // Get OCL from dict and spawn portable structure
    TransportContain::addToContainList(obj);
  }
}

//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::addToContain(Object* obj)
{
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE))
  {
    m_portableStructureIDs.push_back(obj->getID());

    obj->friend_setContainedBy(getObject());//fool portable into thinking my object is his container

  }
  else {
    // Get OCL from dict and spawn portable structure
    TransportContain::addToContain(obj);
    //createOCLForRider(obj);
  }
}

//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::removeFromContain(Object* obj, Bool exposeStealthUnits)
{
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE))
  {
    for (VecObjectIDIt it = m_portableStructureIDs.begin(); it != m_portableStructureIDs.end(); ) {
      if (obj->getID() == (*it))
        m_portableStructureIDs.erase(it);
    }
  }
  else
  {
    // Get corresponding Structure and remove it
    TransportContain::removeFromContain(obj, exposeStealthUnits);
  }
}


//-------------------------------------------------------------------------------------------------
Bool MultiAddOnContain::isValidContainerFor(const Object* obj, Bool checkCapacity) const
{
  // TODO: Check list of allowed passengers
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE) && INVALID_ID == m_portableStructureID)
    return TRUE;

  return TransportContain::isValidContainerFor(obj, checkCapacity);
}


//-------------------------------------------------------------------------------------------------
const Object* MultiAddOnContain::friend_getRider() const
{
  // The draw order dependency bug for riders means that our draw module needs to cheat to get around it.

  if (m_portableStructureID != INVALID_ID)
  {
    const Object* portableAsRider = TheGameLogic->findObjectByID(m_portableStructureID);
    return portableAsRider;
  }

  return NULL;
}

//-------------------------------------------------------------------------------------------------
Bool MultiAddOnContain::isEnclosingContainerFor(const Object* obj) const
{
  if (m_portableStructureID == obj->getID())
  {
    const Object* portableAsRider = TheGameLogic->findObjectByID(m_portableStructureID);
    if (portableAsRider == obj)
      return FALSE;
  }


  return TransportContain::isEnclosingContainerFor(obj);
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// if my object gets selected, then my visible passengers should, too
// this gets called from
void MultiAddOnContain::clientVisibleContainedFlashAsSelected()
{
  if (m_portableStructureID != INVALID_ID)
  {
    Object* portable = getPortableStructure();
    if (portable && portable->isKindOf(KINDOF_PORTABLE_STRUCTURE))
    {
      Drawable* draw = portable->getDrawable();
      if (draw)
      {
        draw->flashAsSelected(); //WOW!
      }
    }
  }
}


Bool MultiAddOnContain::isPassengerAllowedToFire(ObjectID id) const
{
  // WHETHER WE ARE ALLOWED TO FIRE DEPENDS ON WHO WE ARE
  // PASSENGERS (PROPER) MAY ONLY IF THE FLAG IS TRUE
  // RIDERS ARE ALWAYS ALLOWED TO FIRE (GATTLING CANNONS)

  if (getObject() && getObject()->getContainedBy()) // nested containment voids firing, always
    return FALSE;

  if (m_portableStructureID != INVALID_ID && m_portableStructureID == id)
    return TRUE;
  else
  {
    const Object* rider = TheGameLogic->findObjectByID(id);
    if (rider && rider->isKindOf(KINDOF_INFANTRY))
      return TransportContain::isPassengerAllowedToFire(id);//extend
  }

  return FALSE;

}






//-------------------------------------------------------------------------------------------------
void MultiAddOnContain::onContaining(Object* obj, Bool wasSelected)
{
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE))
  {
    DEBUG_LOG((">>>WARNING: MultiAddOnContain::onRemoving called for PortableStructure. Is this right?"));

    // I don't think this was ever called here anyways
    //if (getObject()->testStatus(OBJECT_STATUS_STEALTHED)) {}
    //  StealthUpdate* myStealth = obj->getStealth();
    //  if (myStealth)
    //  {
    //    myStealth->receiveGrant(true);
    //    // note to anyone... once stealth is granted to this gattlingcannon ( or such )
    //    // let its own stealthupdate govern the allowedtostealth cases
    //    // a portable structure never gets removed, so...
    //  }
    //}
    return;
  }
  // extend base class
  TransportContain::onContaining(obj, wasSelected);

  // Spawn the Turret.
  createOCLForRider(obj);

}  // end onContaining

void MultiAddOnContain::onRemoving(Object* obj)
{
  if (obj->isKindOf(KINDOF_PORTABLE_STRUCTURE))
  {
    DEBUG_LOG((">>>WARNING: MultiAddOnContain::onRemoving called for PortableStructure. Is this right?"));
    return;
  }

  // extend base class
  TransportContain::onRemoving(obj);

  // TODO: Remove the corresponding turret

} // end onRemoving


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
bool MultiAddOnContain::createOCLForRider(Object* obj)
{
  const MultiAddOnContainModuleData* d = getMultiAddOnContainModuleData();

  ObjectCreationList* ocl;

  ProductionChangeMap::const_iterator it = d->m_OCLEntries.find(NAMEKEY(obj->getTemplate()->getName()));
  if (it != d->m_OCLEntries.end())
  {
    ocl = (*it).second;
  }
  else {
    return false;
  }

  Object* obj = ocl->create(getObject(), NULL);
  if (obj)
    return true;

  return false;
}



// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MultiAddOnContain::crc(Xfer* xfer)
{

  // extend base class
  TransportContain::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
  * Version Info:
  * 1: Initial version */
  // ------------------------------------------------------------------------------------------------
void MultiAddOnContain::xfer(Xfer* xfer)
{

  // version
  XferVersion currentVersion = 2;
  XferVersion version = currentVersion;
  xfer->xferVersion(&version, currentVersion);

  if (version >= 2)
    xfer->xferObjectID(&m_portableStructureID);

  // extend base class
  TransportContain::xfer(xfer);


}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MultiAddOnContain::loadPostProcess(void)
{

  // extend base class
  TransportContain::loadPostProcess();

}  // end loadPostProcess
