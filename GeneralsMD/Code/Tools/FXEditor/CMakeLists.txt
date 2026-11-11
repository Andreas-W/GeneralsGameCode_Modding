# FX Editor: previews and edits ParticleSystem and FXList INI with the game's own engine.
# A separate executable with its own WinMain and CreateGameEngine(); it runs the game engine in a
# sandbox skirmish and draws an ImGui interface on top. Nothing of it is linked into the game.

add_executable(z_fxeditor WIN32)
set_target_properties(z_fxeditor PROPERTIES OUTPUT_NAME "FXEditorZH${RTS_BUILD_OUTPUT_SUFFIX}")

target_sources(z_fxeditor PRIVATE
    FieldSchema.cpp
    FieldSchema.h
    FXEditorApp.cpp
    FXEditorApp.h
    FXEditorEngine.cpp
    FXEditorEngine.h
    FXEditorMain.cpp
    FXEditorMcp.cpp
    FXEditorMcp.h
    FXEditorPanels.cpp
    FXEditorPanels.h
    FXEditorRenderPanel.cpp
    IniDatabase.cpp
    IniDatabase.h
    PassOrder.cpp
    PassOrder.h
    PreviewScene.cpp
    PreviewScene.h
)

target_include_directories(z_fxeditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    # The command table of the game MCP bridge, which the editor adds its fx.* commands to.
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Source/Common/Mcp
)

target_link_libraries(z_fxeditor PRIVATE
    binkstub
    comctl32
    core_imgui_dx8
    d3d8
    d3dx8
    dinput8
    dxguid
    imgui
    imm32
    milesstub
    vfw32
    winmm
    ws2_32
    z_gameengine
    z_gameenginedevice
    zi_always
)

if(MSVC)
    target_link_options(z_fxeditor PRIVATE "/NODEFAULTLIB:libci.lib")
endif()
