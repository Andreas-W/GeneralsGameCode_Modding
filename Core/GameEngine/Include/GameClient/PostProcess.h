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

// FILE: PostProcess.h /////////////////////////////////////////////////////////
// Post-processing settings from INI: shader passes over the 3D scene and the chain that runs them.
//
//	PostProcessPass BloomExtract
//	  Shader = PP_BloomExtract		; Data\Shaders\PostProcess\PP_BloomExtract.hlsl, else a built-in shader
//	  Inputs = Scene				; Scene, Depth, None, Previous (this pass's own output of the frame before),
//									; an earlier pass, or Texture:<file>, for Input0..Input5
//	  Scale = 2						; output size = screen size / Scale; 0 = a single pixel (the last pass draws into the screen)
//	  HDR = Yes						; 16 bit float output
//	  Param = Threshold 0.75		; a variable of the shader's Params cbuffer, 1 to 4 values
//	End
//
//	PostProcess
//	  Passes = BloomExtract BloomBlurH BloomBlurV ColorGrade
//	  HDRScene = Yes				; draw the scene in 16 bit float, so additive effects keep brightness above 1
//	End
//
//	ShadowMap						; soft shadows from a depth map instead of the stencil shadow volumes
//	  Enabled = Yes
//	  Size = 2048					; texels of the square map that covers the visible area
//	  Softness = 1.5				; radius of the soft edge in texels
//	  Bias = 2						; world units a point is moved towards the sun before the test
//	  DepthBias = 0					; rasterizer depth bias of the casters
//	  SlopeBias = 2					; rasterizer slope scaled depth bias of the casters
//	  Darkness = 1					; scales how much the map's shadow color darkens
//	  MinSunElevation = 0			; degrees; a lower sun is raised to this, which shortens the shadows
//	  Terrain = Yes					; hills and cliffs cast shadows too
//	  Trees = Yes					; trees cast shadows into the map instead of their decal shadows
//	  MaxUpdateRate = 60			; redraws per second while the view stands still; 0 = every frame
//	  DetailCascade = Yes			; a second map, around the middle of the view, for sharper shadows there
//	  DetailCoverage = 0.3			; its width as a fraction of the main map's
//	  DetailSize = 0				; its texels, 0 = Size
//	End
//
//	Material AmericaTanks			; in Data\INI\Material.ini: a pixel shader for every mesh drawn with these textures
//	  Textures = AVTank AVPaladin	; base textures, without extension, case-insensitive
//	  Shader = MAT_Standard			; Data\Shaders\Material\<Shader>.hlsl, else a built-in shader
//	  NormalMap = _nrm				; per texture <texture>_nrm (the default); a file name; or None
//	  SpecularMap = _spec			; the same, default _spec (red = strength, green = gloss)
//	  EmissiveMap = _emis			; the same, default _emis (glow color)
//	  ExtraMap = None				; a sixth texture for custom shaders, default none
//	  Param = Gloss 24				; a variable of the shader's Params cbuffer, 1 to 4 values
//	End
//
//	Material SoftSmoke
//	  Use = Particles				; Meshes (default), Particles (by particle texture), Terrain or Water (one each)
//	  Textures = EXSmokNew1			; for Meshes and Particles
//	  Shader = MAT_Particle			; MAT_Standard for meshes, MAT_Particle, MAT_Terrain, MAT_Water
//	End
//
// For Water the maps are file names in Art\Textures (NormalMap = waves, SpecularMap = foam, EmissiveMap =
// sky); for Terrain the maps come from Art\Terrain\<terrain texture><suffix> (NormalMap, SpecularMap) and
// the game passes its cloud and light map textures.
//
// Only the Direct3D 12 renderer runs post-processing, shadow maps and materials.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"

#include <vector>

class INI;
struct FieldParse;

struct PostProcessParameter
{
	AsciiString m_name;
	Real m_values[4];
	Int m_count;
};

class PostProcessPassTemplate
{
public:
	PostProcessPassTemplate();

	AsciiString m_name;
	AsciiString m_shader;
	std::vector<AsciiString> m_inputs;
	Int m_scale;
	Bool m_hdr;
	std::vector<PostProcessParameter> m_parameters;

	static const FieldParse s_fieldParseTable[];
};

class PostProcessSettings
{
public:
	PostProcessSettings();
	~PostProcessSettings();

	const PostProcessPassTemplate* findPass(const AsciiString& name) const;
	PostProcessPassTemplate* findOrCreatePass(const AsciiString& name);

	std::vector<PostProcessPassTemplate*> m_passes;
	std::vector<AsciiString> m_chain;	///< names of the passes that run, in order
	Bool m_hdrScene;					///< the scene is drawn in 16 bit float while the chain runs

	static const FieldParse s_fieldParseTable[];
};

extern PostProcessSettings* ThePostProcessSettings;

class ShadowMapSettings
{
public:
	ShadowMapSettings();

	Bool m_enabled;
	Int m_size;
	Real m_softness;
	Real m_bias;
	Int m_depthBias;
	Real m_slopeBias;
	Real m_darkness;
	Real m_minSunElevation;
	Bool m_terrain;
	Bool m_trees;
	Real m_maxUpdateRate;
	Bool m_detailCascade;
	Real m_detailCoverage;
	Int m_detailSize;

	static const FieldParse s_fieldParseTable[];
};

extern ShadowMapSettings* TheShadowMapSettings;	///< null without a ShadowMap block

class MaterialTemplate
{
public:
	MaterialTemplate();

	enum Use { USE_MESHES, USE_PARTICLES, USE_TERRAIN, USE_WATER };

	AsciiString m_name;
	Int m_use;							///< Use
	std::vector<AsciiString> m_textures;
	AsciiString m_shader;
	AsciiString m_maps[4];				///< normal, specular, emissive, extra
	std::vector<PostProcessParameter> m_parameters;

	static const FieldParse s_fieldParseTable[];
};

class MaterialSettings
{
public:
	~MaterialSettings();

	MaterialTemplate* findOrCreateMaterial(const AsciiString& name);

	std::vector<MaterialTemplate*> m_materials;
};

extern MaterialSettings* TheMaterialSettings;	///< null without Material blocks
