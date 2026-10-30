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
struct IDirect3DBaseTexture8;

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
	void getMaterials(std::vector<AsciiString>& names, std::vector<Int>& meshTextures, std::vector<AsciiString>& uses);	///< how many textures each got, what it is for
	void collectTextures(std::vector<AsciiString>& names);	///< base textures drawn since the last call; starts collecting

	// MeshMaterialBinderClass
	virtual bool Begin(TextureClass *baseTexture);
	virtual bool Begin_Particle(TextureClass *texture) { return bindParticle(texture); }
	virtual void End();

	/// The material of a particle texture (Use = Particles); End() unbinds it. additive tells the shader how
	/// the system blends (1 = additive, 0 = other, -1 = keep what it was told before).
	bool bindParticle(TextureClass *particleTexture, Int additive = -1);
	/// The terrain or water material (Use = Terrain / Water), with maps the caller made (terrain) or, for
	/// null, the material's own (water); End() unbinds it. Returns false when there is none.
	bool bindTerrain(IDirect3DBaseTexture8 *const *maps);
	bool bindWater(IDirect3DBaseTexture8 *extraMap);	///< extraMap, when not null, replaces the material's own
	Bool hasTerrain();
	Bool hasWater();
	const MaterialTemplate *getTerrainSettings();	///< for its map suffixes; null without a terrain material
	/// Sets a parameter of the terrain or water material, which the game passes per frame (texture
	/// matrices, sun); quietly does nothing when the shader has no such parameter.
	void setTerrainParameter(const char *name, const Real *values, Int count);
	void setWaterParameter(const char *name, const Real *values, Int count);

	/// Copies of the scene for the materials: the depth before the water when a water material runs, the
	/// depth and colour before the particles when particle materials run. Call at those points.
	void captureBeforeWater();
	void captureBeforeParticles();

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
	ID3D8On12Materials *prepare();			///< the device's materials, built for it; null when they do not run
	void setSunParameters(UINT material);
	void build(ID3D8On12Materials *materials);
	void release();
	void clearEntries();
	const Entry& lookup(TextureClass *baseTexture, const std::map<std::string, Int>& table, std::map<TextureClass *, Entry>& entries);
	bool bindEntry(const Entry& entry);
	TextureClass *findMap(const AsciiString& baseName, const AsciiString& spec);

	ID3D8On12Materials *m_materials;		///< the device the materials were built for
	Bool m_built;
	Bool m_enabled;
	Bool m_bound;
	Bool m_collecting;
	std::vector<unsigned int> m_shaders;
	std::vector<Built> m_builtMaterials;
	std::map<std::string, Int> m_textureMaterial;		///< base texture name (lower case, no extension) -> m_builtMaterials index
	std::map<std::string, Int> m_particleMaterial;		///< the same for particle textures
	std::map<TextureClass *, Entry> m_entries;
	std::map<TextureClass *, Entry> m_particleEntries;
	Int m_terrainMaterial;								///< m_builtMaterials index, -1 for none
	Int m_waterMaterial;
	TextureClass *m_waterMaps[4];
	Bool m_particleColor;								///< a particle material wants the scene colour (Distortion)
	std::set<std::string> m_seenTextures;
	AsciiString m_error;
};

extern W3DMaterials *TheW3DMaterials;
