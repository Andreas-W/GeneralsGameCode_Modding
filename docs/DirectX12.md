# Direct3D 12 renderer

Zero Hour normally renders with Direct3D 8. This fork can also render with Direct3D 12. On top of matching
the old picture, the Direct3D 12 renderer adds effects that mods can configure and extend with their own
HLSL shaders:

- **Post-processing**: bloom, tone mapping, color grading, auto exposure, ambient occlusion, fog, depth of
  field, edge smoothing, sharpening and vignette, plus your own passes.
- **Soft shadow maps** instead of the hard stencil shadow volumes. Hills, cliffs and trees cast shadows
  too.
- **HLSL materials** for models, particles, water and terrain: normal, specular and glow maps, animated
  glow, team colour glow, soft particles, heat haze, waves with sun glints and shore foam, and bumpy terrain.
- **Per-map and scripted control**: each map can bring its own look, the look can change with the time of
  day, and map scripts can fade effects in and out.

Direct3D 8 stays the default. Without `-dx12` the game, WorldBuilder and W3DView behave exactly as before.
The new INI files and fields are read but ignored, so one mod can ship for both renderers.

---

## For players

### Requirements

- Windows 10 or newer and a graphics card with Direct3D feature level 11_0. That covers practically every
  card from the last ten years.
- A game exe built with the Direct3D 12 renderer. The mod tells you whether its exe has it.

### Starting the game with Direct3D 12

Add `-dx12` to the command line, for example in a shortcut:

```
generalszh.exe -dx12
```

WorldBuilder (`WorldBuilderZH.exe -dx12`) and W3DView (`W3DViewZH.exe -dx12`) take the same switch.

### Options

| Option | Where | Effect under `-dx12` |
|---|---|---|
| Post-processing | Options menu checkbox (if the mod has added it), or `PostProcessing = yes/no` in `Options.ini` | Turns the whole post-processing chain on or off. |
| 3D Shadows | Options menu | Needed for shadow maps. Off means no unit shadows, as before. |
| Anti-aliasing | Options menu | Works as with Direct3D 8. The screen filters (black and white, blur, fades) also work with anti-aliasing on, which Direct3D 8 cannot do. |

Everything else (resolution, windowed mode, fullscreen, Alt+Tab) works as with Direct3D 8.

### What you will see

You only see changes if the mod ships the INI files described below. Without them, Direct3D 12 draws the
same picture as Direct3D 8.

### If something goes wrong

- Start once without `-dx12` to check whether the problem comes from the renderer.
- Set the environment variable `D3D8ON12_LOG=1` before starting. The renderer then writes
  `d3d8on12.log` into the game folder. Attach it when you report a problem.

---

## For modders

### Files

| File | Contents |
|---|---|
| `Data\INI\PostProcess.ini` (also `Data\INI\Default\PostProcess\*.ini` and `Data\INI\PostProcess\*.ini`) | `PostProcessPass`, `PostProcess`, `PostProcessChain` and `ShadowMap` blocks |
| `Data\INI\Material.ini` (also `Data\INI\Default\Material\*.ini` and `Data\INI\Material\*.ini`) | `Material` blocks |
| `Data\Shaders\PostProcess\<Shader>.hlsl` | Your own post-processing passes, or replacements for built-in ones with the same name |
| `Data\Shaders\Material\<Shader>.hlsl` | Your own material shaders, or replacements for built-in ones |
| `Art\Textures\<texture>_nrm`, `_spec`, `_emis` | Normal, specular and glow maps for model textures (`.dds` or `.tga`) |
| `Art\Terrain\<terrain texture>_nrm.tga`, `_spec.tga` | Normal and specular maps for terrain textures |
| `<map folder>\map.ini` | Render blocks for one map only |

All of these can live inside `.big` files. Rules for every field:

- A parameter that is left out or set to 0 means "default" or "off" in every built-in shader.
- Without `-dx12` everything here is ignored.
- Edits to existing files can be reloaded while the game or WorldBuilder runs (see
  [Working on effects](#working-on-effects)). New files need a restart, because the game lists its data
  files only at startup.

### Post-processing

The chain is a list of passes. Each pass is an HLSL pixel shader that reads up to six inputs and draws into
its own texture. The last pass draws into the screen. Only the 3D view is processed; the interface, health
bars and mouse cursor are drawn afterwards and stay sharp.

```ini
PostProcessPass BloomExtract
  Shader = PP_BloomExtract      ; built-in, or Data\Shaders\PostProcess\<Shader>.hlsl
  Inputs = Scene                ; Input0..Input5, see below
  Scale = 2                     ; output size = screen size / Scale; 0 = one pixel
  HDR = Yes                     ; 16 bit float output, for values above 1
  Param = Threshold 1           ; a variable of the shader's Params, 1 to 4 numbers
  ParamNight = Threshold 0.6    ; replaces Param at night (ParamMorning, ParamAfternoon, ParamEvening, ParamNight)
End

PostProcess
  Passes = BloomExtract BloomBlurH BloomBlurV ColorGrade
  HDRScene = Yes                ; draw the scene in 16 bit float so bright light survives into bloom and tone mapping
End

PostProcessChain Dream          ; an alternative list, for the "Run another chain" script action
  Passes = BloomExtract BloomBlurH BloomBlurV ColorGrade DreamBlur
End
```

**Inputs**:

| Input | What it gives the pass |
|---|---|
| `Scene` | The 3D view as drawn |
| `Depth` | The scene depth (for effects that need distance or world position) |
| `<pass name>` | The output of an earlier pass |
| `Previous` | This pass's own output from the frame before (for effects that build up over time) |
| `Texture:<file>` | A texture, for example a color lookup table |
| `None` | Nothing (keeps the position of later inputs) |

**Built-in passes**:

| Shader | Inputs | Parameters (default when 0) |
|---|---|---|
| `PP_BloomExtract` | scene | `Threshold` (0.75), `Knee` (0.1) |
| `PP_Blur` | image | `Direction` (`1 0` or `0 1`), `Radius` (1) |
| `PP_ColorGrade` | scene, bloom, lookup table (256×16), ambient occlusion, auto exposure, all but the scene optional | `BloomIntensity`, `Exposure` (stops), `Contrast`, `Saturation`, `Tint` + `TintStrength`, `Lift`, `Gamma`, `Gain`, `LutStrength`, `AOStrength` (1), `Tonemap` (0 none, 1 ACES, 2 filmic, 3 Reinhard), `WhitePoint` (4), `HighlightExpand` |
| `PP_Luminance` | scene | none; use `Scale = 8`, `HDR = Yes` |
| `PP_AutoExposure` | `PP_Luminance` output, `Previous` | `Key` (0.18), `Speed` (1.5), `MinExposure` / `MaxExposure` (−2 / 2), `Strength` (1); use `Scale = 0`, `HDR = Yes`, feed into `PP_ColorGrade`'s fifth input |
| `PP_SSAO` | `Depth` | `Radius` (20 world units), `Intensity` (1), `Bias` (2), `Power` (1.5) |
| `PP_DepthBlur` | image, `Depth` | `Direction`, `Sharpness` (1); blurs without crossing depth edges, for ambient occlusion |
| `PP_Fog` | image, `Depth` | `Color`, `Density` (1), `Start` (700), `End` (1600), `HeightStart`, `HeightEnd` |
| `PP_DepthOfField` | image, `Depth` | `FocusDistance` (screen centre), `FocusRange` (150), `BlurRange` (500), `MaxBlur` (3 px), `FarOnly` (1 keeps aircraft sharp) |
| `PP_EdgeAA` | image | `EdgeThreshold` (0.166), `EdgeThresholdMin` (0.0625), `Subpixel` (0.75) |
| `PP_SharpenVignette` | image | `Sharpen` (0..1), `Vignette` (0..1), `VignetteRadius` (0.75), `VignetteSoftness` (0.45) |
| `PP_Copy` | image | none; useful to look at an intermediate result |
| `PP_DepthView` | `Depth` | `Far` (2000); shows distance as grey |

A complete example chain with ambient occlusion, bloom, auto exposure, tone mapping, fog, depth of field,
edge smoothing and a vignette:

```ini
PostProcessPass SSAO
  Shader = PP_SSAO
  Inputs = Depth
  Scale = 2
  Param = Intensity 2
End
PostProcessPass AOBlurH
  Shader = PP_DepthBlur
  Inputs = SSAO Depth
  Scale = 2
  Param = Direction 1 0
End
PostProcessPass AOBlurV
  Shader = PP_DepthBlur
  Inputs = AOBlurH Depth
  Scale = 2
  Param = Direction 0 1
End
PostProcessPass BloomExtract
  Shader = PP_BloomExtract
  Inputs = Scene
  Scale = 2
  HDR = Yes
  Param = Threshold 1
End
PostProcessPass BloomBlurH
  Shader = PP_Blur
  Inputs = BloomExtract
  Scale = 4
  HDR = Yes
  Param = Direction 1 0
  Param = Radius 1.5
End
PostProcessPass BloomBlurV
  Shader = PP_Blur
  Inputs = BloomBlurH
  Scale = 4
  HDR = Yes
  Param = Direction 0 1
  Param = Radius 1.5
End
PostProcessPass Luminance
  Shader = PP_Luminance
  Inputs = Scene
  Scale = 8
  HDR = Yes
End
PostProcessPass AutoExposure
  Shader = PP_AutoExposure
  Inputs = Luminance Previous
  Scale = 0
  HDR = Yes
  Param = MinExposure -1
  Param = MaxExposure 1
  Param = Strength 0.5          ; half the correction, so night maps stay darker than day maps
End
PostProcessPass ColorGrade
  Shader = PP_ColorGrade
  Inputs = Scene BloomBlurV None AOBlurV AutoExposure
  Param = BloomIntensity 0.6
  Param = Tonemap 1
  Param = Exposure 0.25
End
PostProcessPass Fog
  Shader = PP_Fog
  Inputs = ColorGrade Depth
  Param = Color 0.55 0.6 0.65
  Param = Density 0.6
End
PostProcessPass DepthOfField
  Shader = PP_DepthOfField
  Inputs = Fog Depth
End
PostProcessPass EdgeAA
  Shader = PP_EdgeAA
  Inputs = DepthOfField
End
PostProcessPass SharpenVignette
  Shader = PP_SharpenVignette
  Inputs = EdgeAA
  Param = Sharpen 0.4
  Param = Vignette 0.35
End

PostProcess
  Passes = SSAO AOBlurH AOBlurV BloomExtract BloomBlurH BloomBlurV Luminance AutoExposure ColorGrade Fog DepthOfField EdgeAA SharpenVignette
  HDRScene = Yes
End
```

The game's camera sees roughly 500 to 1500 world units far, which is useful when you tune fog and depth of
field.

### Shadow maps

```ini
ShadowMap                       ; in the PostProcess INI
  Enabled = Yes
  Size = 2048                   ; texels of the map that covers the visible area
  Softness = 1.5                ; radius of the soft edge in texels
  Bias = 2                      ; against shadow acne; world units towards the sun
  DepthBias = 0
  SlopeBias = 2
  Darkness = 1                  ; 1 = the map's shadow colour; higher = darker with the same hue; 0 = none
  MinSunElevation = 0           ; degrees; raises a low sun so shadows do not get very long
  Terrain = Yes                 ; hills and cliffs cast shadows
  Trees = Yes                   ; trees cast real shadows instead of their decal blobs
  MaxUpdateRate = 60            ; map redraws per second while the view stands still; 0 = every frame
  DetailCascade = Yes           ; a second, sharper map around the middle of the view
  DetailCoverage = 0.3          ; its width as a fraction of the main map
  DetailSize = 0                ; its texels, 0 = Size
End
```

Every object with `Shadow = SHADOW_VOLUME` casts into the map. Infantry keep their decal shadows. The
player's "3D Shadows" option must be on. The shadow colour comes from the map's lighting, as before.

`ShadowSizeX` on a volume shadow works as with Direct3D 8: it is the lowest sun elevation, in degrees, for
that object's shadow. With `ShadowSizeX = 89` (common on aircraft) the shadow falls almost straight below
the object, so players can see where it flies. Objects with `ShadowDynamicLengthWhenAirborne = Yes` blend
from their `ShadowSizeX` to almost vertical as they lift off, as before.

Shadow maps are usually longer than the old volume shadows, which were cut off. On maps with a low sun,
`MinSunElevation = 40` keeps them close to the old length.

### Materials

A material gives meshes an HLSL pixel shader. There are four kinds (`Use`).

#### Models (`Use = Meshes`, the default)

Materials are assigned by base texture. Every mesh drawn with one of the listed textures gets the material,
on every model and every team colour variant:

```ini
Material AmericaTanks
  Textures = AVTank AVPaladin   ; base textures without extension, not case sensitive
  Shader = MAT_Standard
  NormalMap = _nrm              ; suffix (looks for avtank_nrm), a file name, or None; default _nrm
  SpecularMap = _spec           ; default _spec; red = strength, green = gloss
  EmissiveMap = _emis           ; default _emis; glow colour
  Param = Gloss 32
  Param = EmissiveStrength 2
End
```

A model can also take a material as a whole. Put `Material = AmericaTanks` into its `ModelDraw` block
(`W3DModelDraw`, `W3DTankDraw`, ...). All its opaque meshes then get that material, with the maps found
for each of their textures. Missing maps are fine: the shader just goes without them.

Names ending in `_N`, `_S` and `_E` are already used by retail textures for night, snow and other variants.
That is why the map suffixes default to `_nrm`, `_spec` and `_emis`.

`MAT_Standard` parameters:

| Parameter | Effect |
|---|---|
| `NormalStrength` | Scales the normal map's bumps (1) |
| `FlipGreen` | 1 for normal maps with green pointing down |
| `HeightFromColor` | Fake bumps from the base texture's brightness, to try the look without new art |
| `Specular`, `Gloss` | Highlight strength and sharpness (24; 64 with a map) |
| `EmissiveStrength`, `EmissiveColor` | Glow brightness (above 1 feeds bloom) and tint |
| `TeamEmissive` | 0..1: how much the glow takes the owner's team colour |
| `ScrollSpeed` | Glow texture scroll, texture coordinates per second |
| `PulseRate`, `PulseMin` | Pulsing glow |
| `FlickerRate` | Random glow flicker |

With no maps and no parameters, `MAT_Standard` looks like the old fixed function lighting, only computed
per pixel.

#### Particles (`Use = Particles`)

Matched by particle system texture:

```ini
Material SoftSmoke
  Use = Particles
  Textures = EXSmokNew1
  Shader = MAT_Particle
  Param = SoftDistance 20
End
```

| Parameter | Effect |
|---|---|
| `SoftDistance` | Fades the particle where it meets the ground or units, in world units, so there is no hard cut |
| `EmissiveStrength` | Brightness; above 1 feeds bloom (fire, muzzle flashes) |
| `PulseRate`, `PulseMin` | Pulsing |
| `ScrollSpeed` | Texture scroll |
| `Distortion`, `DistortionSpeed` | Heat haze: bends the scene behind the particle (0.02 is strong) |

#### Water (`Use = Water`, one per mod)

Draws standing water and rivers. The maps are file names in `Art\Textures`: `NormalMap` = waves,
`SpecularMap` = foam, `EmissiveMap` = sky.

```ini
Material Water
  Use = Water
  Shader = MAT_Water
End
```

| Parameter | Effect |
|---|---|
| `WaveScale`, `WaveSpeed`, `WaveStrength` | Size (160), speed (12) and strength (0.25) of the waves |
| `Gloss`, `GlintStrength` | Sun glints (180, 2) |
| `SkyColor`, `HorizonColor`, `Reflection` | Reflected sky, stronger at flat angles |
| `DeepColor`, `DepthScale` | Colour of deep water and the depth until it is reached (40) |
| `ShoreDistance` | Soft fade at the shore (3) |
| `FoamDistance`, `FoamStrength` | Foam along the shore (4, 0.5; `FoamDistance -1` = no foam) |

#### Terrain (`Use = Terrain`, one per mod)

```ini
Material Terrain
  Use = Terrain
  Shader = MAT_Terrain
  Param = Specular 0.3
End
```

Bumps come from `Art\Terrain\<terrain texture>_nrm.tga` where it exists, otherwise from the texture's
brightness. Highlights come from `_spec.tga`. Clouds, the light map and the shroud stay as before.
Parameters: `HeightFromColor` (1; −1 = no brightness bumps), `NormalStrength`, `Specular`, `Gloss` (32),
`FlipGreen`. At the lowest terrain detail only brightness bumps are used.

### Per map, time of day and scripts

**Per map**: a map's `map.ini` can contain `PostProcessPass`, `PostProcess`, `PostProcessChain` and
`ShadowMap` blocks. They change the mod's settings for that map only. A block with the name of an existing
pass changes only the fields it lists (a `Param` replaces the parameter of the same name), and the mod's
settings come back when the game ends. Example: a sepia look and
darker shadows for one mission:

```ini
PostProcessPass ColorGrade
  Param = Saturation -0.7
  Param = Tint 0.9 0.75 0.55
  Param = TintStrength 0.3
End
ShadowMap
  Darkness = 1.5
End
```

**Time of day**: `ParamMorning`, `ParamAfternoon`, `ParamEvening` and `ParamNight` replace a pass's `Param`
when the map runs at that time. For example, use stronger bloom at night.

**Scripts**: WorldBuilder lists these actions under *Map / Post-processing*:

| Action | What it does |
|---|---|
| Set or fade a pass parameter | Pass, parameter, 1 to 4 numbers, seconds to fade over (0 = at once) |
| Switch a pass on or off | A pass that is off hands its first input on, so the chain keeps working |
| Run another chain | Switches to a `PostProcessChain`, or back with `Default` |
| Set or fade the shadow darkness | Fades the shadow map's `Darkness` |

Use them for cutscenes, a flashback in black and white, fading into night, or a blur when the player is
hit. Save games keep these changes. Under Direct3D 8 the actions do nothing. Maps that use them need this
engine.

### Writing your own shaders

Shaders are HLSL pixel shaders (`ps_5_0`) with the entry point `main`. Put them in
`Data\Shaders\PostProcess\` or `Data\Shaders\Material\` and name them in `Shader =`. If a file has the same
name as a built-in shader, the file wins, so you can start from a copy of a built-in one. Compile errors go
to the debug log (and `render_postprocess` / `render_materials` report them). A broken pass switches the
whole chain off until it is reloaded; a broken material switches off only that material.

**A post-processing pass** includes `PostProcess.hlsli` and reads its parameters from `cbuffer Params :
register(b1)`. Any variable there can be set with `Param =` and scripts:

```hlsl
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
    float Strength;     // Param = Strength 0.5
    float3 Color;       // Param = Color 1 0.8 0.6
};
float4 main(PostProcessInput input) : SV_Target
{
    float4 scene = Input0.Sample(LinearClamp, input.uv);
    float grey = Luminance(scene.rgb);
    return float4(lerp(scene.rgb, grey * Color, Strength), 1);
}
```

`PostProcess.hlsli` gives you:
- the textures `Input0`..`Input5`, the samplers `LinearClamp` and `PointClamp`, and `input.uv`;
- `ScreenSize`, `OutputSize` and `InputSize[6]` (width, height, 1/width, 1/height);
- `Time`, `FrameTime`, and the camera matrices `View`, `InverseView`, `Projection` and `InverseProjection`;
- the helpers `Luminance(color)`, `ViewPosition(uv, depth)`, `WorldPosition(viewPosition)` and
  `LinearDepth(depth)`, for passes that read `Depth`.

**A material** includes `Material.hlsli` and reads its parameters from `cbuffer Params : register(b2)`:

```hlsl
#include "Material.hlsli"
cbuffer Params : register(b2)
{
    float3 HouseColor;  // filled in by the game: the owner's team colour
    float Glow;         // Param = Glow 2
};
float4 main(MaterialInput input) : SV_Target
{
    float4 base = BaseTexture.Sample(BaseSampler, input.tex0.xy);
    float3 normal = normalize(input.viewNormal);
    float4 color = float4(base.rgb * Relight(input, normal), base.a * input.diffuse.a);
    float3 glow = HasMap(MAP_EMISSIVE) ? EmissiveMap.Sample(EmissiveSampler, input.tex0.xy).rgb * HouseColor * Glow : 0;
    return Finish(color, glow, input);
}
```

`Material.hlsli` gives you:
- the textures `BaseTexture`, `DetailTexture`, `NormalMap`, `SpecularMap`, `EmissiveMap` and `ExtraMap`
  with their samplers, and `HasMap(MAP_...)`;
- the vertex lighting, view position and view normal in `MaterialInput`;
- `Relight(input, normal)`, which relights the vertex lighting for a bent normal, and
  `SpecularLight(input, normal, power)`;
- `TangentFrame(viewPosition, normal, uv)`, for normal maps without tangents in the model;
- `Finish(color, glow, input)`, which applies fog and the alpha test;
- `MaterialTime` (seconds), and for particles and water the scene behind: `SceneDepth`, `SceneColor`,
  `ScreenUV`, `DepthBehind` and `WorldPosition`.

The full interface is documented in `Core/Libraries/Source/D3D8on12/d3d8on12_ext.h`. The built-in shaders
in `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DPostProcess.cpp` and `W3DMaterials.cpp` are good
examples.

### Working on effects

- **WorldBuilder** started with `-dx12` shows post-processing, materials and shadow maps, including the open
  map's `map.ini` blocks. Use *View > Reload Post-Processing and Materials (DX12)* after editing an INI or
  shader file, and *View > Show Post-Processing (DX12)* to compare with and without.
- **In the game**, the game MCP (see `scripts/worldbuilder-mcp/README.md`) can tune everything live while
  a game runs:
  - `render_postprocess`: toggle the chain, reload, set parameters;
  - `render_materials`: toggle, reload, set parameters, and list the textures on screen to find names
    for `Textures =`;
  - `render_shadows`: change shadow map values, show debug views of the shadow amount and the detail area;
  - `render_timing`: GPU cost per pass.
- Keep the Direct3D 8 picture in mind. Everything here is extra, and players without `-dx12` see the mod as
  it always was.

---

## For developers

- **Design**: the library `Core/Libraries/Source/D3D8on12` implements the Direct3D 8 interfaces
  (`IDirect3D8`, `IDirect3DDevice8`, textures, buffers, surfaces) on Direct3D 12, the way DXVK and d3d8to9
  do it. `DX8Wrapper::Init` creates it instead of `d3d8.dll` when the game runs with `-dx12`, so the engine
  above it is unchanged. The fixed function pipeline is emulated with generated HLSL, and the old
  `vs.1.1`/`ps.1.x` shaders are translated to HLSL.
- **New features** are COM extensions on the device: `ID3D8On12PostProcess`, `ID3D8On12ShadowMap` and
  `ID3D8On12Materials` in `d3d8on12_ext.h`. The engine gets them with `QueryInterface`
  (`DX8Wrapper::Get_Post_Process()` and others). The Direct3D 8 device has none of them, so the engine skips
  those features there.
- **Engine side**: `W3DPostProcess`, `W3DMaterials` and `W3DShadowMap`; the INI parsing and
  `PostProcessControl` (map and script state, saved in save games) are in
  `Core/GameEngine/Source/GameClient/PostProcess.cpp`.
- **Build**: the CMake option `RTS_BUILD_OPTION_DX12` (default `OFF`) builds the renderer and defines
  `RTS_RENDERER_DX12`. `d3d12.dll` and `d3dcompiler_47.dll` are delay-loaded, so they are only loaded
  when the game runs with `-dx12`.
- **Debugging**: the environment variable `D3D8ON12_LOG=1` writes `d3d8on12.log`, and `D3D8ON12_DEBUG=1`
  turns on the Direct3D 12 debug layer (needs the Graphics Tools Windows feature).
- **Testing**: the game MCP's `view_screenshot` makes it easy to compare Direct3D 8 and Direct3D 12 at the
  same camera position. Rendering is client-only, so replays and multiplayer stay in sync between players
  who use different renderers.
