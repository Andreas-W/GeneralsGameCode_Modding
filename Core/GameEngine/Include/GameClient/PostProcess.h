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
//	  Inputs = Scene				; Scene, Depth, None, an earlier pass, or Texture:<file>, for Input0..Input3
//	  Scale = 2						; output size = screen size / Scale (the last pass draws into the screen)
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
//	End
//
// Only the Direct3D 12 renderer runs post-processing and shadow maps.
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

	static const FieldParse s_fieldParseTable[];
};

extern ShadowMapSettings* TheShadowMapSettings;	///< null without a ShadowMap block
