# Dear ImGui renderer backend on the engine's Direct3D 8 device, for tools with an ImGui UI.
add_library(core_imgui_dx8 STATIC
    imgui_impl_dx8.cpp
    imgui_impl_dx8.h
)

target_include_directories(core_imgui_dx8 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(core_imgui_dx8 PUBLIC
    d3d8lib
    imgui
)
