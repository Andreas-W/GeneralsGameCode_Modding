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

// FILE: W3DMaterials.cpp /////////////////////////////////////////////////////
// HLSL materials for model meshes, drawn by the Direct3D 12 renderer.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DMaterials.h"

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/INI.h"
#include "GameClient/PostProcess.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"

#include <ctype.h>

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

W3DMaterials *TheW3DMaterials = nullptr;

#ifdef RTS_RENDERER_DX12

static const char *const SHADER_DIRECTORY = "Data\\Shaders\\Material\\";

// Base texture lit per pixel. Without maps it looks like the fixed function pipeline; a normal map (or
// HeightFromColor) bends the lighting, a specular map or Specular adds highlights, an emissive map glows
// and can scroll, pulse and flicker.
static const char s_standardShader[] = R"(
#include "Material.hlsli"
cbuffer Params : register(b2)
{
	float NormalStrength;	// scales the bumps of the normal map, 0 = 1
	float Specular;			// highlight strength without a specular map, or a factor for the map's red
	float Gloss;			// highlight sharpness, 0 = 24 (with a map: the map's green times this, 0 = 64)
	float EmissiveStrength;	// 0 = 1; above 1 feeds bloom
	float3 EmissiveColor;	// tints the emissive map, 0 0 0 = white
	float HeightFromColor;	// bumps from the base texture's brightness, for trying it without normal maps
	float2 ScrollSpeed;		// texture coordinates per second of the emissive map
	float PulseRate;		// pulses per second of the glow, 0 = steady
	float PulseMin;			// darkest point of a pulse, 0..1
	float FlickerRate;		// random brightness changes per second, 0 = none
	float FlipGreen;		// 1 for normal maps with green pointing down (OpenGL convention)
};
float Brightness(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }
float Random(float value) { return frac(sin(value * 12.9898) * 43758.5453); }
float4 main(MaterialInput input) : SV_Target
{
	float2 uv = input.tex0.xy;
	float4 base = BaseTexture.Sample(BaseSampler, uv);
	float3 normal = float3(0, 0, -1);
	bool hasNormal = dot(input.viewNormal, input.viewNormal) > 1e-6;
	if (hasNormal)
	{
		normal = normalize(input.viewNormal);
		if (HasMap(MAP_NORMAL) || HeightFromColor > 0)
		{
			float3 bump = float3(0, 0, 1);
			if (HasMap(MAP_NORMAL))
			{
				bump = NormalMap.Sample(NormalSampler, uv).xyz * 2 - 1;
				if (FlipGreen > 0)
					bump.y = -bump.y;
			}
			if (HeightFromColor > 0)
			{
				uint width, height;
				BaseTexture.GetDimensions(width, height);
				float2 texel = 1.0 / float2(max(width, 1), max(height, 1));
				float left = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(texel.x, 0)).rgb);
				float right = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(texel.x, 0)).rgb);
				float up = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(0, texel.y)).rgb);
				float down = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(0, texel.y)).rgb);
				bump.xy += float2(left - right, up - down) * HeightFromColor * 4;
			}
			bump.xy *= NormalStrength > 0 ? NormalStrength : 1;
			normal = normalize(mul(normalize(bump), TangentFrame(input.viewPosition, normal, uv)));
		}
	}

	float4 color = float4(base.rgb * Relight(input, normal), base.a * input.diffuse.a);

	if (hasNormal)
	{
		float strength = Specular;
		float power = Gloss > 0 ? Gloss : 24;
		if (HasMap(MAP_SPECULAR))
		{
			float4 map = SpecularMap.Sample(SpecularSampler, uv);
			strength = map.r * (Specular > 0 ? Specular : 1);
			power = 2 + map.g * (Gloss > 0 ? Gloss : 64);
		}
		if (strength > 0)
			color.rgb += SpecularLight(input, normal, power) * strength;
	}

	float3 glow = float3(0, 0, 0);
	if (HasMap(MAP_EMISSIVE))
	{
		float3 tint = any(EmissiveColor > 0) ? EmissiveColor : float3(1, 1, 1);
		float strength = EmissiveStrength > 0 ? EmissiveStrength : 1;
		if (PulseRate > 0)
			strength *= lerp(saturate(PulseMin), 1, 0.5 + 0.5 * sin(Time * PulseRate * 6.2831853));
		if (FlickerRate > 0)
			strength *= 0.7 + 0.3 * Random(floor(Time * FlickerRate));
		glow = EmissiveMap.Sample(EmissiveSampler, uv + ScrollSpeed * Time).rgb * tint * strength;
	}

	return Finish(color, glow, input);
}
)";

struct BuiltInMaterialShader
{
	const char *name;
	const char *source;
	size_t length;
};

static const BuiltInMaterialShader s_builtInShaders[] =
{
	{ "MAT_Standard", s_standardShader, sizeof(s_standardShader) - 1 },
};

//-------------------------------------------------------------------------------------------------
static char *readShaderFile(const AsciiString& path, UINT *length)
{
	File *file = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
	if (file == nullptr)
		return nullptr;
	*length = (UINT)file->size();
	return file->readEntireAndClose();
}

//-------------------------------------------------------------------------------------------------
/** #include in a material shader reads from Data\Shaders\Material. */
//-------------------------------------------------------------------------------------------------
class MaterialShaderIncludes : public ID3DInclude
{
public:
	HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID *data, UINT *bytes) override
	{
		AsciiString path;
		path.format("%s%s", SHADER_DIRECTORY, name);
		char *buffer = readShaderFile(path, bytes);
		if (buffer == nullptr)
			return E_FAIL;
		*data = buffer;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
	{
		delete[] (char *)data;
		return S_OK;
	}
};

//-------------------------------------------------------------------------------------------------
/** Lower case, without the house color prefix "#color#" and without the extension. */
//-------------------------------------------------------------------------------------------------
static std::string baseTextureName(const char *name)
{
	if (name[0] == '#')
	{
		const char *end = strchr(name + 1, '#');
		if (end != nullptr)
			name = end + 1;
	}
	std::string result(name);
	const size_t dot = result.find_last_of('.');
	if (dot != std::string::npos)
		result.erase(dot);
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = (char)tolower((unsigned char)result[i]);
	return result;
}

#endif // RTS_RENDERER_DX12

//-------------------------------------------------------------------------------------------------
W3DMaterials::W3DMaterials() :
	m_materials(nullptr),
	m_built(FALSE),
	m_enabled(TRUE),
	m_bound(FALSE),
	m_collecting(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
W3DMaterials::~W3DMaterials()
{
	if (TheMeshMaterialBinder == this)
		TheMeshMaterialBinder = nullptr;
	release();
	delete TheMaterialSettings;
	TheMaterialSettings = nullptr;
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::init()
{
	loadSettings();
#ifdef RTS_RENDERER_DX12
	TheMeshMaterialBinder = this;
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::loadSettings()
{
	delete TheMaterialSettings;
	TheMaterialSettings = nullptr;

	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\Material", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
	ini.loadFileDirectory("Data\\INI\\Material", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::reload()
{
	release();
	loadSettings();
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::isAvailable()
{
	return DX8Wrapper::Get_Materials() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::isRunning()
{
	return m_built && m_enabled && !m_builtMaterials.empty() && m_materials == DX8Wrapper::Get_Materials();
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::clearEntries()
{
	for (std::map<TextureClass *, Entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
		for (Int i = 0; i < 4; ++i)
			REF_PTR_RELEASE(it->second.maps[i]);
	m_entries.clear();
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::release()
{
#ifdef RTS_RENDERER_DX12
	if (m_materials != nullptr && m_materials == DX8Wrapper::Get_Materials())
	{
		m_materials->Set_Material(0, nullptr);
		for (size_t i = 0; i < m_builtMaterials.size(); ++i)
			m_materials->Release_Material(m_builtMaterials[i].id);
		for (size_t i = 0; i < m_shaders.size(); ++i)
			m_materials->Release_Shader(m_shaders[i]);
	}
#endif
	clearEntries();
	m_shaders.clear();
	m_builtMaterials.clear();
	m_textureMaterial.clear();
	m_materials = nullptr;
	m_built = FALSE;
	m_bound = FALSE;
	m_error.clear();
}

//-------------------------------------------------------------------------------------------------
/** Compiles the shaders and makes the renderer's materials. A material whose shader does not compile
	* is left out; the error is in the log and in getError(). */
//-------------------------------------------------------------------------------------------------
void W3DMaterials::build(ID3D8On12Materials *materials)
{
#ifdef RTS_RENDERER_DX12
	m_materials = materials;
	m_built = TRUE;
	if (TheMaterialSettings == nullptr)
		return;

	MaterialShaderIncludes includes;
	std::map<std::string, unsigned int> compiled;
	const std::vector<MaterialTemplate*>& templates = TheMaterialSettings->m_materials;
	for (size_t index = 0; index < templates.size(); ++index)
	{
		const MaterialTemplate *settings = templates[index];
		std::string shaderName = settings->m_shader.str();
		unsigned int shader = 0;
		std::map<std::string, unsigned int>::const_iterator found = compiled.find(shaderName);
		if (found != compiled.end())
		{
			shader = found->second;
		}
		else
		{
			// A file of the mod first, else a built-in shader.
			AsciiString path;
			path.format("%s%s.hlsl", SHADER_DIRECTORY, settings->m_shader.str());
			UINT length = 0;
			char *fileSource = readShaderFile(path, &length);
			const char *source = fileSource;
			if (source == nullptr)
			{
				for (size_t i = 0; i < ARRAY_SIZE(s_builtInShaders); ++i)
				{
					if (settings->m_shader.compareNoCase(s_builtInShaders[i].name) == 0)
					{
						source = s_builtInShaders[i].source;
						length = (UINT)s_builtInShaders[i].length;
					}
				}
			}
			if (source == nullptr)
			{
				m_error.format("Material %s: shader %s not found (%s)", settings->m_name.str(), settings->m_shader.str(), path.str());
				DEBUG_LOG(("%s", m_error.str()));
				compiled[shaderName] = 0;
				continue;
			}
			ID3DBlob *errors = nullptr;
			if (FAILED(materials->Create_Shader(source, length, settings->m_shader.str(), &includes, &shader, &errors)))
			{
				m_error.format("Material %s: shader %s: %s", settings->m_name.str(), settings->m_shader.str(),
					errors != nullptr ? (const char *)errors->GetBufferPointer() : "could not compile");
				DEBUG_LOG(("%s", m_error.str()));
				shader = 0;
			}
			else
			{
				m_shaders.push_back(shader);
			}
			if (errors != nullptr)
				errors->Release();
			delete[] fileSource;
			compiled[shaderName] = shader;
		}
		if (shader == 0)
			continue;

		Built built;
		built.settings = settings;
		built.id = 0;
		built.textureCount = 0;
		if (FAILED(materials->Create_Material(shader, &built.id)))
			continue;
		for (size_t i = 0; i < settings->m_parameters.size(); ++i)
		{
			const PostProcessParameter& parameter = settings->m_parameters[i];
			if (FAILED(materials->Set_Material_Parameter(built.id, parameter.m_name.str(), parameter.m_values, parameter.m_count)))
				DEBUG_LOG(("Material %s: the shader has no parameter %s", settings->m_name.str(), parameter.m_name.str()));
		}
		m_builtMaterials.push_back(built);
		for (size_t i = 0; i < settings->m_textures.size(); ++i)
			m_textureMaterial[baseTextureName(settings->m_textures[i].str())] = (Int)m_builtMaterials.size() - 1;
	}
#endif
}

//-------------------------------------------------------------------------------------------------
/** A map of a base texture: a suffix such as "_n" adds to the base name, anything else is a file name.
	* Null when the file does not exist, so that the shader knows to go without. */
//-------------------------------------------------------------------------------------------------
TextureClass *W3DMaterials::findMap(const AsciiString& baseName, const AsciiString& spec)
{
#ifdef RTS_RENDERER_DX12
	if (spec.isEmpty() || spec.compareNoCase("None") == 0)
		return nullptr;
	std::string name = spec.getCharAt(0) == '_' ? std::string(baseName.str()) + spec.str() : baseTextureName(spec.str());
	static const char *const extensions[] = { ".dds", ".tga" };
	for (size_t i = 0; i < ARRAY_SIZE(extensions); ++i)
	{
		AsciiString path;
		path.format("Art\\Textures\\%s%s", name.c_str(), extensions[i]);
		if (TheFileSystem->doesFileExist(path.str()))
		{
			AsciiString file;
			file.format("%s%s", name.c_str(), extensions[i]);
			DEBUG_LOG(("Material: %s uses %s", baseName.str(), path.str()));
			return WW3DAssetManager::Get_Instance()->Get_Texture(file.str());
		}
	}
#endif
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const W3DMaterials::Entry& W3DMaterials::lookup(TextureClass *baseTexture)
{
	std::map<TextureClass *, Entry>::iterator found = m_entries.find(baseTexture);
	if (found != m_entries.end())
		return found->second;

	Entry entry;
	entry.material = -1;
	for (Int i = 0; i < 4; ++i)
		entry.maps[i] = nullptr;
#ifdef RTS_RENDERER_DX12
	const std::string name = baseTextureName(baseTexture->Get_Texture_Name().str());
	std::map<std::string, Int>::const_iterator material = m_textureMaterial.find(name);
	if (material != m_textureMaterial.end())
	{
		entry.material = material->second;
		Built& built = m_builtMaterials[entry.material];
		++built.textureCount;
		const AsciiString baseName(name.c_str());
		for (Int i = 0; i < 4; ++i)
			entry.maps[i] = findMap(baseName, built.settings->m_maps[i]);
	}
#endif
	return m_entries[baseTexture] = entry;
}

//-------------------------------------------------------------------------------------------------
/** Before the meshes of a texture category: their base texture decides the material. */
//-------------------------------------------------------------------------------------------------
bool W3DMaterials::Begin(TextureClass *baseTexture)
{
#ifdef RTS_RENDERER_DX12
	if (!m_enabled || baseTexture == nullptr)
		return false;
	ID3D8On12Materials *materials = DX8Wrapper::Get_Materials();
	if (materials == nullptr)
		return false;
	if (m_collecting)
		m_seenTextures.insert(baseTextureName(baseTexture->Get_Texture_Name().str()));
	if (TheMaterialSettings == nullptr)
		return false;
	if (materials != m_materials || !m_built)
	{
		// First use, or the device was created again.
		release();
		build(materials);
	}
	const Entry& entry = lookup(baseTexture);
	if (entry.material < 0)
		return false;
	// The maps go to the renderer directly: the stage textures of DX8Wrapper can be out of date, as the
	// terrain shaders set some on the device themselves.
	IDirect3DBaseTexture8 *maps[4];
	for (Int i = 0; i < 4; ++i)
	{
		maps[i] = nullptr;
		if (entry.maps[i] != nullptr)
		{
			if (!entry.maps[i]->Is_Initialized())
				entry.maps[i]->Init();
			maps[i] = entry.maps[i]->Peek_D3D_Base_Texture();
		}
	}
	materials->Set_Material(m_builtMaterials[entry.material].id, maps);
	m_bound = TRUE;
	return true;
#else
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::End()
{
#ifdef RTS_RENDERER_DX12
	if (!m_bound)
		return;
	if (m_materials != nullptr)
		m_materials->Set_Material(0, nullptr);
	m_bound = FALSE;
#endif
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::setParameter(const AsciiString& material, const AsciiString& name, const Real* values, Int count)
{
#ifdef RTS_RENDERER_DX12
	if (!isRunning())
		return FALSE;
	for (size_t i = 0; i < m_builtMaterials.size(); ++i)
	{
		if (m_builtMaterials[i].settings->m_name.compareNoCase(material) == 0)
			return SUCCEEDED(m_materials->Set_Material_Parameter(m_builtMaterials[i].id, name.str(), values, (UINT)count));
	}
#endif
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::getMaterials(std::vector<AsciiString>& names, std::vector<Int>& meshTextures)
{
	names.clear();
	meshTextures.clear();
	for (size_t i = 0; i < m_builtMaterials.size(); ++i)
	{
		names.push_back(m_builtMaterials[i].settings->m_name);
		meshTextures.push_back(m_builtMaterials[i].textureCount);
	}
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::collectTextures(std::vector<AsciiString>& names)
{
	names.clear();
	for (std::set<std::string>::const_iterator it = m_seenTextures.begin(); it != m_seenTextures.end(); ++it)
		names.push_back(AsciiString(it->c_str()));
	m_seenTextures.clear();
	m_collecting = TRUE;
}
