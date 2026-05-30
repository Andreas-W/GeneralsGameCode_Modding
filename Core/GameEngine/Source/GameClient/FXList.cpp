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

// FILE: FXList.cpp ///////////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, December 2001
// Desc:   FXList descriptions
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_SHADOW_NAMES

#include "GameClient/FXList.h"

#include "Common/DrawModule.h"
#include "Common/GameAudio.h"
#include "Common/GameUtility.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"

#include "GameLogic/Object.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/TerrainLogic.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/PartitionManager.h"
#include "GameClient/Shadow.h"
#include "../../../GameEngineDevice/Include/W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "../../../GameEngineDevice/Include/W3DDevice/GameClient/W3DShadow.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////


FXListStore *TheFXListStore = nullptr;					///< the FXList store definition

//-------------------------------------------------------------------------------------------------
static void adjustVector(Coord3D *vec, const Matrix3D* mtx)
{
	if (mtx)
	{
		Vector3 vectmp;
		vectmp.X = vec->x;
		vectmp.Y = vec->y;
		vectmp.Z = vec->z;
		vectmp = mtx->Rotate_Vector(vectmp);
		vec->x = vectmp.X;
		vec->y = vectmp.Y;
		vec->z = vectmp.Z;
	}
}
//-------------------------------------------------------------------------------------------------
static void adjustVectorXY(Coord3D* vec, const Matrix3D* mtx)
{
	if (mtx)
	{
		//This can be optimized probably.

		Coord3D u, x, y, z, pos;
		Matrix3D mat;
		Real angle = mtx->Get_Z_Rotation();

		pos.x = mtx->Get_X_Translation();
		pos.y = mtx->Get_Y_Translation();
		pos.z = mtx->Get_Z_Translation();
	
		z.x = 0.0f;
		z.y = 0.0f;
		z.z = 1.0f;

		u.x = Cos(angle);
		u.y = Sin(angle);
		u.z = 0.0f;

		y.crossProduct(&z, &u, &y);
		x.crossProduct(&y, &z, &x);

		mat.Set(x.x, y.x, z.x, pos.x,
			      x.y, y.y, z.y, pos.y,
			      x.z, y.z, z.z, pos.z);

		Vector3 vectmp;
		vectmp.X = vec->x;
		vectmp.Y = vec->y;
		vectmp.Z = vec->z;
		vectmp = mat.Rotate_Vector(vectmp);
		vec->x = vectmp.X;
		vec->y = vectmp.Y;
		vec->z = vectmp.Z;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE CLASSES ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
void FXNugget::doFXObj(const Object* primary, const Object* secondary, FXSurfaceInfo* surfaceInfo) const
{
	const Coord3D* p = primary ? primary->getPosition() : nullptr;
	const Matrix3D* mtx = primary ? primary->getTransformMatrix() : nullptr;
	const Real speed = 0.0f;	// yes, that's right -- NOT the object's speed.
	const Coord3D* s = secondary ? secondary->getPosition() : nullptr;
	doFXPos(p, mtx, speed, s, 0.0f, surfaceInfo);
}

//-------------------------------------------------------------------------------------------------
static const char* const AllowedSurfaceNames[] =
{
	"ALL",
	"LAND",
	"WATER",
	NULL
};

enum AllowedSurfaceType CPP_11(: Int) {
	SURFACE_ALL = 0,
	SURFACE_LAND,
	SURFACE_WATER
};

// --------
static bool getSurfaceInfo(const Coord3D* primary, FXSurfaceInfo* surfaceInfo, Bool checkWater)
{
	if (surfaceInfo == NULL || primary == NULL)
		return false;

	if (TheTerrainLogic == NULL)
		return false;

	// Check if we already have the info
	if (surfaceInfo->m_isValid) {
		if (!checkWater || surfaceInfo->m_isWaterChecked) {
			return true;
		}
		else { // compute missing water info
			surfaceInfo->m_waterHeight = TheTerrainLogic->getWaterZ(primary->x, primary->y);
			surfaceInfo->m_isWater = surfaceInfo->m_waterHeight > surfaceInfo->m_groundHeight;
			surfaceInfo->m_isWaterChecked = true;
			return true;
		}
	}

	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(primary);

	if (layer != LAYER_GROUND) {  // Bridge
		surfaceInfo->m_groundHeight = TheTerrainLogic->getLayerHeight(primary->x, primary->y, layer);
		surfaceInfo->m_isBridge = true;
		surfaceInfo->m_isWaterChecked = true;  // if there's a bridge, it can't be water
	}
	else if (checkWater) { // || TheGlobalData->m_heightAboveTerrainIncludesWater) { // do water check
		Real waterZ = 0;
		Real terrainZ = 0;
		
		surfaceInfo->m_isWater = TheTerrainLogic->isUnderwater(primary->x, primary->y, &waterZ, &terrainZ);

		surfaceInfo->m_groundHeight = terrainZ;
		surfaceInfo->m_waterHeight = waterZ;
		surfaceInfo->m_isWaterChecked = true;
	} else {  // Ground height only
		surfaceInfo->m_groundHeight = TheTerrainLogic->getLayerHeight(primary->x, primary->y, layer);
	}

	surfaceInfo->m_isValid = true;

	return true;
}

//-------------------------------------------------------------------------------------------------
class SoundFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(SoundFXNugget, "SoundFXNugget")

public:

	SoundFXNugget()
	{
		m_soundName.clear();
		m_maxAllowedHeight = INFINITY;
		m_minAllowedHeight = -INFINITY;
		m_allowedSurfaceType = SURFACE_ALL;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/, FXSurfaceInfo* surfaceInfo) const
	{
		if (m_allowedSurfaceType != SURFACE_ALL || m_minAllowedHeight > -INFINITY || m_maxAllowedHeight < INFINITY) {

			if (!getSurfaceInfo(primary, surfaceInfo, m_allowedSurfaceType != SURFACE_ALL))
				return;

			if (!isValidSurface(primary, surfaceInfo))
				return;
		}

		AudioEventRTS sound(m_soundName);

		if (primary)
		{
			sound.setPosition(primary);
		}

		TheAudio->addAudioEvent(&sound);
	}

	virtual void doFXObj(const Object* primary, const Object* secondary = NULL, FXSurfaceInfo* surfaceInfo = NULL) const
	{
		if (m_allowedSurfaceType != SURFACE_ALL || m_minAllowedHeight > -INFINITY || m_maxAllowedHeight < INFINITY) {

			if (!getSurfaceInfo(primary->getPosition(), surfaceInfo, m_allowedSurfaceType != SURFACE_ALL))
				return;

			if (!isValidSurface(primary->getPosition(), surfaceInfo))
				return;
		}

		AudioEventRTS sound(m_soundName);
		if (primary)
		{
			sound.setPlayerIndex(primary->getControllingPlayer()->getPlayerIndex());
			sound.setPosition(primary->getPosition());
		}

		TheAudio->addAudioEvent(&sound);
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Name",									INI::parseAsciiString,	nullptr, offsetof( SoundFXNugget, m_soundName ) },
			{ "MinAllowedHeight",			INI::parseReal,							nullptr, offsetof(SoundFXNugget, m_minAllowedHeight) },
			{ "MaxAllowedHeight",			INI::parseReal,							nullptr, offsetof(SoundFXNugget, m_maxAllowedHeight) },
			{ "AllowedSurface",				INI::parseIndexList,				AllowedSurfaceNames, offsetof(SoundFXNugget, m_allowedSurfaceType) },

			{ nullptr, nullptr, nullptr, 0 }
		};

		SoundFXNugget* nugget = newInstance(SoundFXNugget);
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:


	bool isValidSurface(const Coord3D* primary, FXSurfaceInfo* surfaceInfo) const  //@TODO unify code with ParticleSystemFXNugget
	{
		if (primary == NULL || surfaceInfo == NULL)
			return false;

		Real refHeight;
		if (surfaceInfo->m_isWater) {
			if (m_allowedSurfaceType == SURFACE_LAND) return false;
			refHeight = surfaceInfo->m_waterHeight;
		}
		else {
			if (m_allowedSurfaceType == SURFACE_WATER) return false;
			refHeight = surfaceInfo->m_groundHeight;
		}

		Real zOffset = primary->z - refHeight;
		if (zOffset < m_minAllowedHeight || zOffset > m_maxAllowedHeight) return false;

		return true;
	}

	AsciiString		m_soundName;

	Real						m_maxAllowedHeight;
	Real						m_minAllowedHeight;
	AllowedSurfaceType m_allowedSurfaceType;
};
EMPTY_DTOR(SoundFXNugget)

//-------------------------------------------------------------------------------------------------
static Real calcDist(const Coord3D& src, const Coord3D& dst)
{
  Real dx = dst.x - src.x;
  Real dy = dst.y - src.y;
  Real dz = dst.z - src.z;
  return sqrt(dx*dx + dy*dy + dz*dz);
}

//-------------------------------------------------------------------------------------------------
class TracerFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TracerFXNugget, "TracerFXNugget")
public:

	TracerFXNugget()
	{
		m_tracerName.set("GenericTracer");
    m_boneName.clear();
    m_speed = 0.0f; // means "use passed-in speed"
    m_decayAt = 1.0f;
		m_length = 10.0f;
		m_width = 1.0f;
		m_color.red = m_color.green = m_color.blue = 1.0f;
		m_probability = 1.0f;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D *secondary, const Real /*overrideRadius*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (m_probability <= GameClientRandomValueReal(0, 1))
			return;

		if (primary && secondary)
		{
			Drawable *tracer = TheThingFactory->newDrawable(TheThingFactory->findTemplate(m_tracerName));
			if(!tracer)
				return;

			//Kris -- Redid this section Sept 18, 2002
			//Calculate tracer orientations to face from primary to secondary position. This
			//should be the direction that the projectile is being fired towards. It doesn't make
			//sense that the old stuff made use of the muzzle fx bone orientation (because it's a
			//subobject). It had other problems because of elevation variations the tracers would
			//stay on the ground.
			//tracer->setTransformMatrix(primaryMtx);
			Matrix3D tracerMtx;
			Vector3 pos( primary->x, primary->y, primary->z );
			Vector3 dir( secondary->x - primary->x, secondary->y - primary->y, secondary->z - primary->z );
			dir.Normalize(); //This is fantastically crucial for calling buildTransformMatrix!!!!!
			tracerMtx.buildTransformMatrix( pos, dir );
			tracer->setTransformMatrix( &tracerMtx );
			tracer->setPosition(primary);

			Real speed = m_speed;
			if (speed == 0.0f)
			{
				speed = primarySpeed;
			}

			TracerDrawInterface* tdi = nullptr;
			for (DrawModule** d = tracer->getDrawModules(); *d; ++d)
			{
				if ((tdi = (*d)->getTracerDrawInterface()) != nullptr)
				{
					tdi->setTracerParms(speed, m_length, m_width, m_color, 1.0f);
				}
			}

			// estimate how long it will take us to get to the destination
			Real dist = calcDist(*primary, *secondary) - m_length;
			Real frames = (dist >= 0.0f && speed >= 0.0f) ? (dist / speed) : 1;
			Int framesAdjusted = REAL_TO_INT_CEIL(frames * m_decayAt);
			tracer->setExpirationDate(TheGameLogic->getFrame() + framesAdjusted);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary and secondary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "TracerName",			INI::parseAsciiString,			nullptr, offsetof( TracerFXNugget, m_tracerName ) },
			{ "BoneName",				INI::parseAsciiString,			nullptr, offsetof( TracerFXNugget, m_boneName ) },
      { "Speed",          INI::parseVelocityReal,     nullptr, offsetof( TracerFXNugget, m_speed ) },
      { "DecayAt",        INI::parseReal,             nullptr, offsetof( TracerFXNugget, m_decayAt ) },
      { "Length",					INI::parseReal,             nullptr, offsetof( TracerFXNugget, m_length ) },
      { "Width",					INI::parseReal,             nullptr, offsetof( TracerFXNugget, m_width ) },
      { "Color",					INI::parseRGBColor,					nullptr, offsetof( TracerFXNugget, m_color ) },
      { "Probability",		INI::parseReal,             nullptr, offsetof( TracerFXNugget, m_probability ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		TracerFXNugget* nugget = newInstance( TracerFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString			m_tracerName;
  AsciiString     m_boneName;
  Real            m_speed;
  Real            m_decayAt;
	Real						m_length;
	Real						m_width;
	RGBColor				m_color;
	Real						m_probability;
};
EMPTY_DTOR(TracerFXNugget)

//-------------------------------------------------------------------------------------------------
class RayEffectFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(RayEffectFXNugget, "RayEffectFXNugget")
public:

	RayEffectFXNugget()
	{
		m_templateName.clear();
		m_primaryOffset.x = m_primaryOffset.y = m_primaryOffset.z = 0;
		m_secondaryOffset.x = m_secondaryOffset.y = m_secondaryOffset.z = 0;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * secondary, const Real /*overrideRadius*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		const ThingTemplate* tmpl = TheThingFactory->findTemplate(m_templateName);
		DEBUG_ASSERTCRASH(tmpl, ("RayEffect %s not found",m_templateName.str()));
		if (primary && secondary && tmpl)
		{
			Coord3D sourcePos = *primary;
			sourcePos.x += m_primaryOffset.x;
			sourcePos.y += m_primaryOffset.y;
			sourcePos.z += m_primaryOffset.z;

			Coord3D targetPos = *secondary;
			targetPos.x += m_secondaryOffset.x;
			targetPos.y += m_secondaryOffset.y;
			targetPos.z += m_secondaryOffset.z;

			TheGameClient->createRayEffectByTemplate(&sourcePos, &targetPos, tmpl);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary AND secondary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Name",									INI::parseAsciiString,			nullptr, offsetof( RayEffectFXNugget, m_templateName ) },
			{ "PrimaryOffset",				INI::parseCoord3D,					nullptr, offsetof( RayEffectFXNugget, m_primaryOffset ) },
			{ "SecondaryOffset",			INI::parseCoord3D,					nullptr, offsetof( RayEffectFXNugget, m_secondaryOffset ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		RayEffectFXNugget* nugget = newInstance( RayEffectFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString			m_templateName;
	Coord3D					m_primaryOffset;
	Coord3D					m_secondaryOffset;
};
EMPTY_DTOR(RayEffectFXNugget)


//-------------------------------------------------------------------------------------------------
class DecalFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(DecalFXNugget, "DecalFXNugget")
public:

	DecalFXNugget()
	{
		m_templateName.set("GenericDecal"); // TODO
		//m_templateName = AsciiString::TheEmptyString;
		//m_textureName = AsciiString::TheEmptyString;
		//m_opacity = 1.0;		///< value between 0 and 1
		//m_color = 0;		///< color in ARGB format. (Alpha is ignored).
		m_lifetime = 0;
	/*	m_fadeOutTime = 0;
		m_fadeInTime = 0;
		m_type = 0;		/// type of projection
		m_decalSizeX = 0.0;		/// 1/(world space extent of texture in x direction)
		m_decalSizeY = 0.0;		/// 1/(world space extent of texture in y direction)*/
		m_offset.x = m_offset.y = m_offset.z = 0;
		m_angle = 0.0;
		m_orientToObject = FALSE;
		m_randomAngle = FALSE;
		m_probability = 1.0f;
	}

	virtual void doFXPos(const Coord3D* primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D* secondary, const Real /*overrideRadius*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (m_probability <= GameClientRandomValueReal(0, 1))
			return;

		if (primary)
		{
			Coord3D offset = m_offset;
			if (primaryMtx) {
				if (m_orientToObject)
				{
					adjustVector(&offset, primaryMtx);
				}
			}

			Drawable* drawable = TheThingFactory->newDrawable(TheThingFactory->findTemplate(m_templateName));
			if (!drawable)
				return;

			// Does it even make sense to set the matrix?
			if (primaryMtx && m_orientToObject)
				drawable->setTransformMatrix(primaryMtx);

			Coord3D newPos;
			newPos.x = primary->x + offset.x;
			newPos.y = primary->y + offset.y;
			newPos.z = primary->z + offset.z;
			drawable->setPosition(&newPos);

			if (m_randomAngle)
				drawable->setOrientation(GameClientRandomValueReal(0, PI * 2));

			drawable->setExpirationDate(TheGameLogic->getFrame() + m_lifetime);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	virtual void doFXObj(const Object* primary, const Object* secondary, FXSurfaceInfo* surfaceInfo) const
	{
		if (primary)
		{
			doFXPos(primary->getPosition(), primary->getTransformMatrix(), 0.0f, nullptr, 0.0f, surfaceInfo);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI* ini, void* instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "DecalName",			INI::parseAsciiString,			nullptr, offsetof(DecalFXNugget, m_templateName) },
			{ "Lifetime",        INI::parseDurationUnsignedInt, nullptr, offsetof(DecalFXNugget, m_lifetime) },
			{ "Offset",					INI::parseCoord3D,		nullptr, offsetof(DecalFXNugget, m_offset) },
			{ "Angle",					INI::parseReal,             nullptr, offsetof(DecalFXNugget, m_angle) },
			{ "RandomAngle",		INI::parseBool,             nullptr, offsetof(DecalFXNugget, m_randomAngle) },
			{ "OrientToObject",		INI::parseBool,             nullptr, offsetof(DecalFXNugget, m_orientToObject) },
			{ "Probability",		INI::parseReal,             nullptr, offsetof(DecalFXNugget, m_probability) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		DecalFXNugget* nugget = newInstance(DecalFXNugget);
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	AsciiString	m_templateName;
	UnsignedInt m_lifetime;
	Coord3D	m_offset;
	Real m_angle;
	Bool m_orientToObject;
	Bool m_randomAngle;

	// spawn parameters
	Real m_probability;
  // TODO: Height/Surface, etc.
};
EMPTY_DTOR(DecalFXNugget)


//-------------------------------------------------------------------------------------------------
class LightPulseFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(LightPulseFXNugget, "LightPulseFXNugget")
public:

	LightPulseFXNugget() : m_radius(0), m_increaseFrames(0), m_decreaseFrames(0), m_boundingCirclePct(0)
	{
		m_color.red = m_color.green = m_color.blue = 0;
	}

	virtual void doFXObj(const Object* primary, const Object* /*secondary*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (primary)
		{
			Real radius = m_radius;

			if (m_boundingCirclePct > 0)
				radius = (primary->getGeometryInfo().getBoundingCircleRadius() * m_boundingCirclePct);

			TheDisplay->createLightPulse(primary->getPosition(), &m_color, 1, radius, m_increaseFrames, m_decreaseFrames);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (primary)
		{
			TheDisplay->createLightPulse(primary, &m_color, 1, m_radius, m_increaseFrames, m_decreaseFrames);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Color",						INI::parseRGBColor,								nullptr, offsetof( LightPulseFXNugget, m_color ) },
			{ "Radius",						INI::parseReal,										nullptr, offsetof( LightPulseFXNugget, m_radius ) },
			{ "RadiusAsPercentOfObjectSize",		INI::parsePercentToReal,	nullptr, offsetof( LightPulseFXNugget, m_boundingCirclePct ) },
			{ "IncreaseTime",			INI::parseDurationUnsignedInt,	nullptr, offsetof( LightPulseFXNugget, m_increaseFrames ) },
			{ "DecreaseTime",			INI::parseDurationUnsignedInt,	nullptr, offsetof( LightPulseFXNugget, m_decreaseFrames ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		LightPulseFXNugget* nugget = newInstance( LightPulseFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

private:
	RGBColor			m_color;
	Real					m_radius;
	Real					m_boundingCirclePct;
	UnsignedInt		m_increaseFrames;
	UnsignedInt		m_decreaseFrames;
};
EMPTY_DTOR(LightPulseFXNugget)

//-------------------------------------------------------------------------------------------------
class ViewShakeFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ViewShakeFXNugget, "ViewShakeFXNugget")
public:

	ViewShakeFXNugget() : m_shake(View::SHAKE_NORMAL)
	{
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/ , FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (primary)
		{
			if (TheTacticalView)
				TheTacticalView->shake(primary, m_shake);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Type",				parseShakeType,								nullptr, offsetof( ViewShakeFXNugget, m_shake ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		ViewShakeFXNugget* nugget = newInstance( ViewShakeFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:
	static void parseShakeType( INI* ini, void *instance, void *store, const void* /*userData*/ )
	{
		static const LookupListRec shakeTypeNames[] =
		{
			{ "SUBTLE", View::SHAKE_SUBTLE },
			{ "NORMAL", View::SHAKE_NORMAL },
			{ "STRONG", View::SHAKE_STRONG },
			{ "SEVERE", View::SHAKE_SEVERE },
			{ "CINE_EXTREME", View::SHAKE_CINE_EXTREME },
			{ "CINE_INSANE",  View::SHAKE_CINE_INSANE },
			{ nullptr, 0 }
		};
		static_assert(ARRAY_SIZE(shakeTypeNames) == View::SHAKE_COUNT + 1, "Incorrect array size");

		*(Int *)store = INI::scanLookupList(ini->getNextToken(), shakeTypeNames);
	}

private:
	View::CameraShakeType m_shake;

};
EMPTY_DTOR(ViewShakeFXNugget)

//-------------------------------------------------------------------------------------------------
class TerrainScorchFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(TerrainScorchFXNugget, "TerrainScorchFXNugget")
public:

	TerrainScorchFXNugget() : m_scorch(-1), m_radius(0)
	{
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* /*primaryMtx*/, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/, FXSurfaceInfo* surfaceInfo) const
	{
		if (primary)
		{
			// If scormarks are high above the ground
			if (TheGlobalData->m_hideScorchmarksAboveGround) {
				if (!getSurfaceInfo(primary, surfaceInfo, false))
					return;

				PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(primary);
				if (layer != LAYER_GROUND)
					return;

				Real groundHeight = TheTerrainLogic->getLayerHeight(primary->x, primary->y, layer);
				if (primary->z - groundHeight > m_radius)
					return;
			}

			Int scorch = m_scorch;
			if (scorch < 0)
			{
				scorch = GameClientRandomValue( SCORCH_1, SCORCH_4 );
			}
			TheGameClient->addScorch(primary, m_radius, (Scorches)scorch);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Type",				parseScorchType,			nullptr, offsetof( TerrainScorchFXNugget, m_scorch ) },
			{ "Radius",			INI::parseReal,				nullptr, offsetof( TerrainScorchFXNugget, m_radius ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		TerrainScorchFXNugget* nugget = newInstance( TerrainScorchFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	static void parseScorchType( INI* ini, void *instance, void *store, const void* /*userData*/ )
	{
		static const LookupListRec scorchTypeNames[] =
		{
			{ "SCORCH_1",				SCORCH_1 },
			{ "SCORCH_2",				SCORCH_2 },
			{ "SCORCH_3",				SCORCH_3 },
			{ "SCORCH_4",				SCORCH_4 },
			{ "SHADOW_SCORCH",	SHADOW_SCORCH },
			{ "RANDOM",					-1 },
			{ nullptr, 0 }
		};
		static_assert(ARRAY_SIZE(scorchTypeNames) == SCORCH_COUNT + 2, "Incorrect array size");

		*(Int *)store = INI::scanLookupList(ini->getNextToken(), scorchTypeNames);
	}

private:
	Int		m_scorch;
	Real	m_radius;
};
EMPTY_DTOR(TerrainScorchFXNugget)

//-------------------------------------------------------------------------------------------------
class ParticleSystemFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(ParticleSystemFXNugget, "ParticleSystemFXNugget")
public:

	ParticleSystemFXNugget()
	{
		m_name.clear();
		m_count = 1;
		m_radius.setRange(0, 0, GameClientRandomVariable::CONSTANT);
		m_height.setRange(0, 0, GameClientRandomVariable::CONSTANT);
		// -1 means "don't mess with it, just accept the particle-system's defaults"
		m_delay.setRange(-1, -1, GameClientRandomVariable::CONSTANT);
		m_offset.x = m_offset.y = m_offset.z = 0;
		m_orientToObject = false;
		m_attachToObject = false;
		m_createAtGroundHeight = FALSE;
		m_useCallersRadius = FALSE;
		m_rotateX = m_rotateY = m_rotateZ = 0;

		m_maxAllowedHeight = INFINITY;
		m_minAllowedHeight = -INFINITY;
		// m_createAtWaterHeight = FALSE;
		m_allowedSurfaceType = SURFACE_ALL;
		m_useSurfaceInfo = FALSE;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real overrideRadius, FXSurfaceInfo* surfaceInfo) const
	{
		if (primary)
		{
			reallyDoFX(primary, primaryMtx, NULL, overrideRadius, surfaceInfo);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	virtual void doFXObj(const Object* primary, const Object* secondary, FXSurfaceInfo* surfaceInfo) const
	{
		if (primary)
		{

			if (m_ricochet && secondary)
			{
				// HERE WE MUST BUILD A MATRIX WHICH WILL ORIENT THE NEW PARTICLE SYSTEM TO FACE AWAY FROM THE SECONDARY OBJECT
				// THE RESULT SHOULD LOOK LIKE THE DIRECTION OF THE "ATTACK" IS CARRIED THROUGH LIKE A RICOCHET
				Real deltaX = primary->getPosition()->x - secondary->getPosition()->x;
				Real deltaY = primary->getPosition()->y - secondary->getPosition()->y;
				Real aimingAngle = atan2(deltaY, deltaX);
				Matrix3D aimingMatrix(1);
				aimingMatrix.Rotate_Z( aimingAngle );

				reallyDoFX(primary->getPosition(), &aimingMatrix, primary, 0.0f, surfaceInfo);
			}
			else
				// if we have an object, then adjust the offset and direction by the object's transformation
				// matrix, so that (say) an offset of +10 in the z axis "follows" the orientation of the object.
				reallyDoFX(primary->getPosition(), primary->getTransformMatrix(), primary, 0.0f, surfaceInfo);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "Name",									INI::parseAsciiString,			nullptr, offsetof( ParticleSystemFXNugget, m_name ) },
			{ "Count",								INI::parseInt,							nullptr, offsetof( ParticleSystemFXNugget, m_count ) },
			{ "Offset",								INI::parseCoord3D,					nullptr, offsetof( ParticleSystemFXNugget, m_offset ) },
			{ "Radius",								INI::parseGameClientRandomVariable,		nullptr, offsetof( ParticleSystemFXNugget, m_radius ) },
			{ "Height",								INI::parseGameClientRandomVariable,		nullptr, offsetof( ParticleSystemFXNugget, m_height ) },
			{ "InitialDelay",					INI::parseGameClientRandomVariable,		nullptr, offsetof( ParticleSystemFXNugget, m_delay ) },
			{ "RotateX",							INI::parseAngleReal,				nullptr, offsetof( ParticleSystemFXNugget, m_rotateX ) },
			{ "RotateY",							INI::parseAngleReal,				nullptr, offsetof( ParticleSystemFXNugget, m_rotateY ) },
			{ "RotateZ",							INI::parseAngleReal,				nullptr, offsetof( ParticleSystemFXNugget, m_rotateZ ) },
			{ "OrientToObject",				INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_orientToObject ) },
			{ "OrientOffset",				  INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_orientOffset ) },
			{ "OrientXY",				      INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_orientXY ) },
			{ "Ricochet",				      INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_ricochet ) },
			{ "AttachToObject",				INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_attachToObject ) },
			{ "CreateAtGroundHeight",	INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_createAtGroundHeight ) },
			{ "UseCallersRadius",			INI::parseBool,							nullptr, offsetof( ParticleSystemFXNugget, m_useCallersRadius ) },
			// New height controls
			// { "CreateAtWaterHeight",	INI::parseBool,							nullptr, offsetof(ParticleSystemFXNugget, m_createAtWaterHeight) },
			{ "MinAllowedHeight",			INI::parseReal,							nullptr, offsetof(ParticleSystemFXNugget, m_minAllowedHeight) },
			{ "MaxAllowedHeight",			INI::parseReal,							nullptr, offsetof(ParticleSystemFXNugget, m_maxAllowedHeight) },
			{ "AllowedSurface",				INI::parseIndexList,				AllowedSurfaceNames, offsetof(ParticleSystemFXNugget, m_allowedSurfaceType) },
			{ "UseCachedSurfaceInfo", INI::parseBool,							nullptr, offsetof(ParticleSystemFXNugget, m_useSurfaceInfo) },
			{ 0, 0, 0, 0 }
		};

		ParticleSystemFXNugget* nugget = newInstance( ParticleSystemFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	void reallyDoFX(const Coord3D *primary, const Matrix3D* mtx, const Object* thingToAttachTo, Real overrideRadius, FXSurfaceInfo* surfaceInfo) const
	{
		Coord3D offset = m_offset;
		if (mtx) {
			if (m_orientToObject || m_orientOffset)
			{
				adjustVector(&offset, mtx);
			}
			if (m_orientXY) {
				adjustVectorXY(&offset, mtx);
			}
		}

		const ParticleSystemTemplate *tmp = TheParticleSystemManager->findTemplate(m_name);
		DEBUG_ASSERTCRASH(tmp, ("ParticleSystem %s not found",m_name.str()));
		if (tmp)
		{
			Bool needHeightCheck = m_createAtGroundHeight || m_minAllowedHeight > -INFINITY || m_maxAllowedHeight < INFINITY || m_allowedSurfaceType != SURFACE_ALL;
			Bool needWaterCheck = needHeightCheck && (m_allowedSurfaceType != SURFACE_ALL || TheGlobalData->m_heightAboveTerrainIncludesWater);
			Bool needExactCheck = needHeightCheck && !m_useSurfaceInfo && (offset.x != 0 || offset.y != 0 || m_radius.getMinimumValue() != 0 || m_radius.getMaximumValue() != 0);

			if (needHeightCheck && !needExactCheck) {
				if (!getSurfaceInfo(primary, surfaceInfo, needWaterCheck))
					return;

				if (!isValidSurface(primary, surfaceInfo))
					return;
			}

			for (Int i = 0; i < m_count; i++ )
			{
				ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(tmp);
				if (sys)
				{
					Coord3D newPos;
					Real radius = m_radius.getValue();
					Real angle = GameClientRandomValueReal(0.0f, 2.0f * PI);

					newPos.x = primary->x + offset.x + radius * cos(angle);
					newPos.y = primary->y + offset.y + radius * sin(angle);

					Real refHeight;
					if (needExactCheck) {
						FXSurfaceInfo info;
						if (!getSurfaceInfo(primary, &info, needWaterCheck))
							continue;

						if (!isValidSurface(primary, &info))
							continue;

						refHeight = surfaceInfo->m_isWater ? info.m_waterHeight : info.m_groundHeight;
					}

					if (m_createAtGroundHeight) {
						if (needExactCheck) {
							newPos.z = refHeight + offset.z + m_height.getValue();
						}
						else {
							refHeight = surfaceInfo->m_isWater ? surfaceInfo->m_waterHeight : surfaceInfo->m_groundHeight;
							newPos.z = refHeight + offset.z + m_height.getValue();
						}
					}
					else {
						newPos.z = primary->z + offset.z + m_height.getValue();
					}

					if (m_orientToObject && mtx)
					{
						sys->setLocalTransform(mtx);
					}
					else if (m_orientXY) {
						sys->rotateLocalTransformZ(mtx->Get_Z_Rotation());
					}
					if (m_rotateX != 0.0f)
						sys->rotateLocalTransformX(m_rotateX);
					if (m_rotateY != 0.0f)
						sys->rotateLocalTransformY(m_rotateY);
					if (m_rotateZ != 0.0f)
						sys->rotateLocalTransformZ(m_rotateZ);

					if (m_attachToObject && thingToAttachTo)
						sys->attachToObject(thingToAttachTo);
					else
						sys->setPosition( &newPos );

					Real delayInMsec = m_delay.getValue();
					if (delayInMsec >= 0.0f)
					{
						UnsignedInt delayInFrames = REAL_TO_INT_CEIL(ConvertDurationFromMsecsToFrames(delayInMsec));
						sys->setInitialDelay(delayInFrames);
					}

					if( m_useCallersRadius && overrideRadius )
					{
						ParticleSystemInfo::EmissionVolumeType type = sys->getEmisionVolumeType();

						if( type == ParticleSystemInfo::EmissionVolumeType::SPHERE )
							sys->setEmissionVolumeSphereRadius( overrideRadius );
						else if( type == ParticleSystemInfo::EmissionVolumeType::CYLINDER )
							sys->setEmissionVolumeCylinderRadius( overrideRadius );
					}
				}
			}
		}
	}


private:
	AsciiString			m_name;
	Int							m_count;
	Coord3D					m_offset;
	GameClientRandomVariable	m_radius;
	GameClientRandomVariable	m_height;
	GameClientRandomVariable	m_delay;
	Real						m_rotateX, m_rotateY, m_rotateZ;
	Bool						m_orientToObject;
	Bool            m_orientOffset;
	Bool            m_orientXY;
	Bool						m_attachToObject;
	Bool						m_createAtGroundHeight;
	Bool						m_useCallersRadius;
	Bool						m_ricochet;

	Real						m_maxAllowedHeight;
	Real						m_minAllowedHeight;
	//Bool						m_createAtWaterHeight;
	AllowedSurfaceType m_allowedSurfaceType;
	Bool						m_useSurfaceInfo;

	bool isValidSurface(const Coord3D* primary, FXSurfaceInfo* surfaceInfo) const  //@TODO unify code with SoundFXNugget
	{
		if (primary == NULL || surfaceInfo == NULL)
			return false;

		Real refHeight;
		if (surfaceInfo->m_isWater) {
			if (m_allowedSurfaceType == SURFACE_LAND) return false;
			refHeight = surfaceInfo->m_waterHeight;
		}
		else {
			if (m_allowedSurfaceType == SURFACE_WATER) return false;
			refHeight = surfaceInfo->m_groundHeight;
		}

		Real zOffset = primary->z - refHeight;
		if (zOffset < m_minAllowedHeight || zOffset > m_maxAllowedHeight) return false;

		return true;
	}
};
EMPTY_DTOR(ParticleSystemFXNugget)

//-------------------------------------------------------------------------------------------------
class FXListAtBonePosFXNugget : public FXNugget
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(FXListAtBonePosFXNugget, "FXListAtBonePosFXNugget")
public:

	FXListAtBonePosFXNugget()
	{
		m_fx = nullptr;
    m_boneName.clear();
    m_orientToBone = true;
	}

	virtual void doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real /*primarySpeed*/, const Coord3D * /*secondary*/, const Real /*overrideRadius*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		DEBUG_CRASH(("You must use the object form for this effect"));
	}

	virtual void doFXObj(const Object* primary, const Object* /*secondary*/, FXSurfaceInfo* /*surfaceInfo*/) const
	{
		if (primary)
		{
      // first, try the unadorned name.
			doFxAtBones(primary, 0);

      // then, try the 01,02,03...etc names.
			doFxAtBones(primary, 1);
		}
		else
		{
			DEBUG_CRASH(("You must have a primary source for this effect"));
		}
	}

	static void parse(INI *ini, void *instance, void* /*store*/, const void* /*userData*/)
	{
		static const FieldParse myFieldParse[] =
		{
			{ "FX",								  	INI::parseFXList,			    nullptr, offsetof( FXListAtBonePosFXNugget, m_fx ) },
			{ "BoneName",							INI::parseAsciiString,		nullptr, offsetof( FXListAtBonePosFXNugget, m_boneName ) },
			{ "OrientToBone",					INI::parseBool,					  nullptr, offsetof( FXListAtBonePosFXNugget, m_orientToBone ) },
			{ nullptr, nullptr, nullptr, 0 }
		};

		FXListAtBonePosFXNugget* nugget = newInstance( FXListAtBonePosFXNugget );
		ini->initFromINI(nugget, myFieldParse);
		((FXList*)instance)->addFXNugget(nugget);
	}

protected:

	void doFxAtBones(const Object* obj, Int start) const
	{
    Coord3D bonePos[MAX_BONE_POINTS];
    Matrix3D boneMtx[MAX_BONE_POINTS];

    Drawable* draw = obj->getDrawable();
		if (draw)
		{
			// yes, BONEPOS_CURRENT_CLIENT_ONLY -- this is client-only, so should be safe to do.
			Int count = draw->getCurrentClientBonePositions(m_boneName.str(), start, bonePos, boneMtx, MAX_BONE_POINTS);
			for (Int i = 0; i < count; ++i)
			{
				Coord3D p;
				Matrix3D m;
				obj->convertBonePosToWorldPos(&bonePos[i], &boneMtx[i], &p, &m);
				FXList::doFXPos(m_fx, &p, &m, 0.0f, nullptr, 0.0f);
			}
		}
	}

private:

	enum { MAX_BONE_POINTS = 40 };

	const FXList*		m_fx;
  AsciiString     m_boneName;
  Bool            m_orientToBone;
};
EMPTY_DTOR(FXListAtBonePosFXNugget)

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

static const FieldParse TheFXListFieldParse[] =
{
	{ "Sound",											SoundFXNugget::parse, nullptr, 0},
	{ "RayEffect",									RayEffectFXNugget::parse, nullptr, 0},
	{ "Tracer",											TracerFXNugget::parse, nullptr, 0},
	{ "LightPulse",									LightPulseFXNugget::parse, nullptr, 0},
	{ "ViewShake",									ViewShakeFXNugget::parse, nullptr, 0},
	{ "TerrainScorch",							TerrainScorchFXNugget::parse, nullptr, 0},
	{ "ParticleSystem",							ParticleSystemFXNugget::parse, nullptr, 0},
	{ "FXListAtBonePos",						FXListAtBonePosFXNugget::parse, nullptr, 0},
	{ "Decal",											DecalFXNugget::parse, nullptr, 0},
	{ nullptr, nullptr, nullptr, 0 }
};

//-------------------------------------------------------------------------------------------------
FXList::FXList()
{
}

//-------------------------------------------------------------------------------------------------
FXList::~FXList()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void FXList::clear()
{
	for (FXNuggetList::iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{
		deleteInstance(*it);
	}
	m_nuggets.clear();
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXPos(const Coord3D *primary, const Matrix3D* primaryMtx, const Real primarySpeed, const Coord3D *secondary, const Real overrideRadius ) const
{
	const Int playerIndex = rts::getObservedOrLocalPlayer()->getPlayerIndex();

	if (ThePartitionManager->getShroudStatusForPlayer(playerIndex, primary) != CELLSHROUD_CLEAR)
		return;

	FXSurfaceInfo surfaceInfo;  // Cached water/height

	for (FXNuggetList::const_iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{
		(*it)->doFXPos(primary, primaryMtx, primarySpeed, secondary, overrideRadius, &surfaceInfo);
	}
}

//-------------------------------------------------------------------------------------------------
void FXList::doFXObj(const Object* primary, const Object* secondary) const
{
	const Int playerIndex = rts::getObservedOrLocalPlayer()->getPlayerIndex();

	if (primary && primary->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR)
		return;	//the primary object is fogged or shrouded so don't bother with the effect.

	FXSurfaceInfo surfaceInfo;  // Cached water/height

	for (FXNuggetList::const_iterator it = m_nuggets.begin(); it != m_nuggets.end(); ++it)
	{

		// HERE THE PRIMARY IS THE GUY RECEIVING THE FX, AND SECONDARY MIGHT BE THE GUY DEALING IT
		(*it)->doFXObj(primary, secondary, &surfaceInfo);
	}
}



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
FXListStore::FXListStore()
{
}

//-------------------------------------------------------------------------------------------------
FXListStore::~FXListStore()
{
	m_fxmap.clear();
}

//-------------------------------------------------------------------------------------------------
const FXList *FXListStore::findFXList(const char* name) const
{
	if (stricmp(name, "None") == 0)
		return nullptr;

  FXListMap::const_iterator it = m_fxmap.find(NAMEKEY(name));
  if (it != m_fxmap.end())
	{
		return &(*it).second;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/*static */ void FXListStore::parseFXListDefinition(INI *ini)
{
	// read the FXList name
	const char *c = ini->getNextToken();
	NameKeyType key = TheNameKeyGenerator->nameToKey(c);
	FXList& fxl = TheFXListStore->m_fxmap[key];
	fxl.clear();
	ini->initFromINI(&fxl, TheFXListFieldParse);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseFXListDefinition(INI *ini)
{
	FXListStore::parseFXListDefinition(ini);
}
