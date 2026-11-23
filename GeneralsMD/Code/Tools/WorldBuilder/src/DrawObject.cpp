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

#include "StdAfx.h"

#include "DrawObject.h"

#include <stdlib.h>
#include <WW3D2/assetmgr.h>
#include <WW3D2/texture.h>
#include <WWMath/tri.h>
#include <WWMath/colmath.h>
#include <WW3D2/coltest.h>
#include <WW3D2/rinfo.h>
#include <WW3D2/camera.h>
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/shader.h"
#include "Common/MapObject.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/SidesList.h"
#include "resource.h"
#include "wbview3d.h"
#include "WorldBuilderDoc.h"
#include "WHeightMapEdit.h"
#include "MeshMoldOptions.h"
#include "WaterTool.h"
#include "BuildListTool.h"
#include "LayersList.h"
#include "Common/WellKnownKeys.h"
#include "Common/BorderColors.h"
#include "Common/ThingTemplate.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "WW3D2/render2d.h"
#include "GameLogic/Weapon.h"
#include "Common/AudioEventInfo.h"

#ifdef RTS_DEBUG
#define NO_INTENSE_DEBUG 1
#endif

const Real LINE_THICKNESS = 2.0f;
const Real HANDLE_SIZE = (2.0f) * LINE_THICKNESS;


// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_OPAQUE ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA_Z ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_OPAQUE_Z ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// No texturing, zbuffer test without write, vertex color, alpha blending: grid lines hidden behind terrain and objects.
#define SC_GRID ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_DISABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// The same without zbuffer test: lines drawn over water and objects.
#define SC_GRID_ON_TOP ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_DISABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Grid and ruler drawing.
static const Int GRID_BATCH_QUADS = 4096;				///< Quads per draw call (16384 vertices fit 16 bit indices).
static const Int GRID_BLOCK_CELLS = 16;					///< Culling and level of detail work on blocks of this many cells.
static const Real GRID_SAMPLE_LENGTH = MAP_XY_FACTOR/2;	///< Lines are cut into pieces this long to follow the terrain.
static const Real GRID_MIN_PIXELS = 6.0f;				///< Lines closer than this on screen are thinned out.
static const Real GRID_MAX_STEP = 64*MAP_XY_FACTOR;
static const unsigned long GRID_COLOR = 0x80FFFFFF;			// White.
static const Real GRID_LINE_PIXELS = 1.0f;
static const Real RULER_MINOR_MIN_PIXELS = 8.0f;			///< Minor lines closer than this on screen are left out.
static const Int RULER_SECTORS = 24;						///< A sector line every 15 degrees,
static const Int RULER_SECTORS_PER_MAJOR = 3;				///< a major one every 45 degrees.
static const unsigned long RULER_RECT_MAJOR_COLOR = 0xD0FFD040;		// Yellow.
static const unsigned long RULER_RECT_MINOR_COLOR = 0x58FFD040;
static const unsigned long RULER_ANGULAR_MAJOR_COLOR = 0xD040D0FF;	// Cyan.
static const unsigned long RULER_ANGULAR_MINOR_COLOR = 0x5840D0FF;
static const unsigned long RULER_DIAGONAL_COLOR = 0xE0FF50FF;			// Magenta, apart from the orange map boundaries.
static const Real RULER_MAJOR_LINE_PIXELS = 1.5f;
static const Real RULER_MINOR_LINE_PIXELS = 1.0f;
static const Real RULER_DIAGONAL_LINE_PIXELS = 2.0f;


Int DrawObject::m_feedbackShape = BRUSH_SHAPE_CIRCLE;
Int	DrawObject::m_brushWidth = 3;
Int	DrawObject::m_brushFeatherWidth = 3;
Bool	DrawObject::m_toolWantsFeedback = true;
Bool	DrawObject::m_disableFeedback = false;
Bool	DrawObject::m_meshFeedback = false;
Bool	DrawObject::m_rampFeedback = false;
Bool	DrawObject::m_boundaryFeedback = false;
Bool	DrawObject::m_ambientSoundFeedback = false;
Coord3D	DrawObject::m_feedbackPoint;
CPoint DrawObject::m_cellCenter;

Coord3D	DrawObject::m_rampStartPoint;
Coord3D	DrawObject::m_rampEndPoint;
Real DrawObject::m_rampWidth = 0.0f;


Bool DrawObject::m_dragWaypointFeedback = false;
Coord3D DrawObject::m_dragWayStart;
Coord3D DrawObject::m_dragWayEnd;

static Int curHighlight = 0;
static const Int NUM_HIGHLIGHT = 3;




void DrawObject::setWaypointDragFeedback(const Coord3D &start, const Coord3D &end)
{
	m_dragWaypointFeedback = true;
	m_dragWayStart = start;
	m_dragWayEnd = end;
}

void DrawObject::stopWaypointDragFeedback()
{
	m_dragWaypointFeedback = false;
}



DrawObject::~DrawObject()
{
	freeMapResources();
	REF_PTR_RELEASE(m_waterDrawObject);
	TheWaterRenderObj = nullptr;
}

DrawObject::DrawObject() :
	m_drawObjects(true),
	m_drawPolygonAreas(true),
	m_indexBuffer(nullptr),
	m_vertexMaterialClass(nullptr),
	m_vertexBufferTile1(nullptr),
	m_vertexBufferTile2(nullptr),
	m_vertexBufferWater(nullptr),
	m_vertexFeedback(nullptr),
	m_indexFeedback(nullptr),
	m_indexWater(nullptr),
	m_moldMesh(nullptr),
	m_lineRenderer(nullptr),
  m_drawSoundRanges(false),
	m_drawGrid(false),
	m_gridOnTop(false),
	m_gridStep(MAP_XY_FACTOR),
	m_drawRuler(false),
	m_rulerRect(false),
	m_rulerAngular(false),
	m_rulerDiagonals(false),
	m_rulerOnTop(false),
	m_rulerMinor(5*MAP_XY_FACTOR),
	m_rulerMajorEvery(4),
	m_gridIndexBuffer(nullptr),
	m_gridBlocksReady(false),
	m_gridBlocksX(0),
	m_gridBlocksY(0),
	m_gridMinX(0), m_gridMinY(0), m_gridMaxX(0), m_gridMaxY(0)
{
	m_feedbackPoint.x = 20;
	m_feedbackPoint.y = 20;
	initData();
	m_waterDrawObject = new WaterRenderObjClass;
	m_waterDrawObject->init(0, 0, 0, nullptr, WaterRenderObjClass::WATER_TYPE_0_TRANSLUCENT);
	TheWaterRenderObj=m_waterDrawObject;

	//(gth) this was needed to fix the extents bug that is based off water and too small for our maps
	Set_Force_Visible(true);
}


Bool DrawObject::Cast_Ray(RayCollisionTestClass & raytest)
{

	return false;

}


//@todo: MW Handle both of these properly!!
DrawObject::DrawObject(const DrawObject & src)
{
	*this = src;
}

DrawObject & DrawObject::operator = (const DrawObject & that)
{
	DEBUG_CRASH(("oops"));
	return *this;
}

void DrawObject::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	// (gth) CNC3 these bounds don't actually work for all levels...
	// we set the "force visible" flag for this object since it encapsulates all of the UI
	// gadgets for the whole level anyway.
	Vector3	ObjSpaceCenter(TheGlobalData->m_waterExtentX,TheGlobalData->m_waterExtentY,50*MAP_XY_FACTOR);
	float length = ObjSpaceCenter.Length();
	sphere.Init(ObjSpaceCenter, length);
}

void DrawObject::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	// (gth) CNC3 these bounds don't actually work for all levels...
	// we set the "force visible" flag for this object since it encapsulates all of the UI
	// gadgets for the whole level anyway.
	Vector3	minPt(-2*TheGlobalData->m_waterExtentX,-2*TheGlobalData->m_waterExtentY,0);
	Vector3	maxPt(2*TheGlobalData->m_waterExtentX,2*TheGlobalData->m_waterExtentY,100*MAP_XY_FACTOR);
	box.Init(minPt,maxPt);
}

Int DrawObject::Class_ID() const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * DrawObject::Clone() const
{
	return new DrawObject(*this);
}


Int DrawObject::freeMapResources()
{

	REF_PTR_RELEASE(m_indexBuffer);
	REF_PTR_RELEASE(m_vertexBufferTile1);
	REF_PTR_RELEASE(m_vertexBufferTile2);
	REF_PTR_RELEASE(m_vertexBufferWater);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	REF_PTR_RELEASE(m_vertexFeedback);
	REF_PTR_RELEASE(m_indexFeedback);
	REF_PTR_RELEASE(m_indexWater);
	REF_PTR_RELEASE(m_moldMesh);
	REF_PTR_RELEASE(m_gridIndexBuffer);

	delete m_lineRenderer;
	m_lineRenderer = nullptr;

	return 0;
}

// Total number of triangles
#define NUM_TRI 26
// Number of triangles in the arrow.
#define NUM_ARROW_TRI 4
// Number of triangles in the selection pyramid.
#define NUM_SELECT_TRI 16
// Height of selection pyramid.
#define SELECT_PYRAMID_HEIGHT (1.0f)


Int DrawObject::initData()
{
	Int i;

	freeMapResources();	//free old data and ib/vb

	m_numTriangles = 2*NUM_TRI;
	m_indexBuffer=NEW_REF(DX8IndexBufferClass,(m_numTriangles*3, DX8IndexBufferClass::USAGE_DYNAMIC));

	// Fill up the IB
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();

	for (i=0; i<3*m_numTriangles; i+=3)
	{
		ib[0]=i;
		ib[1]=i+1;
		ib[2]=i+2;

		ib+=3;	//skip the 3 indices we just filled
	}

	m_vertexBufferTile1=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,m_numTriangles*3,DX8VertexBufferClass::USAGE_DYNAMIC));
	m_vertexBufferTile2=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,m_numTriangles*3,DX8VertexBufferClass::USAGE_DYNAMIC));

	m_vertexFeedback=NEW_REF(DX8VertexBufferClass,(DX8_FVF_XYZDUV1,NUM_FEEDBACK_VERTEX,DX8VertexBufferClass::USAGE_DYNAMIC));
	m_indexFeedback=NEW_REF(DX8IndexBufferClass,(NUM_FEEDBACK_INDEX,DX8IndexBufferClass::USAGE_DYNAMIC));

	// Grid lines are quads of two triangles: 0,1,2 and 2,1,3.
	m_gridIndexBuffer=NEW_REF(DX8IndexBufferClass,(GRID_BATCH_QUADS*6,DX8IndexBufferClass::USAGE_DYNAMIC));
	{
		DX8IndexBufferClass::WriteLockClass lockGridIdx(m_gridIndexBuffer, D3DLOCK_DISCARD);
		UnsignedShort *gib=lockGridIdx.Get_Index_Array();
		for (i=0; i<GRID_BATCH_QUADS; i++) {
			UnsignedShort base = (UnsignedShort)(i*4);
			gib[0]=base;
			gib[1]=base+1;
			gib[2]=base+2;
			gib[3]=base+2;
			gib[4]=base+1;
			gib[5]=base+3;
			gib+=6;
		}
	}

	//go with a preset material for now.
	m_vertexMaterialClass=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);

	//use a multi-texture shader: (text1*diffuse)*text2.
	m_shaderClass = ShaderClass(SC_OPAQUE);//_PresetOpaque2DShader;//ShaderClass(SC_OPAQUE); //_PresetOpaqueShader;

	m_shaderClass = ShaderClass::_PresetOpaque2DShader;
	updateForWater();
	updateVB(m_vertexBufferTile1, 255<<8, true, false);

	return 0;
}


/** updateMeshVB puts mesh mold triangles into m_vertexFeedback. */

void DrawObject::updateMeshVB()
{
	const Int theAlpha = 64;

	if (m_curMeshModelName != MeshMoldOptions::getModelName()) {
		REF_PTR_RELEASE(m_moldMesh);
		m_curMeshModelName = MeshMoldOptions::getModelName();
	}
	if (m_moldMesh == nullptr) {
 		WW3DAssetManager *pMgr = W3DAssetManager::Get_Instance();
		pMgr->Set_WW3D_Load_On_Demand(false);	 // We don't want it fishing for these assets in the game assets.
		m_moldMesh = (MeshClass*)pMgr->Create_Render_Obj(m_curMeshModelName.str());
		if (m_moldMesh == nullptr) {
			// Try loading the mold asset.
			AsciiString path("data\\editor\\molds\\");
			path.concat(m_curMeshModelName);
			path.concat(".w3d");
			pMgr->Load_3D_Assets(path.str());
			m_moldMesh = (MeshClass*)pMgr->Create_Render_Obj(m_curMeshModelName.str());
		}
		if (m_moldMesh) {
			m_moldMeshBounds = m_moldMesh->Get_Bounding_Sphere();
		}
		pMgr->Set_WW3D_Load_On_Demand(true);
	}
	if (m_moldMesh == nullptr) {
		return;
	}


	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

	if (m_moldMesh == nullptr) {
		return;
	}
	Int i;
	Int numVertex = m_moldMesh->Peek_Model()->Get_Vertex_Count();
	Vector3 *pVert = m_moldMesh->Peek_Model()->Get_Vertex_Array();

//	const Vector3 *pNormal = 	m_moldMesh->Peek_Model()->Get_Vertex_Normal_Array();

	// If we happen to have too many vertex, stop.
	if (numVertex+9>= NUM_FEEDBACK_VERTEX) {
		return;
	}

#if 0	//this wasn't being used (see below) so I commented it out. -MW
	Vector3 lightRay=Normalize(Vector3(-TheGlobalData->m_terrainLightPos[0].x,
		-TheGlobalData->m_terrainLightPos[0].y, -TheGlobalData->m_terrainLightPos[0].z));
#endif

	for (i=0; i<numVertex; i++) {
		curVb->u1 = 0;
		curVb->v1 = 0;
		Vector3 vLoc(pVert[i]);
		vLoc *= MeshMoldOptions::getScale();
		vLoc.Rotate_Z(MeshMoldOptions::getAngle()*PI/180.0f);
		vLoc.X += m_feedbackPoint.x;
		vLoc.Y += m_feedbackPoint.y;
		vLoc.Z += m_feedbackPoint.z;
		curVb->x = vLoc.X;
		curVb->y = vLoc.Y;
		curVb->z = vLoc.Z;

		VertexFormatXYZDUV2 vb;
		vb.x = vLoc.X;
		vb.y = vLoc.Y;
		vb.z = vLoc.Z;

#if 1
		curVb->diffuse = 0x0000ffff | (theAlpha << 24);		// bright cyan.
#else
		TheTerrainRenderObject->doTheLight(&vb, &lightRay, (Vector3 *)(&pNormal[i]), nullptr, 1.0f);
		vb.diffuse &= 0x0000ffff;
		curVb->diffuse = vb.diffuse | (theAlpha << 24);
#endif
		curVb++;
		m_feedbackVertexCount++;
	}
	// Put in the "center anchor"

	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x;
	curVb->y = m_feedbackPoint.y;
	curVb->z = 0;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x+1;
	curVb->y = m_feedbackPoint.y+1;
	curVb->z = m_feedbackPoint.z;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x;
	curVb->y = m_feedbackPoint.y;
	curVb->z = m_feedbackPoint.z-500;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x+1;
	curVb->y = m_feedbackPoint.y+1;
	curVb->z = m_feedbackPoint.z-500;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;


	Int numPoly = m_moldMesh->Get_Model()->Get_Polygon_Count();
	const TriIndex *pPoly =m_moldMesh->Get_Model()->Get_Polygon_Array();
	if (3*numPoly+9 >= NUM_FEEDBACK_INDEX) {
		return;
	}

	for (i=0; i<numPoly; i++) {
		*curIb++ = pPoly[i].I;
		*curIb++ = pPoly[i].J;
		*curIb++ = pPoly[i].K;
		m_feedbackIndexCount+=3;
	}
	*curIb++ = m_feedbackVertexCount-2;
	*curIb++ = m_feedbackVertexCount-1;
	*curIb++ = m_feedbackVertexCount-3;
	*curIb++ = m_feedbackVertexCount-2;
	*curIb++ = m_feedbackVertexCount-3;
	*curIb++ = m_feedbackVertexCount-4;

	*curIb++ = m_feedbackVertexCount-3;
	*curIb++ = m_feedbackVertexCount-1;
	*curIb++ = m_feedbackVertexCount-2;
	*curIb++ = m_feedbackVertexCount-4;
	*curIb++ = m_feedbackVertexCount-3;
	*curIb++ = m_feedbackVertexCount-2;
	m_feedbackIndexCount+=12;

}

/** updateRampVB puts the ramps into a vertex buffer. */

void DrawObject::updateRampVB()
{
	const Int theAlpha = 64;

	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

	Int i, j;
	Int widthVerts = 8;
	Int lengthVerts = 8;
	Int numVertex = widthVerts * lengthVerts;

/*
	Generate the rectangle via the function BuildRectFromSegmentAndWidth(...).
	Note that for the rectangular case, this is easy, as we simply step along the line at
	pre-determined step sizes, with no additional calculation. (IE, we can simply perform
	linear interpolation.) However, with the curved case, we will need to recalculate the
	value every step along the way.


	Ultimately, what I'd like to do is to precompute what the terrain is actually going to
	do, and then use the faux-adjusted vertices, but this is much easier to start from. jkmcd
*/
	Coord3D coordBL, coordTL, coordBR, coordTR;
	BuildRectFromSegmentAndWidth(&m_rampStartPoint, &m_rampEndPoint, m_rampWidth,
															 &coordBL, &coordTL, &coordBR, &coordTR);

	Vector3 bl(coordBL.x, coordBL.y, coordBL.z);
	Vector3 tl(coordTL.x, coordTL.y, coordTL.z);
	Vector3 br(coordBR.x, coordBR.y, coordBR.z);
	Vector3 tr(coordTR.x, coordTR.y, coordTR.z);

	for (i = 0; i < numVertex; i++) {
		curVb->u1 = INT_TO_REAL(i % widthVerts) / widthVerts;
		curVb->v1 = INT_TO_REAL(i / lengthVerts) / lengthVerts;

		curVb->diffuse = curVb->diffuse = 0x0000ffff | (theAlpha << 24);		// bright cyan.

		Vector3 vLoc;
		vLoc.X = (br.X - bl.X) * INT_TO_REAL(i % widthVerts) / (widthVerts  - 1) +
						 (tl.X - bl.X) * INT_TO_REAL(i / lengthVerts) / (lengthVerts - 1) + bl.X;

		vLoc.Y = (br.Y - bl.Y) * INT_TO_REAL(i % widthVerts) / (widthVerts - 1) +
						 (tl.Y - bl.Y) * INT_TO_REAL(i / lengthVerts) / (lengthVerts - 1) + bl.Y;

		vLoc.Z = (br.Z - bl.Z) * INT_TO_REAL(i % widthVerts) / (widthVerts - 1) +
						 (tl.Z - bl.Z) * INT_TO_REAL(i / lengthVerts) / (lengthVerts - 1) + bl.Z;

		curVb->x = vLoc.X;
		curVb->y = vLoc.Y;
		curVb->z = vLoc.Z;

		curVb++;
		m_feedbackVertexCount++;
	}

	// Now do the indices
	for (i = 0; i < lengthVerts - 1; ++i) {
		for (j = 0; j < widthVerts - 1; ++j) {
			(*curIb++) = i * lengthVerts + j;
			(*curIb++) = (i + 1) * lengthVerts + j;
			(*curIb++) = (i + 1) * lengthVerts + j + 1;

			(*curIb++) = i * lengthVerts + j;
			(*curIb++) = (i + 1) * lengthVerts + j + 1;
			(*curIb++) = (i) * lengthVerts + j + 1;
			m_feedbackIndexCount += 6;
		}

	}
#if 0
	// Put in the "center anchor"

	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x;
	curVb->y = m_feedbackPoint.y;
	curVb->z = 0;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x+1;
	curVb->y = m_feedbackPoint.y+1;
	curVb->z = m_feedbackPoint.z;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x;
	curVb->y = m_feedbackPoint.y;
	curVb->z = m_feedbackPoint.z-500;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
	curVb->u1 = 0;
	curVb->v1 = 0;
	curVb->x = m_feedbackPoint.x+1;
	curVb->y = m_feedbackPoint.y+1;
	curVb->z = m_feedbackPoint.z-500;
	curVb->diffuse = 0xFFFF0000;  // red.
	curVb++;
	m_feedbackVertexCount++;
#endif
}

/** updateBoundaryVB puts boundaries into m_vertexFeedback. */
void DrawObject::updateBoundaryVB()
{
//	const Int theAlpha = 64;

	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

 	CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
	Int numBoundaries = pDoc->getNumBoundaries();

	Int i, j;
	for (i = 0; i < numBoundaries; ++i) {
		ICoord2D curBoundary;
		pDoc->getBoundary(i, &curBoundary);
		if (curBoundary.x == 0 || curBoundary.y == 0) {
			// do not show feedback, this is a defunct boundary
			continue;
		}

		for (j = 0; j < 4; ++j) {
			Coord3D startPt, endPt;

			if (j == 0) {
				startPt.x = startPt.y = 0;
				startPt.x *= MAP_XY_FACTOR;
				startPt.y *= MAP_XY_FACTOR;
				startPt.z = TheTerrainRenderObject->getHeightMapHeight(startPt.x, startPt.y, nullptr);
				endPt.x = 0;
				endPt.y = curBoundary.y;
				endPt.x *= MAP_XY_FACTOR;
				endPt.y *= MAP_XY_FACTOR;
				endPt.z = TheTerrainRenderObject->getHeightMapHeight(endPt.x, endPt.y, nullptr);
			} else if (j == 1) {
				startPt = endPt;
				endPt.x = curBoundary.x;
				endPt.y = curBoundary.y;
				endPt.x *= MAP_XY_FACTOR;
				endPt.y *= MAP_XY_FACTOR;
				endPt.z = TheTerrainRenderObject->getHeightMapHeight(endPt.x, endPt.y, nullptr);
			} else if (j == 2) {
				startPt = endPt;
				endPt.x = curBoundary.x;
				endPt.y = 0;
				endPt.x *= MAP_XY_FACTOR;
				endPt.y *= MAP_XY_FACTOR;
				endPt.z = TheTerrainRenderObject->getHeightMapHeight(endPt.x, endPt.y, nullptr);
			} else if (j == 3) {
				startPt = endPt;
				endPt.x = 0;
				endPt.y = 0;
				endPt.x *= MAP_XY_FACTOR;
				endPt.y *= MAP_XY_FACTOR;
				endPt.z = TheTerrainRenderObject->getHeightMapHeight(endPt.x, endPt.y, nullptr);
			}

			if (m_feedbackVertexCount + 8 > NUM_FEEDBACK_VERTEX) {
				return;
			}

			if (m_feedbackIndexCount + 12 > NUM_FEEDBACK_INDEX) {
				return;
			}

			Vector3 normal(endPt.x - startPt.x, endPt.y - startPt.y, endPt.z - startPt.z);
			normal.Normalize();
			normal *= LINE_THICKNESS;
			normal.Rotate_Z(PI/2);

			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x+normal.X;
			curVb->y = startPt.y+normal.Y;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;
			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x-normal.X;
			curVb->y = startPt.y-normal.Y;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;
			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = endPt.x+normal.X;
			curVb->y = endPt.y+normal.Y;
			curVb->z = endPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;
			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = endPt.x-normal.X;
			curVb->y = endPt.y-normal.Y;
			curVb->z = endPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;

			*curIb++ = m_feedbackVertexCount-3;
			*curIb++ = m_feedbackVertexCount-1;
			*curIb++ = m_feedbackVertexCount-2;
			*curIb++ = m_feedbackVertexCount-4;
			*curIb++ = m_feedbackVertexCount-3;
			*curIb++ = m_feedbackVertexCount-2;
			m_feedbackIndexCount+=6;

			// draw a little nugget
			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x;
			curVb->y = startPt.y - HANDLE_SIZE;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;

			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x - HANDLE_SIZE;
			curVb->y = startPt.y;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;

			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x;
			curVb->y = startPt.y + HANDLE_SIZE;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;

			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = startPt.x + HANDLE_SIZE;
			curVb->y = startPt.y;
			curVb->z = startPt.z;
			curVb->diffuse = BORDER_COLORS[i % BORDER_COLORS_SIZE ].m_borderColor;
			curVb++;
			m_feedbackVertexCount++;

			*curIb++ = m_feedbackVertexCount - 4;
			*curIb++ = m_feedbackVertexCount - 2;
			*curIb++ = m_feedbackVertexCount - 3;
			*curIb++ = m_feedbackVertexCount - 4;
			*curIb++ = m_feedbackVertexCount - 1;
			*curIb++ = m_feedbackVertexCount - 2;
			m_feedbackIndexCount+=6;
		}
		// need to push handles in heie.
	}
}

// update the ambient sound Vertex buffers.
// We basically just draw a flag using 12 verts.
//	|\
//	|  \
//	|	 /
//	|/
//	||
//	||
static const Int poleHeight = 20;
static const Int poleWidth = 2;
static const Int flagHeight = 10;
static const Int flagWidth = 10;

void DrawObject::updateAmbientSoundVB()
{
	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

	MapObject* mo = MapObject::getFirstMapObject();

	while (mo) {
		if (!mo->getThingTemplate() || (mo->getThingTemplate()->getEditorSorting() != ES_AUDIO)) {
			mo = mo->getNext();
			continue;
		}

		Coord3D startPt = *mo->getLocation();
		startPt.z = TheTerrainRenderObject->getHeightMapHeight(startPt.x, startPt.y, nullptr);

		if (m_feedbackVertexCount + 6 > NUM_FEEDBACK_VERTEX) {
			return;
		}

		if (m_feedbackIndexCount + 12 > NUM_FEEDBACK_INDEX) {
			return;
		}

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x;
		curVb->y = startPt.y;
		curVb->z = startPt.z;
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x;
		curVb->y = startPt.y;
		curVb->z = startPt.z + poleHeight;
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x + poleWidth;
		curVb->y = startPt.y;
		curVb->z = startPt.z + poleHeight;
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x + poleWidth;
		curVb->y = startPt.y;
		curVb->z = startPt.z;
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x;
		curVb->y = startPt.y;
		curVb->z = startPt.z + poleHeight + flagHeight;
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = startPt.x + flagWidth;
		curVb->y = startPt.y;
		curVb->z = startPt.z + poleHeight + (flagHeight / 2);
		curVb->diffuse = 0xFF2525EF;
		++curVb;
		++m_feedbackVertexCount;

		*curIb++ = m_feedbackVertexCount-6;
		*curIb++ = m_feedbackVertexCount-4;
		*curIb++ = m_feedbackVertexCount-5;

		*curIb++ = m_feedbackVertexCount-6;
		*curIb++ = m_feedbackVertexCount-3;
		*curIb++ = m_feedbackVertexCount-4;

		*curIb++ = m_feedbackVertexCount-5;
		*curIb++ = m_feedbackVertexCount-1;
		*curIb++ = m_feedbackVertexCount-2;

		*curIb++ = m_feedbackVertexCount-5;
		*curIb++ = m_feedbackVertexCount-4;
		*curIb++ = m_feedbackVertexCount-1;
		m_feedbackIndexCount += 12;

		mo = mo->getNext();
	}
}

/** updateMeshVB puts waypoint path triangles into m_vertexFeedback. */

void DrawObject::updateWaypointVB()
{
//	const Int theAlpha = 64;

	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

 	CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
	Int i;
	for (i = 0; i<=pDoc->getNumWaypointLinks(); i++) {
		Bool gotLocation=false;
		Coord3D loc1;
		Coord3D loc2;
 		Bool exists;
		Int waypointID1, waypointID2;

		Int k;
		for (k=0; k<2; k++) {
			Bool ok = false;
			pDoc->getWaypointLink(i, &waypointID1, &waypointID2);
			if (k==0 || i==pDoc->getNumWaypointLinks()) {
				ok = (k==0);
			}	else {
				MapObject *pWay = pDoc->getWaypointByID(waypointID1);
				if (pWay) {
					Bool biDirectional = pWay->getProperties()->getBool(TheKey_waypointPathBiDirectional, &exists);
					if (biDirectional) {
						ok = true;
						pDoc->getWaypointLink(i, &waypointID2, &waypointID1);
					}
				}
			}

			if (i==pDoc->getNumWaypointLinks()) {
				if (m_dragWaypointFeedback) {
					loc1 = m_dragWayStart;
					loc2 = m_dragWayEnd;
					gotLocation = true;
				}
			} else {
				MapObject *pWay1, *pWay2;
				pWay1 = pDoc->getWaypointByID(waypointID1);
				pWay2 = pDoc->getWaypointByID(waypointID2);
				if (pWay1 && pWay2) {
					gotLocation = true;
					loc1 = *pWay1->getLocation();
					loc2 = *pWay2->getLocation();
					AsciiString wayLayer;
					wayLayer = pWay1->getProperties()->getAsciiString(TheKey_objectLayer, &exists);
					if (exists && TheLayersList->isLayerHidden(wayLayer)) {
						gotLocation = false;
					}

					wayLayer = pWay2->getProperties()->getAsciiString(TheKey_objectLayer, &exists);
					if (exists && TheLayersList->isLayerHidden(wayLayer)) {
						gotLocation = false;
					}
				}
			}
			if (gotLocation) {

				Vector3 normal(loc2.x-loc1.x, loc2.y-loc1.y, loc2.z-loc1.z);
				normal.Normalize();
				normal *= 0.5f;
				// Rotate the normal 90 degrees.
				normal.Rotate_Z(PI/2);
				loc1.z = TheTerrainRenderObject->getHeightMapHeight(loc1.x, loc1.y, nullptr);
				loc2.z = TheTerrainRenderObject->getHeightMapHeight(loc2.x, loc2.y, nullptr);

				if (m_feedbackVertexCount+9>= NUM_FEEDBACK_VERTEX) {
					return;
				}
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x+normal.X;
				curVb->y = loc1.y+normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFF000000;  // black.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x-normal.X;
				curVb->y = loc1.y-normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFF000000;  // black.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x+normal.X;
				curVb->y = loc2.y+normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x-normal.X;
				curVb->y = loc2.y-normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;

				if (m_feedbackIndexCount+12 >= NUM_FEEDBACK_INDEX) {
					return;
				}
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-1;
				*curIb++ = m_feedbackVertexCount-2;
				*curIb++ = m_feedbackVertexCount-4;
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-2;
				m_feedbackIndexCount+=6;

				// Do arrowhead.
				Vector3 vec(loc2.x-loc1.x, loc2.y-loc1.y, loc2.z-loc1.z);
				vec.Normalize();
				const Real ARROWHEAD_LEN = 10.0f;
				const Real NORMAL_SHIFT = 6.0f;

				vec *=ARROWHEAD_LEN;
				loc1.x = loc2.x - vec.X;
				loc1.y = loc2.y - vec.Y;
				loc1.z = loc2.z - vec.Z;
				if (m_feedbackVertexCount+9>= NUM_FEEDBACK_VERTEX) {
					return;
				}
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x+NORMAL_SHIFT*normal.X+normal.X;
				curVb->y = loc1.y+NORMAL_SHIFT*normal.Y+normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x+NORMAL_SHIFT*normal.X;
				curVb->y = loc1.y+NORMAL_SHIFT*normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x+normal.X;
				curVb->y = loc2.y+normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x-normal.X;
				curVb->y = loc2.y-normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;

				if (m_feedbackIndexCount+12 >= NUM_FEEDBACK_INDEX) {
					return;
				}
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-1;
				*curIb++ = m_feedbackVertexCount-2;
				*curIb++ = m_feedbackVertexCount-4;
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-2;
				m_feedbackIndexCount+=6;

				if (m_feedbackVertexCount+9>= NUM_FEEDBACK_VERTEX) {
					return;
				}
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x-NORMAL_SHIFT*normal.X;
				curVb->y = loc1.y-NORMAL_SHIFT*normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc1.x-NORMAL_SHIFT*normal.X-normal.X;
				curVb->y = loc1.y-NORMAL_SHIFT*normal.Y-normal.Y;
				curVb->z = loc1.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x+normal.X;
				curVb->y = loc2.y+normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;
				curVb->u1 = 0;
				curVb->v1 = 0;
				curVb->x = loc2.x-normal.X;
				curVb->y = loc2.y-normal.Y;
				curVb->z = loc2.z;
				curVb->diffuse = 0xFFFF0000;  // red.
				curVb++;
				m_feedbackVertexCount++;

				if (m_feedbackIndexCount+12 >= NUM_FEEDBACK_INDEX) {
					return;
				}
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-1;
				*curIb++ = m_feedbackVertexCount-2;
				*curIb++ = m_feedbackVertexCount-4;
				*curIb++ = m_feedbackVertexCount-3;
				*curIb++ = m_feedbackVertexCount-2;
				m_feedbackIndexCount+=6;
			}
		}
	}
}

/** updateMeshVB puts polygon trigger triangles into m_vertexFeedback. */

void DrawObject::updatePolygonVB(PolygonTrigger *pTrig, Bool selected, Bool isOpen)
{
//	const Int theAlpha = 64;

	Int green = 0;
	if (selected) {
		green = (255*curHighlight) / (NUM_HIGHLIGHT-1);
	}
	green = green<<8;
	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

	Int i;
	for (i=0; i<pTrig->getNumPoints(); i++) {
		Coord3D loc1;
		Coord3D loc2;
		ICoord3D iLoc = *pTrig->getPoint(i);
		loc1.x = iLoc.x;
		loc1.y = iLoc.y;
		loc1.z = TheTerrainRenderObject->getHeightMapHeight(loc1.x, loc1.y, nullptr);
		if (i<pTrig->getNumPoints()-1) {
			iLoc = *pTrig->getPoint(i+1);
		} else {
			if (isOpen) break;
			iLoc = *pTrig->getPoint(0);
		}
		loc2.x = iLoc.x;
		loc2.y = iLoc.y;
		loc2.z = TheTerrainRenderObject->getHeightMapHeight(loc2.x, loc2.y, nullptr);
		Vector3 normal(loc2.x-loc1.x, loc2.y-loc1.y, loc2.z-loc1.z);
		normal.Normalize();
		normal *= 0.5f;
		// Rotate the normal 90 degrees.
		normal.Rotate_Z(PI/2);
		// Put in the "center anchor"

		if (m_feedbackVertexCount+9>= NUM_FEEDBACK_VERTEX) {
			return;
		}
		Int diffuse = 0xFFFF0000+green;
		if (pTrig->isWaterArea()) {
			diffuse = 0xFF0000FF+green;
		}
		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = loc1.x+normal.X;
		curVb->y = loc1.y+normal.Y;
		curVb->z = loc1.z;
		curVb->diffuse = diffuse;
		curVb++;
		m_feedbackVertexCount++;
		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = loc1.x-normal.X;
		curVb->y = loc1.y-normal.Y;
		curVb->z = loc1.z;
		curVb->diffuse = diffuse;
		curVb++;
		m_feedbackVertexCount++;
		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = loc2.x+normal.X;
		curVb->y = loc2.y+normal.Y;
		curVb->z = loc2.z;
		curVb->diffuse = diffuse;
		curVb++;
		m_feedbackVertexCount++;
		curVb->u1 = 0;
		curVb->v1 = 0;
		curVb->x = loc2.x-normal.X;
		curVb->y = loc2.y-normal.Y;
		curVb->z = loc2.z;
		curVb->diffuse = diffuse;
		curVb++;
		m_feedbackVertexCount++;

		if (m_feedbackIndexCount+12 >= NUM_FEEDBACK_INDEX) {
			return;
		}
		*curIb++ = m_feedbackVertexCount-3;
		*curIb++ = m_feedbackVertexCount-1;
		*curIb++ = m_feedbackVertexCount-2;
		*curIb++ = m_feedbackVertexCount-4;
		*curIb++ = m_feedbackVertexCount-3;
		*curIb++ = m_feedbackVertexCount-2;
		m_feedbackIndexCount+=6;

	}
}


/** updateFeedbackVB puts brush feedback triangles into m_vertexFeedback. */

void DrawObject::updateFeedbackVB()
{
	const Int theAlpha = 64;
	m_feedbackVertexCount = 0;
	m_feedbackIndexCount = 0;
	DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexFeedback, D3DLOCK_DISCARD);
	UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
	UnsignedShort *curIb = ib;

	DX8VertexBufferClass::WriteLockClass lockVtxBuffer(m_vertexFeedback, D3DLOCK_DISCARD);
	VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();
	VertexFormatXYZDUV1 *curVb = vb;

	Bool doubleResolution = 0;
	Int brushWidth = m_brushWidth;
	Int featherWidth = m_brushFeatherWidth;

	Int shadeR, shadeG, shadeB;
	shadeR = 0;
	shadeG = 125;
	shadeB = 255;
	Int diffuse=shadeB | (shadeG << 8) | (shadeR << 16) | (theAlpha << 24);
	Int featherDiffuse = (shadeG << 8) ;
	Real radius = m_brushWidth/2.0 + m_brushFeatherWidth;
	// The tile tools show a plain square; the height brushes show their shape and feather.
	Bool solidSquare = (m_feedbackShape == BRUSH_SHAPE_SQUARE && m_brushFeatherWidth == 0);

	if (!solidSquare) {
		if (radius < MAX_RADIUS/2) {
			brushWidth = brushWidth*2;
			featherWidth = featherWidth*2;
			doubleResolution = true;
			radius = brushWidth/2.0 + featherWidth;
		}
		radius++;
	}

	CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
	WorldHeightMapEdit *pMap = pDoc->GetHeightMap();
#define ADJUST_FROM_INDEX_TO_REAL(k) ((k-pMap->getBorderSize())*MAP_XY_FACTOR)

	// Big brushes sample every step-th vertex so the grid still fits the buffers.
	Int step = 1;
	if (radius > MAX_RADIUS) step = (Int)ceil(radius/MAX_RADIUS);
	Real offset = 0;
	if (m_brushWidth&1) offset = 0.5f;
	Int minX = floor(m_cellCenter.x-radius+offset);
	Int minY = floor(m_cellCenter.y-radius+offset);
	Int numSamples = (Int)(2*radius/step) + 1;
	Int gi, gj;
	for (gj=0; gj<numSamples; gj++) {
		Int j = minY + gj*step;
		for (gi=0; gi<numSamples; gi++) {
			Int i = minX + gi*step;
			if (m_feedbackVertexCount >= NUM_FEEDBACK_VERTEX) return;
			if (solidSquare) {
				curVb->diffuse = diffuse;
			} else {
				Real blendFactor = Tool::calcBlendFactor(m_feedbackShape, m_cellCenter, i, j, brushWidth, featherWidth);
				if (blendFactor > 0.99) {
					curVb->diffuse = diffuse;
				} else if (blendFactor > 0.05) {
					curVb->diffuse = featherDiffuse | (theAlpha<<24);
				}	else {
					curVb->diffuse = 0;
				}
			}
			Real X, Y, theZ;
			if (doubleResolution) {
				X = ADJUST_FROM_INDEX_TO_REAL(i)/2.0f + ADJUST_FROM_INDEX_TO_REAL(2*offset+m_cellCenter.x)  / 2.0;
				Y = ADJUST_FROM_INDEX_TO_REAL(j)/2.0f + ADJUST_FROM_INDEX_TO_REAL(2*offset+m_cellCenter.y)  / 2.0;
				theZ = TheTerrainRenderObject->getHeightMapHeight(X, Y, nullptr);
			} else {
				X = ADJUST_FROM_INDEX_TO_REAL(i);
				Y = ADJUST_FROM_INDEX_TO_REAL(j);
				theZ = TheTerrainRenderObject->getHeightMapHeight(X, Y, nullptr);
			}
			curVb->u1 = 0;
			curVb->v1 = 0;
			curVb->x = X;
			curVb->y = Y;
			curVb->z = theZ;
			curVb++;
			m_feedbackVertexCount++;
		}
	}
	Int yOffset = numSamples;
	Int halfWidth = yOffset/2;
	Int i, j;
	for (j=0; j<numSamples-1; j++) {
		for (i=0; i<numSamples-1; i++) {
			if (m_feedbackIndexCount+6 > NUM_FEEDBACK_INDEX) return;
			{
				Bool flipForBlend = false;
				if (i>=halfWidth && j>=halfWidth) flipForBlend = true;
				if (i<halfWidth && j<halfWidth) flipForBlend = true;
				if (flipForBlend) {
					*curIb++ = j*yOffset + i+1;
 					*curIb++ = j*yOffset + i+yOffset;
					*curIb++ = j*yOffset + i;
 					*curIb++ = j*yOffset + i+1;
 					*curIb++ = j*yOffset + i+1+yOffset;
					*curIb++ = j*yOffset + i+yOffset;
				} else {
					*curIb++ = j*yOffset + i;
					*curIb++ = j*yOffset + i+1+yOffset;
					*curIb++ = j*yOffset + i+yOffset;
					*curIb++ = j*yOffset + i;
					*curIb++ = j*yOffset + i+1;
					*curIb++ = j*yOffset + i+1+yOffset;
				}
			}
			m_feedbackIndexCount+=6;
		}
	}
}


/** Calculate the sign of the cross product.  If the tails of the vectors are both placed
at 0,0, then the cross product can be interpreted as -1 means v2 is to the right of v1,
1 means v2 is to the left of v1, and 0 means v2 is parallel to v1. */

static Int xpSign(const ICoord3D &v1, const ICoord3D &v2) {
	Real xpdct = (Real)v1.x*v2.y - (Real)v1.y*v2.x;
	if (xpdct<0) return -1;
	if (xpdct>0) return 1;
	return 0;
}


/** updateForWater puts a blue rectangle into the vertex buffer. */

void DrawObject::updateForWater()
{
}

/* This is a code snippet that starts to attempt to solve the concave area problem,
but doesn't, really.
				const Int maxPoints = 256;
				Bool pointFlags[256];
				Int numPoints = pTrig->getNumPoints();
				ICoord3D pLL1, pLL2, pLL3;
				if (numPoints < 3) continue;
				pLL1 = *pTrig->getPoint(numPoints-1);
				pLL2 = *pTrig->getPoint(0);
				pLL3 = *pTrig->getPoint(1);
				pointFlags[0] = true;
				for (k=1; k<numPoints; k++) {
					pointFlags[k] = true;
					ICoord3D pt = *pTrig->getPoint(k);
					if (pt.y < pLL2.y || (pt.y==pLL2.y && pt.x<pLL2.x) ) {
						pLL2 = pt;
						pLL1 = *pTrig->getPoint(k-1);
						if (k<numPoints-1) {
							pLL3 = *pTrig->getPoint(k+1);
						} else {
							pLL3 = *pTrig->getPoint(0);
						}
					}
				}
				ICoord3D v1, v2;
				v1.x = pLL2.x-pLL1.x;
				v1.y = pLL2.y-pLL1.y;
				v1.z = 0;
				v2.x = pLL3.x-pLL2.x;
				v2.y = pLL3.y-pLL2.y;
				v2.z = 0;
				Int windingXpdct = xpSign(v1, v2);
				if (windingXpdct == 0) windingXpdct = -1;
				Bool didSomething = true;
				while (didSomething) {
					didSomething = false;

					for (k=0; k<pTrig->getNumPoints()-1; k++) {
						if (!pointFlags[k]) continue;
						Int kPlus1;
						for (kPlus1 = k+1; kPlus1 < pTrig->getNumPoints()-1; kPlus1++) {
							if (pointFlags[kPlus1]) break;
						}
						if (kPlus1 >= pTrig->getNumPoints()-1) continue;
						Int kPlus2 = kPlus1+1;
						for (kPlus2 = kPlus1+1; kPlus2 < pTrig->getNumPoints(); kPlus2++) {
							if (pointFlags[kPlus2]) break;
						}

						ICoord3D pt1 = *pTrig->getPoint(k);
						ICoord3D pt2 = *pTrig->getPoint(kPlus1);
						ICoord3D pt3 = *pTrig->getPoint(kPlus2);


*/


/** updateVB puts a circle with an arrow into the vertex buffer. */

Int DrawObject::updateVB(DX8VertexBufferClass	*pVB, Int color, Bool doArrow, Bool doDiamond)
{
	Int i, k;

	Real factor = TheGlobalData->m_terrainAmbient[0].red +
								TheGlobalData->m_terrainAmbient[0].green +
								TheGlobalData->m_terrainAmbient[0].blue;
	if (factor > 1.0f) factor = 1.0f;
	Int r = color&0xFF;
	Int g = (color&0x00FF00)>>8;
	Int b = (color&0xFF0000)>>16;

	r *= factor;
	g *= factor;
	b *= factor;
	const Int theAlpha = 127;
	static const Int highlightColors[NUM_HIGHLIGHT] = { ((255<<8) + (255<<16)) ,
				((255<<16)), (255<<8) };
	Int diffuse =  b + (g<<8) + (r<<16) + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
	if (pVB )
	{

		DX8VertexBufferClass::WriteLockClass lockVtxBuffer(pVB, D3DLOCK_DISCARD);
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Vertex_Array();

		const Real theZ = 0.0f;
		Real theRadius = THE_RADIUS;
		Real halfLineWidth = 0.03f*MAP_XY_FACTOR;
		if (doDiamond) {
			theRadius *= 5.0;
		}
		else
		{
			theRadius *= 2.0;
		}

		Int limit = NUM_TRI-(NUM_ARROW_TRI+NUM_SELECT_TRI);
		float curAngle = 0;
		float deltaAngle = 2*PI/limit;
		if (doDiamond) {
			deltaAngle = PI/2;
		}
		for (i=0; i<limit; i++)
		{
			for (k=0; k<3; k++) {
				vb->z=  theZ;
				if (k==0) {
					vb->x=	0;
					vb->y=	0;

					Vector3 vec(0,0,theZ);
					vec.Rotate_Z(curAngle+(deltaAngle/2));
					vb->x=	vec.X;
					vb->y=	vec.Y;
				} else if (k==1) {
					Vector3 vec(theRadius/10,0,theZ);
					vec.Rotate_Z(curAngle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				} else if (k==2) {
					Real angle = curAngle+deltaAngle;
					if (i==limit-1) {
						angle = 0;
					}
					Vector3 vec(theRadius/10,0,theZ);
					vec.Rotate_Z(angle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				}
				vb->diffuse=diffuse;
				vb->u1=0;
				vb->v1=0;
				vb[3*NUM_TRI] = *vb;
				if (k==0) {
					vb[3*NUM_TRI].z += 3.0;
					vb[3*NUM_TRI].diffuse = diffuse;
				}
				vb++;
			}
			curAngle += deltaAngle;
		}

		if (!doDiamond) {
			theRadius /= 2.0;
		}

		if (!doArrow) {
			theRadius /= 20;
			halfLineWidth /= 20;
		}
		/* Now do the arrow. */
		for (k=0; k<3; k++) {
			vb->x=	(k&1)?2*theRadius:0.0f;
			vb->y=	-halfLineWidth + ((k&2)?2*halfLineWidth:0);
			vb->z=  theZ;
			vb->diffuse=highlightColors[curHighlight] + (theAlpha<<24);
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}
		for (k=0; k<3; k++) {
			vb->x=	(k&1)?0.0f:2*theRadius;
			vb->y=	halfLineWidth - ((k&2)?2*halfLineWidth:0);
			vb->z=  theZ;
			vb->diffuse=highlightColors[curHighlight] + (theAlpha<<24);
			vb->u1=0;
			vb->v1=0;
			vb++;
		}
		for (k=0; k<3; k++) {
			if (k==0) { vb->x=theRadius; vb->y = 0;}
			else if (k==1) { vb->x=2*theRadius + 2*halfLineWidth; vb->y = 0;}
			else { vb->x=theRadius; vb->y = 2*halfLineWidth;}
			vb->z=  theZ;
			vb->diffuse=highlightColors[curHighlight] + (theAlpha<<24);
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}
		for (k=0; k<3; k++) {
			if (k==0) { vb->x=theRadius; vb->y = 0;}
			else if (k==1) { vb->x=theRadius; vb->y = -2*halfLineWidth;}
			else { vb->x=2*theRadius + 2*halfLineWidth; vb->y = 0;}
			vb->z=  theZ;
			vb->diffuse=highlightColors[curHighlight] + (theAlpha<<24);
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}

		if (!doArrow) {
			theRadius *= 20;
			halfLineWidth *= 20;
		}


		limit = NUM_SELECT_TRI;
		curAngle = 0;
		deltaAngle = 2*PI/limit;
		if (doDiamond) {
			theRadius/=5.0f;
		}
		for (i=0; i<limit; i++)
		{
			for (k=0; k<3; k++) {
				vb->z=  theZ;
				if (k==0) {
					vb->x=	0;
					vb->y=	0;

					Vector3 vec(theRadius*4/5,0,theZ);
					vec.Rotate_Z(curAngle+(deltaAngle/2));
					vb->x=	vec.X;
					vb->y=	vec.Y;
				} else if (k==1) {
					Vector3 vec(theRadius,0,theZ);
					vec.Rotate_Z(curAngle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				} else if (k==2) {
					Real angle = curAngle+deltaAngle;
					if (i==limit-1) {
						angle = 0;
					}
					Vector3 vec(theRadius,0,theZ);
					vec.Rotate_Z(angle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				}
				vb->diffuse = highlightColors[curHighlight] + (theAlpha<<24);
				vb->u1=0;
				vb->v1=0;
				vb[3*NUM_TRI] = *vb;
				if (k==0) {
					vb[3*NUM_TRI].z += 3.0;
					vb[3*NUM_TRI].diffuse = highlightColors[curHighlight] + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
				}
				vb++;
			}
			curAngle += deltaAngle;

		}

#if 0
		// Now do the highlight triangle.  This is in yellow.
		for (k=0; k<3; k++) {
			vb->x = k==0?theRadius:0;
			vb->y = k==1?theRadius:0;
			vb->z=  k==2?theZ+SELECT_PYRAMID_HEIGHT:theZ;
			vb->diffuse= highlightColors[curHighlight] + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}
		for (k=0; k<3; k++) {
			vb->x = k==1?-theRadius:0;
			vb->y = k==0?theRadius:0;
			vb->z=  k==2?theZ+SELECT_PYRAMID_HEIGHT:theZ;
			vb->diffuse= highlightColors[curHighlight] + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}

		for (k=0; k<3; k++) {
			vb->x = k==1?theRadius:0;
			vb->y = k==0?-theRadius:0;
			vb->z=  k==2?theZ+SELECT_PYRAMID_HEIGHT:theZ;
			vb->diffuse= highlightColors[curHighlight] + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}
		for (k=0; k<3; k++) {
			vb->x = k==0?-theRadius:0;
			vb->y = k==1?-theRadius:0;
			vb->z=  k==2?theZ+SELECT_PYRAMID_HEIGHT:theZ;
			vb->diffuse= highlightColors[curHighlight] + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
			vb->u1=0;
			vb->v1=0;
			vb[3*NUM_TRI] = *vb;
			vb++;
		}
#endif
		return 0; //success.
	}
	return -1;
}

#define BOUNDING_BOX_LINE_WIDTH 2.0f
/** Draw an object's bounding box into the vertex buffer. **/
// MLL C&C3
void DrawObject::updateVBWithBoundingBox(MapObject *pMapObj, CameraClass* camera)
{
	if (!pMapObj || !m_lineRenderer || !pMapObj->getThingTemplate()) {
		return;
	}

	unsigned long color = 0xFFAA00AA; // Purple

	GeometryInfo ginfo = pMapObj->getThingTemplate()->getTemplateGeometryInfo();

	Coord3D pos = *pMapObj->getLocation();
	if (TheTerrainRenderObject) {
		// Make sure that the position is on the terrain.
		pos.z += TheTerrainRenderObject->getHeightMapHeight(pos.x, pos.y, nullptr);
	}

#ifdef USE_ADVANCED_COLLISION
	// one outline per shape of a compound geometry
	for (Int shapeIndex = 0; shapeIndex < ginfo.getShapeCount(); ++shapeIndex)
	{
		GeometryPrimitive prim;
		ginfo.getWorldPrimitive(shapeIndex, pos, pMapObj->getAngle(), prim);
		const Coord3D shapePos = prim.position;

		switch (prim.type)
		{
			//---------------------------------------------------------------------------------------------
			case GEOMETRY_BOX:
			{
				Real angle = prim.angle;
				Real c = (Real)cos(angle);
				Real s = (Real)sin(angle);
				Real exc = prim.majorRadius*c;
				Real eyc = prim.minorRadius*c;
				Real exs = prim.majorRadius*s;
				Real eys = prim.minorRadius*s;
				Coord3D pts[4];
				pts[0].x = shapePos.x - exc - eys;
				pts[0].y = shapePos.y + eyc - exs;
				pts[0].z = 0;
				pts[1].x = shapePos.x + exc - eys;
				pts[1].y = shapePos.y + eyc + exs;
				pts[1].z = 0;
				pts[2].x = shapePos.x + exc + eys;
				pts[2].y = shapePos.y - eyc + exs;
				pts[2].z = 0;
				pts[3].x = shapePos.x - exc + eys;
				pts[3].y = shapePos.y - eyc - exs;
				pts[3].z = 0;
				Real z = shapePos.z;
				for (int i = 0; i < 2; i++) {
					for (int corner = 0; corner < 4; corner++) {
						ICoord2D start, end;
						pts[corner].z = z;
						pts[(corner+1)&3].z = z;
						bool shouldStart = worldToScreen(&pts[corner], &start, camera);
						bool shouldEnd = worldToScreen(&pts[(corner+1)&3], &end, camera);
						if (shouldStart && shouldEnd) {
							m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
						}
					}

					z += prim.getMaxHeightAbovePosition();
				}
				break;
			}

			//---------------------------------------------------------------------------------------------
			case GEOMETRY_SPHERE:	// not quite right, but close enough
			case GEOMETRY_CYLINDER:
			{
				Real angle, inc = PI/4.0f;
				Real radius = prim.majorRadius;
				Coord3D pnt, lastPnt;
				ICoord2D start, end;
				Real z = shapePos.z;

				bool shouldEnd, shouldStart;
				// Draw the cylinder.
				for (int i=0; i<2; i++) {
					angle = 0.0f;
					lastPnt.x = shapePos.x + radius * (Real)cos(angle);
					lastPnt.y = shapePos.y + radius * (Real)sin(angle);
					lastPnt.z = z;
					shouldEnd = worldToScreen(&lastPnt, &end, camera);

					for( angle = inc; angle <= 2.0f * PI; angle += inc ) {
						pnt.x = shapePos.x + radius * (Real)cos(angle);
						pnt.y = shapePos.y + radius * (Real)sin(angle);
						pnt.z = z;
						shouldStart = worldToScreen(&pnt, &start, camera);
						if (shouldStart && shouldEnd) {
							m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
						}
						lastPnt = pnt;
						end = start;
						shouldEnd = shouldStart;
					}

					// Next time around, draw the top of the cylinder.
					z += prim.getMaxHeightAbovePosition();
				}

				// Draw centerline
				pnt.x = shapePos.x;
				pnt.y = shapePos.y;
				pnt.z = shapePos.z;
				shouldStart = worldToScreen( &pnt, &start, camera);
				pnt.z = shapePos.z + prim.getMaxHeightAbovePosition();
				shouldEnd = worldToScreen( &pnt, &end, camera);
				if (shouldStart && shouldEnd) {
					m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
				}
				break;
			}
		}
	}
#else
	switch (ginfo.getGeomType())
	{
		//---------------------------------------------------------------------------------------------
		case GEOMETRY_BOX:
		{
			Real angle = pMapObj->getAngle();
			Real c = (Real)cos(angle);
			Real s = (Real)sin(angle);
			Real exc = ginfo.getMajorRadius()*c;
			Real eyc = ginfo.getMinorRadius()*c;
			Real exs = ginfo.getMajorRadius()*s;
			Real eys = ginfo.getMinorRadius()*s;
			Coord3D pts[4];
			pts[0].x = pos.x - exc - eys;
			pts[0].y = pos.y + eyc - exs;
			pts[0].z = 0;
			pts[1].x = pos.x + exc - eys;
			pts[1].y = pos.y + eyc + exs;
			pts[1].z = 0;
			pts[2].x = pos.x + exc + eys;
			pts[2].y = pos.y - eyc + exs;
			pts[2].z = 0;
			pts[3].x = pos.x - exc + eys;
			pts[3].y = pos.y - eyc - exs;
			pts[3].z = 0;
			Real z = pos.z;
			for (int i = 0; i < 2; i++) {
				for (int corner = 0; corner < 4; corner++) {
					ICoord2D start, end;
					pts[corner].z = z;
					pts[(corner+1)&3].z = z;
					bool shouldStart = worldToScreen(&pts[corner], &start, camera);
					bool shouldEnd = worldToScreen(&pts[(corner+1)&3], &end, camera);
					if (shouldStart && shouldEnd) {
						m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
					}
				}

				z += ginfo.getMaxHeightAbovePosition();
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GEOMETRY_SPHERE:	// not quite right, but close enough
		case GEOMETRY_CYLINDER:
		{
			Real angle, inc = PI/4.0f;
			Real radius = ginfo.getMajorRadius();
			Coord3D pnt, lastPnt;
			ICoord2D start, end;
			Real z = pos.z;

			bool shouldEnd, shouldStart;
			// Draw the cylinder.
			for (int i=0; i<2; i++) {
				angle = 0.0f;
				lastPnt.x = pos.x + radius * (Real)cos(angle);
				lastPnt.y = pos.y + radius * (Real)sin(angle);
				lastPnt.z = z;
				shouldEnd = worldToScreen(&lastPnt, &end, camera);

				for( angle = inc; angle <= 2.0f * PI; angle += inc ) {
					pnt.x = pos.x + radius * (Real)cos(angle);
					pnt.y = pos.y + radius * (Real)sin(angle);
					pnt.z = z;
					shouldStart = worldToScreen(&pnt, &start, camera);
					if (shouldStart && shouldEnd) {
						m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
					}
					lastPnt = pnt;
					end = start;
					shouldEnd = shouldStart;
				}

				// Next time around, draw the top of the cylinder.
				z += ginfo.getMaxHeightAbovePosition();
			}

			// Draw centerline
			pnt.x = pos.x;
			pnt.y = pos.y;
			pnt.z = pos.z;
			shouldStart = worldToScreen( &pnt, &start, camera);
			pnt.z = pos.z + ginfo.getMaxHeightAbovePosition();
			shouldEnd = worldToScreen( &pnt, &end, camera);
			if (shouldStart && shouldEnd) {
				m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), BOUNDING_BOX_LINE_WIDTH, color);
			}
			break;
		}
	}
#endif
}

/** Draw a "circle" into the m_lineRenderer, e.g. to visualize weapon range, sight range, sound range **/
void DrawObject::addCircleToLineRenderer( const Coord3D & center, Real radius, Real width, unsigned long color, CameraClass* camera )
{
  Real angle, inc = PI/4.0f;
  Coord3D pnt, lastPnt;
  ICoord2D start, end;
  Real z = center.z;

  // Draw the circle.
  angle = 0.0f;
  lastPnt.x = center.x + radius * (Real)cos(angle);
  lastPnt.y = center.y + radius * (Real)sin(angle);
  lastPnt.z = z;
  bool shouldEnd = worldToScreen(&lastPnt, &end, camera);

  for( angle = inc; angle <= 2.0f * PI; angle += inc ) {
    pnt.x = center.x + radius * (Real)cos(angle);
    pnt.y = center.y + radius * (Real)sin(angle);
    pnt.z = z;

    bool shouldStart = worldToScreen(&pnt, &start, camera);
    if (shouldStart && shouldEnd) {
      m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), width, color);
    }

    lastPnt = pnt;
    end = start;
    shouldEnd = shouldStart;
  }

}

#define SIGHT_RANGE_LINE_WIDTH 2.0f
/** Draw an object's sight range into the vertex buffer. **/
// MLL C&C3
void DrawObject::updateVBWithSightRange(MapObject *pMapObj, CameraClass* camera)
{
	if (!pMapObj || !m_lineRenderer || !pMapObj->getThingTemplate()) {
		return;
	}

	const unsigned long color = 0xFFF0F0F0; // Light blue.

	Real radius = pMapObj->getThingTemplate()->friend_calcVisionRange();

	Coord3D pos = *pMapObj->getLocation();
	if (TheTerrainRenderObject) {
		// Make sure that the position is on the terrain.
		pos.z += TheTerrainRenderObject->getHeightMapHeight(pos.x, pos.y, nullptr);
	}

  addCircleToLineRenderer(pos, radius, SIGHT_RANGE_LINE_WIDTH, color, camera );
}

#define WEAPON_RANGE_LINE_WIDTH 1.0f
/** Draw an object's weapon range into the vertex buffer. **/
// MLL C&C3
void DrawObject::updateVBWithWeaponRange(MapObject *pMapObj, CameraClass* camera)
{
	if (!pMapObj || !m_lineRenderer || !pMapObj->getThingTemplate()) {
		return;
	}

  // const unsigned long colors[WEAPONSLOT_COUNT] = {0xFF00FF00, 0xFFE0F00A, 0xFFFF0000}; // Green, Yellow, Red
	const unsigned long colors[WEAPONSLOT_COUNT] = {
	  0xFF00FF00, // Green
	  0xFFFFFF00, // Yellow
	  0xFFFF0000, // Red
	  0xFF0000FF, // Blue
	  0xFF00FFFF, // Cyan
	  0xFFFF00FF, // Magenta
	  0xFF000000, // Black
	  0xFFFFFFFF  // White
	};


	Coord3D pos = *pMapObj->getLocation();
	if (TheTerrainRenderObject) {
		// Make sure that the position is on the terrain.
		pos.z += TheTerrainRenderObject->getHeightMapHeight(pos.x, pos.y, nullptr);
	}

	const WeaponTemplateSetVector& weapons = pMapObj->getThingTemplate()->getWeaponTemplateSets();

	for (WeaponTemplateSetVector::const_iterator it = weapons.begin(); it != weapons.end(); ++it)	{
		if (it->hasAnyWeapons() == false) {
			continue;
		}

		for (int i = 0; i < WEAPONSLOT_COUNT; i++) {
			const WeaponTemplate* tmpl = it->getNth((WeaponSlotType)i);

			if (tmpl == nullptr) {
				continue;
			}

			Real radius = tmpl->getUnmodifiedAttackRange();

      addCircleToLineRenderer(pos, radius, WEAPON_RANGE_LINE_WIDTH, colors[i], camera );
		}
	}
}

#define SOUND_RANGE_LINE_WIDTH 1.0f
/** Draw an object's min & max sound ranges into the vertex buffer. **/
// MLL C&C3
void DrawObject::updateVBWithSoundRanges(MapObject *pMapObj, CameraClass* camera)
{
  if (!pMapObj || !m_lineRenderer) {
    return;
  }

  const unsigned long colors[2] = {0xFF0000FF, 0xFFFF00FF}; // Blue and purple
                                                            // Colors match those used in W3DView.cpp


  Coord3D pos = *pMapObj->getLocation();
  if (TheTerrainRenderObject) {
    // Make sure that the position is on the terrain.
    pos.z += TheTerrainRenderObject->getHeightMapHeight(pos.x, pos.y, nullptr);
  }

  // Does this object actually have an attached sound?
  const AudioEventInfo * audioInfo = nullptr;

  Dict * properties = pMapObj->getProperties();

  Bool exists = false;
  AsciiString ambientName = properties->getAsciiString( TheKey_objectSoundAmbient, &exists );

  if ( exists )
  {
    if ( ambientName.isEmpty() )
    {
      // User has removed normal sound
      return;
    }
    else
    {
      if ( TheAudio == nullptr )
      {
        DEBUG_CRASH( ("TheAudio is null! Can't draw sound circles") );
        return;
      }

      audioInfo = TheAudio->findAudioEventInfo( ambientName );

      if ( audioInfo == nullptr )
      {
        DEBUG_CRASH( ("Override audio named %s is missing; Can't draw sound circles", ambientName.str() ) );
        return;
      }
    }
  }
  else
  {
    const ThingTemplate * thingTemplate = pMapObj->getThingTemplate();
    if ( thingTemplate == nullptr )
    {
      // No sound if no template
      return;
    }

    if ( !thingTemplate->hasSoundAmbient() )
    {
      return;
    }

    const AudioEventRTS * event = thingTemplate->getSoundAmbient();

    if ( event == nullptr )
    {
      return;
    }

    audioInfo = event->getAudioEventInfo();

    if ( audioInfo == nullptr )
    {
      // May just not be set up yet
      if ( TheAudio == nullptr )
      {
        DEBUG_CRASH( ("TheAudio is null! Can't draw sound circles") );
        return;
      }

      audioInfo = TheAudio->findAudioEventInfo( event->getEventName() );

      if ( audioInfo == nullptr )
      {
        DEBUG_CRASH( ("Default ambient sound %s has no info; Can't draw sound circles", event->getEventName().str() ) );
        return;
      }
    }
  }

  // Should have set up audioInfo or returned by now
  DEBUG_ASSERTCRASH( audioInfo != nullptr, ("Managed to finish setting up audio info without setting it?!?" ) );
  if ( audioInfo == nullptr )
  {
    return;
  }

  // Get the current radius (could be overridden)
  Real minRadius = audioInfo->m_minDistance;
  Real maxRadius = audioInfo->m_maxDistance;
  Bool customized = properties->getBool( TheKey_objectSoundAmbientCustomized, &exists );
  if ( exists && customized )
  {
    Real valReal;

    valReal = properties->getReal( TheKey_objectSoundAmbientMinRange, &exists );
    if ( exists )
    {
      minRadius = valReal;
    }
    valReal = properties->getReal( TheKey_objectSoundAmbientMaxRange, &exists );
    if ( exists )
    {
      maxRadius = valReal;
    }
  }
  addCircleToLineRenderer(pos, minRadius, SOUND_RANGE_LINE_WIDTH, colors[0], camera );
  addCircleToLineRenderer(pos, maxRadius, SOUND_RANGE_LINE_WIDTH, colors[1], camera );
}


#define TEST_ART_HIGHLIGHT_LINE_WIDTH 5.0f
/** Draw test art with an X on it. **/
// MLL C&C3
void DrawObject::updateVBWithTestArtHighlight(MapObject *pMapObj, CameraClass* camera)
{
	if (!pMapObj || !m_lineRenderer || pMapObj->getThingTemplate() || pMapObj->isScorch()) {
		// It is test art if it doesn't have a ThingTemplate.
		return;
	}

	unsigned long color = 0xFFA000A0; // Purple


	Coord3D pos = *pMapObj->getLocation();
	if (TheTerrainRenderObject) {
		// Make sure that the position is on the terrain.
		pos.z += TheTerrainRenderObject->getHeightMapHeight(pos.x, pos.y, nullptr);
	}

	Real angle, inc = PI/2.0f;
	Coord3D pnt, lastPnt;
	ICoord2D start, end;
	Real z = pos.z;
	Real radius = 30.0f;

	// Draw the diamond.
	angle = 0.0f;
	lastPnt.x = pos.x + radius * (Real)cos(angle);
	lastPnt.y = pos.y + radius * (Real)sin(angle);
	lastPnt.z = z;
	bool shouldEnd = worldToScreen(&lastPnt, &end, camera);

	for( angle = inc; angle <= 2.0f * PI; angle += inc ) {
		pnt.x = pos.x + radius * (Real)cos(angle);
		pnt.y = pos.y + radius * (Real)sin(angle);
		pnt.z = z;

		bool shouldStart = worldToScreen(&pnt, &start, camera);
		if (shouldStart && shouldEnd) {
			m_lineRenderer->Add_Line(Vector2(start.x, start.y), Vector2(end.x, end.y), TEST_ART_HIGHLIGHT_LINE_WIDTH, color);
		}

		lastPnt = pnt;
		end = start;
		shouldEnd = shouldStart;
	}

}


/** Transform a 3D Coordinate into 2D screen space **/
// MLL C&C3
bool DrawObject::worldToScreen(const Coord3D *w, ICoord2D *s, CameraClass* camera)
{

	if ((w == nullptr) || (s == nullptr) || (camera == nullptr)) {
		return false;
	}

	Vector3 world;
	Vector3 screen;

	world.Set(w->x, w->y, w->z);
	camera->Project(screen, world);

	//
	// note that the screen coord returned from the project W3D camera
	// gave us a screen coords that range from (-1,-1) bottom left to
	// (1,1) top right ... we are turning that into (0,0) upper left
	// coords now
	//
	W3DLogicalScreenToPixelScreen(screen.X, screen.Y, &s->x, &s->y, m_winSize.x, m_winSize.y);

	if ((screen.X > 2.0f) || (screen.Y > 2.0f) || (screen.X < -2.0f) || (screen.Y < -2.0f)) {
		// Too far off the screen.
		return false;
	}

	return (true);
}

/** Tells drawobject where the tool is located, so it can draw feedback. */
void DrawObject::setFeedbackPos(Coord3D pos)
{
	m_feedbackPoint = pos;
	// center on half pixel for even widths.
	if (!(m_brushWidth&1)) {
		pos.x += MAP_XY_FACTOR/2;
		pos.y += MAP_XY_FACTOR/2;
	}
	CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
	if (!pDoc) return;
	CPoint ndx;
	pDoc->getCellIndexFromCoord(pos, &ndx);
	if (ndx.x != m_cellCenter.x || ndx.y != m_cellCenter.y) {
		m_cellCenter = ndx;
		if (m_toolWantsFeedback && !m_disableFeedback) {
			WbView3d *pView = pDoc->Get3DView();
			if (pView) {
				pView->Invalidate(false);
			}
		}
	}
}

void DrawObject::setRampFeedbackParms(const Coord3D *start, const Coord3D *end, Real rampWidth)
{
	DEBUG_ASSERTCRASH(start && end, ("Parameter passed into setRampFeedbackParms was null. Not allowed"));
	if (!(start && end)) {
		return;
	}

	m_rampStartPoint = *start;
	m_rampEndPoint = *end;
	m_rampWidth = rampWidth;

}


bool _skip_drawobject_render = false;

/** Render draws into the current 3d context. */
void DrawObject::Render(RenderInfoClass & rinfo)
{
//DEBUG!
if (_skip_drawobject_render) {
	return;
}

	if (m_lineRenderer == nullptr) {
		// This can't be created in init because the doc hasn't been created yet.
		m_lineRenderer = new Render2DClass();
		ASSERT(m_lineRenderer);
		CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
		ASSERT(pDoc);
		WbView3d *pView = pDoc->Get3DView();
		ASSERT(pView);
		m_winSize = pView->getActualWinSize();
		m_lineRenderer->Set_Coordinate_Range(RectClass(0, 0, m_winSize.x, m_winSize.y));
		m_lineRenderer->Reset();
		m_lineRenderer->Enable_Texturing(FALSE);
	}

	DX8Wrapper::Apply_Render_State_Changes();

	DX8Wrapper::Set_Material(m_vertexMaterialClass);
	DX8Wrapper::Set_Shader(m_shaderClass);
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
	DX8Wrapper::Apply_Render_State_Changes();
	Int count=0;
	Int i;
	bool linesToRender = false;

	curHighlight++;
	if (curHighlight >= NUM_HIGHLIGHT) {
		curHighlight = 0;
	}
	m_waterDrawObject->update();
	DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile1);
  if (m_drawObjects || m_drawWaypoints || m_drawBoundingBoxes || m_drawSightRanges || m_drawWeaponRanges || m_drawSoundRanges || m_drawTestArtHighlight) {
		//Apply the shader and material

		//WST Variables below are for optimization to reduce VB updates which are extremely slow
		// Optimization strategy is to remember last setting and avoid re-updating unless it changed
		int rememberLastSettingVB1 = -99999;
		int rememberLastSettingVB2 = -99999;

		MapObject *pMapObj;
		for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) {
			// simple Draw test.
			if (pMapObj->getFlags() & FLAG_DONT_RENDER) {
				continue;
			}

// DEBUG!
if (pMapObj->isSelected()) {
 Transform.Get_Translation();
}
			Coord3D loc = *pMapObj->getLocation();
			if (TheTerrainRenderObject) {
				loc.z += TheTerrainRenderObject->getHeightMapHeight(loc.x, loc.y, nullptr);
			}
			// Cull.
			//SphereClass bounds(Vector3(loc.x, loc.y, loc.z), THE_RADIUS);
			//if (rinfo.Camera.Cull_Sphere(bounds)) {
			//	continue;
			//}
			Bool doArrow = true;
			if (pMapObj->getFlag(FLAG_ROAD_FLAGS) || pMapObj->getFlag(FLAG_BRIDGE_FLAGS) || pMapObj->isWaypoint())
			{
				doArrow = false;
			}

			Bool doDiamond = pMapObj->isWaypoint();
			if (doDiamond) {
				if (!m_drawWaypoints) {
					continue;
				}
			}	else {
				// MLL C&C3
				if (pMapObj->isSelected()) {
					if (doArrow && m_drawBoundingBoxes) {
						linesToRender = true;
						updateVBWithBoundingBox(pMapObj, &rinfo.Camera);
					}
					if (doArrow && m_drawSightRanges) {
						linesToRender = true;
						updateVBWithSightRange(pMapObj, &rinfo.Camera);
					}
					if (doArrow && m_drawWeaponRanges) {
						linesToRender = true;
						updateVBWithWeaponRange(pMapObj, &rinfo.Camera);
					}
          if (doArrow && m_drawSoundRanges) {
            linesToRender = true;
            updateVBWithSoundRanges(pMapObj, &rinfo.Camera);
          }
				}

				if (doArrow && m_drawTestArtHighlight) {
					linesToRender = true;
					updateVBWithTestArtHighlight(pMapObj, &rinfo.Camera);
				}

				if (!m_drawObjects) {
					continue;
				}
				if (BuildListTool::isActive()) {
					continue;
				}
			}

			if (count&1) {
				int setting = pMapObj->getColor();

				if (doArrow) {
					setting |= (1<<25);
				}
				if (doDiamond) {
					setting |= (1<<26);
				}

				if (setting != rememberLastSettingVB1)	{
					rememberLastSettingVB1 = setting;
					updateVB(m_vertexBufferTile1,pMapObj->getColor(), doArrow, doDiamond);
				}
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile1);

			} else {
				int setting = pMapObj->getColor();

				if (doArrow) {
					setting |= (1<<25);
				}
				if (doDiamond) {
					setting |= (1<<26);
				}

				if (setting != rememberLastSettingVB2) {
					rememberLastSettingVB2 = setting;
					updateVB(m_vertexBufferTile2, pMapObj->getColor(), doArrow, doDiamond);
				}
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile2);
			}

			///@todo - remove the istree stuff, or get the info from the thing template.  jba.
			Bool isTree = false;

			Vector3 vec(loc.x, loc.y, loc.z);
			Matrix3D tm(Transform);
			Matrix3x3 rot(true);
			rot.Rotate_Z(pMapObj->getAngle());

			tm.Set_Translation(vec);
			tm.Set_Rotation(rot);
			int polyCount = NUM_TRI;
			if (!pMapObj->isSelected()) {
				polyCount -= NUM_ARROW_TRI+NUM_SELECT_TRI;
			}

			DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);
			if (isTree) {
				DX8Wrapper::Draw_Triangles(	NUM_TRI*3,polyCount, 0,	(m_numTriangles*3));
			} else {
				DX8Wrapper::Draw_Triangles(	0,polyCount, 0,	(m_numTriangles*3));
			}

			count++;
		}
	}
	if (m_drawPolygonAreas) {
 		DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferWater);
		Int selected;
		for (selected = 0; selected < 2; selected++) {
			for (PolygonTrigger *pTrig=PolygonTrigger::getFirstPolygonTrigger(); pTrig; pTrig = pTrig->getNext()) {
				DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
				if (!pTrig->getShouldRender()) continue;
				Bool polySelected = PolygonTool::isSelected(pTrig);
				if (polySelected && !selected) continue;
				if (!polySelected && selected) continue;
				for (i=0; i<pTrig->getNumPoints(); i++) {
					Bool pointSelected = (polySelected && PolygonTool::getSelectedPointNdx()==i);
					ICoord3D iLoc = *pTrig->getPoint(i);
					Coord3D loc;
					loc.x = iLoc.x;
					loc.y = iLoc.y;
					loc.z = TheTerrainRenderObject->getHeightMapHeight(loc.x, loc.y, nullptr);
					SphereClass bounds(Vector3(loc.x, loc.y, loc.z), THE_RADIUS);
					if (rinfo.Camera.Cull_Sphere(bounds)) {
						continue;
					}
					const Bool ARROW=false;
					const Bool DIAMOND=true;
					const Int RED = 0x0000FF; // red in BGR.
					const Int BLUE = 0xFF7f00; // bright blue.
					Int color = RED;
					if (pTrig->isWaterArea()) {
						color = BLUE;
					}
					if (count&1) {
						updateVB(m_vertexBufferTile1, color, ARROW, DIAMOND);
						DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile1);
					} else {
						updateVB(m_vertexBufferTile2, color, ARROW, DIAMOND);
						DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile2);
					}
					count++;

					Vector3 vec(loc.x, loc.y, loc.z);
					Matrix3D tm(Transform);
					tm.Set_Translation(vec);

					int polyCount = NUM_TRI;
					if (!pointSelected) {
						polyCount -= NUM_ARROW_TRI+NUM_SELECT_TRI;
					}

					DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
					DX8Wrapper::Set_Transform(D3DTS_WORLD,tm);
					DX8Wrapper::Draw_Triangles(	0,polyCount, 0,	(m_numTriangles*3));
				}
				Matrix3D tmReset(Transform);
				DX8Wrapper::Set_Transform(D3DTS_WORLD,tmReset);
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile1);
				updatePolygonVB(pTrig, polySelected, polySelected && PolygonTool::isSelectedOpen());
 				DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
				if (m_feedbackIndexCount>0) {
					DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
					DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
				}
			}
			DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
		}
	}


 	if (BuildListTool::isActive()) for (i=0; i<TheSidesList->getNumSides(); i++) {
		SidesInfo *pSide = TheSidesList->getSideInfo(i);
		for (BuildListInfo *pBuild = pSide->getBuildList(); pBuild; pBuild = pBuild->getNext()) {
			Coord3D loc = *pBuild->getLocation();
			if (TheTerrainRenderObject) {
				loc.z += TheTerrainRenderObject->getHeightMapHeight(loc.x, loc.y, nullptr);
			}
			// Cull.
			SphereClass bounds(Vector3(loc.x, loc.y, loc.z), THE_RADIUS);
			if (rinfo.Camera.Cull_Sphere(bounds)) {
				continue;
			}
			if (!m_drawObjects) {
				continue;
			}
			const Int GREEN = 0x00FF00; // GREEN in BGR.
			if (count&1) {
				updateVB(m_vertexBufferTile1, GREEN, true, false);
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile1);
			} else {
				updateVB(m_vertexBufferTile2, GREEN, true, false);
				DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferTile2);
			}
			count++;
// ok to here.
			Vector3 vec(loc.x, loc.y, loc.z);
			Matrix3D tmXX(Transform);
			Matrix3x3 rot(true);
			rot.Rotate_Z(pBuild->getAngle());

			tmXX.Set_Translation(vec);
			tmXX.Set_Rotation(rot);
			int polyCountA = NUM_TRI;
			if (!pBuild->isSelected()) {
				polyCountA -= NUM_ARROW_TRI+NUM_SELECT_TRI;
			}

#if 1
			DX8Wrapper::Set_Transform(D3DTS_WORLD,tmXX);
			DX8Wrapper::Draw_Triangles(	0,polyCountA, 0,	(m_numTriangles*3));
#endif

		}
	}

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
 	DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferWater);
	Matrix3D tmReset(Transform);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,tmReset);

	if (m_drawWaypoints) {
		updateWaypointVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(m_shaderClass);
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
			DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferWater);
		}
	}



#if 1
	if (m_meshFeedback) {
		updateMeshVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(SC_OPAQUE_Z);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
		}
	} else if (m_toolWantsFeedback && !m_disableFeedback) {
		updateFeedbackVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(ShaderClass::_PresetAlpha2DShader);
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
		}
	}
#endif

#if 1
	if (m_rampFeedback) {
		updateRampVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(SC_OPAQUE_Z);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_WIREFRAME);	// we want a solid ramp
			DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);				// disable lighting
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
		}
	}
#endif

#if 1
	if (m_boundaryFeedback) {
		updateBoundaryVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(m_shaderClass);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);	// we want a solid ramp
			DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);				// disable lighting
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
		}
	}
#endif

	DX8Wrapper::Set_Vertex_Buffer(nullptr);	//release reference to vertex buffer
	DX8Wrapper::Set_Index_Buffer(nullptr,0);	//release reference to vertex buffer

	// Grid and ruler: before the water, so lines under water are seen through it, unless drawn over everything.
	m_gridBlocksReady = (m_drawGrid || m_drawRuler) && TheTerrainRenderObject && m_gridIndexBuffer && setupGridBlocks(rinfo);
	renderGrid(rinfo, false);

	if (m_ambientSoundFeedback) {
		updateAmbientSoundVB();
		if (m_feedbackIndexCount>0) {
 			DX8Wrapper::Set_Vertex_Buffer(m_vertexFeedback);
			DX8Wrapper::Set_Index_Buffer(m_indexFeedback,0);
			DX8Wrapper::Set_Shader(m_shaderClass);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE,D3DFILL_SOLID);	// we want a solid ramp
			DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);				// disable lighting
			DX8Wrapper::Draw_Triangles(	0, m_feedbackIndexCount/3, 0,	m_feedbackVertexCount);
		}
	}

  DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);
 	DX8Wrapper::Set_Vertex_Buffer(m_vertexBufferWater);

	if (m_waterDrawObject) {
		m_waterDrawObject->renderWater();
	}

	renderGrid(rinfo, true);

	if (m_drawLetterbox) {
		int w = m_winSize.x;
		int h = m_winSize.y;
		int size = (int)((h - (9.0f / 16.0f * w)) * 0.5f);
		RectClass rect(0, 0, w, size);
		m_lineRenderer->Add_Quad(rect, 0xFF000000);
		rect.Set(0, h - size, w, h);
		m_lineRenderer->Add_Quad(rect, 0xFF000000);
		linesToRender = true;
	}

	// Render any lines that have been added, like bounding boxes.
	// MLL C&C3
	if (linesToRender && m_lineRenderer) {
		m_lineRenderer->Render();
		// Clear the old lines.
		m_lineRenderer->Reset();
	}
}


/** Finds the visible blocks of the height map and how far from the camera each is. False if nothing to draw. */
Bool DrawObject::setupGridBlocks(RenderInfoClass & rinfo)
{
	CWorldBuilderDoc *pDoc = CWorldBuilderDoc::GetActiveDoc();
	WorldHeightMapEdit *pMap = pDoc ? pDoc->GetHeightMap() : nullptr;
	if (!pMap) {
		return false;
	}
	const Int xCells = pMap->getXExtent() - 1;
	const Int yCells = pMap->getYExtent() - 1;
	const Int border = pMap->getBorderSizeInline();
	if (xCells <= 0 || yCells <= 0) {
		return false;
	}
	m_gridMinX = -border*MAP_XY_FACTOR;
	m_gridMinY = -border*MAP_XY_FACTOR;
	m_gridMaxX = (xCells-border)*MAP_XY_FACTOR;
	m_gridMaxY = (yCells-border)*MAP_XY_FACTOR;
	m_gridBlocksX = (xCells + GRID_BLOCK_CELLS - 1) / GRID_BLOCK_CELLS;
	m_gridBlocksY = (yCells + GRID_BLOCK_CELLS - 1) / GRID_BLOCK_CELLS;
	m_gridBlocks.resize(m_gridBlocksX*m_gridBlocksY);

	CameraClass &camera = rinfo.Camera;
	const Vector3 camPos = camera.Get_Position();
	const Real viewHeight = (m_winSize.y > 0) ? (Real)m_winSize.y : 600.0f;
	const Real tanHalfFov = (Real)tan(camera.Get_Vertical_FOV()*0.5f);
	Bool anyVisible = false;

	for (Int j = 0; j < m_gridBlocksY; j++) {
		for (Int i = 0; i < m_gridBlocksX; i++) {
			GridBlock &block = m_gridBlocks[j*m_gridBlocksX + i];
			const Int x0 = i*GRID_BLOCK_CELLS;
			const Int y0 = j*GRID_BLOCK_CELLS;
			const Int x1 = min(x0 + GRID_BLOCK_CELLS, xCells);
			const Int y1 = min(y0 + GRID_BLOCK_CELLS, yCells);
			Int minHeight = pMap->getHeight(x0, y0);
			Int maxHeight = minHeight;
			for (Int y = y0; y <= y1; y++) {
				for (Int x = x0; x <= x1; x++) {
					Int h = pMap->getHeight(x, y);
					if (h < minHeight) minHeight = h;
					if (h > maxHeight) maxHeight = h;
				}
			}
			// A margin for the lift above the terrain.
			Vector3 minPt((x0-border)*MAP_XY_FACTOR, (y0-border)*MAP_XY_FACTOR, minHeight*MAP_HEIGHT_SCALE - 1.0f);
			Vector3 maxPt((x1-border)*MAP_XY_FACTOR, (y1-border)*MAP_XY_FACTOR, maxHeight*MAP_HEIGHT_SCALE + 5.0f);
			AABoxClass box;
			box.Init_Min_Max(minPt, maxPt);
			block.visible = !camera.Cull_Box(box);
			if (!block.visible) {
				continue;
			}
			anyVisible = true;

			// Distance from the camera to the nearest point of the block decides how fine it is drawn.
			Vector3 nearest(
				max(minPt.X, min(camPos.X, maxPt.X)),
				max(minPt.Y, min(camPos.Y, maxPt.Y)),
				max(minPt.Z, min(camPos.Z, maxPt.Z)));
			Real dist = (nearest - camPos).Length();
			if (dist < 1.0f) {
				dist = 1.0f;
			}
			block.pixelsPerUnit = viewHeight / (2.0f*dist*tanHalfFov);
			block.unitsPerPixel = 1.0f / block.pixelsPerUnit;
			block.lift = 0.3f + dist*0.002f;
		}
	}
	return anyVisible;
}

/** Spacing at which lines that are spacing apart are drawn: doubled while they would be too close on screen,
		so the lines drawn are always a subset of the full set. */
static Real gridLodSpacing(Real spacing, Real pixelsPerUnit)
{
	while (spacing*pixelsPerUnit < GRID_MIN_PIXELS && spacing < GRID_MAX_STEP) {
		spacing *= 2;
	}
	return spacing;
}

/** The grid block at a world position; null outside the height map. */
const DrawObject::GridBlock *DrawObject::gridBlockAt(Real x, Real y) const
{
	if (x < m_gridMinX || y < m_gridMinY || x > m_gridMaxX || y > m_gridMaxY) {
		return nullptr;
	}
	const Real blockSize = GRID_BLOCK_CELLS*MAP_XY_FACTOR;
	Int i = (Int)((x - m_gridMinX) / blockSize);
	Int j = (Int)((y - m_gridMinY) / blockSize);
	if (i >= m_gridBlocksX) i = m_gridBlocksX-1;
	if (j >= m_gridBlocksY) j = m_gridBlocksY-1;
	if (i < 0 || j < 0) {
		return nullptr;
	}
	return &m_gridBlocks[j*m_gridBlocksX + i];
}

/** Adds one flat quad lying on the terrain. With lodSpacing > 0 the segment belongs to a ring at
		lodIndex*lodSpacing from the center: a minor ring (lodIndex < 0) is left out where rings that far apart
		are too close on screen, a major ring where the block's level of detail skips it. */
void DrawObject::addGridSegment(Real x0, Real y0, Real x1, Real y1, unsigned long color, Real widthPixels, Real lodSpacing, Int lodIndex)
{
	const GridBlock *block = gridBlockAt((x0+x1)*0.5f, (y0+y1)*0.5f);
	if (!block || !block->visible) {
		return;
	}
	if (lodSpacing > 0) {
		if (lodIndex < 0) {
			if (lodSpacing*block->pixelsPerUnit < RULER_MINOR_MIN_PIXELS) {
				return;
			}
		} else {
			Int mul = REAL_TO_INT(gridLodSpacing(lodSpacing, block->pixelsPerUnit) / lodSpacing);
			if (mul > 1 && (lodIndex % mul) != 0) {
				return;
			}
		}
	}
	Real dx = x1 - x0;
	Real dy = y1 - y0;
	Real len = (Real)sqrt(dx*dx + dy*dy);
	if (len < 0.001f) {
		return;
	}
	const Real halfWidth = 0.5f * widthPixels * block->unitsPerPixel;
	const Real nx = -dy/len*halfWidth;
	const Real ny = dx/len*halfWidth;
	const Real z0 = TheTerrainRenderObject->getHeightMapHeight(x0, y0, nullptr) + block->lift;
	const Real z1 = TheTerrainRenderObject->getHeightMapHeight(x1, y1, nullptr) + block->lift;

	VertexFormatXYZNDUV2 v;
	memset(&v, 0, sizeof(v));
	v.nz = 1.0f;
	v.diffuse = color;
	v.x = x0-nx; v.y = y0-ny; v.z = z0; m_gridVerts.push_back(v);
	v.x = x0+nx; v.y = y0+ny; v.z = z0; m_gridVerts.push_back(v);
	v.x = x1-nx; v.y = y1-ny; v.z = z1; m_gridVerts.push_back(v);
	v.x = x1+nx; v.y = y1+ny; v.z = z1; m_gridVerts.push_back(v);

	if ((Int)m_gridVerts.size() >= GRID_BATCH_QUADS*4) {
		flushGrid();
	}
}

/** Adds a straight line, cut into short pieces that follow the terrain. */
void DrawObject::addGridLine(Real x0, Real y0, Real x1, Real y1, unsigned long color, Real widthPixels)
{
	Real dx = x1 - x0;
	Real dy = y1 - y0;
	Real len = (Real)sqrt(dx*dx + dy*dy);
	Int count = (Int)ceil(len / GRID_SAMPLE_LENGTH);
	if (count < 1) {
		count = 1;
	}
	Real prevX = x0;
	Real prevY = y0;
	for (Int i = 1; i <= count; i++) {
		Real t = (Real)i / count;
		Real x = x0 + dx*t;
		Real y = y0 + dy*t;
		addGridSegment(prevX, prevY, x, y, color, widthPixels, 0, -1);
		prevX = x;
		prevY = y;
	}
}

/** Adds the lines at originX + k*spacing and originY + k*spacing that lie in one block, leaving out
		every skipEvery-th line (counted from the origin) when skipEvery > 0. A line on the edge between two
		blocks belongs to the block above or right of it; the map's last edge to the last block. */
void DrawObject::addBlockLines(Int i, Int j, Real originX, Real originY, Real spacing, Int skipEvery, unsigned long color, Real widthPixels)
{
	const Real blockSize = GRID_BLOCK_CELLS*MAP_XY_FACTOR;
	const Real bx0 = m_gridMinX + i*blockSize;
	const Real by0 = m_gridMinY + j*blockSize;
	const Real bx1 = min(bx0 + blockSize, m_gridMaxX);
	const Real by1 = min(by0 + blockSize, m_gridMaxY);
	const Bool lastX = (i == m_gridBlocksX-1);
	const Bool lastY = (j == m_gridBlocksY-1);
	for (Int k = (Int)ceil((bx0-originX)/spacing - 0.001f); ; k++) {
		Real x = originX + k*spacing;
		if (x > bx1 + 0.001f || (!lastX && x > bx1 - 0.001f)) {
			break;
		}
		if (skipEvery > 0 && ((k % skipEvery) + skipEvery) % skipEvery == 0) {
			continue;
		}
		addGridLine(x, by0, x, by1, color, widthPixels);
	}
	for (Int k = (Int)ceil((by0-originY)/spacing - 0.001f); ; k++) {
		Real y = originY + k*spacing;
		if (y > by1 + 0.001f || (!lastY && y > by1 - 0.001f)) {
			break;
		}
		if (skipEvery > 0 && ((k % skipEvery) + skipEvery) % skipEvery == 0) {
			continue;
		}
		addGridLine(bx0, y, bx1, y, color, widthPixels);
	}
}

/** Draws the quads gathered so far. */
void DrawObject::flushGrid()
{
	Int quads = (Int)m_gridVerts.size() / 4;
	if (quads <= 0) {
		return;
	}
	DynamicVBAccessClass vbAccess(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, quads*4);
	{
		DynamicVBAccessClass::WriteLockClass lock(&vbAccess);
		VertexFormatXYZNDUV2 *verts = lock.Get_Formatted_Vertex_Array();
		memcpy(verts, &m_gridVerts[0], quads*4*sizeof(VertexFormatXYZNDUV2));
	}
	DX8Wrapper::Set_Vertex_Buffer(vbAccess);
	DX8Wrapper::Set_Index_Buffer(m_gridIndexBuffer, 0);
	DX8Wrapper::Draw_Triangles(0, quads*2, 0, quads*4);
	m_gridVerts.clear();
}

/** Draws the snap grid and the ruler on the terrain. Called twice per frame: before the water for what is
		hidden by terrain, objects and water (onTop false), and after it for what is drawn over everything. */
void DrawObject::renderGrid(RenderInfoClass & rinfo, Bool onTop)
{
	const Bool drawGrid = m_drawGrid && m_gridOnTop == onTop && m_gridStep > 0;
	const Bool drawRuler = m_drawRuler && m_rulerOnTop == onTop && (m_rulerRect || m_rulerAngular || m_rulerDiagonals)
		&& m_rulerMinor > 0 && m_rulerMajorEvery > 0;
	if (!m_gridBlocksReady || (!drawGrid && !drawRuler)) {
		return;
	}

	DX8Wrapper::Set_Material(m_vertexMaterialClass);
	DX8Wrapper::Set_Shader(onTop ? ShaderClass(SC_GRID_ON_TOP) : ShaderClass(SC_GRID));
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_Texture(1, nullptr);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(true));
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
	m_gridVerts.clear();

	// The snap grid, block by block so each block can thin out its lines. Lines lie on multiples of the
	// block's spacing, counted from the lower left of the playable area, so they match the snapped positions.
	if (drawGrid) {
		for (Int j = 0; j < m_gridBlocksY; j++) {
			for (Int i = 0; i < m_gridBlocksX; i++) {
				const GridBlock &block = m_gridBlocks[j*m_gridBlocksX + i];
				if (block.visible) {
					addBlockLines(i, j, 0, 0, gridLodSpacing(m_gridStep, block.pixelsPerUnit), 0, GRID_COLOR, GRID_LINE_PIXELS);
				}
			}
		}
	}

	if (drawRuler) {
		// The ruler is centered on the playable area, which is also the center of the height map.
		const Real centerX = (m_gridMinX + m_gridMaxX) * 0.5f;
		const Real centerY = (m_gridMinY + m_gridMaxY) * 0.5f;
		const Real majorSpacing = m_rulerMinor*m_rulerMajorEvery;

		if (m_rulerRect) {
			for (Int j = 0; j < m_gridBlocksY; j++) {
				for (Int i = 0; i < m_gridBlocksX; i++) {
					const GridBlock &block = m_gridBlocks[j*m_gridBlocksX + i];
					if (!block.visible) {
						continue;
					}
					if (m_rulerMinor*block.pixelsPerUnit >= RULER_MINOR_MIN_PIXELS) {
						addBlockLines(i, j, centerX, centerY, m_rulerMinor, m_rulerMajorEvery, RULER_RECT_MINOR_COLOR, RULER_MINOR_LINE_PIXELS);
					}
					addBlockLines(i, j, centerX, centerY, gridLodSpacing(majorSpacing, block.pixelsPerUnit), 0, RULER_RECT_MAJOR_COLOR, RULER_MAJOR_LINE_PIXELS);
				}
			}
		}

		// Distance from the center to the map edge in a direction.
		struct RayToEdge {
			static Real length(Real cx, Real cy, Real dx, Real dy, Real minX, Real minY, Real maxX, Real maxY) {
				Real t = 1e9f;
				if (dx > 0.0001f) t = min(t, (maxX-cx)/dx);
				if (dx < -0.0001f) t = min(t, (minX-cx)/dx);
				if (dy > 0.0001f) t = min(t, (maxY-cy)/dy);
				if (dy < -0.0001f) t = min(t, (minY-cy)/dy);
				return t;
			}
		};

		if (m_rulerAngular) {
			// Only rings that can reach the visible blocks, and only as fine as the finest visible block needs.
			const Real blockSize = GRID_BLOCK_CELLS*MAP_XY_FACTOR;
			Real visMinX = m_gridMaxX, visMinY = m_gridMaxY, visMaxX = m_gridMinX, visMaxY = m_gridMinY;
			Real maxPixelsPerUnit = 0;
			for (Int j = 0; j < m_gridBlocksY; j++) {
				for (Int i = 0; i < m_gridBlocksX; i++) {
					const GridBlock &block = m_gridBlocks[j*m_gridBlocksX + i];
					if (!block.visible) {
						continue;
					}
					visMinX = min(visMinX, m_gridMinX + i*blockSize);
					visMinY = min(visMinY, m_gridMinY + j*blockSize);
					visMaxX = max(visMaxX, min(m_gridMinX + (i+1)*blockSize, m_gridMaxX));
					visMaxY = max(visMaxY, min(m_gridMinY + (j+1)*blockSize, m_gridMaxY));
					maxPixelsPerUnit = max(maxPixelsPerUnit, block.pixelsPerUnit);
				}
			}
			Real nearX = max(visMinX, min(centerX, visMaxX)) - centerX;
			Real nearY = max(visMinY, min(centerY, visMaxY)) - centerY;
			Real farX = max(fabs(visMinX-centerX), fabs(visMaxX-centerX));
			Real farY = max(fabs(visMinY-centerY), fabs(visMaxY-centerY));
			Real minDist = (Real)sqrt(nearX*nearX + nearY*nearY);
			Real maxDist = (Real)sqrt(farX*farX + farY*farY);

			// Rings are counted in minor steps; every m_rulerMajorEvery-th is a major ring.
			Int ringStep = 1;
			if (m_rulerMinor*maxPixelsPerUnit < RULER_MINOR_MIN_PIXELS) {
				ringStep = m_rulerMajorEvery * REAL_TO_INT(gridLodSpacing(majorSpacing, maxPixelsPerUnit) / majorSpacing);
			}
			Int firstRing = max(1, (Int)floor(minDist/m_rulerMinor));
			firstRing = ((firstRing + ringStep - 1) / ringStep) * ringStep;
			Int lastRing = (Int)ceil(maxDist/m_rulerMinor);

			for (Int ring = firstRing; ring <= lastRing; ring += ringStep) {
				const Bool major = (ring % m_rulerMajorEvery) == 0;
				const unsigned long color = major ? RULER_ANGULAR_MAJOR_COLOR : RULER_ANGULAR_MINOR_COLOR;
				const Real width = major ? RULER_MAJOR_LINE_PIXELS : RULER_MINOR_LINE_PIXELS;
				const Real lodSpacing = major ? majorSpacing : m_rulerMinor;
				const Int lodIndex = major ? ring / m_rulerMajorEvery : -1;
				const Real radius = ring*m_rulerMinor;
				Int count = (Int)ceil(2*PI*radius / (2*GRID_SAMPLE_LENGTH));
				count = max(32, min(count, 8192));
				const Real cosStep = (Real)cos(2*PI/count);
				const Real sinStep = (Real)sin(2*PI/count);
				Real dx = radius;
				Real dy = 0;
				for (Int s = 0; s < count; s++) {
					Real ndx = dx*cosStep - dy*sinStep;
					Real ndy = dx*sinStep + dy*cosStep;
					addGridSegment(centerX+dx, centerY+dy, centerX+ndx, centerY+ndy, color, width, lodSpacing, lodIndex);
					dx = ndx;
					dy = ndy;
				}
			}

			for (Int sector = 0; sector < RULER_SECTORS; sector++) {
				const Bool major = (sector % RULER_SECTORS_PER_MAJOR) == 0;
				const Real angle = sector * 2*PI / RULER_SECTORS;
				const Real dx = (Real)cos(angle);
				const Real dy = (Real)sin(angle);
				Real len = RayToEdge::length(centerX, centerY, dx, dy, m_gridMinX, m_gridMinY, m_gridMaxX, m_gridMaxY);
				addGridLine(centerX, centerY, centerX + dx*len, centerY + dy*len,
					major ? RULER_ANGULAR_MAJOR_COLOR : RULER_ANGULAR_MINOR_COLOR,
					major ? RULER_MAJOR_LINE_PIXELS : RULER_MINOR_LINE_PIXELS);
			}
		}

		if (m_rulerDiagonals) {
			for (Int quadrant = 0; quadrant < 4; quadrant++) {
				const Real angle = PI/4 + quadrant*PI/2;
				const Real dx = (Real)cos(angle);
				const Real dy = (Real)sin(angle);
				Real len = RayToEdge::length(centerX, centerY, dx, dy, m_gridMinX, m_gridMinY, m_gridMaxX, m_gridMaxY);
				addGridLine(centerX, centerY, centerX + dx*len, centerY + dy*len, RULER_DIAGONAL_COLOR, RULER_DIAGONAL_LINE_PIXELS);
			}
		}
	}

	flushGrid();

	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	DX8Wrapper::Set_Index_Buffer(nullptr, 0);
}


void BuildRectFromSegmentAndWidth(const Coord3D* start, const Coord3D* end, Real width,
																	Coord3D* outBL, Coord3D* outTL, Coord3D* outBR, Coord3D* outTR)
{
/*
	Here's how we're generating the surface to render:
 		1) Assign longSeg to be the segment from rampStartPoint to rampStopPoint
 		2) Cross product with the segment (0, 0, 1)
 		3) Normalize to get the unit vector (which is in the XY plane.)
 		4) Multiply the unit vector by the ramp width / 2
 		5) Store the four corners of the ramp as startPoint + unit, startPoint - unit,
 			 endPoint + unit and endPoint - unit.
 		6) This gives us a surface that has endpoints which always lie flat along the ground.
*/

	if (!(start && end && outBL && outTL && outBR && outTR)) {
		return;
	}

	// 1)
	Vector3 longSeg;
	if (start->length() > end->length()) {
		longSeg.X = end->x - start->x;
		longSeg.Y = end->y - start->y;
		longSeg.Z = end->z - start->z;
	} else {
		longSeg.X = start->x - end->x;
		longSeg.Y = start->y - end->y;
		longSeg.Z = start->z - end->z;
	}

	// 2)
	Vector3 upSeg(0.0f, 0.0f, 1.0f);
	Vector3 unitVec;

	Vector3::Cross_Product(longSeg, upSeg, &unitVec);

	// 3)
	unitVec.Normalize();

	// 4)
	unitVec.Scale(Vector3(width, width, width));

	Coord3D bl = { start->x + unitVec.X, start->y + unitVec.Y, start->z + unitVec.Z };
	Coord3D tl = { end->x + unitVec.X, end->y + unitVec.Y, end->z + unitVec.Z };
	Coord3D br = { start->x - unitVec.X, start->y - unitVec.Y, start->z - unitVec.Z };
	Coord3D tr = { end->x - unitVec.X, end->y - unitVec.Y, end->z - unitVec.Z };


	// 5)
	(*outBL) = bl;
	(*outTL) = tl;
	(*outBR) = br;
	(*outTR) = tr;

	// 6)
	// all done
}
