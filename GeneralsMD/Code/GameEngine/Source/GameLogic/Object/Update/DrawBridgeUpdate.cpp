
// FILE: DrawBridgeUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   Update module to handle state change of draw bridges
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_MAXHEALTHCHANGETYPE_NAMES						// for TheMaxHealthChangeTypeNames[]

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/BitFlagsIO.h"
#include "Common/Radar.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"
#include "Common/Xfer.h"

#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameClient/Line2D.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/FXList.h"
#include "GameClient/ControlBar.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BridgeBehavior.h"
#include "GameLogic/Module/BridgeTowerBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/DrawBridgeUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeUpdateModuleData::DrawBridgeUpdateModuleData()
{
	m_openingDuration = 0U;
	m_closingDuration = 0U;
	m_openingPushForce = 0.0f;
	m_closingDamageTime = 0U;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DrawBridgeUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "OpeningDuration", INI::parseDurationUnsignedInt, nullptr, offsetof(DrawBridgeUpdateModuleData, m_openingDuration)},
		{ "ClosingDuration", INI::parseDurationUnsignedInt, nullptr, offsetof(DrawBridgeUpdateModuleData, m_closingDuration)},
		{ "OpeningPushForce", INI::parseAccelerationReal, nullptr, offsetof(DrawBridgeUpdateModuleData, m_openingPushForce)},
		{ "ClosingDamageTime", INI::parseDurationUnsignedInt, nullptr, offsetof(DrawBridgeUpdateModuleData, m_closingDamageTime)},
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DrawBridgeUpdate::DrawBridgeUpdate(Thing* thing, const ModuleData* moduleData) :
	UpdateModule(thing, moduleData)
{
	m_bridgeOpened = false;
	m_nextReadyFrame = 0U;
	m_openingFrame = 0U;
	m_closingDamageFrame = 0U;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeUpdate::~DrawBridgeUpdate(void)
{
}

// ------------------------------------------------------------------------------------------------
/** On delete */
// ------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::onDelete()
{
	// extend base class
	UpdateModule::onDelete();

}

//-------------------------------------------------------------------------------------------------
// Validate that we have the necessary data from the ini file.
//-------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::onObjectCreated()
{
}

//-------------------------------------------------------------------------------------------------
CommandOption DrawBridgeUpdate::getCommandOption() const
{
	return m_bridgeOpened ? OPTION_TWO : OPTION_ONE;
}

bool DrawBridgeUpdate::setDrawBridgeState(bool opened, const Object* fromTower)
{
	UnsignedInt now = TheGameLogic->getFrame();

	if (m_nextReadyFrame <= now && getObject()->getBodyModule()->getHealth() > 0.0f) {

		const DrawBridgeUpdateModuleData* data = getDrawBridgeUpdateModuleData();

		m_nextReadyFrame = now + (m_bridgeOpened ? data->m_closingDuration : data->m_openingDuration);

		if (m_bridgeOpened != opened) {
			m_bridgeOpened = opened;

			Object* obj = getObject();

			// bridge state was changed, we need to update
			Bridge* bridge = TheTerrainLogic->findBridgeAt(obj->getPosition());
			if (bridge != nullptr) {
				TheAI->pathfinder()->changeBridgeState(bridge->getLayer(), !m_bridgeOpened);
				bridge->setDrawBridgeStage(m_bridgeOpened);
			}

			if (m_bridgeOpened) {
				obj->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_CLOSING, MODELCONDITION_DOOR_1_OPENING);
				GeometryInfo openBridgeGeom(GeometryType::GEOMETRY_BOX, true, 0.0f, 0.0f, 0.0f);
				obj->setGeometryInfo(openBridgeGeom);
				m_openingFrame = TheGameLogic->getFrame();
				m_closingDamageFrame = 0U;
			}
			else {
				obj->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_OPENING, MODELCONDITION_DOOR_1_CLOSING);
				obj->setGeometryInfo(obj->getTemplate()->getTemplateGeometryInfo());
				m_openingFrame = 0U; // when rapid toggling is possible
				m_closingDamageFrame = TheGameLogic->getFrame() + data->m_closingDamageTime;
			}
		}
		return true;
	}
	return false;
}

void DrawBridgeUpdate::onBridgeDestroyed()
{
	m_openingFrame = 0U;
	m_closingDamageFrame = 0U;
	Object* obj = getObject();
	obj->clearModelConditionFlags(MODELCONDITION_DOOR_1_OPENING);
	obj->clearModelConditionFlags(MODELCONDITION_DOOR_1_CLOSING);
	m_bridgeOpened = true;
}

void DrawBridgeUpdate::onBridgeRepaired()
{
	m_openingFrame = 0U;
	m_closingDamageFrame = 0U;
	Object* obj = getObject();
	obj->clearModelConditionFlags(MODELCONDITION_DOOR_1_OPENING);
	obj->clearModelConditionFlags(MODELCONDITION_DOOR_1_CLOSING);
	m_bridgeOpened = false;
}

// This method launches units away by applying force in a 45 degree upwards angle to simulate an opening
// drawbridge. The side depends on position from center. The push is in bridge length direction.
void DrawBridgeUpdate::pushObjectsOnOpeningDrawbridge( void ) {
	// horizontal acceleration to apply; if no push force is configured, do nothing.
	// The force is scaled by each object's mass below so this is the acceleration every unit feels.
	Real pushAccel = getDrawBridgeUpdateModuleData()->m_openingPushForce;
	if( pushAccel <= 0.0f )
		return;

	Object* bridge = getObject();
	const Coord3D* bridgePos = bridge->getPosition();

	Bridge* terrainBridge = TheTerrainLogic->findBridgeAt(bridgePos);
	if (terrainBridge == nullptr)
		return;

	BridgeInfo bridgeInfo;
	terrainBridge->getBridgeInfo( &bridgeInfo );

	// polygon describing the bridge hole surface, used to test which objects are on it
	Coord3D bridgePolygon[ 4 ];
	bridgePolygon[ 0 ] = bridgeInfo.fromLeftHole;
	bridgePolygon[ 1 ] = bridgeInfo.fromRightHole;
	bridgePolygon[ 2 ] = bridgeInfo.toRightHole;
	bridgePolygon[ 3 ] = bridgeInfo.toLeftHole;

	// scan radius reaches from the bridge center out to a corner, covering the whole surface
	Coord2D v;
	v.x = bridgeInfo.toLeft.x - bridgePos->x;
	v.y = bridgeInfo.toLeft.y - bridgePos->y;
	Real radius = v.length();

	// length axis = direction along the bridge (from -> to). The two drawbridge halves hinge at the
	// center and rise toward their own ends, so each unit is flung toward the end of the half it is on.
	Coord2D lengthVector;
	lengthVector.x = bridgeInfo.to.x - bridgeInfo.from.x;
	lengthVector.y = bridgeInfo.to.y - bridgeInfo.from.y;
	Real length = lengthVector.length();
	if( length == 0.0f )
		return;		// degenerate bridge, no sensible length direction
	lengthVector.x /= length;
	lengthVector.y /= length;

	// scan all objects within the bridge radius
	ObjectIterator *iter = ThePartitionManager->iterateObjectsInRange( bridgePos, radius, FROM_CENTER_2D );
	MemoryPoolObjectHolder hold( iter );
	Object *other;
	for( other = iter->first(); other; other = iter->next() )
	{
		// never push the bridge itself or its towers
		if( other->isKindOf( KINDOF_BRIDGE ) || other->isKindOf( KINDOF_BRIDGE_TOWER ) )
			continue;

		// don't shove fixed structures or already-dead objects
		if( other->isKindOf( KINDOF_IMMOBILE ) || other->isEffectivelyDead() )
			continue;

		// leave airborne units alone, only things resting on the bridge get thrown
		if( other->isAirborneTarget() )
			continue;

		// only push objects within the bridge footprint
		if( PointInsideArea2D( other->getPosition(), bridgePolygon, 4 ) == FALSE )
			continue;

		// Only throw units standing on the drawbridge deck, not those on the ground/water below it.
		// We can't use getLayer() here: a unit crossing the bridge gets relayered to LAYER_GROUND the
		// moment the bridge layer is disabled (the toggle does this before we run), so it would be
		// skipped and fall through the opening hole. getBridgeHeight returns the deck height from the
		// bridge's flat corner geometry regardless of open/destroyed state, so compare z to that.
		const Real ON_DECK_Z_TOLERANCE = 20.0f;
		Real deckZ = terrainBridge->getBridgeHeight( other->getPosition(), nullptr );
		if( other->getPosition()->z < deckZ - ON_DECK_Z_TOLERANCE )
			continue;

		// only things with physics can be shoved
		PhysicsBehavior *physics = other->getPhysics();
		if( physics == nullptr )
			continue;

		// project the object's offset from the center onto the length axis; the sign tells us which
		// half of the bridge it is on, so we fling it toward that same end.
		Coord2D toObj;
		toObj.x = other->getPosition()->x - bridgePos->x;
		toObj.y = other->getPosition()->y - bridgePos->y;
		Real side = toObj.x * lengthVector.x + toObj.y * lengthVector.y;
		Real pushDir = ( side >= 0.0f ) ? 1.0f : -1.0f;

		// Move the unit off the bridge layer onto the ground layer before launching. Height and landing
		// are both measured against the unit's layer, so while a unit sits on LAYER_BRIDGE:
		//  1) Landing: the physics ground clamp snaps the unit to getLayerHeight(x, y, getLayer()). On
		//     LAYER_BRIDGE that is the deck, so the unit lands back on the (now open, impassable) bridge.
		//     On LAYER_GROUND it falls through the opening hole down to the ground/water below.
		//  2) Airborne tests: getLayerHeight(LAYER_BRIDGE) == deck height, so the unit never reads as
		//     above terrain. On LAYER_GROUND its height is measured against the (far lower) chasm/water
		//     floor, so it correctly counts as airborne while arcing.
		// A unit crossing the bridge was already relayered to LAYER_GROUND by the toggle; setLayer is a
		// no-op for it. Standing units stay on LAYER_BRIDGE, so relayer them here.
		other->setLayer( LAYER_GROUND );

		// Let the unit leave the ground and arc freely. allowToFall leaves the z alone instead of
		// snapping it back to the layer height. Physics clears it automatically when the unit lands.
		physics->setAllowToFall( true );

		// A unit that walked onto the bridge and stopped still carries OBJECT_STATUS_BRAKING: the
		// locomotor sets it while braking to a halt (so the unit stops exactly on its goal) and never
		// clears it once idle. While that status is set, PhysicsBehavior::update integrates ONLY the z
		// velocity and discards the horizontal (PhysicsUpdate.cpp ~L714), so our horizontal launch is
		// thrown away and the unit shoots straight up -- the vertical push has no such restriction. Moving
		// units are not braking, which is exactly why they launched correctly. Clear it so the full 3D
		// velocity integrates.
		other->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_BRAKING ) );

		// F = m*a, so scaling by mass makes the resulting acceleration uniform across all units.
		// Equal horizontal and vertical magnitude gives a 45 degree upward launch.
		Real strength = physics->getMass() * pushAccel;
		Coord3D force;
		force.x = lengthVector.x * pushDir * strength;
		force.y = lengthVector.y * pushDir * strength;
		force.z = strength;
		// A moving (motive) unit: applyForce would reproject the horizontal force onto its facing axis and
		// lose the along-bridge launch, so use applyMotiveForce, which clears the motive state for this one
		// application so the full 3D force applies. A standing (non-motive) unit: use plain applyForce so we
		// do not flag it motive (an idle unit flagged motive gets scrubVelocity2D(0) from the locomotor).
		if( physics->isMotive() )
			physics->applyMotiveForce( &force );
		else
			physics->applyForce( &force );
	}
}

// When a drawbridge is closed units or ships directly below it that are too high need to be killed
void DrawBridgeUpdate::destroyObjectsUnderClosingDrawbridge( void ) {
	Object* bridge = getObject();
	const Coord3D* bridgePos = bridge->getPosition();

	Bridge* terrainBridge = TheTerrainLogic->findBridgeAt(bridgePos);
	if (terrainBridge == nullptr)
		return;

	BridgeInfo bridgeInfo;
	terrainBridge->getBridgeInfo(&bridgeInfo);

	// polygon describing the bridge hole surface, used to test which objects are below it
	Coord3D bridgePolygon[4];
	bridgePolygon[0] = bridgeInfo.fromLeftHole;
	bridgePolygon[1] = bridgeInfo.fromRightHole;
	bridgePolygon[2] = bridgeInfo.toRightHole;
	bridgePolygon[3] = bridgeInfo.toLeftHole;

	// scan radius reaches from the bridge center out to a corner, covering the whole surface
	Coord2D v;
	v.x = bridgeInfo.toLeft.x - bridgePos->x;
	v.y = bridgeInfo.toLeft.y - bridgePos->y;
	Real radius = v.length();

	DamageInfo smashByBridgeDmg;
	smashByBridgeDmg.in.m_sourceID = getObject()->getID();
	smashByBridgeDmg.in.m_sourceTemplate = getObject()->getTemplate();

	const auto owner = getObject()->getControllingPlayer();
	if (owner != nullptr)
		smashByBridgeDmg.in.m_sourcePlayerMask = owner->getPlayerMask();

	smashByBridgeDmg.in.m_damageType = DAMAGE_CRUSH;
	smashByBridgeDmg.in.m_deathType = DEATH_CRUSHED;
	smashByBridgeDmg.in.m_amount = 999999.0f;
	smashByBridgeDmg.in.m_kill = true;

	// scan all objects within the bridge radius
	ObjectIterator* iter = ThePartitionManager->iterateObjectsInRange(bridgePos, radius, FROM_CENTER_2D);
	MemoryPoolObjectHolder hold(iter);
	Object* other;
	for (other = iter->first(); other; other = iter->next())
	{
		// never damage the bridge itself or its towers
		if (other->isKindOf(KINDOF_BRIDGE) || other->isKindOf(KINDOF_BRIDGE_TOWER) || other->isKindOf(KINDOF_UNATTACKABLE))
			continue;

		// unit must be below the bridge and not on top
		if (other->getLayer() != LAYER_GROUND)
			continue;

		// don't kill already-dead objects or airborne targets
		if (other->isEffectivelyDead() || other->isAirborneTarget())
			continue;

		// only destroy objects within the bridge hole footprint
		if (PointInsideArea2D(other->getPosition(), bridgePolygon, 4) == FALSE)
			continue;

		// check if unit height is larger than required bridge height.
		const auto * cell = TheAI->pathfinder()->getClippedCell(LAYER_GROUND, other->getPosition());
		if (cell->getBridgeLayer() == terrainBridge->getLayer() && cell->getBridgeHeight() < other->getRequiredBridgeHeight()) {
			other->attemptDamage(&smashByBridgeDmg);
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime DrawBridgeUpdate::update()
{
	if (m_openingFrame != 0U) {
		const DrawBridgeUpdateModuleData* data = getDrawBridgeUpdateModuleData();
		if (TheGameLogic->getFrame() < (m_openingFrame + data->m_openingDuration)) {
			pushObjectsOnOpeningDrawbridge();
		}
		else {
			m_openingFrame = 0U; //opening finished
		}
	}
	if (m_closingDamageFrame != 0U) {
		if (TheGameLogic->getFrame() >= (m_closingDamageFrame)) {
			destroyObjectsUnderClosingDrawbridge();
			m_closingDamageFrame = 0U;
		}
	}
	return UPDATE_SLEEP_NONE;
}


//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::crc(Xfer* xfer)
{

	// extend base class
	UpdateModule::crc(xfer);

}

//------------------------------------------------------------------------------------------------
// Xfer method
//	Version Info:
//	1: Initial version
//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	UpdateModule::xfer(xfer);

	xfer->xferBool(&m_bridgeOpened);

	xfer->xferUnsignedInt(&m_nextReadyFrame);

	xfer->xferUnsignedInt(&m_openingFrame);

	xfer->xferUnsignedInt(&m_closingDamageFrame);
}

//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::loadPostProcess(void)
{
	
	// extend base class
	UpdateModule::loadPostProcess();

}
