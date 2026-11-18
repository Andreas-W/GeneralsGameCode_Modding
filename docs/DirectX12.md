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
  Passes = BloomExtract BloomBlurH BloomBlurV ColorGrade DepthOfField
  Disabled = DepthOfField       ; passes of Passes that are switched off; they keep their place (after Passes)
  HDRScene = Yes                ; draw the scene in 16 bit float so bright light survives into bloom and tone mapping
End

PostProcessChain Dream          ; an alternative list, for the "Run another chain" script action
  Passes = BloomExtract BloomBlurH BloomBlurV ColorGrade DreamBlur
End
```

**Switched off passes** (`Disabled`, a script, or not in `Passes`): the pass is left out of the chain. A pass that reads it
gets the switched off pass's first input as its Input0, so the image goes through. Input1..5 are optional
(bloom, lookup table, occlusion, exposure): one that comes from a switched off pass, or from a pass whose
image came through one, is `None`. Switching off `SSAO` therefore removes the occlusion, although
`AOBlurH` and `AOBlurV` are still on, and passes whose output nothing reads any more do not run. A pass
can stay in `Passes` and in the `Inputs` of other passes while it is off. `Passes` clears `Disabled`, so
write `Disabled` after it.

So connect every pass as if all were on: each image pass reads the image pass before it, and the end of
every branch sits in its slot of `PP_ColorGrade`. A pass that is on but that nothing reads on the way to
the screen does not run (for example `AutoExposure` while `PP_ColorGrade` has no fifth input). The FX
Editor's **Auto inputs** sets the `Inputs` this way from the shaders and the order of the passes.

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
| `PP_ColorGrade` | scene, bloom, lookup table (256×16), ambient occlusion, auto exposure, all but the scene optional | `BloomIntensity`, `Exposure` (stops), `Contrast`, `Saturation`, `Tint` + `TintStrength`, `Lift`, `Gamma`, `Gain`, `LutStrength`, `AOStrength` (1), `Tonemap` (picks a curve, a whole number: 0 none, 1 ACES, 2 filmic, 3 Reinhard), `TonemapStrength` (1; less blends toward the image without the curve, clipped at white), `WhitePoint` (4; filmic and Reinhard only), `HighlightExpand` |
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

A material can start from another one with `Base` as its first line. Everything is copied (`Use`, `Textures`,
`Shader`, the maps, `Param` and the time-of-day `Param` lines); the lines after it change the copy, and a `Param`
replaces the one of the same name:

```ini
Material OceanShips
  Base = Ocean                 ; first line: lines before it would be undone
  Param = Refraction 1
  Param = WaveAmplitude 3.6
End
```

The FX Editor's Direct3D 12 panel shows what a material gets from its `Base` in blue, with the base's name in the
tooltip. Changing such a field or parameter, or its `+` button, writes it into the material itself; its `x`
removes it again, back to the base's value. Time-of-day lines of the base are inherited too and are edited in
the text.

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

#### Water (`Use = Water`)

Draws standing water and rivers. The maps are file names in `Art\Textures`: `NormalMap` = waves,
`SpecularMap` = foam, `EmissiveMap` = sky; `MAT_Ocean` also takes `ExtraMap` as the crest foam pattern of
standing water (rivers use that slot for their edge, so their crests use `SpecularMap`). Without a `WaterRendering` block (see below) all water uses the
last water material.

There are two built-in water shaders:
- `MAT_Water`: flat water, as before, with shader waves, glints, sky and shore foam.
- `MAT_Ocean`: water that moves. Its surface rises and falls in real waves. By default it is opaque like
  `MAT_Water`, coloured by the map's water texture and water colour, so the shallow water of maps not made
  for ships hides its ground. With `Param = Refraction 1` you see through it to the ground and to units below:
  the water darkens what is below it by its depth, and caustics play on the ground. Ripples, sky and screen
  reflections, sun glints, shore foam, and waves running up the beach come on top either way.

```ini
Material Water
  Use = Water
  Shader = MAT_Water
End
```

`MAT_Water` parameters:

| Parameter | Effect |
|---|---|
| `WaveScale`, `WaveSpeed`, `WaveStrength` | Size (160), speed (12) and strength (0.25) of the waves |
| `Gloss`, `GlintStrength` | Sun glints (180, 2) |
| `SkyColor`, `HorizonColor`, `Reflection` | Reflected sky overhead (0.35 0.5 0.7) and at the horizon (0.7 0.8 0.9), stronger at flat angles (0.4) |
| `DeepColor`, `DepthScale`, `DeepOpacity` | Colour of deep water (0.02 0.08 0.12), the depth until it is reached (40), how opaque it gets (0.7) |
| `ShoreDistance` | Soft fade at the shore (3) |
| `FoamDistance`, `FoamStrength` | Foam along the shore (4, 0.5; `FoamDistance -1` = no foam) |

`MAT_Ocean` parameters. A value of 0 means the default in brackets; −1 turns off what is marked:

| Parameter | Effect |
|---|---|
| `WaveAmplitude` | Height of the largest wave in world units (1.5; −1 = flat) |
| `WaveLength`, `WaveSpeed`, `WaveDirection` | Longest wave (140), speed factor (1), direction x y (1 0.35); three shorter waves run beside it |
| `WaveChoppiness` | 0..1, how sharp the crests are (0.6) |
| `NoiseHeight` | World units the fractal noise lifts and lowers the surface on top of the waves, so that their pattern does not repeat (0 = none). Three layers of the shared `NoiseTexture`, each of another size, turned and drifting its own way. It moves the vertices (coarse layers) and the per-pixel normal, and counts into the wave height, so crest foam and crest colour break up where the noise is low. About 65 more instructions and 3 texture reads per pixel when on; nothing when 0 |
| `NoiseScale` | World units one repeat of the largest layer covers (600); the others are 0.43 and 0.19 of it |
| `NoiseSpeed` | World units per second the layers drift (3) |
| `NoiseDetail` | 0..1, strength of each finer layer against the one before (0.5) |
| `WaveFadeDistance` | Camera distance at which the waves are flat (1400) |
| `ShoreFadeDistance`, `DepthFade` | Distance from the shore (60) and water depth (10) over which the waves grow |
| `NormalStrength`, `NormalScale`, `NormalSpeed` | Ripples: strength (0.35), size (90), speed (6) |
| `SkyColor`, `HorizonColor`, `Reflection` | Reflected sky overhead (0.30 0.45 0.65) and at the horizon (0.70 0.80 0.90), or a latitude-longitude `EmissiveMap`, and its strength at flat angles (0.8) |
| `SSRStrength`, `SSRMaxDistance` | Reflection of what is on screen (0.8; −1 = none) and how far it is searched (400) |
| `Gloss`, `GlintStrength` | Sun glints (800, 1.2) |
| `ViewGlint` | Weaker glints from a second light that turns with the camera and always stands beyond the water, so that glints show whichever way the camera faces (the sun's only show when it looks towards the sun), as a factor of `GlintStrength` (0 = none; 0.35 is subtle). Where both meet the stronger counts |
| `ViewGlintGloss`, `ViewGlintHeight` | Their sharpness (half of `Gloss`) and the height of that light above the horizon in degrees (the sun's height); a higher light moves them towards the bottom of the screen |
| `Refraction` | 1 = see through the water (deep water made for ships); 0 = opaque (the default). Needs `WaterRendering Refraction = Yes` (the default). `Absorption`, `ScatterColor`, `UnderwaterAmbient`, `RefractionStrength` and the caustics only work with it; `DeepColor`, `DeepOpacity` and `DepthScale` only without it |
| `RefractionStrength` | How much the waves bend what is below (1) |
| `Absorption` | How fast red, green and blue fade per world unit of water (0.035 0.012 0.008) |
| `ScatterColor`, `UnderwaterAmbient` | The water's own colour (0.06 0.32 0.36) and how much light it gets (1) |
| `WaveShading` | How much the water's own colour is lighter on wave slopes facing the sun and darker on the others, so that the waves show from the steep camera (0 = none); lit from the sun's side at most 45 degrees high, the average brightness stays |
| `CrestScatter`, `CrestScatterColor` | How much lighter the water's own colour is on crests and darker in troughs (0 = none), and its colour on high crests (`ScatterColor`) |
| `CausticStrength`, `CausticScale`, `CausticSpeed` | Light lines on the ground below (0.5; −1 = none), cell size (35), speed (1) |
| `TextureTint` | How much the map's water texture and colour tint the water (0.25; −1 = none) |
| `ShoreDistance` | Water depth over which the edge fades in (1); foam stays visible up to the waterline, so it marks where the water begins on a soft edge. Keep it small on opaque water: water shallower than this shows the ground, and the water of many maps is only a few units deep |
| `ShoreHardness` | 0 = a soft foamy edge, 1 = a hard foam line |
| `ShoreWaveLength`, `ShoreWaveSpeed`, `ShoreWaveRange` | Waves running up the beach: spacing (28), speed (7), how far out they form (45) |
| `ShoreWaveHeight` | How much they lift the surface (0.5; −1 = none) |
| `FoamDistance`, `FoamStrength` | Shore foam: water depth it covers (3; −1 = no shore foam) and its strength (0.85) |
| `ShoreFoamWidth` | Distance from the shoreline the shore foam also covers, so that steep coasts get it too (6; −1 = by depth only) |
| `ShoreFoamMinimum` | 0..1, a foam line along the shore that is always there, whatever the shore waves do (0 = none) |
| `ShoreFoamMinimumTexture` | 0..1, how much the shore foam pattern breaks up that line (0.5; −1 = a flat line) |
| `ShoreWaveFoamDepth` | Water depth up to which the waves running up the beach carry foam, full up to half of it (6; −1 = none); how far out they form is `ShoreWaveRange` |
| `ShoreWaveFoam` | Strength of the foam on the shore waves, a factor on top of `FoamStrength` (1); low values give ponds faint waves next to a steady shore line (`ShoreFoamMinimum`) |
| `ShoreFoamWindward` | 0..1, how much the shore foam keeps to coasts facing `WaveDirection` (0 = all coasts alike; the minimum line stays everywhere) |
| `FoamScale`, `FoamSpeed` | World units one repeat of the shore foam pattern covers (40; the red channel of `SpecularMap`, else built-in noise a quarter that size) and how fast it drifts (factor, 1) |
| `FoamColor` | Colour of all foam (0.92 0.95 1.0) |
| `CrestFoam` | Strength of the foam on high wave crests (0.4; −1 = none); `FoamStrength` does not apply to it |
| `CrestFoamScale`, `CrestFoamSpeed` | Size and drift of the crest foam pattern, the red channel of `ExtraMap`, else of `SpecularMap` (0 = the same as `FoamScale`, `FoamSpeed`) |
| `CrestFoamStart`, `CrestFoamRange` | Wave height where crest foam begins, −1 (trough) to 1 (highest crest) (0.45), and how much higher the whole foam pattern shows (0.4); in between only its brightest spots |
| `CrestFoamSoftness` | How soft the edges of the crest foam patches are (0.15; larger = blurrier) |
| `DeepColor`, `DeepOpacity`, `DepthScale` | Only without refraction: like `MAT_Water` (0.02 0.08 0.12, 0.75, 40); lit by the map's light. With refraction, `ScatterColor` and `Absorption` do their job |
| `ShoreOpacityWidth` | Only without refraction: world units from the shoreline over which the water goes from fully clear to deep (`DeepColor`, `DeepOpacity`), whatever its depth, like the DX8 water (0 = by depth, `DepthScale`, and the edge only clear over `ShoreDistance`). Needs the water field; water outside it keeps the depth |
| `ShoreLift`, `ShoreLiftWidth` | Smooth shoreline: the surface rises this many world units at the shoreline of the water field (2; −1 = none) and back to its level over that distance into the water (10). It covers the steps the terrain triangles cut into the water's edge on steep banks; beyond the field's shoreline the water fades out, so the edge follows the smoothed shoreline (`ShoreFieldSmoothing`). Raise the lift on very steep banks |
| `ContactFoam`, `ContactFoamWidth`, `ContactFoamTexture` | Foam where an object cuts the water: hulls, amphibious units, piers, the legs of an oil rig. Taken from the scene depth behind the surface, so it needs no setup on the objects: strength (1; −1 = none), how far behind the surface along the view it fades out (5), and how much the shore foam pattern breaks it up (0.5; −1 = flat). Terrain gets no contact foam, its shore has the shore foam |
| `DisturbanceHeight`, `DisturbanceNormal`, `DisturbanceFoam` | How the ripples of ships and explosions (the water disturbance, see below) show: world units of height per unit of them (1), how much they bend the surface for the light (1), how much of their foam shows (1); −1 turns each off |

Example presets for three kinds of water, opaque for every map, plus a see-through ocean for maps made for
ships (their `map.ini` picks it, see below):

```ini
Material Ocean
  Use = Water
  Shader = MAT_Ocean
  Param = WaveAmplitude 1.2
End
Material OceanShips
  Base = Ocean
  Param = Refraction 1
  Param = WaveAmplitude 2
  ParamNight = ScatterColor 0.01 0.05 0.08
End
Material Lake
  Use = Water
  Shader = MAT_Ocean
  Param = WaveAmplitude 0.4
  Param = WaveLength 60
  Param = ShoreWaveHeight -1
  Param = ScatterColor 0.05 0.22 0.18
End
Material River
  Use = Water
  Shader = MAT_Ocean
  Param = WaveAmplitude 0.3
  Param = ShoreWaveHeight -1
  Param = Absorption 0.08 0.05 0.04
  Param = ScatterColor 0.12 0.16 0.08
End
```

All Material blocks take `ParamMorning`, `ParamAfternoon`, `ParamEvening` and `ParamNight`, like passes.

#### Which water gets which material (`WaterRendering`)

A `WaterRendering` block, usually in `Water.ini`, picks the water material for each body of water and sets
what the engine prepares for it:

```ini
WaterRendering
  OceanMaterial = Ocean        ; standing water that reaches the edge of the playable area
  LakeMaterial = Lake          ; any other standing water
  RiverMaterial = River        ; rivers
  PolygonMaterial = Lake Harbour Basin   ; <material> <water area name>: before the kinds
  OceanMinimumArea = 300000   ; world units² the bounding box of standing water at the edge needs to be an ocean
  GridSpacing = 10             ; world units between the vertices of water whose material moves them
  RiverSubdivisions = 4        ; such rivers get each quad cut into 4 x 4 pieces
  Refraction = Yes             ; allows materials with Param Refraction 1 to see through the water; No = all opaque
  DrawWaterTracks = No         ; the map's .wak waves too (MAT_Ocean makes its own)
  ShoreFieldResolution = 2     ; texels per terrain cell of the water field
  ShoreFieldRange = 100        ; largest distance to the shore it holds
  ShoreFieldSmoothing = 10     ; world units the terrain is smoothed over for it, so its shoreline is smooth; 0 = not
  Disturbance = Yes            ; ships and explosions ripple the water (WaterRipple FX, WaterInteractionClientUpdate)
  DisturbanceRange = 1024      ; world units around the camera the ripples are kept for
  DisturbanceFadeTime = 1      ; seconds in which they die down to half
  ReplaceHeightAmbientLight = Yes   ; leave MapData TerrainHeightAmbientLight to MAT_Ocean
End
```

Each water area gets the material of its `PolygonMaterial` line (by the area's name in WorldBuilder), else the
one of its kind: a river is a river, standing water with a point within one cell of the edge of the playable
area an ocean when its bounding box covers at least `OceanMinimumArea` (300000, about 550 x 550; the corner
ponds of Tournament Desert, up to 476 x 411, stay lakes), any other standing water a lake. An empty or unknown material name gives the last water
material. The scene is only copied before the water when a material the map uses has `Refraction 1`, so maps with
opaque water do not pay for it.

The **water field** is a texture over the
map that `MAT_Ocean` reads. It holds the terrain height, the distance to the shore and the direction away from
it. The engine builds it when the map loads, and again when the water level or the terrain changes. Its terrain
height is smoothed over `ShoreFieldSmoothing` (a Gaussian blur, that value being two standard deviations), and
near the shore the distance comes from the smoothed height and slope, so the shoreline it describes is a smooth
curve instead of the steps of the terrain triangles. Shore fade, foam, opacity, waves and `ShoreLift` follow it.

The **water disturbance** holds the ripples of ships and explosions for `MAT_Ocean`: a half float texture over
`DisturbanceRange` around the camera, which fades by half every `DisturbanceFadeTime` while new shapes are added
into it 30 times a second (crest and trough height, foam, a highlight mask for submarines). The water bends its
surface and lifts it a little from it, and adds its foam. It costs one small texture update per frame and a few
texture reads per water pixel; with `Disturbance = No` nothing is updated or read. An FXList adds a ring of waves:

```ini
FXList FX_ShellHitWater
  WaterRipple
    StartRadius = 5          ; world units the ring starts with
    EndRadius = 60           ; and grows to (scaled by the effect's radius when it has one)
    Height = 1               ; 0..1, strength of its crest and trough
    Foam = 0.3               ; 0..1, strength of its foam
    Splash = 0               ; 0..1, a round splash in the middle
    Duration = 2000          ; milliseconds it grows and dies away in
    MaxHeightAboveWater = 30 ; higher above the water nothing happens (air bursts)
  End
End
```

Only on water and only with the Direct3D 12 water; otherwise the nugget does nothing. The game MCP command
`render.water_ripple` adds one at a position, for tuning.

Boats, ships and amphibious units get their hull and a wake trail into it with a client module, which works on
top of any draw module:

```ini
Object AmericaShipDestroyer
  ; ...
  ClientUpdate = WaterInteractionClientUpdate ModuleTag_Water
    HullLength = 0            ; world units along the boat, 0 = from its geometry
    HullWidth = 0             ; across, 0 = from its geometry
    MinSpeed = 3              ; world units per second; slower counts as standing
    MaxSpeed = 40             ; full strength from here
    HullHeight = 0.6          ; crest and trough of the hull at full speed
    HullFoam = 0.5            ; foam around the hull at full speed
    IdleRipple = 0.15         ; the hull while standing
    WakeSpacing = 10          ; world units between the splashes of the wake trail
    WakeHeight = 0.25         ; a splash at full speed
    WakeFoam = 0.3            ; its foam at full speed
    MaxHeightAboveWater = 5   ; higher above the water (or deeper than MaxDepth) nothing happens
    MaxDepth = 10
    BowWaveParticleSystem =   ; optional, attached while moving, speed and count scaled by speed
    WakeParticleSystem =      ; optional, the same
  End
End
```

It does nothing on land (amphibious units), for hidden objects and for objects the local player does not see
(fogged or shrouded), so ripples never give away an enemy. The particle systems work with Direct3D 8 too.

### Per map, time of day and scripts

**Materials per map**: a map's `map.ini` may contain `Material` and `WaterRendering` blocks too, and
WorldBuilder shows them.
- A `Material` block replaces the whole material of that name, unless it starts with `Base` = its own name:
  then it changes only what it lists.
- A `WaterRendering` block changes only the fields it lists.
- Both come back to the mod's settings when the game ends.

Example: a map made for ships uses the mod's see-through materials. Name every kind its water may be detected
as, or single water areas by name:

```ini
WaterRendering
  OceanMaterial = OceanShips
  LakeMaterial = OceanShips                    ; enclosed basins count as lakes
  PolygonMaterial = OceanShips Harbour Basin   ; or one area by its name, whatever its kind
End
```

Example: murky water for one map, the mod's Ocean with two parameters changed:

```ini
Material Ocean
  Base = Ocean
  Param = ScatterColor 0.12 0.15 0.06
  Param = DeepColor 0.10 0.12 0.05
End
```

Draw radius cursors and other decals at water height on top of the water with `RadiusDecalsAboveWater = Yes` in
`GameData.ini`; otherwise see-through water bends them like the ground.

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

**Post-processing per map**: a map's `map.ini` can contain `PostProcessPass`, `PostProcess`, `PostProcessChain` and
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
| Switch a pass on or off | Also passes the chain's `Disabled` has; the chain keeps working (see switched off passes) |
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
  `ScreenUV`, `DepthBehind`, `WorldPosition`, `SceneViewPosition(uv)` and `SceneWorldPosition(uv)`;
- for water, the water field and the shroud: `WaterField` (terrain height, distance to the shore, direction
  away from it) and `ShroudTexture`, with `HasWaterField()` / `HasShroud()`. The game passes their world to
  texture transforms as the parameters `WaterFieldTransform` and `ShroudTransform`. The samplers
  `LinearClampSampler`, `LinearWrapSampler` and `PointClampSampler` go with them;
- for water, `WaterDisturbance` (t11, `HasWaterDisturbance()`): the ripples of ships and explosions (red crest,
  green trough, blue foam, alpha highlight), repeating every `DisturbanceTransform.w` world units; sample it at
  `world.xy * DisturbanceTransform.x` with `LinearWrapSampler` and fade it out away from the camera centre
  (`DisturbanceTransform.yz`), as `MAT_Ocean` does;
- for every material, `NoiseTexture` (t12, with `LinearWrapSampler`, `HasNoise()`): tileable fractal noise the
  game makes at start, 256 x 256 with mipmaps. Red is the height (-1..1), green and blue its slope per repeat of
  the texture (d/du, d/dv). Sample it in a few layers of different size and direction so that it does not show
  its repeat (as `MAT_Ocean` does for `NoiseHeight`).

**Moving vertices**: a material can also define a vertex function, which moves each vertex in world space
before it is drawn:

```hlsl
void MaterialVertex(inout MaterialVertexData v)
{
    v.worldPosition.z += sin(v.worldPosition.x * 0.05 + Time) * 2;   // also v.worldNormal, v.tex0 (read only)
    v.data = float4(1, 0, 0, 0);                                     // arrives as input.materialData
}
```

The same file is compiled a second time for it, with `MATERIAL_VERTEX_STAGE` defined. There it may read the
constants, the parameters, `WaterField`, `WaterDisturbance` and `NoiseTexture` (with `SampleLevel`), but not the other textures. Water whose
material has a vertex function is drawn as a finer grid (`WaterRendering GridSpacing`). Compile errors in it
are reported like those of the pixel shader.

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
