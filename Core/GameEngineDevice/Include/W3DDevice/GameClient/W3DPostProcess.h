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

// FILE: W3DPostProcess.h /////////////////////////////////////////////////////
// Runs the PostProcess chain of the INI (GameClient/PostProcess.h) over the 3D scene, with the
// post-processing of the Direct3D 12 renderer. With Direct3D 8 it does nothing.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"

#include <vector>

class CameraClass;
class TextureClass;
struct ID3D8On12PostProcess;

class W3DPostProcess
{
public:
	W3DPostProcess();
	~W3DPostProcess();

	void init();		///< reads the PostProcess INI; the chain is built on the first run
	void reload();		///< reads the INI and the shader files again
	void run(CameraClass *camera);	///< after the 3D scene of this camera, before the interface

	Bool setParameter(const AsciiString& pass, const AsciiString& name, const Real* values, Int count);

	Bool isAvailable();										///< the renderer can post-process
	Bool isRunning();										///< a chain is built and turned on
	const std::vector<AsciiString>& getPasses() const { return m_passNames; }
	const AsciiString& getError() const { return m_error; }

private:
	void loadSettings();
	void build(ID3D8On12PostProcess* postProcess);
	void applyParameters();		///< the INI parameters for the time of day
	void releaseChain();		///< the chain, not the compiled shaders
	void release();

	ID3D8On12PostProcess* m_postProcess;		///< the device the chain was built for
	std::vector<unsigned int> m_shaders;
	std::vector<AsciiString> m_shaderNames;		///< the Shader name of each of m_shaders
	unsigned int m_builtGeneration;				///< ThePostProcessControl's generation the chain was built for
	Int m_appliedTimeOfDay;
	std::vector<TextureClass*> m_textures;
	std::vector<AsciiString> m_passNames;
	Bool m_built;
	AsciiString m_error;
	unsigned int m_startTime;
};

extern W3DPostProcess *TheW3DPostProcess;
