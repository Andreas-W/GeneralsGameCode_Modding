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

// FILE: W3DMaterials.h ///////////////////////////////////////////////////////
// HLSL materials for model meshes from the Material INI blocks (GameClient/PostProcess.h): every mesh
// drawn with one of a material's base textures gets its pixel shader and maps, with the materials of
// the Direct3D 12 renderer. With Direct3D 8 it does nothing.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "WW3D2/dx8renderer.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class TextureClass;
class MaterialTemplate;
struct ID3D8On12Materials;

class W3DMaterials : public MeshMaterialBinderClass
{
public:
	W3DMaterials();
	virtual ~W3DMaterials();

	void init();		///< reads the Material INI and becomes the mesh material binder; built on first use
	void reload();		///< reads the INI and the shader files again

	Bool isAvailable();									///< the renderer has materials
	Bool isRunning();									///< materials are built and turned on
	void setEnabled(Bool enabled) { m_enabled = enabled; }
	Bool setParameter(const AsciiString& material, const AsciiString& name, const Real* values, Int count);
	const AsciiString& getError() const { return m_error; }
	void getMaterials(std::vector<AsciiString>& names, std::vector<Int>& meshTextures);	///< and how many textures each got
	void collectTextures(std::vector<AsciiString>& names);	///< base textures drawn since the last call; starts collecting

	// MeshMaterialBinderClass
	virtual bool Begin(TextureClass *baseTexture);
	virtual void End();

private:
	struct Built
	{
		const MaterialTemplate *settings;
		unsigned int id;			///< the renderer's material
		Int textureCount;			///< base textures that got it so far
	};

	struct Entry
	{
		Int material;				///< index into m_built, -1 for none
		TextureClass *maps[4];		///< normal, specular, emissive, extra; null where there is none
	};

	void loadSettings();
	void build(ID3D8On12Materials *materials);
	void release();
	void clearEntries();
	const Entry& lookup(TextureClass *baseTexture);
	TextureClass *findMap(const AsciiString& baseName, const AsciiString& spec);

	ID3D8On12Materials *m_materials;		///< the device the materials were built for
	Bool m_built;
	Bool m_enabled;
	Bool m_bound;
	Bool m_collecting;
	std::vector<unsigned int> m_shaders;
	std::vector<Built> m_builtMaterials;
	std::map<std::string, Int> m_textureMaterial;		///< base texture name (lower case, no extension) -> m_builtMaterials index
	std::map<TextureClass *, Entry> m_entries;
	std::set<std::string> m_seenTextures;
	AsciiString m_error;
};

extern W3DMaterials *TheW3DMaterials;
