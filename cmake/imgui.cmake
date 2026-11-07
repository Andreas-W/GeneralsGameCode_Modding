# Fetch Dear ImGui (docking branch) for the tools that draw their own editor UI (FX Editor).
# Builds a static library `imgui` with the core sources and the Win32 platform backend.
# The renderer backend is in Core/Libraries/Source/ImGuiDX8, on top of the engine's D3D8 device.

include(FetchContent)
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG        v1.91.9b-docking
)

FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
	${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)

target_include_directories(imgui PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
	${imgui_SOURCE_DIR}/misc/cpp
)

# The engine is built with /W3 and its own warning set; keep third-party noise out of the build log.
if(MSVC)
	target_compile_options(imgui PRIVATE /W0)
endif()

target_link_libraries(imgui PUBLIC imm32 dwmapi)
