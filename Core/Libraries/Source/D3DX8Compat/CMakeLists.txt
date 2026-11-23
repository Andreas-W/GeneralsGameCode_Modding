# The D3DX8 functions the engine uses, for x64 builds: d3dx8.lib exists only for 32-bit.
# Same names and signatures as d3dx8.h, so no call site changes.

add_library(core_d3dx8compat STATIC)
set_target_properties(core_d3dx8compat PROPERTIES OUTPUT_NAME d3dx8compat)

target_sources(core_d3dx8compat PRIVATE
    AssembledShaders.inl
    D3DX8Compat.cpp
)

# Headers only: d3d8lib links this library, so linking d3d8lib back would be a cycle.
target_include_directories(core_d3dx8compat PRIVATE
    $<TARGET_PROPERTY:d3d8lib,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(core_d3dx8compat PRIVATE
    $<TARGET_PROPERTY:d3d8lib,INTERFACE_COMPILE_DEFINITIONS>
)
