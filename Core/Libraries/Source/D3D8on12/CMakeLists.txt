# D3D8 interfaces implemented on Direct3D 12. DX8Wrapper uses it in place of d3d8.dll when the game
# runs with -dx12.

set(D3D8ON12_SRC
    d3d8on12.h
    d3d8on12_ext.h
    Caps.cpp
    Device.cpp
    Device.h
    Draw.cpp
    Formats.cpp
    Formats.h
    Gpu.cpp
    Guids.cpp
    Interface.cpp
    Interface.h
    Pipeline.h
    PostProcess.cpp
    PostProcess.h
    Private.h
    Resources.cpp
    Resources.h
    ShaderGen.cpp
    ShaderTranslate.cpp
    Targets.cpp
)

add_library(core_d3d8on12 STATIC)
set_target_properties(core_d3d8on12 PROPERTIES OUTPUT_NAME d3d8on12)

target_sources(core_d3d8on12 PRIVATE ${D3D8ON12_SRC})

target_include_directories(core_d3d8on12 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(core_d3d8on12 PUBLIC
    d3d8lib
    d3d12
    d3dcompiler
    dxgi
    delayimp
)

# Executables that contain the renderer still start where Direct3D 12 is missing; only -dx12 needs it.
target_link_options(core_d3d8on12 INTERFACE
    /DELAYLOAD:d3d12.dll
    /DELAYLOAD:d3dcompiler_47.dll
)
