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

## Progress (2026-11-23)

- Preset `win64-modding-zh` / build preset `w64-modding-zh` in `CMakePresets.json`, output `build/win64`. Configure and build from a dev shell started with `vcvarsall.bat amd64` (not `amd64_x86`).
- Crash handlers ported (`WWLib/Except.cpp`, `WWLib/DbgHelpLoader`, `debug/debug_stack|except|debug.cpp`, `StackDump.cpp`): 64-bit dbghelp entry points, `RtlCaptureContext`, x64 register dumps. Note: on x64 `imagehlp.h` `#define`s `StackWalk`, `SymGetModuleBase` etc. to the `...64` names.
- D3DX8 stand-in `Core/Libraries/Source/D3DX8Compat` (x64 MSVC only): math, texture creation, surface copy/convert/scale, mip generation, no fonts; the 4 runtime-assembled pixel shaders come from `AssembledShaders.inl` (assembled once with the 32-bit D3DX8).
- x64 always uses D3D8on12 (`DX8Wrapper_UseDX12` defaults to 1 on `_WIN64`).
- **`generalszh.exe` x64 compiles and links.** 32-bit build still builds.
- First x64 compile: 0 errors outside the crash handlers, but **577 pointer-truncation warnings** (C4311/C4312) that will break at runtime; most in the GUI gadgets via 32-bit `WindowMsgData`. Upstream #3459 (78 files) fixes many of them.
- Audio: miniaudio (0.11.26) chosen; `cmake/miniaudio.cmake` prepared, not included yet.
- Truncations, own fixes (no upstream cherry-pick): `WindowMsgData` is `uintptr_t` (577 → 210 warnings). Fixed the real ones: list box multi-selection returned half a pointer (chat lobby), IME candidate list, surface lock arithmetic, Huffman encoder, asset name length, `BOGUSPTR`, registry `HKEY`, thread handle, debug pointer print. Memory pools align to 8 on x64 (`MEM_BOUND_ALIGNMENT`).
- The ~190 remaining warnings are small integers kept in pointers (list item data, INI `userData`, GameSpy profile ids) and read back: harmless. Not fixed: Miles/Bink handles (replaced on x64 anyway), WWSaveLoad pointer ids and WWAudio callbacks (W3D tools only), `verchk` (W3DView only).
- Saves: `xferUser` writes whole structs; where those hold pointers, x64 saves differ from 32-bit ones.
- Silent x64 build: no Miles, no Bink on x64 (`RTS_NO_MILES` / `RTS_NO_BINK`, `AudioManagerDummy`, plain `VideoPlayer` = movies skipped). Temporary until miniaudio and FFmpeg video.
- **First x64 runs (2026-11-23)**, deployed as `generalszh64.exe` in the mod folder: Armed Neighbors skirmish loads, renders through D3D8on12, logic 30 fps, combat works; same INI assertions as 32-bit. Fixed on the way:
  - DDS header struct `LegacyDDSURFACEDESC2` had a `void*` (file layout 4 bytes): all DDS textures failed to load (magenta). Now `unsigned` plus a `static_assert` of 124 bytes.
  - `ObjectPoolClass` (WWLib `mempool.h`) placed objects 4 bytes after a pointer-sized block header: corrupted block chain, crash in a static destructor at exit, then a hidden "Debug not inited" message box made it hang.
- Not x64: dozer build orders via the MCP fail on 32-bit too in this mod. The installed `generalszh.exe` (2026-11-07) crashes on the current mod data (`AdditionalGeometry` in `ZZ_CompoundTest.ini`): it predates compound geometry.
- Open: x64 assertion stack dumps print garbage addresses (`StackDump` path); exit-time crashes still show an invisible message box instead of exiting (`DebugLogRaw` after debug shutdown).
- **Audio on x64 (miniaudio 0.11.26)**: `Core/Libraries/Source/MilesMiniaudio` implements the ~70 `AIL_*` functions `MilesAudioManager` uses on miniaudio, linked statically; `milesstub` is an alias for it on x64 (`cmake/miles.cmake` fetches only the header there, `BUILD_STUBS`). The manager and its game logic are unchanged (except the removed 32-bit-only check in `streamingFileOpen`, which stopped debug builds as soon as a stream opened). Handles are small integers in pointer form; end callbacks run on a service thread under a global recursive lock (like Miles' timer thread); samples play a copy of the WAV image (PCM / IMA ADPCM), streams read the whole file; stereo output; 3D = pan from camera right vector + Miles rolloff (full volume up to min_dist) + occlusion as low-pass; delay filter is a no-op. Trace: env `MILES_MINIAUDIO_TRACE=<file>`.
  - Tested: music stream, 2D samples start/end/loop, 3D samples with pitch variation, clean exit. Listened to by the user on 2026-11-23 with the window focused: sounds right. (Automated runs show volume 0: the game mutes itself while its window has no focus, `WM_ACTIVATE`.)
- **Movies on x64 (2026-11-23)**: `win64-modding-zh` now inherits `default-vcpkg` (triplet `x64-windows`, `RTS_BUILD_OPTION_FFMPEG=ON`), so vcpkg builds FFmpeg 7.1.1 from `vcpkg.json`; `VCPKG_ROOT` must be a git clone of vcpkg (user's is now `C:\Users\Gernot\vcpkg`). The in-tree `FFmpegVideoPlayer` plays the `.bik` movies; with `RTS_MOVIE_AUDIO_MINIAUDIO` (x64 only) its decoded audio goes through `MilesMiniaudioPcm.h` (a ring-buffer data source on the same miniaudio device, silence when empty), at 80% of the speech volume like Bink. Runtime DLLs next to the exe: `avcodec-61`, `avformat-61`, `avutil-59`, `swresample-5`, `swscale-8`. Tested: `GeneralsChallengeBackground` (GC_Background.bik, 1 audio track) plays fullscreen, its PCM stream opens (44.1 kHz stereo), ends cleanly, clean exit. Only two movies exist in this mod folder (`GC_Background`, `VS_small` without audio); `OFS_Intro` (Sizzle) is not present. x86 unchanged: Bink + Miles.
- **WorldBuilder x64 (2026-11-23)**: only MFC `OnTimer` handlers needed `UINT_PTR` (4 classes). Three tree IDs in `wbview3d.cpp` come from truncated `MapObject` pointers, used the same way everywhere: left as is. Deployed as `WorldBuilderZH64.exe`; it renders through D3D8on12 and passes `smoke_test.py` (map, cliffs, scripts, screenshot, save/reopen, town, outposts). It needs the x64 MFC (`mfc140.dll`, from the VC redistributable) and `zlib1.dll`.
- **zlib on x64**: with the vcpkg toolchain `core_compression` finds vcpkg's zlib (a DLL), so both x64 exes import `zlib1.dll`. The game first ran only because Git's `mingw64\bin\zlib1.dll` was on PATH; the vcpkg one now sits in the mod folder.
- Tools for later: `scratchpad` scripts `stacks.py` (thread stacks of a hung process) and `addr2sym.py` (offset → source line from the PDB); no Windows debugger is installed.

## Proposed order

1. MSVC x64 preset (`win64-modding-zh`, `amd64` dev shell, vcpkg `x64-windows`), stubs linked, browser off → get the real error list.
2. Fix compile: asm, truncations (ideas from #3459), crash handlers.
3. D3DX8 replacement → links.
4. Force D3D8on12 on x64 → first run (silent, no video).
5. FFmpeg video.
6. OpenAL audio backend (game + WorldBuilder).
7. WorldBuilder x64 build and run.
