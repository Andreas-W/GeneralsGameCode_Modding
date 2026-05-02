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

// FILE: BridgeTowerBehavior.cpp //////////////////////////////////////////////////////////////////
// Author: Colin Day, July 2002
// Desc:   Behavior module for the towers attached to bridges that can be targeted
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/BridgeBehavior.h"
#include "GameLogic/Module/BridgeTowerBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/Line2D.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
BridgeTowerBehavior::BridgeTowerBehavior( Thing *thing, const ModuleData *moduleData )
									 : BehaviorModule( thing, moduleData )
{

	m_bridgeID = INVALID_ID;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
BridgeTowerBehavior::~BridgeTowerBehavior( void )
{

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::setBridge( Object *bridge )
{

	if( bridge == nullptr )
		m_bridgeID = INVALID_ID;
	else
		m_bridgeID = bridge->getID();

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
ObjectID BridgeTowerBehavior::getBridgeID( void )
{

	return m_bridgeID;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::setTowerType( BridgeTowerType type )
{

	m_type = type;

}

// Pushes a unit off a bridge
void pushUnitOffBridge(Coord2D& unitPos, const Coord2D& bridgePos, const Coord2D & sideVector, Real bridgeWidth, PhysicsBehavior* unitPhysics)
{
	if (unitPhysics == nullptr) return;

	// Get the direction from the bridge's center to the unit
	Coord2D dirToUnit;
	dirToUnit.x = unitPos.x - bridgePos.x;
	dirToUnit.y = unitPos.y - bridgePos.y;


	//Dot product projects the unit's position onto that sideways vector
	// This tells us the left/right direction AND the exact distance from the center.
	Real distanceToSide = (dirToUnit.x * sideVector.x) + (dirToUnit.y * sideVector.y);

	// Get absolute distance to check if they are actually on the bridge
	//Real absDist = (distanceToSide < 0.0f) ? -distanceToSide : distanceToSide;

	// Calculate how far we need to push them to reach the edge
	//Real halfWidth = bridgeWidth * 0.5f;
	//Real pushAmount = halfWidth - absDist;
	//Real pushAmount = bridgeWidth * 0.5f * unitPhysics->getMass();


	Real pushAmount = unitPhysics->getMass() * bridgeWidth * 0.5f;

	// Only push if the unit is actually currently ON the bridge width
	if (pushAmount > 0.0f)
	{
		// Use the sign template from BaseType.h: 1 for right, -1 for left.
		int pushDirection = sign(distanceToSide);

		// Edge case: if they are in the dead mathematical center, force them to one side
		if (pushDirection == 0)
		{
			pushDirection = 1;
		}

		Coord3D pushforce(sideVector.x * pushDirection * pushAmount, sideVector.y * pushDirection * pushAmount, 0.0f);
		DEBUG_LOG(("BRIDGE_PUSH: %f, %f, %f", pushforce.x, pushforce.y, pushforce.z));
		unitPhysics->applyForce(&pushforce);
		// Apply the push along the side vector
		//unitPos->x += sideVector.x * pushDirection * pushAmount;
		//unitPos->y += sideVector.y * pushDirection * pushAmount;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void pushObjectsOnBridgeRestore(const Object* bridge)
{
	const Coord3D* bridgePos = bridge->getPosition();

	Bridge* terrainBridge = TheTerrainLogic->findBridgeAt(bridgePos);
	if (terrainBridge)
	{
		//PathfindLayerEnum bridgeLayer = terrainBridge->getLayer();

		BridgeInfo bridgeInfo;

		// get the bridge info
		terrainBridge->getBridgeInfo(&bridgeInfo);

		// setup a polygon area using the bridge extents
		Coord3D bridgePolygon[4];
		bridgePolygon[0] = bridgeInfo.fromLeft;
		bridgePolygon[1] = bridgeInfo.fromRight;
		bridgePolygon[2] = bridgeInfo.toRight;
		bridgePolygon[3] = bridgeInfo.toLeft;

		//
		// find the lowest Z point of the bridge area ... we will use this to figure out of
		// objects in the bridge area are "on top" of the bridge
		//
		//Real lowBridgeZ = bridgePolygon[0].z;
		//for (Int i = 0; i < 4; ++i)
		//	if (bridgePolygon[i].z < lowBridgeZ)
		//		lowBridgeZ = bridgePolygon[i].z;

		//
		// given the polygon area, how big is the radius that we need to scan in the world
		// to cover from the center of the bridge (the bridge object position) to the edge
		// of the bridge
		//
		Coord2D v;
		v.x = bridgeInfo.toLeft.x - bridgePos->x;
		v.y = bridgeInfo.toLeft.y - bridgePos->y;
		Real radius = v.length();

		// Calculate the perpendicular "Sideways" vector of the bridge.
		// If the bridge's forward vector is (Cos(angle), Sin(angle)),
		// its sideways perpendicular vector is (-Sin(angle), Cos(angle)).
		const Coord3D* rot = bridge->getUnitDirectionVector2D();
		Coord2D sideVector(rot->x, rot->y);
		sideVector.rotateByAngle(PI * 0.5f);

		//sideVector.x = -Sin(bridge->getOrientation());
		//sideVector.y = Cos(bridge->getOrientation());

		// scan the objects in the radius
		ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(bridgePos,
			radius,
			FROM_CENTER_2D);
		MemoryPoolObjectHolder hold(iter);
		Object* other;
		for (other = iter->first(); other; other = iter->next())
		{

			// ignore some kind of objects
			if (other->isKindOf(KINDOF_BRIDGE) || other->isKindOf(KINDOF_BRIDGE_TOWER))
				continue;

			// ignore airborne objects
			//if (other->isAboveTerrain())
			//	continue;

			if (other->isAirborneTarget())
				continue;

			// ignore objects that were not actually on the bridge
			//if (other->getPosition()->z < lowBridgeZ)
			//	continue;

			// ignore objects that are not inside the bridge polygon
			if (PointInsideArea2D(other->getPosition(), bridgePolygon, 4) == FALSE)
				continue;

			// if object not on same layer as bridge do nothing
			//if (bridgeLayer != other->getLayer())
			//	continue;

			//if (other->getLayer() == bridgeLayer)
			//	other->setLayer(LAYER_GROUND);


			//push units away from bridge
			PhysicsBehavior* physics = other->getPhysics();
			if (physics != nullptr) {

				Coord2D upos(other->getPosition()->x, other->getPosition()->y);
				Coord2D bpos(bridgePos->x, bridgePos->y);

				pushUnitOffBridge(upos, bpos, sideVector, radius, physics);
			}

			// if they have physics, let 'em fall, otherwise just kill 'em
			//PhysicsBehavior* physics = other->getPhysics();
			//if (physics)
			//	physics->setAllowToFall(true);
			//else
			//	other->kill();

		}

	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::onDamage( DamageInfo *damageInfo )
{
	Object *bridge = TheGameLogic->findObjectByID( getBridgeID() );

	// sanity
	if( bridge == nullptr )
		return;

	//
	// get our body info so we now how much damage percent is being done to us ... we need this
	// so that we can propagate the same damage percentage amount the towers and the bridge
	//
	BodyModuleInterface *body = getObject()->getBodyModule();
	Real damagePercentage = damageInfo->in.m_amount / body->getMaxHealth();

	// get the bridge behavior module for our bridge
	BehaviorModule **bmi;
	BridgeBehaviorInterface *bridgeInterface = nullptr;
	for( bmi = bridge->getBehaviorModules(); *bmi; ++bmi )
	{

		bridgeInterface = (*bmi)->getBridgeBehaviorInterface();
		if( bridgeInterface )
			break;

	}
	DEBUG_ASSERTCRASH( bridgeInterface != nullptr, ("BridgeTowerBehavior::onDamage - no 'BridgeBehaviorInterface' found") );
	if( bridgeInterface )
	{

		//
		// damage each of the other towers if the source of this damage isn't from the bridge
		// or other towers
		//
		Object *source = TheGameLogic->findObjectByID( damageInfo->in.m_sourceID );
		if( source == nullptr ||
			  (source->isKindOf( KINDOF_BRIDGE ) == FALSE &&
				 source->isKindOf( KINDOF_BRIDGE_TOWER ) == FALSE) )
		{

			for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
			{
				Object *tower;

				tower = TheGameLogic->findObjectByID( bridgeInterface->getTowerID( (BridgeTowerType)i ) );
				if( tower && tower != getObject() )
				{
					BodyModuleInterface *towerBody = tower->getBodyModule();
					DamageInfo towerDamage;

					towerDamage.in.m_amount = damagePercentage * towerBody->getMaxHealth();
					towerDamage.in.m_sourceID = getObject()->getID();  // we're now the source
					towerDamage.in.m_damageType = damageInfo->in.m_damageType;
					towerDamage.in.m_deathType = damageInfo->in.m_deathType;
					tower->attemptDamage( &towerDamage );

				}

			}

			//
			// damage bridge object, but make sure it's done through the bridge interface
			// so that it doesn't automatically propagate that damage to the towers
			//
			BodyModuleInterface *bridgeBody = bridge->getBodyModule();
			DamageInfo bridgeDamage;

			bridgeDamage.in.m_amount = damagePercentage * bridgeBody->getMaxHealth();
			bridgeDamage.in.m_sourceID = getObject()->getID();  // we're now the source
			bridgeDamage.in.m_damageType = damageInfo->in.m_damageType;
			bridgeDamage.in.m_deathType = damageInfo->in.m_deathType;
			bridge->attemptDamage( &bridgeDamage );

		}

	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::onHealing( DamageInfo *damageInfo )
{
	Object *bridge = TheGameLogic->findObjectByID( getBridgeID() );

	// sanity
	if( bridge == nullptr )
		return;

	//
	// get our body info so we now how much healing percent is being done to us ... we need this
	// so that we can propagate the same healing percentage amount the towers and the bridge
	//
	BodyModuleInterface *body = getObject()->getBodyModule();
	Real healingPercentage = damageInfo->in.m_amount / body->getMaxHealth();

	// get the bridge behavior module for our bridge
	BehaviorModule **bmi;
	BridgeBehaviorInterface *bridgeInterface = nullptr;
	for( bmi = bridge->getBehaviorModules(); *bmi; ++bmi )
	{

		bridgeInterface = (*bmi)->getBridgeBehaviorInterface();
		if( bridgeInterface )
			break;

	}
	DEBUG_ASSERTCRASH( bridgeInterface != nullptr, ("BridgeTowerBehavior::onHealing - no 'BridgeBehaviorInterface' found") );
	if( bridgeInterface )
	{

		//
		// heal each of the other towers if the source of this healing isn't from the bridge
		// or other towers
		//
		Object *source = TheGameLogic->findObjectByID( damageInfo->in.m_sourceID );
		if( source == nullptr ||
			  (source->isKindOf( KINDOF_BRIDGE ) == FALSE &&
				 source->isKindOf( KINDOF_BRIDGE_TOWER ) == FALSE) )
		{

			for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
			{
				Object *tower;

				tower = TheGameLogic->findObjectByID( bridgeInterface->getTowerID( (BridgeTowerType)i ) );
				if( tower && tower != getObject() )
				{
					BodyModuleInterface *towerBody = tower->getBodyModule();
					tower->attemptHealing(healingPercentage * towerBody->getMaxHealth(), getObject());

				}

			}

			//
			// heal bridge object, but make sure it's done through the bridge interface
			// so that it doesn't automatically propagate that healing to the towers.
			//

			BodyModuleInterface* bridgeBody = bridge->getBodyModule();
			// if bridge is fully destroyed, do not heal it
			if (bridgeBody->getHealth() > 0.0f) {
				bridge->attemptHealing(healingPercentage * bridgeBody->getMaxHealth(), getObject());
			}

			// if healed to full, repair bridge if destroyed
			if (body->getHealth() >= body->getMaxHealth() && bridgeBody->getHealth() <= 0.0f) {
				bridge->attemptHealing(bridgeBody->getMaxHealth(), getObject());

				pushObjectsOnBridgeRestore(bridge);
				//TODO Heal UP effect, condition state?
				/*BridgeBehaviorInterface* bbi = BridgeBehavior::getBridgeBehaviorInterfaceFromObject(bridge);
				if (bbi != nullptr) {
					// tell the bridge to create scaffolding if necessary
					bbi->createScaffolding();
				}*/
			}

		}

	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::onBodyDamageStateChange( const DamageInfo* damageInfo,
																									 BodyDamageType oldState,
																									 BodyDamageType newState )
{

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::onDie( const DamageInfo *damageInfo )
{

	// kill the bridge object, this will kill all the towers
	Object *bridge = TheGameLogic->findObjectByID( getBridgeID() );
	if( bridge )
		bridge->kill();

}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** Given an object, return a bridge tower interface if that object has one */
// ------------------------------------------------------------------------------------------------
BridgeTowerBehaviorInterface *BridgeTowerBehavior::getBridgeTowerBehaviorInterfaceFromObject( Object *obj )
{

	// sanity
	if( obj == nullptr || obj->isKindOf( KINDOF_BRIDGE_TOWER ) == FALSE )
		return nullptr;

	BehaviorModule **bmi;
	BridgeTowerBehaviorInterface *bridgeTowerInterface = nullptr;
	for( bmi = obj->getBehaviorModules(); *bmi; ++bmi )
	{

		bridgeTowerInterface = (*bmi)->getBridgeTowerBehaviorInterface();
		if( bridgeTowerInterface )
			return bridgeTowerInterface;

	}

	// interface not found
	return nullptr;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::crc( Xfer *xfer )
{

	// extend base class
	BehaviorModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	BehaviorModule::xfer( xfer );

	// xfer bridge object ID
	xfer->xferObjectID( &m_bridgeID );

	// xfer tower type
	xfer->xferUser( &m_type, sizeof( BridgeTowerType ) );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBehavior::loadPostProcess( void )
{

	// extend base class
	BehaviorModule::loadPostProcess();

}
