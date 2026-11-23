# 64-bit (x64) survey — Zero Hour game + WorldBuilder

Branch `64bit`, off `main` 5bf7618d1. Survey only, no code changed yet.

## Verdict

No hard show-stopper. Two runtime pieces have **no 64-bit version** and must be replaced:

1. **Direct3D 8 runtime** — Windows ships no 64-bit `d3d8.dll` (only `SysWOW64\d3d8.dll`; checked on this machine).
   Way out already in tree: **D3D8on12**. On x64 the game becomes **DX12-only**.
2. **Miles audio** — no 64-bit Miles 6. Needs a new audio backend (**biggest work item**).

Smaller: D3DX8 static lib is 32-bit only (small replacement), Bink → existing FFmpeg player.

## Third-party libraries

| Library | How the engine uses it | x64 status | Plan |
|---|---|---|---|
| `d3d8.dll` (runtime) | `LoadLibrary` + `GetProcAddress("Direct3DCreate8")`, `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp:317`; `-dx12` switches to `Direct3DCreate8On12` (:308) | **none exists** | Always use D3D8on12 on x64 (`Core/Libraries/Source/D3D8on12`, ~9.4k lines, own shader translation). Fallback if needed: DXVK d3d8 or d3d8to9 x64 builds (not checked) |
| `d3dx8.lib` (static, min-dx8-sdk) | math helpers (inverse, scaling, translation, vec4 transform…) in `TerrainTex.cpp`, `W3DShaderManager.cpp`, `W3DTreeBuffer.cpp`, `W3DWater.cpp`, `pointgr.cpp`; texture/surface helpers in `dx8wrapper.cpp` + `surfaceclass.cpp`: `D3DXCreateTexture` ×4, `D3DXLoadSurfaceFromSurface` ×3, `D3DXFilterTexture` ×2, `D3DXCreateTextureFromFileExA`, `D3DXCreateFont`, `D3DXAssembleShader` (one helper, `dx8wrapper.cpp:1887`; shaders are otherwise precompiled `.pso`/`.vso`), `D3DXGetErrorStringA` | 32-bit only | Small in-tree replacement (math inline, texture ops via device/D3D8on12 calls) |
| `dinput8.lib`, `dxguid.lib` | input, GUIDs | Windows SDK has x64 | OK |
| Miles `mss32.dll` 6.x + providers (`MSS\*.m3d`, `mssmp3.asi`…) | `Core/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp` (3488 lines); WorldBuilder creates it directly (`GeneralsMD/Code/Tools/WorldBuilder/src/WorldBuilder.cpp:438`) | stub compiles for x64 (upstream #1563 is in tree) but is a **null implementation → silent**. Newer 64-bit Miles is commercial, not redistributable, API differs | New backend: OpenAL Soft (LGPL, vcpkg `openal-soft`) + decoding via FFmpeg (or minimp3/dr_wav). Reference: upstream PR #784 (OpenAL, closed, +4450 lines, `OpenALAudioManager.cpp` 3068 lines) |
| Bink `binkw32.dll` | video | stub null; no free x64 Bink | Existing FFmpeg player (`RTS_BUILD_OPTION_FFMPEG`, `Core/GameEngineDevice/Source/VideoDevice/FFmpeg`), vcpkg ffmpeg x64 |
| GameSpy SDK | source (FetchContent) | builds from source; 64-bit cleanliness unknown | compile and see; online play can be gated off |
| EABrowserEngine / `BrowserEngine.dll` | 32-bit COM embedded browser (`dx8webbrowser.cpp`) | 32-bit binary | disable on x64 |
| `dbghelp.dll` | crash handlers (`DbgHelpLoader.cpp`) | system x64 exists | needs `...64` API ports (done in upstream #3248, closed) |
| zlib, lzhl, stb, imgui, tracy, reactos-atl | source | fine | — |
| MFC (WorldBuilder) | — | VS2022 ships x64 MFC | OK |
| `DebugWindow.dll`, `ParticleEditor.dll` | built from repo | rebuild x64 | — |
| `steam_api.dll`, `patchw32.dll` (mod folder) | not referenced by engine code | — | ignore |

## Decisions

- x64 is **DX12-only** (D3D8on12). Accepted.
- Audio backend: open, see below.

## Miles usage (what a replacement must cover)

`MilesAudioManager.cpp`, ~70 distinct `AIL_*` calls. Features:

| Feature | Miles call | Used by mod data |
|---|---|---|
| 2D samples: volume, pitch (playback rate), end callback | `AIL_*_sample*`, `AIL_register_EOS_callback` | everywhere |
| 3D samples: position, min/max distance, listener orientation, end callback | `AIL_*_3D_*` | everywhere |
| Low-pass when off-screen (`LowPassCutoff`) | `AIL_set_3D_sample_occlusion` (:1414) | 30 events |
| Start delay (`Delay = min max`) | DSP filter "Mono Delay" (`mssdsp.flt`, :1389) | ~28 events |
| Streams: music (MP3), speech; loop count, position, pause; read from `.big` | `AIL_open_stream`, `AIL_set_file_callbacks` (:435) | all music/speech |
| IMA ADPCM WAV decode into cache | `AIL_WAV_info`, `AIL_decompress_ADPCM` (:3267) | many WAVs |
| Speaker setup (2/4/5.1/7.1/headphone/surround) from `AudioSettings.ini` | `AIL_set_3D_speaker_type` | settings |
| 3D provider: "Miles Fast 2D Positional Audio" or "Dolby Surround" only | `AIL_open_3D_provider` (:1827) | — |
| Video audio (Bink via DirectSound) | `getHandleForBink` | movies |

**Not used**: EAX, reverb, HRTF, hardware 3D. Any backend with a low-pass filter covers it.

## Audio backend: upstream state

Upstream did **not** settle on a backend; it still ships Miles only.
- #784 OpenAL (feliwir, closed unmerged): reviewer found movie and effect sound but **no music, no unit/EVA voices**. Points to OpenAL work in jmarshall2323/CnC_Generals_Zero_Hour.
- #3228 "generalsmd 64bit" (closed): used **XAudio2** (`XAudio2AudioDevice/`).
- Residue in our tree: `FFmpegVideoPlayer.cpp` has `#ifdef RTS_USE_OPENAL` blocks for movie audio (from the Linux FFmpeg work, #714). Without a backend, FFmpeg movies are silent.
- OpenAL's draw upstream is cross-platform (Linux/macOS ports), not a feature Miles needs.

Candidates: **miniaudio** (public domain, one header, decodes MP3 + IMA ADPCM WAV, low-pass and delay nodes, VFS hook for `.big`), **OpenAL Soft** (LGPL DLL, EFX low-pass, needs decoders, partial reference code), **XAudio2** (part of Windows, no DLL to ship, X3DAudio + voice filters, Windows-only, needs decoders).

## In-house code (tackle-able)

- **Inline asm**: 21 files in Core/GeneralsMD — `WWLib/cpudetect.cpp` (18), `WWMath/vp.cpp` (15), `WWLib/mutex.h` (12), `WWDebug/wwmemlog.cpp`, `WWMath/wwmath.h`, `StackDump.cpp`, profile/debug libs, `matrix3d.cpp`, `Except.cpp`, `dx8wrapper.h`, `BaseType.h`.
- **Pointer truncation**: upstream #3459 (merged 2026-10, **not in tree**, 27 files) fixes a batch (window user data, persist factory, huffencode, GameSpy utils…). 37 `Get/SetWindowLong` / `GWL_*` sites. Real count comes from the first x64 compile; upstream #3248 still had ~550 x64 errors (MinGW) after its fixes.
- **Determinism**: x64 uses SSE, not x87 (`setFPMode()` in `INI.cpp:389`, `SimulationMathCrc.cpp`). 32-bit and 64-bit builds will **not** be replay/multiplayer compatible with each other. Upstream deterministic math (#2670) is not in tree.
- **Saves**: Xfer of pointer-sized fields can change the save format between builds.

## Upstream references

Tracking issue #473 (open). #3248 / #3250 (x64 foundation, closed, MinGW). #3459 (pointer truncations, merged). #784 (OpenAL, closed). Fork does not merge upstream; port pieces selectively.

## Proposed order

1. MSVC x64 preset (`win64-modding-zh`, `amd64` dev shell, vcpkg `x64-windows`), stubs linked, browser off → get the real error list.
2. Fix compile: asm, truncations (ideas from #3459), crash handlers.
3. D3DX8 replacement → links.
4. Force D3D8on12 on x64 → first run (silent, no video).
5. FFmpeg video.
6. OpenAL audio backend (game + WorldBuilder).
7. WorldBuilder x64 build and run.
