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

// FILE: OrbitalBeamUpdate.h /////////////////////////////////////////////////////////////////////
// Desc:   Update module stub for an orbital beam effect.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "Common/AudioEventRTS.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class FXList;
class WeaponTemplate;
enum ParticleSystemID CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
enum InterpolationType CPP_11(: Int)
{
	INTERP_LINEAR = 0,		///< constant speed
	INTERP_SMOOTH,				///< smoothstep: ease in/out
	INTERP_SMOOTHER,			///< smootherstep: silkier ease in/out
	INTERP_EASE_IN,				///< slow start, fast finish (quadratic)
	INTERP_EASE_OUT,			///< fast start, slow finish (quadratic)
	INTERP_EASE_IN_CUBIC,	///< stronger accelerating start
	INTERP_EASE_OUT_CUBIC,///< stronger decelerating finish
	INTERP_SINE,					///< cosine ease in/out
	INTERP_EXPO_IN,				///< near-still then whip in
	INTERP_EXPO_OUT,			///< instant burst then long settle
	INTERP_CIRCULAR,			///< arc-like, hard finish
	INTERP_BACK,					///< overshoots past the center then settles
	INTERP_ELASTIC,				///< springy oscillation before settling
	INTERP_BOUNCE,				///< bounces before settling
	INTERP_STEP_HOLD,			///< holds at the ring, then ramps in near the end
	INTERP_PULSE,					///< discrete stepped movement inward
};

//-------------------------------------------------------------------------------------------------
class OrbitalBeamUpdateModuleData : public UpdateModuleData
{
public:

	OrbitalBeamUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
	static void parseAppendWeaponTemplate( INI* ini, void* instance, void* store, const void* userData ); ///< appends one weapon to the listable ChargeWeapon field

	UnsignedShort m_numChargeBeams; // < how many charge beams are spawned
	Real          m_chargeBeamRadius; // < distance from center the charge beams are spawned
	Real          m_chargeBeamHeight; // < height above the surface the charge/final beams originate from
	Bool          m_chargeBeamsStartCenter; // < if set, charge beams originate from a fixed apex above the object center instead of directly above each impact
	Real          m_chargeBeamRotation; // < total angle (radians) each beam sweeps around center while converging; 0 = no rotation
	InterpolationType m_chargeBeamInterpolation; // < easing mode for the charge beam convergence animation
	UnsignedInt		m_initialDelayFrames; // < time until everything starts
	UnsignedInt		m_delayBetweenChargeBeamsFrames; // < delay between initial charge beams are spawned
	UnsignedInt		m_delayChargeToAnimFrames;  // < delay between all charge beams are spawned and they start to animate
	UnsignedInt   m_beamAnimationDurationFrames; // < duration how long the animation phase is going
	UnsignedInt   m_delayAnimationToFinalFrames; // < delay between finished animation and final beam
	UnsignedInt   m_finalBeamDurationFrames; // < How long the final beam is active

	AsciiString			m_chargeBeamLaserNameName;
	AsciiString			m_finalBeamLaserNameName;

	Bool					m_randomizeChargeBeamOrder; // < if set, charge beams spawn in randomized order (same positions)
	Bool					m_hitWaterSurface; // < if set, beam endpoints rest on the water surface instead of the floor when over water

	const FXList*	m_chargeBeamStartFX; // < optional FXList played at each charge beam's impact point when it spawns
	const FXList*	m_finalFX; // < optional FXList played once at the object center when the final beam appears
	const WeaponTemplate*	m_finalWeapon; // < optional weapon fired at the object center when the final beam appears
	std::vector<const WeaponTemplate*> m_chargeWeapons; // < weapons fired at the object center during the charge beam animation stage

	AudioEventRTS	m_initialSound; // < plays during the initial delay and charge beam spawning
	AudioEventRTS	m_chargeSound; // < plays during the charge beam animation

	std::vector<AsciiString> m_chargeBeamParticleSystemNames; // < particle systems on each charge beam's impact point (both terrains)
	std::vector<AsciiString> m_chargeBeamParticleSystemNamesWater; // < additional systems only while the impact point is over water
	std::vector<AsciiString> m_chargeBeamParticleSystemNamesLand; // < additional systems only while the impact point is over land

private:

};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class OrbitalBeamUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( OrbitalBeamUpdate, "OrbitalBeamUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( OrbitalBeamUpdate, OrbitalBeamUpdateModuleData )

public:

	OrbitalBeamUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update();

protected:

	enum
	{
		STAGE_INITIAL_DELAY = 0,	///< waiting before anything happens
		STAGE_SPAWN_CHARGE,				///< spawning the ring of charge beams
		STAGE_ANIMATE,						///< charge beams translate inward toward the center
		STAGE_FINAL,							///< charge beams gone, single final beam active
		STAGE_DONE								///< final beam gone, object destroyed
	};

	Real surfaceZAt( Real x, Real y ) const;																			///< beam endpoint z at (x,y): water surface when HitWaterSurface and over water, else ground height
	Real interpolate( Real t ) const;																							///< remap a 0..1 animation factor by the configured interpolation mode
	Coord3D beamSkyPos( const Coord3D* groundPos, const Coord3D* center ) const;		///< beam origin: fixed apex above center (ChargeBeamsStartCenter) or directly above the impact
	DrawableID spawnBeam( const AsciiString& tmplName, const Coord3D* startPos, const Coord3D* groundPos );	///< spawn a start->ground laser drawable, returns its id
	void killBeam( DrawableID& id );																							///< destroy a beam drawable immediately and invalidate the id
	void beginBeamFadeOut( DrawableID& id );																			///< fade the beam out then destroy it later (or destroy now if no fade), invalidates the id
	void updateDyingBeams( UnsignedInt now );																			///< destroy beams whose fade-out has elapsed

	void spawnChargeBeamSystems( const Coord3D* groundPos );												///< create this beam's particle systems at its impact point
	void moveChargeBeamSystems( size_t beamIndex, const Coord3D* groundPos );			///< move a beam's particle systems to its impact point, swapping terrain sets on shoreline crossing
	void destroyChargeBeamSystems();																							///< destroy all charge beam particle systems
	void createTerrainSystems( Bool overWater, const Coord3D* pos, std::vector<ParticleSystemID>& out );	///< create the water- or land-specific systems at pos
	void createSystemsFromNames( const std::vector<AsciiString>& names, const Coord3D* pos, std::vector<ParticleSystemID>& out ); ///< resolve names and create particle systems at pos (INVALID id kept for unresolved names)
	void destroyParticleSystemList( std::vector<ParticleSystemID>& ids );					///< destroy each valid system id then clear the list

	void startPhaseSound( AudioEventRTS& playing, const AudioEventRTS& tmpl );			///< start a looping phase sound attached to this object
	void stopPhaseSound( AudioEventRTS& playing );																///< stop a phase sound (safe even if never started)
	void fireFinalWeapon( const Coord3D* pos );																		///< fire the final weapon at pos and schedule the next shot from its fire rate
	void updateChargeWeapons( const Coord3D* pos, UnsignedInt now );								///< fire each due charge weapon at pos, rescheduling from its own fire rate

	UnsignedInt							m_stage;								///< current stage of the effect
	UnsignedInt							m_stageStartFrame;			///< frame the current stage was entered
	UnsignedInt							m_nextChargeSpawnFrame;	///< next frame a charge beam may be spawned
	UnsignedShort						m_chargeBeamsSpawned;		///< how many charge beams have been spawned so far
	Bool										m_started;							///< has the effect been seeded on the first update
	Bool										m_chargeFadeStarted;		///< charge beams have begun fading out (at animation end)
	UnsignedInt							m_nextFinalFireFrame;		///< frame the next final-weapon shot is due (STAGE_FINAL)
	DrawableID							m_finalBeamID;					///< the final beam drawable

	std::vector<UnsignedInt>	m_nextChargeWeaponFire;	///< per charge weapon, frame its next shot is due (STAGE_ANIMATE)

	std::vector<DrawableID>	m_chargeBeamIDs;				///< the spawned charge beam drawables
	std::vector<Coord3D>		m_chargeBeamGroundPos;	///< each charge beam's original ground position (ring), used to lerp inward
	std::vector<UnsignedShort>	m_spawnOrder;				///< ring slot filled at each spawn step (shuffled when randomized)
	std::vector<DrawableID>	m_dyingBeamIDs;					///< beams currently fading out, awaiting destruction
	std::vector<UnsignedInt>	m_dyingBeamDeathFrame;	///< frame each dying beam should be destroyed
	std::vector<ParticleSystemID>	m_chargeBeamSystemIDs;	///< flat list of each charge beam's generic particle systems (beam i at [i*N .. i*N+N))
	std::vector< std::vector<ParticleSystemID> >	m_chargeBeamTerrainSystemIDs;	///< per-beam water/land-specific systems (size varies with terrain)
	std::vector<Bool>				m_chargeBeamOverWater;	///< per-beam last-known terrain (true = over water), for detecting shoreline crossings

	AudioEventRTS						m_initialSoundEvent;		///< currently playing InitialSound (holds its handle)
	AudioEventRTS						m_chargeSoundEvent;			///< currently playing ChargeSound (holds its handle)

};
