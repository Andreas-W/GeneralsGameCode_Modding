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

// FILE: OrbitalBeamUpdate.cpp ///////////////////////////////////////////////////////////////////
// Desc:   Update module for an orbital beam effect: a ring of charge beams that converge inward
//         and are then replaced by a single final beam, after which the object dies.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameAudio.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/LaserUpdate.h"
#include "GameLogic/Module/OrbitalBeamUpdate.h"

// How far above the ground a beam starts (in the sky). Matches ParticleUplinkCannonUpdate.
constexpr const Real ORBITAL_BEAM_Z_OFFSET = 3500.0f;

// Order must match enum InterpolationType.
static const char* TheInterpolationTypeNames[] =
{
	"LINEAR",
	"SMOOTH",
	"SMOOTHER",
	"EASE_IN",
	"EASE_OUT",
	"EASE_IN_CUBIC",
	"EASE_OUT_CUBIC",
	"SINE",
	"EXPO_IN",
	"EXPO_OUT",
	"CIRCULAR",
	"BACK",
	"ELASTIC",
	"BOUNCE",
	"STEP_HOLD",
	"PULSE",
	nullptr
};

//-------------------------------------------------------------------------------------------------
OrbitalBeamUpdateModuleData::OrbitalBeamUpdateModuleData()
{
	m_numChargeBeams = 0;
	m_chargeBeamRadius = 0.0f;
	m_chargeBeamHeight = ORBITAL_BEAM_Z_OFFSET;
	m_chargeBeamsStartCenter = FALSE;
	m_chargeBeamRotation = 0.0f;
	m_chargeBeamInterpolation = INTERP_LINEAR;
	m_initialDelayFrames = 0;
	m_delayBetweenChargeBeamsFrames = 0;
	m_delayChargeToAnimFrames = 0;
	m_beamAnimationDurationFrames = 0;
	m_delayAnimationToFinalFrames = 0;
	m_finalBeamDurationFrames = 0;
	m_randomizeChargeBeamOrder = FALSE;
	m_hitWaterSurface = FALSE;
	m_chargeBeamStartFX = nullptr;
	m_finalFX = nullptr;
	m_finalWeapon = nullptr;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void OrbitalBeamUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "NumChargeBeams",					INI::parseUnsignedShort,			nullptr, offsetof( OrbitalBeamUpdateModuleData, m_numChargeBeams ) },
		{ "ChargeBeamRadius",				INI::parseReal,									nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamRadius ) },
		{ "ChargeBeamHeight",				INI::parseReal,									nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamHeight ) },
		{ "ChargeBeamsStartCenter",			INI::parseBool,									nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamsStartCenter ) },
		{ "ChargeBeamRotation",				INI::parseAngleReal,						nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamRotation ) },
		{ "ChargeBeamInterpolation",		INI::parseIndexList,						TheInterpolationTypeNames, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamInterpolation ) },
		{ "InitialDelay",						INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_initialDelayFrames ) },
		{ "DelayBetweenChargeBeams",		INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_delayBetweenChargeBeamsFrames ) },
		{ "DelayChargeToAnim",				INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_delayChargeToAnimFrames ) },
		{ "BeamAnimationDuration",			INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_beamAnimationDurationFrames ) },
		{ "DelayAnimationToFinal",			INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_delayAnimationToFinalFrames ) },
		{ "FinalBeamDuration",				INI::parseDurationUnsignedInt,	nullptr, offsetof( OrbitalBeamUpdateModuleData, m_finalBeamDurationFrames ) },
		{ "ChargeBeamName",					INI::parseAsciiString,					nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamLaserNameName ) },
		{ "FinalBeamName",					INI::parseAsciiString,					nullptr, offsetof( OrbitalBeamUpdateModuleData, m_finalBeamLaserNameName ) },
		{ "RandomizeChargeBeamOrder",		INI::parseBool,									nullptr, offsetof( OrbitalBeamUpdateModuleData, m_randomizeChargeBeamOrder ) },
		{ "HitWaterSurface",				INI::parseBool,									nullptr, offsetof( OrbitalBeamUpdateModuleData, m_hitWaterSurface ) },
		{ "ChargeBeamStartFX",				INI::parseFXList,								nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamStartFX ) },
		{ "FinalFX",						INI::parseFXList,								nullptr, offsetof( OrbitalBeamUpdateModuleData, m_finalFX ) },
		{ "InitialSound",					INI::parseAudioEventRTS,				nullptr, offsetof( OrbitalBeamUpdateModuleData, m_initialSound ) },
		{ "ChargeSound",					INI::parseAudioEventRTS,				nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeSound ) },
		{ "FinalWeapon",					INI::parseWeaponTemplate,				nullptr, offsetof( OrbitalBeamUpdateModuleData, m_finalWeapon ) },
		{ "ChargeWeapon",					OrbitalBeamUpdateModuleData::parseAppendWeaponTemplate, nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeWeapons ) },
		{ "ChargeBeamParticleSystem",		INI::parseAsciiStringVectorAppend, nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamParticleSystemNames ) },
		{ "ChargeBeamParticleSystemWater",	INI::parseAsciiStringVectorAppend, nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamParticleSystemNamesWater ) },
		{ "ChargeBeamParticleSystemLand",	INI::parseAsciiStringVectorAppend, nullptr, offsetof( OrbitalBeamUpdateModuleData, m_chargeBeamParticleSystemNamesLand ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
/** Listable field: each occurrence appends one weapon template to the charge weapons vector. */
//-------------------------------------------------------------------------------------------------
/*static*/ void OrbitalBeamUpdateModuleData::parseAppendWeaponTemplate( INI* ini, void* instance, void* store, const void* /*userData*/ )
{
	std::vector<const WeaponTemplate*>* vec = (std::vector<const WeaponTemplate*>*)store;
	const WeaponTemplate* w = nullptr;
	INI::parseWeaponTemplate( ini, instance, &w, nullptr );
	if( w != nullptr )
		vec->push_back( w );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OrbitalBeamUpdate::OrbitalBeamUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{
	m_stage = STAGE_INITIAL_DELAY;
	m_stageStartFrame = 0;
	m_nextChargeSpawnFrame = 0;
	m_chargeBeamsSpawned = 0;
	m_started = FALSE;
	m_chargeFadeStarted = FALSE;
	m_nextFinalFireFrame = 0;
	m_finalBeamID = INVALID_DRAWABLE_ID;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
OrbitalBeamUpdate::~OrbitalBeamUpdate( void )
{
	// Clean up any beams still alive if we get destroyed prematurely.
	for( std::vector<DrawableID>::iterator it = m_chargeBeamIDs.begin(); it != m_chargeBeamIDs.end(); ++it )
	{
		killBeam( *it );
	}
	for( std::vector<DrawableID>::iterator it = m_dyingBeamIDs.begin(); it != m_dyingBeamIDs.end(); ++it )
	{
		killBeam( *it );
	}
	killBeam( m_finalBeamID );
	destroyChargeBeamSystems();
	stopPhaseSound( m_initialSoundEvent );
	stopPhaseSound( m_chargeSoundEvent );
}

//-------------------------------------------------------------------------------------------------
/** Beam endpoint z at (x,y): the water surface when HitWaterSurface is set and the point is over
	* water, otherwise the ground height. */
//-------------------------------------------------------------------------------------------------
Real OrbitalBeamUpdate::surfaceZAt( Real x, Real y ) const
{
	Real waterZ;
	if( getOrbitalBeamUpdateModuleData()->m_hitWaterSurface && TheTerrainLogic->isUnderwater( x, y, &waterZ ) )
		return waterZ;

	return TheTerrainLogic->getGroundHeight( x, y );
}

//-------------------------------------------------------------------------------------------------
/** Remap a 0..1 animation factor by the configured interpolation mode. */
//-------------------------------------------------------------------------------------------------
Real OrbitalBeamUpdate::interpolate( Real t ) const
{
	switch( getOrbitalBeamUpdateModuleData()->m_chargeBeamInterpolation )
	{
		case INTERP_SMOOTH:
			return t * t * (3.0f - 2.0f * t); // smoothstep

		case INTERP_SMOOTHER:
			return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); // smootherstep

		case INTERP_EASE_IN:
			return t * t;

		case INTERP_EASE_OUT:
			return t * (2.0f - t);

		case INTERP_EASE_IN_CUBIC:
			return t * t * t;

		case INTERP_EASE_OUT_CUBIC:
		{
			const Real u = 1.0f - t;
			return 1.0f - u * u * u;
		}

		case INTERP_SINE:
			return 0.5f - 0.5f * cos( PI * t );

		case INTERP_EXPO_IN:
			return t <= 0.0f ? 0.0f : (Real)pow( 2.0f, 10.0f * (t - 1.0f) );

		case INTERP_EXPO_OUT:
			return t >= 1.0f ? 1.0f : 1.0f - (Real)pow( 2.0f, -10.0f * t );

		case INTERP_CIRCULAR:
			return 1.0f - sqrt( 1.0f - t * t );

		case INTERP_BACK:
		{
			// easeOutBack: overshoots past the center then settles back to it.
			const Real c1 = 1.70158f;
			const Real c3 = c1 + 1.0f;
			const Real u = t - 1.0f;
			return 1.0f + c3 * u * u * u + c1 * u * u;
		}

		case INTERP_ELASTIC:
		{
			// easeOutElastic
			if( t <= 0.0f ) return 0.0f;
			if( t >= 1.0f ) return 1.0f;
			const Real c4 = (2.0f * PI) / 3.0f;
			return (Real)pow( 2.0f, -10.0f * t ) * sin( (t * 10.0f - 0.75f) * c4 ) + 1.0f;
		}

		case INTERP_BOUNCE:
		{
			// easeOutBounce
			const Real n1 = 7.5625f;
			const Real d1 = 2.75f;
			Real u = t;
			if( u < 1.0f / d1 )
				return n1 * u * u;
			else if( u < 2.0f / d1 )
			{
				u -= 1.5f / d1;
				return n1 * u * u + 0.75f;
			}
			else if( u < 2.5f / d1 )
			{
				u -= 2.25f / d1;
				return n1 * u * u + 0.9375f;
			}
			else
			{
				u -= 2.625f / d1;
				return n1 * u * u + 0.984375f;
			}
		}

		case INTERP_STEP_HOLD:
			// Hold at the ring, then ramp in over the last 20% of the animation.
			return t < 0.8f ? 0.0f : (t - 0.8f) / 0.2f;

		case INTERP_PULSE:
		{
			// Discrete stepped movement inward.
			const Real steps = 5.0f;
			return floor( t * steps ) / steps;
		}

		case INTERP_LINEAR:
		default:
			return t;
	}
}

//-------------------------------------------------------------------------------------------------
/** Beam origin: a fixed apex above the object center when ChargeBeamsStartCenter is set, otherwise
	* directly above the impact point. Height above the local surface is ChargeBeamHeight. */
//-------------------------------------------------------------------------------------------------
Coord3D OrbitalBeamUpdate::beamSkyPos( const Coord3D* groundPos, const Coord3D* center ) const
{
	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();

	Coord3D sky;
	if( data->m_chargeBeamsStartCenter )
	{
		sky.x = center->x;
		sky.y = center->y;
		sky.z = center->z + data->m_chargeBeamHeight;
	}
	else
	{
		sky = *groundPos;
		sky.z += data->m_chargeBeamHeight;
	}
	return sky;
}

//-------------------------------------------------------------------------------------------------
/** Spawn a beam drawable from startPos down to groundPos. */
//-------------------------------------------------------------------------------------------------
DrawableID OrbitalBeamUpdate::spawnBeam( const AsciiString& tmplName, const Coord3D* startPos, const Coord3D* groundPos )
{
	if( tmplName.isEmpty() )
		return INVALID_DRAWABLE_ID;

	const ThingTemplate *tmpl = TheThingFactory->findTemplate( tmplName );
	if( tmpl == nullptr )
		return INVALID_DRAWABLE_ID;

	Drawable *beam = TheThingFactory->newDrawable( tmpl );
	if( beam == nullptr )
		return INVALID_DRAWABLE_ID;

	static const NameKeyType nameKeyClientUpdate = NAMEKEY( "LaserUpdate" );
	LaserUpdate *update = (LaserUpdate*)beam->findClientUpdateModule( nameKeyClientUpdate );
	if( update )
	{
		update->initLaser( nullptr, nullptr, startPos, groundPos, AsciiString::TheEmptyString );
	}

	return beam->getID();
}

//-------------------------------------------------------------------------------------------------
/** Destroy a beam drawable and invalidate its id. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::killBeam( DrawableID& id )
{
	if( id != INVALID_DRAWABLE_ID )
	{
		Drawable *beam = TheGameClient->findDrawableByID( id );
		if( beam )
			TheGameClient->destroyDrawable( beam );
		id = INVALID_DRAWABLE_ID;
	}
}

//-------------------------------------------------------------------------------------------------
/** Fade the beam's alpha out then destroy it once the fade elapses. If no fade duration is
	* configured, destroy it immediately. Either way the passed id is invalidated. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::beginBeamFadeOut( DrawableID& id )
{
	if( id == INVALID_DRAWABLE_ID )
		return;

	Drawable *beam = TheGameClient->findDrawableByID( id );
	if( beam )
	{
		static const NameKeyType nameKeyClientUpdate = NAMEKEY( "LaserUpdate" );
		LaserUpdate *update = (LaserUpdate*)beam->findClientUpdateModule( nameKeyClientUpdate );
		if( update )
		{
			// Use the fade-out duration configured on the beam's own laser template.
			const UnsignedInt fadeFrames = update->getFadeOutFrames();
			if( fadeFrames > 0 )
			{
				update->startFadeOut( fadeFrames );
				m_dyingBeamIDs.push_back( id );
				m_dyingBeamDeathFrame.push_back( TheGameLogic->getFrame() + fadeFrames );
				id = INVALID_DRAWABLE_ID;
				return;
			}
		}
	}

	// No fade configured (or no laser module) -- destroy right away.
	killBeam( id );
}

//-------------------------------------------------------------------------------------------------
/** Destroy any dying beams whose fade-out has elapsed. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::updateDyingBeams( UnsignedInt now )
{
	for( size_t i = 0; i < m_dyingBeamIDs.size(); )
	{
		if( now >= m_dyingBeamDeathFrame[ i ] )
		{
			killBeam( m_dyingBeamIDs[ i ] );
			// swap-erase to keep it cheap; both lists stay in lockstep
			m_dyingBeamIDs[ i ] = m_dyingBeamIDs.back();
			m_dyingBeamDeathFrame[ i ] = m_dyingBeamDeathFrame.back();
			m_dyingBeamIDs.pop_back();
			m_dyingBeamDeathFrame.pop_back();
		}
		else
		{
			++i;
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Resolve each name to a particle system template and create it at pos. One id is appended per
	* name (INVALID_PARTICLE_SYSTEM_ID for names that fail to resolve or create) so callers relying on
	* a fixed per-beam stride stay aligned. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::createSystemsFromNames( const std::vector<AsciiString>& names, const Coord3D* pos, std::vector<ParticleSystemID>& out )
{
	for( std::vector<AsciiString>::const_iterator it = names.begin(); it != names.end(); ++it )
	{
		ParticleSystemID id = INVALID_PARTICLE_SYSTEM_ID;

		const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate( *it );
		if( tmpl )
		{
			ParticleSystem *system = TheParticleSystemManager->createParticleSystem( tmpl );
			if( system )
			{
				system->setPosition( pos );
				id = system->getSystemID();
			}
		}

		out.push_back( id );
	}
}

//-------------------------------------------------------------------------------------------------
/** Create the water- or land-specific systems at pos, appending their ids to out. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::createTerrainSystems( Bool overWater, const Coord3D* pos, std::vector<ParticleSystemID>& out )
{
	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();
	const std::vector<AsciiString>& names =
		overWater ? data->m_chargeBeamParticleSystemNamesWater : data->m_chargeBeamParticleSystemNamesLand;

	createSystemsFromNames( names, pos, out );
}

//-------------------------------------------------------------------------------------------------
/** Destroy each valid system id in the list, then clear it. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::destroyParticleSystemList( std::vector<ParticleSystemID>& ids )
{
	for( std::vector<ParticleSystemID>::iterator it = ids.begin(); it != ids.end(); ++it )
	{
		if( *it != INVALID_PARTICLE_SYSTEM_ID )
			TheParticleSystemManager->destroyParticleSystemByID( *it );
	}
	ids.clear();
}

//-------------------------------------------------------------------------------------------------
/** Start a looping phase sound attached to this object (no-op if the event is unset). */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::startPhaseSound( AudioEventRTS& playing, const AudioEventRTS& tmpl )
{
	if( tmpl.getEventName().isEmpty() )
		return;

	playing = tmpl;
	playing.setObjectID( getObject()->getID() );
	playing.setPlayingHandle( TheAudio->addAudioEvent( &playing ) );
}

//-------------------------------------------------------------------------------------------------
/** Stop a phase sound. Safe even if it was never started. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::stopPhaseSound( AudioEventRTS& playing )
{
	TheAudio->removeAudioEvent( playing.getPlayingHandle() );
}

//-------------------------------------------------------------------------------------------------
/** Fire the final weapon at pos, then schedule the next shot from the weapon's own fire rate. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::fireFinalWeapon( const Coord3D* pos )
{
	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();
	if( !data->m_finalWeapon )
		return;

	TheWeaponStore->createAndFireTempWeapon( data->m_finalWeapon, getObject(), pos );

	WeaponBonus bonus; // defaults to 1.0 for all fields (rate-of-fire divisor = 1)
	Int delay = data->m_finalWeapon->getDelayBetweenShots( bonus );
	if( delay < 1 )
		delay = 1; // never stall

	m_nextFinalFireFrame = TheGameLogic->getFrame() + delay;
}

//-------------------------------------------------------------------------------------------------
/** Fire each charge weapon whose shot is due at pos, then reschedule it from its own fire rate. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::updateChargeWeapons( const Coord3D* pos, UnsignedInt now )
{
	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();

	for( size_t i = 0; i < data->m_chargeWeapons.size() && i < m_nextChargeWeaponFire.size(); ++i )
	{
		if( now < m_nextChargeWeaponFire[ i ] )
			continue;

		TheWeaponStore->createAndFireTempWeapon( data->m_chargeWeapons[ i ], getObject(), pos );

		WeaponBonus bonus; // defaults to 1.0 for all fields (rate-of-fire divisor = 1)
		Int delay = data->m_chargeWeapons[ i ]->getDelayBetweenShots( bonus );
		if( delay < 1 )
			delay = 1; // never stall

		m_nextChargeWeaponFire[ i ] = now + delay;
	}
}

//-------------------------------------------------------------------------------------------------
/** Create this beam's configured particle systems at its impact point. The generic systems keep an
	* exact per-beam stride (one entry per template even on failure); the terrain systems are picked
	* by the impact point's current terrain. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::spawnChargeBeamSystems( const Coord3D* groundPos )
{
	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();

	// Generic systems (kept at an exact per-beam stride = name count).
	createSystemsFromNames( data->m_chargeBeamParticleSystemNames, groundPos, m_chargeBeamSystemIDs );

	// Terrain-specific systems for this beam's current terrain.
	const Bool overWater = TheTerrainLogic->isUnderwater( groundPos->x, groundPos->y );
	m_chargeBeamTerrainSystemIDs.push_back( std::vector<ParticleSystemID>() );
	createTerrainSystems( overWater, groundPos, m_chargeBeamTerrainSystemIDs.back() );
	m_chargeBeamOverWater.push_back( overWater );
}

//-------------------------------------------------------------------------------------------------
/** Move a beam's particle systems to its current impact point, swapping the terrain-specific set
	* when the impact point crosses the shoreline. */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::moveChargeBeamSystems( size_t beamIndex, const Coord3D* groundPos )
{
	// Generic systems (fixed stride).
	const size_t numSystems = getOrbitalBeamUpdateModuleData()->m_chargeBeamParticleSystemNames.size();
	const size_t base = beamIndex * numSystems;
	for( size_t k = 0; k < numSystems; ++k )
	{
		if( base + k >= m_chargeBeamSystemIDs.size() )
			break;

		ParticleSystem *system = TheParticleSystemManager->findParticleSystem( m_chargeBeamSystemIDs[ base + k ] );
		if( system )
			system->setPosition( groundPos );
	}

	if( beamIndex >= m_chargeBeamTerrainSystemIDs.size() )
		return;

	// Swap the terrain-specific set on a shoreline crossing.
	const Bool overWater = TheTerrainLogic->isUnderwater( groundPos->x, groundPos->y );
	std::vector<ParticleSystemID>& terrainSystems = m_chargeBeamTerrainSystemIDs[ beamIndex ];
	if( overWater != m_chargeBeamOverWater[ beamIndex ] )
	{
		destroyParticleSystemList( terrainSystems );
		createTerrainSystems( overWater, groundPos, terrainSystems );
		m_chargeBeamOverWater[ beamIndex ] = overWater;
	}

	// Move the terrain systems.
	for( std::vector<ParticleSystemID>::iterator it = terrainSystems.begin(); it != terrainSystems.end(); ++it )
	{
		ParticleSystem *system = TheParticleSystemManager->findParticleSystem( *it );
		if( system )
			system->setPosition( groundPos );
	}
}

//-------------------------------------------------------------------------------------------------
/** Destroy all charge beam particle systems (generic + terrain). */
//-------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::destroyChargeBeamSystems()
{
	destroyParticleSystemList( m_chargeBeamSystemIDs );

	for( std::vector< std::vector<ParticleSystemID> >::iterator it = m_chargeBeamTerrainSystemIDs.begin();
			it != m_chargeBeamTerrainSystemIDs.end(); ++it )
	{
		destroyParticleSystemList( *it );
	}
	m_chargeBeamTerrainSystemIDs.clear();
	m_chargeBeamOverWater.clear();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime OrbitalBeamUpdate::update( void )
{
	// Orbital beam update has multiple stages
	// 0: initial delay, waiting
	// 1: spawning charge beams. Charge beams are spawned in a circular radius around the center (Object pos)
	//    starting at 0 degree, each beam is offset by the same angle
	// 2: Animation phase, beams move inward and rotate while doing so (rotaion is future TODO, not yet to implement)
	// 3: Final phase, charge beams dissappear, final beam is spawned
	// 4: object dies

	const OrbitalBeamUpdateModuleData *data = getOrbitalBeamUpdateModuleData();
	const UnsignedInt now = TheGameLogic->getFrame();

	// Seed the timing on the very first update.
	if( !m_started )
	{
		m_started = TRUE;
		m_stageStartFrame = now;

		// InitialSound plays through the initial delay and charge beam spawning.
		startPhaseSound( m_initialSoundEvent, data->m_initialSound );
	}

	// Destroy any beams whose fade-out has finished, regardless of stage.
	updateDyingBeams( now );

	// The center of the effect is the position of the object we are attached to.
	Coord3D center = *getObject()->getPosition();
	center.z = surfaceZAt( center.x, center.y );

	switch( m_stage )
	{
		//---------------------------------------------------------------------------------------------
		case STAGE_INITIAL_DELAY:
		{
			if( now - m_stageStartFrame >= data->m_initialDelayFrames )
			{
				m_stage = STAGE_SPAWN_CHARGE;
				m_stageStartFrame = now;
				m_nextChargeSpawnFrame = now;

				// Build the ring-slot order for spawning. Positions are identical either way;
				// only the order in which slots are filled changes.
				m_spawnOrder.resize( data->m_numChargeBeams );
				for( UnsignedShort i = 0; i < data->m_numChargeBeams; ++i )
					m_spawnOrder[ i ] = i;

				if( data->m_randomizeChargeBeamOrder )
				{
					// Fisher-Yates shuffle using the synced logic RNG (deterministic across clients).
					for( Int i = (Int)data->m_numChargeBeams - 1; i > 0; --i )
					{
						Int j = GameLogicRandomValue( 0, i );
						std::swap( m_spawnOrder[ i ], m_spawnOrder[ j ] );
					}
				}
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case STAGE_SPAWN_CHARGE:
		{
			// Spawn charge beams one at a time, paced by m_delayBetweenChargeBeamsFrames.
			while( m_chargeBeamsSpawned < data->m_numChargeBeams && now >= m_nextChargeSpawnFrame )
			{
				const UnsignedShort slot = m_spawnOrder[ m_chargeBeamsSpawned ];
				const Real angle = (Real)slot * TWO_PI / (Real)data->m_numChargeBeams;

				Coord3D groundPos;
				groundPos.x = center.x + data->m_chargeBeamRadius * cos( angle );
				groundPos.y = center.y + data->m_chargeBeamRadius * sin( angle );
				groundPos.z = surfaceZAt( groundPos.x, groundPos.y );

				Coord3D sky = beamSkyPos( &groundPos, &center );
				m_chargeBeamGroundPos.push_back( groundPos );
				m_chargeBeamIDs.push_back( spawnBeam( data->m_chargeBeamLaserNameName, &sky, &groundPos ) );
				spawnChargeBeamSystems( &groundPos );
				FXList::doFXPos( data->m_chargeBeamStartFX, &groundPos );

				++m_chargeBeamsSpawned;
				m_nextChargeSpawnFrame += data->m_delayBetweenChargeBeamsFrames;
			}

			// Once all are spawned, wait the charge->anim delay before animating.
			if( m_chargeBeamsSpawned >= data->m_numChargeBeams &&
					now - m_stageStartFrame >= data->m_delayChargeToAnimFrames )
			{
				m_stage = STAGE_ANIMATE;
				m_stageStartFrame = now;

				// InitialSound ends, ChargeSound plays during the animation.
				stopPhaseSound( m_initialSoundEvent );
				startPhaseSound( m_chargeSoundEvent, data->m_chargeSound );

				// Arm the charge weapons so they all fire on the first animate frame.
				m_nextChargeWeaponFire.assign( data->m_chargeWeapons.size(), now );
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case STAGE_ANIMATE:
		{
			// Fire the charge weapons at the center throughout the animation stage.
			updateChargeWeapons( &center, now );

			// Translate each charge beam's ground endpoint from its ring position toward the center.
			Real factor = 1.0f;
			if( data->m_beamAnimationDurationFrames > 0 )
			{
				factor = (Real)(now - m_stageStartFrame) / (Real)data->m_beamAnimationDurationFrames;
				if( factor > 1.0f )
					factor = 1.0f;
			}

			if( !m_chargeFadeStarted )
			{
				// Eased progress used for the beam positions (timing/completion stay on raw factor).
				const Real t = interpolate( factor );

				// Still animating: translate each charge beam's ground endpoint inward.
				for( size_t i = 0; i < m_chargeBeamIDs.size(); ++i )
				{
					const Coord3D& start = m_chargeBeamGroundPos[ i ];
					Coord3D groundPos;
					if( data->m_chargeBeamRotation != 0.0f )
					{
						// Spiral inward: radius shrinks to 0 while the angle sweeps the total rotation.
						const Real dx = start.x - center.x;
						const Real dy = start.y - center.y;
						const Real baseAngle = atan2( dy, dx );
						const Real baseRadius = sqrt( dx * dx + dy * dy );
						const Real curRadius = baseRadius * (1.0f - t);
						const Real curAngle = baseAngle + t * data->m_chargeBeamRotation;
						groundPos.x = center.x + curRadius * cos( curAngle );
						groundPos.y = center.y + curRadius * sin( curAngle );
					}
					else
					{
						// Straight-in translation.
						groundPos.x = start.x + (center.x - start.x) * t;
						groundPos.y = start.y + (center.y - start.y) * t;
					}
					groundPos.z = surfaceZAt( groundPos.x, groundPos.y );

					// Keep this beam's particle systems on the moving impact point.
					moveChargeBeamSystems( i, &groundPos );

					Drawable *beam = TheGameClient->findDrawableByID( m_chargeBeamIDs[ i ] );
					if( beam == nullptr )
						continue;

					static const NameKeyType nameKeyClientUpdate = NAMEKEY( "LaserUpdate" );
					LaserUpdate *update = (LaserUpdate*)beam->findClientUpdateModule( nameKeyClientUpdate );
					if( update == nullptr )
						continue;

					Coord3D skyPos = beamSkyPos( &groundPos, &center );

					// Move the beam without re-initializing it, so fade-in/widen/fade-out keep running.
					// Center-start beams keep a fixed apex; vertical beams track the moving impact.
					update->updateContinuousLaser( nullptr, nullptr, &skyPos, &groundPos );
				}

				// Animation just finished: begin fading the charge beams now, so they fade out
				// during the anim->final wait rather than at the transition. The impact particle
				// systems die together with the start of the fade.
				if( factor >= 1.0f )
				{
					destroyChargeBeamSystems();

					for( std::vector<DrawableID>::iterator it = m_chargeBeamIDs.begin(); it != m_chargeBeamIDs.end(); ++it )
					{
						beginBeamFadeOut( *it );
					}
					m_chargeBeamIDs.clear();
					m_chargeBeamGroundPos.clear();
					m_chargeFadeStarted = TRUE;

					// ChargeSound ends when the animation completes.
					stopPhaseSound( m_chargeSoundEvent );
				}
			}

			// After the anim->final delay, spawn the final beam and advance.
			if( m_chargeFadeStarted &&
					now - m_stageStartFrame >= data->m_beamAnimationDurationFrames + data->m_delayAnimationToFinalFrames )
			{
				m_stage = STAGE_FINAL;
				m_stageStartFrame = now;

				Coord3D finalSky = beamSkyPos( &center, &center );
				m_finalBeamID = spawnBeam( data->m_finalBeamLaserNameName, &finalSky, &center );

				// One-shot FX at the center as the final beam appears.
				FXList::doFXPos( data->m_finalFX, &center );

				// Fire the final weapon at the center as the final beam appears; schedules repeats.
				fireFinalWeapon( &center );
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case STAGE_FINAL:
		{
			// Keep firing the final weapon at its own fire rate while the final beam is alive.
			if( data->m_finalWeapon && now >= m_nextFinalFireFrame )
				fireFinalWeapon( &center );

			if( now - m_stageStartFrame >= data->m_finalBeamDurationFrames )
			{
				// Fade the final beam out (or kill it now), then move to teardown.
				beginBeamFadeOut( m_finalBeamID );
				m_stage = STAGE_DONE;
				m_stageStartFrame = now;
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case STAGE_DONE:
		{
			// Wait for any fading beams to finish before we destroy ourselves, otherwise the
			// beams (independent drawables) would be orphaned mid-fade.
			if( m_dyingBeamIDs.empty() )
			{
				TheGameLogic->destroyObject( getObject() );
				return UPDATE_SLEEP_FOREVER;
			}
			break;
		}
	}

	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpdateModule::xfer( xfer );

	// stage state
	xfer->xferUnsignedInt( &m_stage );
	xfer->xferUnsignedInt( &m_stageStartFrame );
	xfer->xferUnsignedInt( &m_nextChargeSpawnFrame );
	xfer->xferUnsignedShort( &m_chargeBeamsSpawned );
	xfer->xferBool( &m_started );
	xfer->xferBool( &m_chargeFadeStarted );
	xfer->xferUnsignedInt( &m_nextFinalFireFrame );
	xfer->xferDrawableID( &m_finalBeamID );

	// charge beam drawables
	UnsignedShort beamCount = (UnsignedShort)m_chargeBeamIDs.size();
	xfer->xferUnsignedShort( &beamCount );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_chargeBeamIDs.resize( beamCount, INVALID_DRAWABLE_ID );
		m_chargeBeamGroundPos.resize( beamCount );
	}
	for( UnsignedShort i = 0; i < beamCount; ++i )
	{
		xfer->xferDrawableID( &m_chargeBeamIDs[ i ] );
		xfer->xferCoord3D( &m_chargeBeamGroundPos[ i ] );
	}

	// per charge weapon next-fire schedule
	UnsignedShort chargeWeaponCount = (UnsignedShort)m_nextChargeWeaponFire.size();
	xfer->xferUnsignedShort( &chargeWeaponCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_nextChargeWeaponFire.resize( chargeWeaponCount, 0 );
	for( UnsignedShort i = 0; i < chargeWeaponCount; ++i )
		xfer->xferUnsignedInt( &m_nextChargeWeaponFire[ i ] );

	// spawn order (must be xfered so randomized order stays in sync)
	UnsignedShort orderCount = (UnsignedShort)m_spawnOrder.size();
	xfer->xferUnsignedShort( &orderCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_spawnOrder.resize( orderCount );
	for( UnsignedShort i = 0; i < orderCount; ++i )
		xfer->xferUnsignedShort( &m_spawnOrder[ i ] );

	// beams currently fading out, awaiting destruction
	UnsignedShort dyingCount = (UnsignedShort)m_dyingBeamIDs.size();
	xfer->xferUnsignedShort( &dyingCount );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_dyingBeamIDs.resize( dyingCount, INVALID_DRAWABLE_ID );
		m_dyingBeamDeathFrame.resize( dyingCount );
	}
	for( UnsignedShort i = 0; i < dyingCount; ++i )
	{
		xfer->xferDrawableID( &m_dyingBeamIDs[ i ] );
		xfer->xferUnsignedInt( &m_dyingBeamDeathFrame[ i ] );
	}

	// charge beam generic particle systems (flat, N per beam)
	UnsignedShort systemCount = (UnsignedShort)m_chargeBeamSystemIDs.size();
	xfer->xferUnsignedShort( &systemCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_chargeBeamSystemIDs.resize( systemCount, INVALID_PARTICLE_SYSTEM_ID );
	for( UnsignedShort i = 0; i < systemCount; ++i )
		xfer->xferUser( &m_chargeBeamSystemIDs[ i ], sizeof( ParticleSystemID ) );

	// charge beam terrain-specific particle systems (per beam, variable size) + terrain state
	UnsignedShort terrainBeamCount = (UnsignedShort)m_chargeBeamTerrainSystemIDs.size();
	xfer->xferUnsignedShort( &terrainBeamCount );
	if( xfer->getXferMode() == XFER_LOAD )
	{
		m_chargeBeamTerrainSystemIDs.resize( terrainBeamCount );
		m_chargeBeamOverWater.resize( terrainBeamCount, FALSE );
	}
	for( UnsignedShort i = 0; i < terrainBeamCount; ++i )
	{
		// std::vector<Bool> is a proxy specialization, so xfer through a temp.
		Bool overWater = m_chargeBeamOverWater[ i ];
		xfer->xferBool( &overWater );
		m_chargeBeamOverWater[ i ] = overWater;

		UnsignedShort innerCount = (UnsignedShort)m_chargeBeamTerrainSystemIDs[ i ].size();
		xfer->xferUnsignedShort( &innerCount );
		if( xfer->getXferMode() == XFER_LOAD )
			m_chargeBeamTerrainSystemIDs[ i ].resize( innerCount, INVALID_PARTICLE_SYSTEM_ID );
		for( UnsignedShort k = 0; k < innerCount; ++k )
			xfer->xferUser( &m_chargeBeamTerrainSystemIDs[ i ][ k ], sizeof( ParticleSystemID ) );
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void OrbitalBeamUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}
