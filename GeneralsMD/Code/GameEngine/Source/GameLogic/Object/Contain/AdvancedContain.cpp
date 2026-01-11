// FILE: AdvancedContain.cpp ////////////////////////////////////////////////////////////////////////
// Desc:   Contain module that allows multiple Objects and Units
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/AdvancedContain.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"



// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
AdvancedContainModuleData::AdvancedContainModuleData()
{
//	m_initialPayload.count = 0;
	m_experienceSinkForRider = TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AdvancedContainModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  TransportContainModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
    { "PayloadTemplateName",  INI::parseAsciiStringVectorAppend, NULL, offsetof(AdvancedContainModuleData, m_payloadTemplateNameData) },
    { "ExperienceSinkForRider",  INI::parseBool, NULL, offsetof(AdvancedContainModuleData, m_experienceSinkForRider) },

		{ 0, 0, 0, 0 }
	};
  p.add(dataFieldParse);
}
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
//void OverlordContainModuleData::parseInitialPayload( INI* ini, void *instance, void *store, const void* /*userData*/ )
//{
//	OverlordContainModuleData* self = (OverlordContainModuleData*)instance;
//	const char* name = ini->getNextToken();
//	const char* countStr = ini->getNextTokenOrNull();
//	Int count = countStr ? INI::scanInt(countStr) : 1;

//	self->m_initialPayload.name.set(name);
//	self->m_initialPayload.count = count;
//}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
AdvancedContain::AdvancedContain( Thing *thing, const ModuleData *moduleData ) :
								 TransportContain( thing, moduleData )
{
	m_redirectionActivated = FALSE;

  m_payloadCreated = FALSE;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
AdvancedContain::~AdvancedContain( void )
{

}


void AdvancedContain::onObjectCreated( void )
{
	AdvancedContain::createPayload();
}

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void AdvancedContain::createPayload()
{
	AdvancedContainModuleData* self = (AdvancedContainModuleData*)getAdvancedContainModuleData();


  // Any number of different passengers can be loaded here at init time
	Object* object = getObject();
	ContainModuleInterface *contain = object->getContain();
	if( contain )
  {
		contain->enableLoadSounds( FALSE );

	  TemplateNameList list = self->m_payloadTemplateNameData;
	  TemplateNameIterator iter = list.begin();
	  while ( iter != list.end() )
	  {
		  const ThingTemplate* temp = TheThingFactory->findTemplate( *iter );
		  if (temp)
		  {
			  Object* payload = TheThingFactory->newObject( temp, object->getTeam() );

			  if( contain->isValidContainerFor( payload, true ) )
			  {
				  contain->addToContain( payload );
			  }
			  else
			  {
				  DEBUG_CRASH( ( "OverlordContain::createPayload: %s is full, or not valid for the payload %s!", object->getName().str(), self->m_initialPayload.name.str() ) );
			  }

      }

      ++iter;
    }

		contain->enableLoadSounds( TRUE );

  }

	m_payloadCreated = TRUE;

}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void AdvancedContain::onBodyDamageStateChange( const DamageInfo* damageInfo,
																				BodyDamageType oldState,
																				BodyDamageType newState)  ///< state change callback
{
	// I can't use any convienience functions, as they will all get routed to the bunker I may carry.
	// I want just me.
	// Oh, and I don't want this function trying to do death.  That is more complicated and will be handled
	// on my death.
	if( newState != BODY_RUBBLE  &&  m_containListSize == 1 )
	{
		Object *myGuy = m_containList.front();
		myGuy->getBodyModule()->setDamageState( newState );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ContainModuleInterface * AdvancedContain::getRedirectedContain() const
{
	// Naturally, I can not use a redirectible convienience function
	// to answer if I am redirecting yet.

	// If I am empty, say no.
	if( m_containListSize < 1 )
		return NULL;

	if( !m_redirectionActivated )
		return NULL;// Shut off early to allow death to happen without my bunker having
	// trouble finding me to say goodbye as messages get sucked up the pipe to him.

	Object *myGuy = m_containList.front();
	if( myGuy )
		return myGuy->getContain();

	return NULL;// Or say no if they have no contain.
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------



//-------------------------------------------------------------------------------------------------
void AdvancedContain::onDie( const DamageInfo *damageInfo )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::onDie( damageInfo );
		return;
	}
	//Everything is fine if I am empty or carrying a regular guy.  If I have a redirected contain
	// set up, then I need to handle the order of death explicitly, or things will become confused
	// when I stop redirecting in the middle of the process.  Or I will get confused as my commands
	// get sucked up the pipe.

	// So this is an extend that lets me control the order of death.

	deactivateRedirectedContain();
	Object *myGuy = m_containList.front();
	myGuy->kill();

	TransportContain::onDie( damageInfo );
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::onDelete( void )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::onDelete( );
		return;
	}

	// Without my throwing the redirect switch, teardown deletion will get confused and fire off a bunch of asserts
	getRedirectedContain()->removeAllContained();

	deactivateRedirectedContain();
	removeAllContained();

	TransportContain::onDelete( );
}

// ------------------------------------------------------------------------------------------------
void AdvancedContain::onCapture( Player *oldOwner, Player *newOwner )
{
	if( m_containListSize < 1 )
		return;

	// Need to capture our specific rider.  He will then kick passengers out if he is a Transport
	Object *myGuy = m_containList.front();
	myGuy->setTeam( newOwner->getDefaultTeam() );
}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::isGarrisonable() const
{
	if( getRedirectedContain() == NULL )
		return FALSE;

	return getRedirectedContain()->isGarrisonable();
}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::isKickOutOnCapture()
{
	if( getRedirectedContain() == NULL )
		return FALSE;// Me the Overlord doesn't want to

	return getRedirectedContain()->isKickOutOnCapture();
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::addToContainList( Object *obj )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::addToContainList( obj );
		return;
	}

	getRedirectedContain()->addToContainList( obj );
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::addToContain( Object *obj )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::addToContain( obj );
		return;
	}

	getRedirectedContain()->addToContain( obj );

}

//-------------------------------------------------------------------------------------------------
/** Remove 'obj' from the m_containList of objects in this module.
	* This will trigger an onRemoving event for the object that this module
	* is a part of and an onRemovedFrom event for the object being removed */
//-------------------------------------------------------------------------------------------------
void AdvancedContain::removeFromContain( Object *obj, Bool exposeStealthUnits )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::removeFromContain( obj, exposeStealthUnits );
		return;
	}

	getRedirectedContain()->removeFromContain( obj, exposeStealthUnits );

}

//-------------------------------------------------------------------------------------------------
/** Remove all contained objects from the contained list */
//-------------------------------------------------------------------------------------------------
void AdvancedContain::removeAllContained( Bool exposeStealthUnits )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::removeAllContained( exposeStealthUnits );
		return;
	}

	const ContainedItemsList *fullList = getRedirectedContain()->getContainedItemsList();

	Object *obj;
	ContainedItemsList::const_iterator it;
	it = (*fullList).begin();
	while( it != (*fullList).end() )
	{
		obj = *it;
		it++;
		removeFromContain( obj, exposeStealthUnits );
	}
}

//-------------------------------------------------------------------------------------------------
/** Iterate the contained list and call the callback on each of the objects */
//-------------------------------------------------------------------------------------------------
void AdvancedContain::iterateContained( ContainIterateFunc func, void *userData, Bool reverse )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::iterateContained( func, userData, reverse );
		return;
	}

	getRedirectedContain()->iterateContained( func, userData, reverse );
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::onContaining( Object *obj, Bool wasSelected )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::onContaining( obj, wasSelected );


    if ( obj->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
    {
  		activateRedirectedContain();//Am now carrying something

			// And this contain style explicitly sucks XP from our little friend.
			if( getAdvancedContainModuleData()->m_experienceSinkForRider  &&  obj->getExperienceTracker() )
				obj->getExperienceTracker()->setExperienceSink(getObject()->getID());


      if ( obj->isKindOf( KINDOF_PORTABLE_STRUCTURE ) && getObject()->testStatus( OBJECT_STATUS_STEALTHED ) )
      {
        StealthUpdate *myStealth =  obj->getStealth();
        if ( myStealth )
        {
          myStealth->receiveGrant( true );
          // note to anyone... once stealth is granted to this gattlingcannon ( or such )
          // let its own stealthupdate govern the allowedtostealth cases
          // a portable structure never gets removed, so...
        }
      }




    }


    return;
	}

	OpenContain::onContaining( obj, wasSelected );

	getRedirectedContain()->onContaining( obj, wasSelected );

}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::killAllContained( void )
{
	// This is a game call meant to clear actual passengers.  We don't want it to kill our turret.  That'd be wierd.
	if( getRedirectedContain() )
	{
		getRedirectedContain()->killAllContained();
	}
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::onRemoving( Object *obj )
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		TransportContain::onRemoving( obj );
		return;
	}

	OpenContain::onRemoving(obj);

	getRedirectedContain()->onRemoving( obj );

}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::isValidContainerFor(const Object* obj, Bool checkCapacity) const
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
		return TransportContain::isValidContainerFor( obj, checkCapacity );

	return getRedirectedContain()->isValidContainerFor( obj, checkCapacity );
}

//-------------------------------------------------------------------------------------------------
UnsignedInt AdvancedContain::getContainCount() const
{
	ContainModuleInterface* redir = getRedirectedContain();

	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( redir == NULL )
		return TransportContain::getContainCount( );

	return redir->getContainCount();
}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::getContainerPipsToShow(Int& numTotal, Int& numFull)
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
	{
		numTotal = 0;
		numFull = 0;
		return false;
	}
	else
	{
		return getRedirectedContain()->getContainerPipsToShow(numTotal, numFull);
	}
}

//-------------------------------------------------------------------------------------------------
Int AdvancedContain::getContainMax( ) const
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
		return TransportContain::getContainMax( );

	return getRedirectedContain()->getContainMax();
}

//-------------------------------------------------------------------------------------------------
const ContainedItemsList* AdvancedContain::getContainedItemsList() const
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
		return TransportContain::getContainedItemsList( );

	return getRedirectedContain()->getContainedItemsList();
}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::isEnclosingContainerFor( const Object *obj ) const
{
	// All of this redirection stuff makes it so that while I am normally a transport
	// for Overlord subObjects, once I have a passenger, _I_ become a transport of their type.
	// So, the answer to this question depends on if it is my passenger asking, or theirs.
	// As always, I can't use convience functions that get redirected on a ? like this.
	if( m_containListSize > 0  &&  obj ==  m_containList.front() )
		return FALSE;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool AdvancedContain::isDisplayedOnControlBar() const
{
	// Do you mean me the Overlord, or my behavior of passing stuff on to my passengers?
	if( getRedirectedContain() == NULL )
		return FALSE;//No need to call up inheritance, this is a module based question, and I say no.

	return getRedirectedContain()->isDisplayedOnControlBar();
}

//-------------------------------------------------------------------------------------------------
const Object * AdvancedContain::friend_getRider() const
{
// The draw order dependency bug for riders means that our draw module needs to cheat to get
	//around it.	So this is another function that knows it is getting around redirection to ask
	// an Overlord specific function.

 	if( m_containListSize > 0 )
 		return m_containList.front();


	return NULL;
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::activateRedirectedContain()
{
	m_redirectionActivated = TRUE;
}

//-------------------------------------------------------------------------------------------------
void AdvancedContain::deactivateRedirectedContain()
{
	m_redirectionActivated = FALSE;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
// if my object gets selected, then my visible passengers should, too
// this gets called from
void AdvancedContain::clientVisibleContainedFlashAsSelected()
{
	// THIS OVERRIDES GRAHAMS NASTY OVERRIDE THING
	// SO WE CAN FLASH THE PORTABLE BUNKER INSTEAD OF ITS OCCUPANTS
	const ContainedItemsList* items = TransportContain::getContainedItemsList();

	if( items )
	{
		ContainedItemsList::const_iterator it;
		it = items->begin();

		while( it != items->end() )
		{
			Object *object = *it;
			if ( object && object->isKindOf( KINDOF_PORTABLE_STRUCTURE ) )
			{
				Drawable *draw = object->getDrawable();
				if ( draw )
				{
					draw->flashAsSelected(); //WOW!
				}
			}

			++it;
		}
	}

}


Bool AdvancedContain::isPassengerAllowedToFire( ObjectID id ) const
{
	Object *passenger = TheGameLogic->findObjectByID(id);

	if(passenger != NULL)
	{
		//only allow infantry, and turrets and such.  no vehicles.
		if(passenger->isKindOf(KINDOF_INFANTRY) == FALSE && passenger->isKindOf(KINDOF_PORTABLE_STRUCTURE) == FALSE)
			return FALSE;
	}


  if ( getObject() && getObject()->getContainedBy() ) // nested containment voids firing, always
    return FALSE;

  return TransportContain::isPassengerAllowedToFire();
}



// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AdvancedContain::crc( Xfer *xfer )
{

	// extend base class
	TransportContain::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void AdvancedContain::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	TransportContain::xfer( xfer );

	// redirection activated
	xfer->xferBool( &m_redirectionActivated );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AdvancedContain::loadPostProcess( void )
{

	// extend base class
	TransportContain::loadPostProcess();

}
