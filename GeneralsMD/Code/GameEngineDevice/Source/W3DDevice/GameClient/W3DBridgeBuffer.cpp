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

// FILE: W3DBridgeBuffer.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DBridgeBuffer.cpp
//
// Created:   John Ahlquist, May 2001
//
// Desc:      Draw buffer to handle all the bridges in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------

#include "W3DDevice/GameClient/W3DBridgeBuffer.h"

#include "W3DDevice/GameClient/W3DAssetManager.h"
#include <WW3D2/texture.h>
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/TerrainRoads.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "W3DDevice/GameLogic/W3DTerrainLogic.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/scene.h"


//-----------------------------------------------------------------------------
//         Private Data
//-----------------------------------------------------------------------------
// A W3D shader that does alpha, texturing, tests zbuffer, doesn't update zbuffer.
#define SC_ALPHA_DETAIL ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_ENABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailAlphaShader(SC_ALPHA_DETAIL);


#define SC_ALPHA_MIRROR ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

static ShaderClass detailShader(SC_ALPHA_MIRROR);

#define NO_USE_BRIDGE_NORMALS

//-----------------------------------------------------------------------------
//         Private Classes
//-----------------------------------------------------------------------------
//=============================================================================
// W3DBridge constructor.
//=============================================================================
/** Initializes pointers & values.  */
//=============================================================================
W3DBridge::W3DBridge() :
m_bridgeTexture(nullptr),
m_leftMesh(nullptr),
m_sectionMesh(nullptr),
m_rightMesh(nullptr),
m_visible(false),
m_curDamageState(BODY_PRISTINE),
m_scale(1.0),
m_animType(BRIDGE_ANIM_NONE),
m_animStartFrame(0),
m_pendingDamageState(BODY_PRISTINE)
{
}

//=============================================================================
// W3DBridge destructor.
//=============================================================================
/** Frees objects.  */
//=============================================================================
W3DBridge::~W3DBridge()
{
	clearBridge();
}

//=============================================================================
// W3DBridge::renderBridge
//=============================================================================
/** Renders the bride.  It is assumed that the shared vertex and index buffers
are already set.  */
//=============================================================================
void W3DBridge::renderBridge(Bool wireframe)
{
	if (m_visible && m_numPolygons && m_numVertex) {
		if (!wireframe) DX8Wrapper::Set_Texture(0,m_bridgeTexture);
		// Draw all the bridges.
		DX8Wrapper::Draw_Triangles(	m_firstIndex, m_numPolygons, m_firstVertex,	m_numVertex);
	}
}

//=============================================================================
// W3DBridge::clearBridge
//=============================================================================
/** Frees all bridge objects (meshes & texture).  */
//=============================================================================
void W3DBridge::clearBridge()
{
	m_visible = false;
	REF_PTR_RELEASE(m_bridgeTexture);
	REF_PTR_RELEASE(m_leftMesh);
	REF_PTR_RELEASE(m_sectionMesh);
	REF_PTR_RELEASE(m_rightMesh);
}

//=============================================================================
// W3DBridge::cullBridge
//=============================================================================
/** Culls bridge to camera.  */
//=============================================================================
Bool W3DBridge::cullBridge(CameraClass * camera)
{
	///@todo - cull bridges.
	Bool wasVisible = m_visible;

	m_visible = true;

	return(wasVisible != m_visible);

}

#define BRIDGE_FLOAT_AMT (0.25f)

//=============================================================================
// W3DBridge::init
//=============================================================================
/** Inits a bridges location & type so it can be load'ed.  */
//=============================================================================
void W3DBridge::init(Vector3 fromLoc, Vector3 toLoc, AsciiString bridgeTemplateName)
{
	m_start = fromLoc;
	m_end = toLoc;
	m_templateName = bridgeTemplateName;
	m_enabled = true;
}

//=============================================================================
// W3DBridge::init
//=============================================================================
/** Loads a bridge model(if not already loaded) and gets meshes for use at
specified location.  */
//=============================================================================
Bool W3DBridge::load(BodyDamageType curDamageState)
{
	REF_PTR_RELEASE(m_bridgeTexture);
	REF_PTR_RELEASE(m_leftMesh);
	REF_PTR_RELEASE(m_sectionMesh);
	REF_PTR_RELEASE(m_rightMesh);

	Real scale, width, length;
	char textureFile[_MAX_PATH] = "No Texture";
	char modelName[_MAX_PATH] = "BRIDGESECTIONAL";

	/// @todo, should these be defaults in INI??? CBD
	scale = 0.7f;
	width = 34;
	length = 170;

	// try to find bridge in INI
	TerrainRoadType *bridge = TheTerrainRoads->findBridge( m_templateName );
	if (!bridge) return false;

	scale = bridge->getBridgeScale();
	switch (curDamageState) {
		default: return false;

		case 	BODY_PRISTINE:
			strlcpy(textureFile, bridge->getTexture().str(), ARRAY_SIZE(textureFile));
			strlcpy(modelName, bridge->getBridgeModel().str(), ARRAY_SIZE(modelName));
			break;
		case BODY_DAMAGED:
			strlcpy(textureFile, bridge->getTextureDamaged().str(), ARRAY_SIZE(textureFile));
			strlcpy(modelName, bridge->getBridgeModelNameDamaged().str(), ARRAY_SIZE(modelName));
			break;
		case BODY_REALLYDAMAGED:
			strlcpy(textureFile, bridge->getTextureReallyDamaged().str(), ARRAY_SIZE(textureFile));
			strlcpy(modelName, bridge->getBridgeModelNameReallyDamaged().str(), ARRAY_SIZE(modelName));
			break;
		case BODY_RUBBLE:
			strlcpy(textureFile, bridge->getTextureBroken().str(), ARRAY_SIZE(textureFile));
			strlcpy(modelName, bridge->getBridgeModelNameBroken().str(), ARRAY_SIZE(modelName));
			break;
	}

	WW3DAssetManager *pMgr = W3DAssetManager::Get_Instance();
	char left[_MAX_PATH];
	char section[_MAX_PATH];
	char right[_MAX_PATH];

	static_assert(ARRAY_SIZE(left) >= ARRAY_SIZE(modelName), "Incorrect array size");
	static_assert(ARRAY_SIZE(section) >= ARRAY_SIZE(modelName), "Incorrect array size");
	static_assert(ARRAY_SIZE(right) >= ARRAY_SIZE(modelName), "Incorrect array size");
	strcpy(left, modelName);
	strlcat(left, ".BRIDGE_LEFT", ARRAY_SIZE(left));
	strcpy(section, modelName);
	strlcat(section, ".BRIDGE_SPAN", ARRAY_SIZE(section));
	strcpy(right, modelName);
	strlcat(right, ".BRIDGE_RIGHT", ARRAY_SIZE(right));

	m_bridgeTexture = pMgr->Get_Texture(textureFile,  MIP_LEVELS_3);
	m_leftMtx.Make_Identity();
	m_rightMtx.Make_Identity();
	m_sectionMtx.Make_Identity();

	RenderObjClass *pObj = pMgr->Create_Render_Obj(modelName );
	if (!pObj) return false;
	Int i;
	for (i=0; i<pObj->Get_Num_Sub_Objects(); i++) {
		RenderObjClass *pSub = pObj->Get_Sub_Object(i);
		Matrix3D mtx = pSub->Get_Transform();
		if (0==strnicmp(left, pSub->Get_Name(), strlen(left))) {
			m_leftMtx = mtx;
			strlcpy(left, pSub->Get_Name(), ARRAY_SIZE(left));
		}
		if (0==strnicmp(section, pSub->Get_Name(), strlen(section))) {
			m_sectionMtx = mtx;
			strlcpy(section, pSub->Get_Name(), ARRAY_SIZE(section));
		}
		if (0==strnicmp(right, pSub->Get_Name(), strlen(right))) {
			m_rightMtx = mtx;
			strlcpy(right, pSub->Get_Name(), ARRAY_SIZE(right));
		}
		REF_PTR_RELEASE(pSub);
		//DEBUG_LOG(("Sub obj name %s", pSub->Get_Name()));
	}

	REF_PTR_RELEASE(pObj);

	m_leftMesh = (MeshClass*)pMgr->Create_Render_Obj(left );
	m_sectionMesh = (MeshClass*)pMgr->Create_Render_Obj(section);
	m_rightMesh = (MeshClass*)pMgr->Create_Render_Obj(right);
	m_scale = scale;


	if (m_leftMesh == nullptr) {
		clearBridge();
		return(false);
	}
	m_bridgeType = SECTIONAL_BRIDGE;

	if (m_rightMesh == nullptr || m_sectionMesh == nullptr) {
		m_bridgeType = FIXED_BRIDGE;
	}

	Int numVertex = m_leftMesh->Peek_Model()->Get_Vertex_Count();
	Vector3 *pVert = m_leftMesh->Peek_Model()->Get_Vertex_Array();
	m_leftMinX = FLT_MAX;
	m_leftMaxX = -FLT_MAX;
	m_minY = FLT_MAX;
	m_maxY = -FLT_MAX;
	for (i=0; i<numVertex; i++) {
		Vector3 vert;
		Matrix3D::Transform_Vector(m_leftMtx, pVert[i], &vert);
		if (m_leftMinX > vert.X) m_leftMinX = vert.X;
		if (m_minY > vert.Y) m_minY = vert.Y;
		if (vert.X > m_leftMaxX) m_leftMaxX = vert.X;
		if (vert.Y > m_maxY) m_maxY = vert.Y;	 // Note - we assume all sections are the same width, so we only do maxY for first section.
	}
	if (m_bridgeType == SECTIONAL_BRIDGE) {
		numVertex = m_sectionMesh->Peek_Model()->Get_Vertex_Count();
		pVert = m_sectionMesh->Peek_Model()->Get_Vertex_Array();
		m_sectionMinX = FLT_MAX;
		m_sectionMaxX = -FLT_MAX;
		for (i=0; i<numVertex; i++) {
			Vector3 vert;
			Matrix3D::Transform_Vector(m_sectionMtx, pVert[i], &vert);
			if (m_sectionMinX > vert.X) m_sectionMinX = vert.X;
			if (vert.X > m_sectionMaxX) m_sectionMaxX = vert.X;
		}

		numVertex = m_rightMesh->Peek_Model()->Get_Vertex_Count();
		pVert = m_rightMesh->Peek_Model()->Get_Vertex_Array();
		m_rightMinX = FLT_MAX;
		m_rightMaxX = -FLT_MAX;
		for (i=0; i<numVertex; i++) {
			Vector3 vert;
			Matrix3D::Transform_Vector(m_rightMtx, pVert[i], &vert);
			if (m_rightMinX > vert.X) m_rightMinX = vert.X;
			if (vert.X > m_rightMaxX) m_rightMaxX = vert.X;
		}
	} else {
		m_sectionMinX = m_leftMaxX;
		m_sectionMaxX = m_leftMaxX;
		m_rightMinX = m_leftMaxX;
		m_rightMaxX = m_leftMaxX;
	}
	length = m_rightMaxX - m_leftMinX;
	if (length < 1) length = 1;
	m_length = length;
	if (m_bridgeType == SECTIONAL_BRIDGE) {
		Real allowableError = 0.05f*length;
		// make sure the sections align.

		if (m_leftMaxX>m_sectionMinX+allowableError) {
			m_bridgeType = FIXED_BRIDGE;
		}

		if (m_rightMinX<m_sectionMaxX-allowableError) {
			m_bridgeType = FIXED_BRIDGE;
		}

	}
	return(true);
}


//=============================================================================
// W3DBridge::getBridgeInfo
//=============================================================================
/** Gets the location info for the bridge.  */
//=============================================================================
void W3DBridge::getBridgeInfo(BridgeInfo *pInfo)
{

	pInfo->from.x = m_start.X;
	pInfo->from.y = m_start.Y;
	pInfo->from.z = m_start.Z;
	pInfo->to.x = m_end.X;
	pInfo->to.y = m_end.Y;
	pInfo->to.z = m_end.Z;
	pInfo->bridgeWidth = (m_maxY - m_minY) *m_scale;

	Vector3 vec = 	m_end-m_start;
	Vector3 vecNormal(-vec.Y, vec.X, 0);
	vecNormal.Normalize();

	// From left = from + vecNormal*maxY*scale
	pInfo->fromLeft.x = m_start.X + vecNormal.X * m_maxY * m_scale;
	pInfo->fromLeft.y = m_start.Y + vecNormal.Y * m_maxY * m_scale;
	pInfo->fromLeft.z = m_start.Z + vecNormal.Z * m_maxY * m_scale;

	// From right = from + vecNormal*minY*scale
	pInfo->fromRight.x = m_start.X + vecNormal.X * m_minY * m_scale;
	pInfo->fromRight.y = m_start.Y + vecNormal.Y * m_minY * m_scale;
	pInfo->fromRight.z = m_start.Z + vecNormal.Z * m_minY * m_scale;

	// to left = to + vecNormal*maxY*scale
	pInfo->toLeft.x = m_end.X + vecNormal.X * m_maxY * m_scale;
	pInfo->toLeft.y = m_end.Y + vecNormal.Y * m_maxY * m_scale;
	pInfo->toLeft.z = m_end.Z + vecNormal.Z * m_maxY * m_scale;

	// to right = to + vecNormal*minY*scale
	pInfo->toRight.x = m_end.X + vecNormal.X * m_minY * m_scale;
	pInfo->toRight.y = m_end.Y + vecNormal.Y * m_minY * m_scale;
	pInfo->toRight.z = m_end.Z + vecNormal.Z * m_minY * m_scale;

}



//=============================================================================
// rotateInBridgeFrame
//=============================================================================
/** Rotates a vector inside an orthonormal bridge frame: a pitch in the (along, up)
plane for the multi span fold, and a roll in the (across, up) plane for the single
span bank.  Both are plain 2D rotations because the frame is orthonormal.

The frame is built from the bridge end points rather than from vec/vecNormal/vecZ,
because those three are not orthogonal for a sloped bridge that does not run along
world X -- decomposing against them would skew the fold instead of rotating it. */
//=============================================================================
static Vector3 rotateInBridgeFrame(const Vector3 &v, const BridgeSectionAnim *anim)
{
	Real a = Vector3::Dot_Product(v, anim->along);
	Real c = Vector3::Dot_Product(v, anim->across);
	Real u = Vector3::Dot_Product(v, anim->up);

	// pitch -- the fold, used when there are enough sections to fold against each other
	if (anim->angle != 0.0f) {
		Real ca = (Real)cos(anim->angle);
		Real sa = (Real)sin(anim->angle);
		Real na = a*ca - u*sa;
		u = a*sa + u*ca;
		a = na;
	}

	// roll -- the sideways bank of a single span deck sinking
	if (anim->roll != 0.0f) {
		Real cr = (Real)cos(anim->roll);
		Real sr = (Real)sin(anim->roll);
		Real nc = c*cr - u*sr;
		u = c*sr + u*cr;
		c = nc;
	}

	return anim->along * a + anim->across * c + anim->up * u;
}

//=============================================================================
// W3DBridge::getModelVertices
//=============================================================================
/** Gets the vertex values for a section of a bridge.  */
//=============================================================================
Int W3DBridge::getModelVertices(VertexFormatXYZNDUV1 *destination_vb, Int curVertex, Real xOffset,
																Vector3 &vec, Vector3 &vecNormal, Vector3 &vecZ, Vector3 &offset,
																const Matrix3D &mtx,
																MeshClass *pMesh, RefRenderObjListIterator *pLightsIterator,
																const BridgeSectionAnim *anim)
{
	if (pMesh == nullptr)
		return(0);

	Int i;
	Int numVertex = pMesh->Peek_Model()->Get_Vertex_Count();
	Vector3 *pVert = pMesh->Peek_Model()->Get_Vertex_Array();

	const Vector3 *pNormal = 	pMesh->Peek_Model()->Get_Vertex_Normal_Array();

	// If we happen to have too many bridges, stop.
	if (curVertex+numVertex+2>= W3DBridgeBuffer::MAX_BRIDGE_VERTEX) {
		return(0);
	}

	Vector3 lightRay[MAX_GLOBAL_LIGHTS];
	const Coord3D *lightPos;

	for (Int lightIndex=0; lightIndex < TheGlobalData->m_numGlobalLights; lightIndex++)
	{
		lightPos=&TheGlobalData->m_terrainLightPos[lightIndex];
		lightRay[lightIndex].Set(-lightPos->x,-lightPos->y,	-lightPos->z);
//		__asm {int 3}; //see if it really needs normalization!!
		lightRay[lightIndex].Normalize();
	}

	const Vector2*uvs=pMesh->Peek_Model()->Get_UV_Array_By_Index(0);
	VertexFormatXYZNDUV1 *curVb = destination_vb+curVertex;

	for (i=0; i<numVertex; i++) {
		Vector3 vLoc;
		Vector3 vertex;
		Matrix3D::Transform_Vector(mtx, pVert[i], &vertex);
		vLoc = (vertex.X+xOffset) * vec + vertex.Y*vecNormal + vertex.Z*vecZ;

		vLoc.X += m_start.X;
		vLoc.Y += m_start.Y;
		vLoc.Z += m_start.Z;

		// fold and sink this section if a collapse/rebuild animation is running
		if (anim) {
			vLoc = anim->pivot + rotateInBridgeFrame(vLoc - anim->pivot, anim);
			vLoc.Z -= anim->drop;
		}

		curVb->x = vLoc.X;
		curVb->y = vLoc.Y;
		curVb->z = vLoc.Z;

		VERTEX_FORMAT vb;
		vb.x = vLoc.X;
		vb.y = vLoc.Y;
		vb.z = vLoc.Z;

		Vector3 normal;
		Matrix3D::Rotate_Vector(mtx, pNormal[i], &normal);
#ifdef USE_BRIDGE_NORMALS
		curVb->nx = normal.X;
		curVb->ny = normal.Y;
		curVb->nz = normal.Z;
		curVb->diffuse = 0xFF000000;
#else
		normal = (normal.X) * vec + normal.Y*vecNormal + normal.Z*vecZ;
		// the normals have to turn with the geometry or a falling section lights wrongly
		if (anim) {
			normal = rotateInBridgeFrame(normal, anim);
		}
		normal.Normalize();
		TheTerrainRenderObject->doTheLight(&vb, lightRay, &normal, nullptr, 1.0f);
		curVb->nx = 0;	//will these to keep AGP write buffer happy.
		curVb->ny = 0;
		curVb->nz = 1;
		curVb->diffuse = vb.diffuse | 0xFF000000;
#endif
		curVb->u1 = uvs[i].U;
		curVb->v1 = uvs[i].V;
		curVb++;
	}
	return(numVertex);
}

//=============================================================================
// W3DBridge::getModelVerticesFixed
//=============================================================================
/** Gets the vertex values for a section of a fixed bridge.  */
//=============================================================================
Int W3DBridge::getModelVerticesFixed(VertexFormatXYZNDUV1 *destination_vb, Int curVertex,
																const Matrix3D &mtx, MeshClass *pMesh, RefRenderObjListIterator *pLightsIterator)
{
	if (pMesh == nullptr)
		return(0);

	Vector3 vec = m_end - m_start;
	if (vec.Length2() < 1.0f) {
		vec.Normalize();
	}
	Vector3 vecNormal(-vec.Y, vec.X, 0);
	vecNormal.Normalize();
	Real deltaZ = m_end.Z - m_start.Z;
	deltaZ /= vec.Length();
	Real deltaX = sqrt(1.0 - deltaZ*deltaZ);
	Vector3 vecZ(-deltaZ, 0, deltaX);
	vec /= m_length;
	vecNormal *= m_scale;
	vecZ *= m_scale;
	Real xOffset = -m_leftMinX;
	return(getModelVertices(destination_vb, curVertex, xOffset, vec, vecNormal, vecZ, m_start, mtx, pMesh, pLightsIterator));
}

//=============================================================================
// W3DBridge::getAnimPhase
//=============================================================================
/** Progress of the running deck animation, 0..1.

Deliberately computed from absolute elapsed frames rather than an accumulated
delta: drawBridges runs more than once per rendered frame (the water reflection
pass calls it again), and an accumulator would advance the animation twice as
fast whenever the bridge is reflected. */
//=============================================================================
Real W3DBridge::getAnimPhase(UnsignedInt now) const
{
	TerrainRoadType *bridge = TheTerrainRoads ? TheTerrainRoads->findBridge(m_templateName) : nullptr;
	if (bridge == nullptr)
		return 1.0f;

	UnsignedInt duration = (m_animType == BRIDGE_ANIM_REBUILD) ?
													bridge->getBridgeRebuildDuration() : bridge->getBridgeCollapseDuration();

	// a zero duration means the modder did not ask for an animation
	if (duration == 0 || now < m_animStartFrame)
		return 1.0f;

	Real t = (Real)(now - m_animStartFrame) / (Real)duration;
	if (t > 1.0f) t = 1.0f;

	// the rebuild is simply the collapse played backwards
	if (m_animType == BRIDGE_ANIM_REBUILD)
		t = 1.0f - t;

	return t;
}

//=============================================================================
// W3DBridge::computeSectionAnim
//=============================================================================
/** Builds the deformation for one span section.

With several sections the failure ripples out from mid span: the centre sections
start folding immediately, the ones near the banks lag by up to BridgeCollapseStagger
of the total duration, and each half hinges about its outboard edge so the deck folds
inward.

A bridge that resolves to a single section has nothing to fold against -- hinging it
about one edge just swings it like a trapdoor -- so it banks sideways about its own
centre and sinks instead.  The stagger is meaningless there and is ignored.

Returns false when this section has nothing to do, so untouched sections keep the
cheap path. */
//=============================================================================
Bool W3DBridge::computeSectionAnim(Int section, Int numSpans, Real phase, Real xOffset,
																		 const Vector3 &vec, BridgeSectionAnim *anim)
{
	if (anim == nullptr || numSpans < 1)
		return false;

	TerrainRoadType *bridge = TheTerrainRoads ? TheTerrainRoads->findBridge(m_templateName) : nullptr;
	if (bridge == nullptr)
		return false;

	const Bool singleSpan = (numSpans == 1);

	Real drop = bridge->getBridgeCollapseDrop();
	Real tilt = bridge->getBridgeCollapseTilt();
	Real roll = bridge->getBridgeCollapseSingleSpanRoll();

	if (singleSpan) {
		// a zero single span drop just reuses the normal one
		Real singleDrop = bridge->getBridgeCollapseSingleSpanDrop();
		if (singleDrop != 0.0f)
			drop = singleDrop;
		tilt = 0.0f;										// no fold, it banks instead
		if (drop == 0.0f && roll == 0.0f)
			return false;
	} else {
		roll = 0.0f;										// the fold does not bank
		if (drop == 0.0f && tilt == 0.0f)
			return false;
	}

	Real sp;
	Bool leftHalf = false;
	if (singleSpan) {

		// one section, so there is nothing to stagger against
		sp = phase;

	} else {

		Real stagger = bridge->getBridgeCollapseStagger();
		if (stagger < 0.0f) stagger = 0.0f;
		if (stagger > 0.9f) stagger = 0.9f;		// keep the divide below sane

		Real half = numSpans * 0.5f;
		Real center = (Real)section + 0.5f;
		leftHalf = center < half;

		// 0 at mid span, 1 at the banks
		Real d = (Real)fabs(center - half) / half;
		sp = (phase - d * stagger) / (1.0f - stagger);

	}

	if (sp <= 0.0f)
		return false;										// this section has not started moving yet
	if (sp > 1.0f) sp = 1.0f;

	sp = sp * sp;											// ease in, so it reads as falling and not sliding

	//
	// build an orthonormal frame from the bridge end points.  vec/vecNormal/vecZ cannot be used
	// for this: vecZ is a rotation about world Y regardless of the bridge heading, so the three
	// are not mutually perpendicular unless the bridge happens to run along world X.
	//
	Vector3 dir = m_end - m_start;
	if (dir.Length2() < 0.0001f)
		return false;
	anim->along = dir;
	anim->along.Normalize();

	Vector3::Cross_Product(Vector3(0.0f, 0.0f, 1.0f), anim->along, &anim->across);
	if (anim->across.Length2() < 0.0001f)
		anim->across = Vector3(0.0f, 1.0f, 0.0f);	// dead vertical bridge, pick anything sane
	anim->across.Normalize();

	Vector3::Cross_Product(anim->along, anim->across, &anim->up);
	anim->up.Normalize();

	anim->drop = sp * drop;

	Real pivotX;
	if (singleSpan) {

		// bank about the middle of the section so it sinks rather than swinging off one edge
		anim->angle = 0.0f;
		anim->roll = sp * roll;
		pivotX = (m_sectionMinX + m_sectionMaxX) * 0.5f;

	} else {

		// the two halves fold toward each other, hinging about the edge nearest the bank
		anim->angle = sp * tilt * (leftHalf ? 1.0f : -1.0f);
		anim->roll = 0.0f;
		pivotX = leftHalf ? m_sectionMinX : m_sectionMaxX;

	}

	anim->pivot = m_start + vec * (pivotX + xOffset);

	return true;
}

//=============================================================================
// W3DBridge::updateAnimation
//=============================================================================
/** Reconciles the buffer model with the logic damage state, running the deck
animation across the transition instead of popping between models.  Returns true
if the shared vertex buffer needs rebuilding this frame.

Client side only -- nothing here feeds back into the logic.  The logic has already
flipped to BODY_RUBBLE by the time a collapse starts, so riders are dead and the
layer is closed while the deck is still visibly falling. */
//=============================================================================
Bool W3DBridge::updateAnimation(BodyDamageType logicState, UnsignedInt now)
{
	TerrainRoadType *bridge = TheTerrainRoads ? TheTerrainRoads->findBridge(m_templateName) : nullptr;
	UnsignedInt collapseDuration = bridge ? bridge->getBridgeCollapseDuration() : 0;
	UnsignedInt rebuildDuration = bridge ? bridge->getBridgeRebuildDuration() : 0;

	// let a running animation finish before looking at the logic state again
	if (m_animType == BRIDGE_ANIM_COLLAPSE) {
		if (now >= m_animStartFrame + collapseDuration) {
			// the deck has finished falling, so now show the wreck
			m_animType = BRIDGE_ANIM_NONE;
			BodyDamageType prevState = m_curDamageState;
			m_curDamageState = m_pendingDamageState;
			if (!load(m_pendingDamageState))
				load(prevState);
		}
		return true;
	}

	if (m_animType == BRIDGE_ANIM_REBUILD) {
		if (now >= m_animStartFrame + rebuildDuration)
			m_animType = BRIDGE_ANIM_NONE;
		return true;
	}

	if (logicState == m_curDamageState)
		return false;

	//
	// healthy -> rubble.  hold the model we are already showing and fold it; the broken model
	// is swapped in when the fall lands.  the BRIDGE_ANIM_NONE guard above matters here, or
	// this would restart every frame since m_curDamageState deliberately lags the logic.
	//
	if (logicState == BODY_RUBBLE && collapseDuration > 0) {
		m_animType = BRIDGE_ANIM_COLLAPSE;
		m_animStartFrame = now;
		m_pendingDamageState = logicState;
		return true;
	}

	// every other transition swaps the model straight away, as it always did
	BodyDamageType prevState = m_curDamageState;
	m_curDamageState = logicState;
	if (!load(logicState)) {
		// put the old model back
		load(prevState);
		m_curDamageState = logicState;
	}

	// rubble -> healthy.  the deck is whole geometry again, so unfold it into place.
	if (prevState == BODY_RUBBLE && rebuildDuration > 0) {
		m_animType = BRIDGE_ANIM_REBUILD;
		m_animStartFrame = now;
	}

	return true;
}

//=============================================================================
// W3DBridge::getIndicesNVertices
//=============================================================================
/** Gets the index values and vertex values for a bridge.  */
//=============================================================================
void W3DBridge::getIndicesNVertices(UnsignedShort *destination_ib, VertexFormatXYZNDUV1 *destination_vb,
																		Int *curIndexP, Int *curVertexP, RefRenderObjListIterator *pLightsIterator)
{
	Int numI;
	Int numV;
	m_firstVertex = *curVertexP;
	m_firstIndex = *curIndexP;
	m_numVertex = 0;
	m_numPolygons = 0;
	if (m_sectionMesh == nullptr) {
		numV = getModelVerticesFixed(destination_vb, *curVertexP, m_leftMtx, m_leftMesh, pLightsIterator);
		if (!numV)
		{	//not enough room for vertices
			DEBUG_ASSERTCRASH( numV, ("W3DBridge::GetIndicesNVertices(). Vertex overflow.") );
			return;
		}
		numI = getModelIndices( destination_ib, *curIndexP, *curVertexP, m_leftMesh);
		if (!numI)
		{	//not enough room for indices
			DEBUG_ASSERTCRASH( numI, ("W3DBridge::GetIndicesNVertices(). Index overflow.") );
			return;
		}
		*curIndexP += numI;
		*curVertexP += numV;
		m_numVertex += numV;
		m_numPolygons += numI/3;
		return;
	}

	Vector3 vec = m_end - m_start;
	if (vec.Length2() < 1.0f) {
		vec.Normalize();
	}

	Vector3 vecNormal(-vec.Y, vec.X, 0);
	vecNormal.Normalize();
	vecNormal *= m_scale;

	// Rotate along the y axis to get the appropriate Z height adjustment.
	Real deltaZ = m_end.Z - m_start.Z;
	Real desiredLength = vec.Length();
	deltaZ /= desiredLength;
	Real deltaX = sqrt(1.0 - deltaZ*deltaZ);
	Vector3 vecZ(-deltaZ, 0, deltaX);
	vecZ *= m_scale;

	Real spanLength = m_rightMinX - m_leftMaxX;
	Int numSpans = 1;
	if (m_bridgeType != FIXED_BRIDGE) {
		Real spannable = desiredLength - (m_length-spanLength);
		numSpans = REAL_TO_INT_FLOOR( (spannable + spanLength/2)/spanLength);
		if (numSpans<0) numSpans = 0;
	}

	Real bridgeLength = m_length + (numSpans-1)*spanLength;
	Real xOffset = -m_leftMinX;

	// Draw the left end.
	vec /= bridgeLength;
	numV = getModelVertices(destination_vb, *curVertexP, xOffset, vec, vecNormal, vecZ, m_start,
		m_leftMtx, m_leftMesh, pLightsIterator);
	if (!numV)
	{	//not enough room for vertices
		DEBUG_ASSERTCRASH( numV, ("W3DBridge::GetIndicesNVertices(). Vertex overflow.") );
		return;
	}
	numI = getModelIndices( destination_ib, *curIndexP, *curVertexP, m_leftMesh);
	if (!numI)
	{	//not enough room for indices
		DEBUG_ASSERTCRASH( numI, ("W3DBridge::GetIndicesNVertices(). Index overflow.") );
		return;
	}
	*curIndexP += numI;
	*curVertexP += numV;
	m_numVertex += numV;
	m_numPolygons += numI/3;

	Int i;
	//
	// the deck animation folds the span sections only -- the left and right end pieces sit on
	// the abutments and stay put.
	//
	Real animPhase = 0.0f;
	if (m_animType != BRIDGE_ANIM_NONE && TheGameLogic)
		animPhase = getAnimPhase(TheGameLogic->getFrame());

	// draw the spans.
	for (i=0; i<numSpans; i++) {
		BridgeSectionAnim sectionAnim;
		const BridgeSectionAnim *pAnim = nullptr;
		if (m_animType != BRIDGE_ANIM_NONE &&
				computeSectionAnim(i, numSpans, animPhase, xOffset+i*spanLength, vec, &sectionAnim))
			pAnim = &sectionAnim;

		numV = getModelVertices(destination_vb, *curVertexP, xOffset+i*spanLength, vec, vecNormal, vecZ, m_start,
			m_sectionMtx, m_sectionMesh, pLightsIterator, pAnim);
		if (!numV)
		{	//not enough room for vertices
			DEBUG_ASSERTCRASH( numV, ("W3DBridge::GetIndicesNVertices(). Vertex overflow.") );
			return;
		}
		numI = getModelIndices( destination_ib, *curIndexP, *curVertexP, m_sectionMesh);
		if (!numI)
		{	//not enough room for indices
			DEBUG_ASSERTCRASH( numI, ("W3DBridge::GetIndicesNVertices(). Index overflow.") );
			return;
		}
		*curIndexP += numI;
		*curVertexP += numV;
		m_numVertex += numV;
		m_numPolygons += numI/3;
	}

	// Draw the right end.
	numV = getModelVertices(destination_vb, *curVertexP, xOffset+(numSpans-1)*spanLength, vec, vecNormal, vecZ, m_start,
		m_rightMtx, m_rightMesh, pLightsIterator);
	if (!numV)
	{	//not enough room for vertices
		DEBUG_ASSERTCRASH( numV, ("W3DBridge::GetIndicesNVertices(). Vertex overflow.") );
		return;
	}
	numI = getModelIndices( destination_ib, *curIndexP, *curVertexP, m_rightMesh);
	if (!numI)
	{	//not enough room for indices
		DEBUG_ASSERTCRASH( numI, ("W3DBridge::GetIndicesNVertices(). Index overflow.") );
		return;
	}
	*curIndexP += numI;
	*curVertexP += numV;
	m_numVertex += numV;
	m_numPolygons += numI/3;
}

//=============================================================================
// W3DBridge::getModelIndices
//=============================================================================
/** Gets the index values for a particular mesh section of the bridge.  */
//=============================================================================
Int W3DBridge::getModelIndices(UnsignedShort *destination_ib, Int curIndex, Int vertexOffset, MeshClass *pMesh)
{
	if (pMesh == nullptr)
		return(0);
	Int numPoly = pMesh->Peek_Model()->Get_Polygon_Count();
	const TriIndex *pPoly =pMesh->Peek_Model()->Get_Polygon_Array();
	if (curIndex+3*numPoly+6 >= W3DBridgeBuffer::MAX_BRIDGE_INDEX) {
		return(0);
	}
	UnsignedShort *curIb = destination_ib+curIndex;
	Int i;
	for (i=0; i<numPoly; i++) {
		*curIb++ = vertexOffset + pPoly[i].I;
		*curIb++ = vertexOffset + pPoly[i].J;
		*curIb++ = vertexOffset + pPoly[i].K;
	}
	return(numPoly*3);
}

//-----------------------------------------------------------------------------
//         Private Functions
//-----------------------------------------------------------------------------

///@todo - Sort bridges by texture for better performance.

//=============================================================================
// W3DBridgeBuffer::cull
//=============================================================================
/** Culls the bridges, marking the visible flag.  If a bridge changes visibility, it sets
m_anythingChanged */
//=============================================================================
void W3DBridgeBuffer::cull(CameraClass * camera)
{
	Int curBridge;

	m_anythingChanged = m_updateVis;

	for (curBridge=0; curBridge<m_numBridges; curBridge++) {
		if (m_bridges[curBridge].cullBridge(camera)) {
			m_anythingChanged = true;
		}
	}
}


//=============================================================================
// W3DBridgeBuffer::loadBridgesInVertexAndIndexBuffers
//=============================================================================
/** Loads the bridges into the vertex buffer for drawing. */
//=============================================================================
void W3DBridgeBuffer::loadBridgesInVertexAndIndexBuffers(RefRenderObjListIterator *pLightsIterator)
{
	if (!m_indexBridge || !m_vertexBridge || !m_initialized) {
		return;
	}
	m_curNumBridgeVertices = 0;
	m_curNumBridgeIndices = 0;
	VertexFormatXYZNDUV1 *vb;
	UnsignedShort *ib;
	// Lock the buffers.
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBridge, D3DLOCK_DISCARD);
	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexBridge, D3DLOCK_DISCARD);
	vb=(VertexFormatXYZNDUV1*)lockVtxBuffer.Get_Vertex_Array();
	ib = lockIdxBuffer.Get_Index_Array();

//	UnsignedShort *curIb = ib;

//	VertexFormatXYZNDUV1 *curVb = vb;

	Int curBridge;

	for (curBridge=0; curBridge<m_numBridges; curBridge++) {
		m_bridges[curBridge].getIndicesNVertices(ib, vb, &m_curNumBridgeIndices,
			&m_curNumBridgeVertices, pLightsIterator);
	}
}

//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// W3DBridgeBuffer::~W3DBridgeBuffer
//=============================================================================
/** Destructor. Releases w3d assets. */
//=============================================================================
W3DBridgeBuffer::~W3DBridgeBuffer()
{
	freeBridgeBuffers();
}

//=============================================================================
// W3DBridgeBuffer::W3DBridgeBuffer
//=============================================================================
/** Constructor. Sets m_initialized to true if it finds the w3d models it needs
for the bridges. */
//=============================================================================
W3DBridgeBuffer::W3DBridgeBuffer()
{
	m_initialized = false;
	m_vertexMaterial = nullptr;
	m_vertexBridge = nullptr;
	m_indexBridge = nullptr;
	m_bridgeTexture = nullptr;
	m_curNumBridgeVertices=0;
	m_curNumBridgeIndices=0;
	clearAllBridges();
	allocateBridgeBuffers();
	m_initialized = true;
}


//=============================================================================
// W3DBridgeBuffer::freeBridgeBuffers
//=============================================================================
/** Frees the index and vertex buffers. */
//=============================================================================
void W3DBridgeBuffer::freeBridgeBuffers()
{
	REF_PTR_RELEASE(m_vertexBridge);
	REF_PTR_RELEASE(m_indexBridge);
	REF_PTR_RELEASE(m_vertexMaterial);
}

//=============================================================================
// W3DBridgeBuffer::allocateBridgeBuffers
//=============================================================================
/** Allocates the index and vertex buffers. */
//=============================================================================
void W3DBridgeBuffer::allocateBridgeBuffers()
{
	if (TheGlobalData->m_headless)
		return;
	m_vertexBridge=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZNDUV1,MAX_BRIDGE_VERTEX+4,DX8VertexBufferClass::USAGE_DYNAMIC));
	m_indexBridge=NEW_REF(DX8IndexBufferClass,(MAX_BRIDGE_INDEX+4, DX8IndexBufferClass::USAGE_DYNAMIC));
	m_vertexMaterial=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
#ifdef USE_BRIDGE_NORMALS
	m_vertexMaterial= NEW VertexMaterialClass();
	m_vertexMaterial->Set_Shininess(0.0);
	m_vertexMaterial->Set_Ambient(1,1,1);
	m_vertexMaterial->Set_Diffuse(1,1,1);
	m_vertexMaterial->Set_Specular(0,0,0);
	m_vertexMaterial->Set_Emissive(0,0,0);
	m_vertexMaterial->Set_Opacity(1);
	m_vertexMaterial->Set_Lighting(true);
	m_vertexMaterial->Set_Diffuse_Color_Source(VertexMaterialClass::COLOR1);
#endif
	m_curNumBridgeVertices=0;
	m_curNumBridgeIndices=0;
}

//=============================================================================
// W3DBridgeBuffer::clearAllBridges
//=============================================================================
/** Removes all bridges. */
//=============================================================================
void W3DBridgeBuffer::clearAllBridges()
{
	Int curBridge;
	for (curBridge=0; curBridge<m_numBridges; curBridge++) {
		m_bridges[curBridge].clearBridge();
	}
	m_curNumBridgeIndices = 0;
	m_numBridges=0;
}

//=============================================================================
// W3DBridgeBuffer::loadBridges
//=============================================================================
/** loadBridges.  When loaded, tell the terrain logic where the bridge is. */
//=============================================================================
void W3DBridgeBuffer::loadBridges(W3DTerrainLogic *pTerrainLogic, Bool saveGame)
{
	// TheSuperHackers @logic-client-separation helmutbuhler 26/4/2025
	// W3DBridgeBuffer shouldn't add objects to W3DTerrainLogic
	clearAllBridges();
	MapObject *pMapObj;
	MapObject *pMapObj2;
	for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) {
		if (pMapObj->getFlag(FLAG_BRIDGE_POINT1)) {
			pMapObj2 = pMapObj->getNext();
			if ( !pMapObj2 || !pMapObj2->getFlag(FLAG_BRIDGE_POINT2)) {
				DEBUG_LOG(("Missing second bridge point.  Ignoring first."));
			}
			if (pMapObj2==nullptr) break;
			if (!pMapObj2->getFlag(FLAG_BRIDGE_POINT2)) continue;
			Vector3 from, to;
			from.Set(pMapObj->getLocation()->x, pMapObj->getLocation()->y, 0);
			from.Z = TheTerrainRenderObject->getHeightMapHeight(from.X, from.Y, nullptr) + BRIDGE_FLOAT_AMT;
			to.Set(pMapObj2->getLocation()->x, pMapObj2->getLocation()->y, 0);
			to.Z = TheTerrainRenderObject->getHeightMapHeight(to.X, to.Y, nullptr) + BRIDGE_FLOAT_AMT;
			addBridge(from, to, pMapObj->getName(), pTerrainLogic, pMapObj->getProperties());
			pMapObj = pMapObj2;
		}
	}
	if (pTerrainLogic) {
		pTerrainLogic->updateBridgeDamageStates();
	}
}

//=============================================================================
//=============================================================================
static RenderObjClass* createTower( SimpleSceneClass *scene,
																		W3DAssetManager *assetManager,
																		MapObject *mapObject,
																	  BridgeTowerType type,
																	  BridgeInfo *bridgeInfo )
{
	RenderObjClass* tower = nullptr;

	// sanity
	if( scene == nullptr ||
			assetManager == nullptr ||
			mapObject == nullptr ||
			bridgeInfo == nullptr ||
			type < 0 || type >= BRIDGE_MAX_TOWERS )
		return nullptr;

	// get template for this bridge
	DEBUG_ASSERTCRASH( TheTerrainRoads, ("createTower: TheTerrainRoads is null") );
	TerrainRoadType *bridgeTemplate = TheTerrainRoads->findBridge( mapObject->getName() );
	if( bridgeTemplate == nullptr )
		return nullptr;

	// given the type of tower (corner position) find the appropriate spot to put the tower
	Coord3D towerPos;
	switch( type )
	{

		case BRIDGE_TOWER_FROM_LEFT:	towerPos = bridgeInfo->fromLeft;		break;
		case BRIDGE_TOWER_FROM_RIGHT: towerPos = bridgeInfo->fromRight;		break;
		case BRIDGE_TOWER_TO_LEFT:		towerPos = bridgeInfo->toLeft;			break;
		case BRIDGE_TOWER_TO_RIGHT:		towerPos = bridgeInfo->toRight;			break;
		default: return nullptr;

	}

	// set the Z position to that of the terrain
	towerPos.z = TheTerrainRenderObject->getHeightMapHeight( towerPos.x, towerPos.y, nullptr);

	// find the thing template for the tower we want to construct
	AsciiString towerTemplateName = bridgeTemplate->getTowerObjectName( type );
	DEBUG_ASSERTCRASH( TheThingFactory, ("createTower: TheThingFactory is null") );
	const ThingTemplate *towerTemplate = TheThingFactory->findTemplate( towerTemplateName );
	if( towerTemplate == nullptr )
		return nullptr;

	// find the name of the render object to show
	const ModuleInfo& mi = towerTemplate->getDrawModuleInfo();
	if( mi.getCount() <= 0 )
		return nullptr;
	const ModuleData* mdd = mi.getNthData(0);
	const W3DModelDrawModuleData* md = mdd ? mdd->getAsW3DModelDrawModuleData() : nullptr;
	if( md == nullptr )
		return nullptr;
	ModelConditionFlags state;
	state.clear();
	AsciiString modelName = md->getBestModelNameForWB( state );

	// create the render object
	Int playerColor = 0xFFFFFF;
	tower = assetManager->Create_Render_Obj( modelName.str(), 1.0f, playerColor );

	// tie the render object into the map object
	mapObject->setBridgeRenderObject( type, tower );

	// set the position of the tower render object to the position in the world
	Matrix3D transform;
	transform.Make_Identity();
	transform.Set_X_Translation( towerPos.x );
	transform.Set_Y_Translation( towerPos.y );
	transform.Set_Z_Translation( towerPos.z );
	tower->Set_Transform( transform );

	// set the angle for the tower
	/// @todo --> write me

	// add tower render object to the scene
	scene->Add_Render_Object( tower );

	// return the render object of the tower created
	return tower;

}

//=============================================================================
//=============================================================================
static void updateTowerPos( RenderObjClass* tower,
														BridgeTowerType type,
														BridgeInfo* bridgeInfo )
{

	// sanity
	if( tower == nullptr || type < 0 || type >= BRIDGE_MAX_TOWERS || bridgeInfo == nullptr )
		return;

	//
	// compute the angle of the bridge ... we consider the angle of the bridge to be
	// from 'from' to 'to' in the bridge info ... and so does the game
	//
	Coord2D v;
	v.x = bridgeInfo->toLeft.x - bridgeInfo->fromLeft.x;
	v.y = bridgeInfo->toLeft.y - bridgeInfo->fromLeft.y;
	Real angle = v.toAngle();

	//
	// given the type of tower (corner position) find the appropriate spot to put the tower
	// NOTE that we're also adjusting the angle for the from side to point the
	// opposite way the "bridge is pointing"
	//
	Coord3D towerPos;
	switch( type )
	{

		case BRIDGE_TOWER_FROM_LEFT:	towerPos = bridgeInfo->fromLeft;		angle += PI; break;
		case BRIDGE_TOWER_FROM_RIGHT: towerPos = bridgeInfo->fromRight;		angle += PI; break;
		case BRIDGE_TOWER_TO_LEFT:		towerPos = bridgeInfo->toLeft;			break;
		case BRIDGE_TOWER_TO_RIGHT:		towerPos = bridgeInfo->toRight;			break;
		default: return;

	}

	// set the position of the tower render object to the position in the world
	Matrix3D transform;
	transform.Make_Identity();
	transform.Set_X_Translation( towerPos.x );
	transform.Set_Y_Translation( towerPos.y );
	transform.Set_Z_Translation( towerPos.z );
	transform.Rotate_Z( angle );
	tower->Set_Transform( transform );

	// set the angle for the tower
//	tower->setAngle( angle );

}

//=============================================================================
// W3DBridgeBuffer::worldBuilderUpdateBridgeTowers
//=============================================================================
/** loadBridges.  When loaded, tell the terrain logic where the bridge is. */
//=============================================================================
void W3DBridgeBuffer::worldBuilderUpdateBridgeTowers( W3DAssetManager *assetManager,
																											SimpleSceneClass *scene )
{
	MapObject *pMapObj;
	MapObject *pMapObj2;

	for( pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext() )
	{

		if( pMapObj->getFlag( FLAG_BRIDGE_POINT1 ) )
		{

			pMapObj2 = pMapObj->getNext();
			if( !pMapObj2 || !pMapObj2->getFlag( FLAG_BRIDGE_POINT2 ) )
				DEBUG_LOG(("Missing second bridge point.  Ignoring first."));

			if( pMapObj2 == nullptr )
				break;
			if( !pMapObj2->getFlag( FLAG_BRIDGE_POINT2 ) )
				continue;

			//
			// now that we've got the two map objects that are bridge point 1 and 2, get the
			// bridge info that has been stored
			//
			for( Int i = 0; i < m_numBridges; ++i )
			{

				//
				// find the bridge with the matching name and position ... note we're just matching
				// (x,y) here cause name and location (without the additional complication of Z) is
				// really all we have to match bridges.
				/// @todo integrate the editor with the game ... will never happen tho ...
				//
				if( m_bridges[ i ].getTemplateName() == pMapObj->getName() &&
						m_bridges[ i ].getStart()->X == pMapObj->getLocation()->x &&
						m_bridges[ i ].getStart()->Y == pMapObj->getLocation()->y &&
						m_bridges[ i ].getEnd()->X == pMapObj2->getLocation()->x &&
						m_bridges[ i ].getEnd()->Y == pMapObj2->getLocation()->y )
				{
					RenderObjClass *towerRenderObj;

					// get the bridge info
					BridgeInfo bridgeInfo;
					m_bridges[ i ].getBridgeInfo( &bridgeInfo );

					// go through all bridge tower render objects
					Bool created;
					for( Int j = 0; j < BRIDGE_MAX_TOWERS; ++j )
					{

						// create render object if needed
						created = FALSE;
						towerRenderObj = pMapObj->getBridgeRenderObject( (BridgeTowerType)j );
						if( towerRenderObj == nullptr )
						{

							towerRenderObj = createTower( scene, assetManager, pMapObj, (BridgeTowerType)j, &bridgeInfo );
							created = TRUE;

						}

						// sanity
						DEBUG_ASSERTCRASH( towerRenderObj != nullptr, ("worldBuilderUpdateBridgeTowers: unable to create tower for bridge '%s'",
															 m_bridges[ i ].getTemplateName().str()) );

						// update the position of the towers
						updateTowerPos( towerRenderObj, (BridgeTowerType)j, &bridgeInfo );

						// release the initial ref count of 1 for a newly created tower
						if( created )
							REF_PTR_RELEASE( towerRenderObj );

					}

				}

			}

			// skip the 2nd map object representing the second half of the bridgef
			pMapObj = pMapObj2;

		}

	}

}

//=============================================================================
// W3DBridgeBuffer::addBridge
//=============================================================================
/** Adds a bridge.  Name is the GDF object name. */
//=============================================================================
void W3DBridgeBuffer::addBridge(Vector3 fromLoc, Vector3 toLoc, AsciiString name, W3DTerrainLogic *pTerrainLogic, Dict *props)
{
	if (m_numBridges >= MAX_BRIDGES) {
		return;
	}

	if (!m_initialized) {
		return;
	}
	m_bridges[m_numBridges].init(fromLoc, toLoc, name);
	if (m_bridges[m_numBridges].load(BODY_PRISTINE)) {
		W3DBridge *pBridge = m_bridges+m_numBridges;
		if (pTerrainLogic) {
			BridgeInfo info;
			pBridge->getBridgeInfo(&info);
			info.bridgeIndex = m_numBridges;
			pTerrainLogic->addBridgeToLogic(&info, props, name);
		}
		m_numBridges++;
	}
}

//=============================================================================
// W3DBridgeBuffer::updateCenter
//=============================================================================
/** Updates the drawing buffer, based on the camera position. */
//=============================================================================
void W3DBridgeBuffer::updateCenter(CameraClass *camera, RefRenderObjListIterator *pLightsIterator)
{
	cull(camera);
	if (m_anythingChanged || m_curNumBridgeIndices == 0) {
		loadBridgesInVertexAndIndexBuffers(pLightsIterator);
	}
	m_updateVis = false;
}

//=============================================================================
// W3DBridgeBuffer::drawBridges
//=============================================================================
/** Draws the bridges. */
//=============================================================================
void W3DBridgeBuffer::drawBridges(CameraClass * camera, Bool wireframe, TextureClass *cloudTexture)
{

	Int curBridge;
	if (TheTerrainLogic) {
		for (curBridge=0; curBridge<m_numBridges; curBridge++) {
			m_bridges[curBridge].setEnabled(false);
		}
		/* Check for any changed damage states, and advance any running deck animation. */
		Bool changed = false;
		UnsignedInt now = TheGameLogic ? TheGameLogic->getFrame() : 0;
		for (Bridge *bridge = TheTerrainLogic->getFirstBridge(); bridge; bridge = bridge->getNext()) {
			BridgeInfo info;
			bridge->getBridgeInfo(&info);
			if (info.bridgeIndex<0 || info.bridgeIndex>=m_numBridges) {
				continue;
			}
			m_bridges[info.bridgeIndex].setEnabled(true);
			if (m_bridges[info.bridgeIndex].updateAnimation(info.curDamageState, now))
				changed = true;
		}
		if (changed) {
			loadBridgesInVertexAndIndexBuffers(nullptr);
		}
	}	else {
		// In wb, all are enabled.
		for (curBridge=0; curBridge<m_numBridges; curBridge++) {
			m_bridges[curBridge].setEnabled(true);
		}
	}



	if (m_curNumBridgeIndices == 0) {
		return;
	}

	DX8Wrapper::Set_Material(m_vertexMaterial);
	// Setup the vertex buffer, shader & texture.
	DX8Wrapper::Set_Index_Buffer(m_indexBridge,0);
	DX8Wrapper::Set_Vertex_Buffer(m_vertexBridge);
	DX8Wrapper::Set_Shader(detailAlphaShader);
#ifdef RTS_DEBUG
	//DX8Wrapper::Set_Shader(detailShader); // shows alpha clipping.
#endif

	DX8Wrapper::Apply_Render_State_Changes();

	if (!wireframe && cloudTexture)
	{	//Force a cloud texture projection into stage 1
		W3DShaderManager::setTexture(1,cloudTexture);
		W3DShaderManager::setShader(W3DShaderManager::ST_CLOUD_TEXTURE,1);
	}

	for (curBridge=0; curBridge<m_numBridges; curBridge++) {
		if (m_bridges[curBridge].isEnabled() && m_bridges[curBridge].isVisible()) {
			m_bridges[curBridge].renderBridge(wireframe);
		}
	}

	if (!wireframe && cloudTexture)
		//Force a cloud texture projection into stage 1
		W3DShaderManager::resetShader(W3DShaderManager::ST_CLOUD_TEXTURE);

	//Render shroud pass over all the bridges
	if (!wireframe && TheTerrainRenderObject->getShroud())
	{
		//Reset to a known shader.
		DX8Wrapper::Invalidate_Cached_Render_States();
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Material(m_vertexMaterial);
		DX8Wrapper::Set_Index_Buffer(m_indexBridge,0);
		DX8Wrapper::Set_Vertex_Buffer(m_vertexBridge);
		DX8Wrapper::Apply_Render_State_Changes();
		//Apply custom shroud projection shader.
		W3DShaderManager::setTexture(0,TheTerrainRenderObject->getShroud()->getShroudTexture());
		W3DShaderManager::setShader(W3DShaderManager::ST_SHROUD_TEXTURE, 0);
		for (curBridge=0; curBridge<m_numBridges; curBridge++) {
			if (m_bridges[curBridge].isEnabled() && m_bridges[curBridge].isVisible()) {
				//Pretend we're in wireframe so function doesn't reset the shroud texture.
				m_bridges[curBridge].renderBridge(TRUE);
			}
		}
		W3DShaderManager::resetShader(W3DShaderManager::ST_SHROUD_TEXTURE);
	}
}


