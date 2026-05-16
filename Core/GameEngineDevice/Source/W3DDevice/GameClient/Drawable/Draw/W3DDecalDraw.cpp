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

// FILE: W3DDecalDraw.cpp ////////////////////////////////////////////////////////////////////////
// Author: Andi W, May 26
// Desc:   Draw module for Decal FXNugget
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#define DEFINE_SHADOW_NAMES

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameClient/Drawable.h"
#include "GameClient/Shadow.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/Module/W3DDecalDraw.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
//#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"

//-------------------------------------------------------------------------------------------------
W3DDecalDrawModuleData::W3DDecalDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
W3DDecalDrawModuleData::~W3DDecalDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DDecalDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
		{ "Texture", INI::parseAsciiString, nullptr, offsetof(W3DDecalDrawModuleData, m_textureName) },
		{ "Color", INI::parseColorInt, nullptr, offsetof(W3DDecalDrawModuleData, m_color) },
		{ "Opacity", INI::parsePercentToReal, nullptr, offsetof(W3DDecalDrawModuleData, m_opacity) },
		{ "Style", INI::parseBitString32,	TheShadowNames, offsetof(W3DDecalDrawModuleData, m_type) },
		{ "FadeOutTime", INI::parseDurationUnsignedInt, nullptr, offsetof(W3DDecalDrawModuleData, m_fadeOutTime) },
		{ "FadeInTime", INI::parseDurationUnsignedInt, nullptr, offsetof(W3DDecalDrawModuleData, m_fadeInTime) },
		{ "SizeX", INI::parseReal, nullptr, offsetof(W3DDecalDrawModuleData, m_decalSizeX) },
		{ "SizeY", INI::parseReal, nullptr, offsetof(W3DDecalDrawModuleData, m_decalSizeY) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
//
//void W3DDecalDraw::initDecal(AsciiString texture, Real opacity, Int color, ShadowType type, UnsignedInt lifetime, UnsignedInt fadeOutTime, UnsignedInt fadeInTime, Real sizeX, Real sizeY)
//{
//
//}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DDecalDraw::W3DDecalDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData )
{
	//Drawable* draw = getDrawable();
	//const W3DDecalDrawModuleData* data = getW3DDecalDrawModuleData();
	// draw->getExpirationDate();

	m_fullyObscuredByShroud = false;
	m_renderBox = nullptr;
	m_shadow = nullptr;
	m_frameCreated = 0;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DDecalDraw::~W3DDecalDraw( void )
{
	if (m_shadow)
		m_shadow->release();
	m_shadow = nullptr;

	if (m_renderBox) {
		if (W3DDisplay::m_3DScene != nullptr)
			W3DDisplay::m_3DScene->Remove_Render_Object(m_renderBox);
		REF_PTR_RELEASE(m_renderBox);
		m_renderBox = nullptr;
	}

}
//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::init_shadow()
{
	const W3DDecalDrawModuleData* data = getW3DDecalDrawModuleData();

	Shadow::ShadowTypeInfo shadowInfo;
	strlcpy(shadowInfo.m_ShadowName, data->m_textureName.str(), ARRAY_SIZE(shadowInfo.m_ShadowName));
	shadowInfo.allowUpdates = FALSE;		//shadow image will never update
	shadowInfo.allowWorldAlign = TRUE;	//shadow image will wrap around world objects
	shadowInfo.m_type = data->m_type;
	shadowInfo.m_sizeX = data->m_decalSizeX;
	shadowInfo.m_sizeY = data->m_decalSizeY;
	shadowInfo.m_offsetX = 0.0f; // TODO
	shadowInfo.m_offsetY = 0.0f; // TODO
	//shadowInfo.m_hasDynamicLength = FALSE;

	DEBUG_ASSERTCRASH(m_shadow == nullptr, ("m_shadow is not null"));

	//Drawable* draw = getDrawable();

	if (TheProjectedShadowManager)
		m_shadow = TheProjectedShadowManager->addDecal(m_renderBox, &shadowInfo);
	if (m_shadow)
	{
		m_shadow->setColor(data->m_color);
		m_shadow->setOpacity(REAL_TO_INT(data->m_opacity * 255.0f));
		m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
		m_shadow->enableShadowRender(true);
	}
}
//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::init_renderBox(const Matrix3D* transformMtx)
{
	const W3DDecalDrawModuleData* data = getW3DDecalDrawModuleData();

	Vector3 center = { 0, 0, 0 };
	Vector3 extent = { data->m_decalSizeX, data->m_decalSizeY, 1.0f };

	m_renderBox = NEW OBBoxRenderObjClass(
		OBBoxClass(center, extent)
	);

	if (W3DDisplay::m_3DScene != nullptr)
		W3DDisplay::m_3DScene->Add_Render_Object(m_renderBox);
	m_renderBox->Set_Transform(*transformMtx);

}

//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::setFullyObscuredByShroud(Bool fullyObscured)
{
	if (m_fullyObscuredByShroud != fullyObscured)
	{
		m_fullyObscuredByShroud = fullyObscured;

		if (m_shadow)
			m_shadow->enableShadowInvisible(m_fullyObscuredByShroud);
	}
}

//void W3DDecalDraw::setShadowsEnabled(Bool enable)
//{
//	if (m_shadow)
//		m_shadow->enableShadowRender(enable);
//	m_shadowEnabled = enable;
//}

//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::reactToTransformChange( const Matrix3D *oldMtx,
																							 const Coord3D *oldPos,
																							 Real oldAngle )
{
	if (m_renderBox)
	{
		m_renderBox->Set_Transform(*getDrawable()->getTransformMatrix());
	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::doDrawModule(const Matrix3D* transformMtx)
{
	//DEBUG_LOG(("W3DDecalDraw::doDrawModule 1"));

	if (m_renderBox == nullptr) {

		init_renderBox(transformMtx);

		init_shadow();
	}

	if (m_shadow == nullptr) {
		init_shadow();
	}

	if (m_renderBox)
	{
		Matrix3D mtx = *transformMtx;
		m_renderBox->Set_Transform(mtx);
	}

	// Update decal opacity from lifetime
	const W3DDecalDrawModuleData* data = getW3DDecalDrawModuleData();
	UnsignedInt expDate = getDrawable()->getExpirationDate();

	Real opacity = data->m_opacity;

	UnsignedInt now = TheGameLogic->getFrame();
	if (m_frameCreated == 0)
		m_frameCreated = now;

	if (data->m_fadeInTime > 0) {
		opacity = INT_TO_REAL(now - m_frameCreated) / INT_TO_REAL(data->m_fadeInTime);
		if (opacity > 1.0f)
			opacity = 1.0f;
	}

	if (data->m_fadeOutTime > 0 && expDate != 0) {
		opacity *= INT_TO_REAL(expDate - now) / INT_TO_REAL(data->m_fadeOutTime);
		if (opacity > 1.0f)
			opacity = 1.0f;
		else if (opacity < 0.0f)
			opacity = 0.0f;
	}

	if (opacity != data->m_opacity)
		m_shadow->setOpacity(REAL_TO_INT(opacity * 255.0f));

	//if (expDate != 0)
	//{
	//	Real decay = m_opacity / (expDate - TheGameLogic->getFrame());
	//	m_opacity -= decay;
	//	m_theTracer->Set_Opacity(m_opacity);
	//}

	return;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// no data to save here, nobody will ever notice

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

}
