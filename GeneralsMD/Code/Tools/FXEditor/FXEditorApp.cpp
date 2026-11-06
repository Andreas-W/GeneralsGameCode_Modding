/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Precompiled/PreRTS.h"

#include "FXEditorApp.h"
#include "FXEditorPanels.h"
#include "FXEditorMcp.h"
#include "IniDatabase.h"

#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameMcp.h"
#include "Common/GlobalData.h"
#include "GameClient/Display.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/HeaderTemplate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameLogic/GameLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "WW3D2/dx8wrapper.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx8.h"

#include <deque>
#include <stdarg.h>
#include <stdio.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern int DX8Wrapper_UseDX12;
extern HWND ApplicationHWnd;

namespace
{

AsciiString s_previewMap;
AsciiString s_startError;
Bool s_imguiReady = FALSE;
Bool s_hideOverlay = FALSE;
Bool s_frameBuilt = FALSE;
std::string s_layoutFile;

Int s_pendingWidth = 0;
Int s_pendingHeight = 0;
DWORD s_pendingSince = 0;

PreviewScene *s_scene = nullptr;
IniDatabase *s_ini = nullptr;

std::deque<FXEditorLogEntry> s_log;
std::vector<FXEditorLogEntry> s_logCopy;
UnsignedInt s_logSerial = 0;

PreviewKind s_selectedKind = PREVIEW_PARTICLE_SYSTEM;
AsciiString s_selectedName;

Bool s_quitRequested = FALSE;
Bool s_iniLoadedLogged = FALSE;

//-------------------------------------------------------------------------------------------------
void drawOverlay()
{
	if (!s_imguiReady || s_hideOverlay || !s_frameBuilt)
		return;
	ImDrawData *data = ImGui::GetDrawData();
	if (data && data->Valid)
		ImGui_ImplDX8_RenderDrawData(data);
}

//-------------------------------------------------------------------------------------------------
/** The display follows the window size once the user has stopped resizing. */
//-------------------------------------------------------------------------------------------------
void applyPendingResize()
{
	if (s_pendingWidth <= 0 || TheDisplay == nullptr || GetTickCount() - s_pendingSince < 250)
		return;
	const Int width = s_pendingWidth;
	const Int height = s_pendingHeight;
	s_pendingWidth = 0;
	if (width < 320 || height < 240)
		return;
	if (width == (Int)TheDisplay->getWidth() && height == (Int)TheDisplay->getHeight())
		return;
	if (!TheDisplay->setDisplayMode(width, height, TheDisplay->getBitDepth(), TRUE))
	{
		FXEditorApp::log(TRUE, "Could not change the display to %dx%d", width, height);
		return;
	}
	// As the options menu does after a resolution change (and the game MCP's view.display_mode).
	TheWritableGlobalData->m_xResolution = width;
	TheWritableGlobalData->m_yResolution = height;
	if (TheHeaderTemplateManager)
		TheHeaderTemplateManager->onResolutionChanged();
	if (TheMouse)
		TheMouse->onResolutionChanged();
	if (TheShell)
		TheShell->recreateWindowLayouts();
	if (TheInGameUI)
	{
		TheInGameUI->recreateControlBar();
		TheInGameUI->refreshCustomUiResources();
	}
	if (s_scene && s_scene->isReady())
		HideControlBar(TRUE);
}

//-------------------------------------------------------------------------------------------------
/** The default arrangement: library on the left, inspector on the right, preview and log below. */
//-------------------------------------------------------------------------------------------------
void buildDefaultLayout(ImGuiID dockspace)
{
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspace);
	ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

	ImGuiID center = dockspace;
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.34f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
	ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.35f, nullptr, &left);

	ImGui::DockBuilderDockWindow(FXEditorPanels::LIBRARY, left);
	ImGui::DockBuilderDockWindow(FXEditorPanels::AUDIO, leftBottom);
	ImGui::DockBuilderDockWindow(FXEditorPanels::INSPECTOR, right);
	ImGui::DockBuilderDockWindow(FXEditorPanels::LOG, bottom);
	ImGui::DockBuilderDockWindow(FXEditorPanels::PREVIEW, bottom);
	ImGui::DockBuilderFinish(dockspace);
}

//-------------------------------------------------------------------------------------------------
void drawQuitDialog()
{
	if (s_quitRequested)
	{
		ImGui::OpenPopup("Unsaved changes");
		s_quitRequested = FALSE;
	}
	if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("%d block(s) have changes that are not saved.", s_ini->getDirtyCount());
		if (ImGui::Button("Save all and quit"))
		{
			AsciiString error;
			if (s_ini->save(-1, error))
				TheGameEngine->setQuitting(TRUE);
			else
				FXEditorApp::log(TRUE, "Save failed: %s", error.str());
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Quit without saving"))
		{
			TheGameEngine->setQuitting(TRUE);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

//-------------------------------------------------------------------------------------------------
void drawInterface()
{
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	const ImGuiID dockspace = ImGui::GetID("FXEditorDockSpace");
	static Bool layoutChecked = FALSE;
	if (!layoutChecked)
	{
		layoutChecked = TRUE;
		if (ImGui::DockBuilderGetNode(dockspace) == nullptr)
			buildDefaultLayout(dockspace);
	}
	ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

	FXEditorPanels::drawMainMenu();
	FXEditorPanels::drawLibrary();
	FXEditorPanels::drawInspector();
	FXEditorPanels::drawPreview();
	FXEditorPanels::drawAudio();
	FXEditorPanels::drawLog();
	FXEditorPanels::drawOverlayInfo();
	drawQuitDialog();
}

} // namespace

//-------------------------------------------------------------------------------------------------
void FXEditorApp::setPreviewMap(const char *map)
{
	s_previewMap = map;
}

const AsciiString &FXEditorApp::getPreviewMap()
{
	return s_previewMap;
}

void FXEditorApp::setStartError(const char *error)
{
	s_startError = error;
}

const AsciiString &FXEditorApp::getStartError()
{
	return s_startError;
}

//-------------------------------------------------------------------------------------------------
Bool FXEditorApp::handleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!s_imguiReady)
		return FALSE;
	return ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam) != 0;
}

Bool FXEditorApp::wantsMouse()
{
	return s_imguiReady && ImGui::GetIO().WantCaptureMouse;
}

Bool FXEditorApp::wantsKeyboard()
{
	return s_imguiReady && ImGui::GetIO().WantCaptureKeyboard;
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::onWindowResized(Int width, Int height, Bool minimized)
{
	if (minimized || width <= 0 || height <= 0)
		return;
	s_pendingWidth = width;
	s_pendingHeight = height;
	s_pendingSince = GetTickCount();
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::init()
{
	s_scene = new PreviewScene;
	s_ini = new IniDatabase;

	// A missing map would start an empty world without terrain.
	const AsciiString &mapFile = TheGlobalData->m_initialFile;
	if (s_startError.isEmpty() && !mapFile.isEmpty() && !TheFileSystem->doesFileExist(mapFile.str()))
		s_startError.format("no map found for '%s' (looked for %s)", s_previewMap.str(), mapFile.str());

	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device == nullptr || ApplicationHWnd == nullptr)
	{
		log(TRUE, "No Direct3D device: the editor interface is not available.");
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
	s_layoutFile = TheGlobalData->getPath_UserData().str();
	s_layoutFile += "FXEditorLayout.ini";
	io.IniFilename = s_layoutFile.c_str();

	ImGui::StyleColorsDark();
	ImGuiStyle &style = ImGui::GetStyle();
	style.WindowRounding = 3.0f;
	style.FrameRounding = 2.0f;
	style.Colors[ImGuiCol_WindowBg].w = 0.94f;

	ImGui_ImplWin32_Init(ApplicationHWnd);
	ImGui_ImplDX8_Init(device);
	s_imguiReady = TRUE;
	W3DDisplay::m_overlayDraw = drawOverlay;

	FXEditorMcp::registerCommands();

	log(FALSE, "FX Editor started (%s, MCP bridge on port %d)", isDX12() ? "Direct3D 12" : "Direct3D 8",
		TheGameMcp ? TheGameMcp->getPort() : 0);
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::shutdown()
{
	W3DDisplay::m_overlayDraw = nullptr;
	FXEditorPanels::shutdown();
	if (s_imguiReady)
	{
		ImGui_ImplDX8_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		s_imguiReady = FALSE;
	}
	delete s_scene;
	s_scene = nullptr;
	delete s_ini;
	s_ini = nullptr;
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::update()
{
	applyPendingResize();
	if (s_scene)
		s_scene->update();

	// The INI text is read once the game data is loaded.
	if (s_ini && !s_ini->isLoaded() && s_scene && s_scene->isReady())
	{
		s_ini->load();
		log(FALSE, "Read %d ParticleSystem and FXList blocks from %d files", s_ini->getBlockCount(), s_ini->getFileCount());
	}

	if (!s_imguiReady)
		return;
	if (ApplicationHWnd && IsIconic(ApplicationHWnd))
	{
		s_frameBuilt = FALSE;
		return;
	}

	ImGui_ImplDX8_NewFrame();
	ImGui_ImplWin32_NewFrame();
	// The back buffer, not the window, is what the interface is drawn on.
	if (TheDisplay)
		ImGui::GetIO().DisplaySize = ImVec2((float)TheDisplay->getWidth(), (float)TheDisplay->getHeight());
	ImGui::NewFrame();
	drawInterface();
	ImGui::Render();
	s_frameBuilt = TRUE;
}

//-------------------------------------------------------------------------------------------------
PreviewScene &FXEditorApp::scene()
{
	return *s_scene;
}

IniDatabase &FXEditorApp::ini()
{
	return *s_ini;
}

Bool FXEditorApp::isDX12()
{
	return DX8Wrapper_UseDX12 != 0;
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::log(Bool error, const char *format, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	FXEditorLogEntry entry;
	entry.serial = ++s_logSerial;
	entry.error = error;
	entry.text = buffer;
	s_log.push_back(entry);
	while (s_log.size() > 500)
		s_log.pop_front();
	DEBUG_LOG(("FXEditor: %s", buffer));
}

const FXEditorLogEntry *FXEditorApp::getLog(Int &count)
{
	s_logCopy.assign(s_log.begin(), s_log.end());
	count = (Int)s_logCopy.size();
	return s_logCopy.empty() ? nullptr : &s_logCopy[0];
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::select(PreviewKind kind, const AsciiString &name)
{
	if (kind == PREVIEW_SOUND)
		return;
	s_selectedKind = kind;
	s_selectedName = name;
}

PreviewKind FXEditorApp::getSelectedKind()
{
	return s_selectedKind;
}

const AsciiString &FXEditorApp::getSelectedName()
{
	return s_selectedName;
}

Int FXEditorApp::getSelectedBlock()
{
	if (s_ini == nullptr || s_selectedName.isEmpty())
		return -1;
	return s_ini->findBlock(s_selectedKind, s_selectedName);
}

//-------------------------------------------------------------------------------------------------
Bool FXEditorApp::play(PreviewKind kind, const AsciiString &name, AsciiString &error)
{
	if (!s_scene->play(kind, name, error))
		return FALSE;
	select(kind, name);
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::onBlockChanged(Int block)
{
	// What plays may use the changed block directly or through an FXList or a slave system.
	if (s_scene && s_scene->isPlaying())
		s_scene->restart();
}

//-------------------------------------------------------------------------------------------------
Bool FXEditorApp::captureScreenshot(const char *path, Bool withInterface, AsciiString &error)
{
	if (TheGameMcpScreenshot == nullptr || TheDisplay == nullptr)
	{
		error = "the display cannot take screenshots";
		return FALSE;
	}
	if (ApplicationHWnd && IsIconic(ApplicationHWnd))
	{
		error = "the window is minimized";
		return FALSE;
	}
	// A fresh frame, with or without the interface on top.
	s_hideOverlay = !withInterface;
	TheDisplay->draw();
	s_hideOverlay = FALSE;

	const size_t len = strlen(path);
	const Bool png = len > 4 && _stricmp(path + len - 4, ".png") == 0;
	if (!TheGameMcpScreenshot(path, png ? SCREENSHOT_PNG : SCREENSHOT_JPEG, 90))
	{
		error.format("could not write '%s'", path);
		return FALSE;
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void FXEditorApp::requestQuit()
{
	if (s_ini && s_imguiReady && s_ini->getDirtyCount() > 0)
		s_quitRequested = TRUE;
	else if (TheGameEngine)
		TheGameEngine->setQuitting(TRUE);
}
