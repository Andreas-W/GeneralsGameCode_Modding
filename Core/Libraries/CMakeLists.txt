# WW common libraries decended from earliest C&C games
add_subdirectory(Source/WWVegas)

# profiling library
add_subdirectory(Source/profile)

# debugging library
add_subdirectory(Source/debug)

add_subdirectory(Source/EABrowserDispatch)
add_subdirectory(Source/EABrowserEngine)
add_subdirectory(Source/Compression)

if(RTS_BUILD_OPTION_DX12)
    add_subdirectory(Source/D3D8on12)
endif()

# D3DX8 and Miles exist only for 32-bit; x64 builds use these stand-ins.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_subdirectory(Source/D3DX8Compat)
    add_subdirectory(Source/MilesMiniaudio)
endif()

# ImGui renderer backend for tools (only when ImGui was fetched)
if(TARGET imgui)
    add_subdirectory(Source/ImGuiDX8)
endif()
