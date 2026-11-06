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

// FX Editor entry point. The editor runs the whole game engine: it starts a sandbox skirmish on a
// preview map through the game MCP bridge and draws its own ImGui interface on top of the game view.
// The window and its message handling follow Main/WinMain.cpp of the game, without the load screen
// and the single-instance check, so the editor can run next to the game and WorldBuilder.

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlobj.h>
#include <crtdbg.h>

#include <string>
#include <vector>

#include "Lib/BaseType.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/StackDump.h"
#include "Common/version.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/IMEManager.h"
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32GameEngine.h"

#include "FXEditorEngine.h"
#include "FXEditorApp.h"

// GLOBALS the engine expects from the executable ////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;
HWND ApplicationHWnd = nullptr;
Win32Mouse *TheWin32Mouse = nullptr;
DWORD TheMessageTime = 0;

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
const char *gAppPrefix = "fxe_"; // own debug log file name

static Bool isWinMainActive = false;

static CriticalSection critSec1, critSec2, critSec3, critSec4, critSec5;

// Window size when nothing else is asked for on the command line.
static const Int DEFAULT_WIDTH = 1600;
static const Int DEFAULT_HEIGHT = 900;

//-------------------------------------------------------------------------------------------------
/** Window procedure: the editor interface gets every message first. Mouse input reaches the game
	only when it is not over an editor window; keyboard input is filtered in FXEditorKeyboard. */
//-------------------------------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	try
	{
		if (FXEditorApp::handleWindowMessage(hWnd, message, wParam, lParam))
			return TRUE;

		if (TheIMEManager && TheIMEManager->serviceIMEMessage(hWnd, message, wParam, lParam))
			return TheIMEManager->result();

		switch (message)
		{
			case WM_SYSCOMMAND:
				// No system menu on Alt.
				if ((wParam & 0xFFF0) == SC_KEYMENU)
					return 1;
				break;

			case WM_QUERYENDSESSION:
				if (TheGameEngine && !TheGameEngine->getQuitting())
					TheGameEngine->setQuitting(TRUE);
				return 0;

			case WM_CLOSE:
				// Asks first when there are changes that are not saved.
				if (TheGameEngine && !TheGameEngine->getQuitting())
					FXEditorApp::requestQuit();
				return 0;

			case WM_MOVE:
			case WM_SIZE:
				if (TheMouse)
					TheMouse->refreshCursorCapture();
				if (message == WM_SIZE)
					FXEditorApp::onWindowResized(LOWORD(lParam), HIWORD(lParam), wParam == SIZE_MINIMIZED);
				break;

			case WM_SETFOCUS:
				if (TheKeyboard)
					TheKeyboard->resetKeys();
				if (TheMouse)
					TheMouse->regainFocus();
				break;

			case WM_KILLFOCUS:
				if (TheKeyboard)
					TheKeyboard->resetKeys();
				if (TheMouse)
				{
					TheMouse->loseFocus();
					if (TheMouse->isCursorInside())
						TheMouse->onCursorMovedOutside();
				}
				break;

			case WM_ACTIVATEAPP:
				if ((bool)wParam != (bool)isWinMainActive)
				{
					isWinMainActive = (BOOL)wParam;
					if (TheGameEngine)
						TheGameEngine->setIsActive(isWinMainActive);
				}
				return 0;

			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
			case WM_MOUSEWHEEL:
				if (TheWin32Mouse && !FXEditorApp::wantsMouse())
					TheWin32Mouse->addWin32Event(message, wParam, lParam, TheMessageTime);
				return 0;

			case WM_LBUTTONUP:
			case WM_MBUTTONUP:
			case WM_RBUTTONUP:
				// Always, so a drag started in the view ends even when released over the interface.
				if (TheWin32Mouse)
					TheWin32Mouse->addWin32Event(message, wParam, lParam, TheMessageTime);
				return 0;

			case WM_MOUSEMOVE:
			{
				if (TheWin32Mouse == nullptr || TheMouse == nullptr)
					return 0;
				if (!TheMouse->isCursorInside())
					TheMouse->onCursorMovedInside();
				TheWin32Mouse->addWin32Event(message, wParam, lParam, TheMessageTime);
				return 0;
			}

			case WM_SETCURSOR:
				// Over the editor interface the normal arrow; over the view the game's cursor.
				if (FXEditorApp::wantsMouse())
				{
					SetCursor(LoadCursor(nullptr, IDC_ARROW));
					return TRUE;
				}
				if (TheWin32Mouse && (HWND)wParam == ApplicationHWnd)
					TheWin32Mouse->setCursor(TheWin32Mouse->getMouseCursor());
				return TRUE;

			case WM_ERASEBKGND:
				return TRUE;
		}
	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught exception in FXEditor WndProc"));
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

//-------------------------------------------------------------------------------------------------
static Bool createAppWindow(HINSTANCE hInstance, Int nCmdShow, Int width, Int height)
{
	WNDCLASS wndClass = { CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInstance,
		LoadIcon(nullptr, IDI_APPLICATION), nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, TEXT("FXEditorWindow") };
	RegisterClass(&wndClass);

	const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, style, FALSE);

	HWND hWnd = CreateWindow(TEXT("FXEditorWindow"), TEXT("FX Editor - Command and Conquer Generals Zero Hour"), style,
		(GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2,
		rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr);
	if (hWnd == nullptr)
		return FALSE;

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	SetForegroundWindow(hWnd);
	SetFocus(hWnd);
	ApplicationHWnd = hWnd;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
static Bool hasArgument(const std::vector<std::string> &args, const char *name, Bool prefix = FALSE)
{
	for (size_t i = 1; i < args.size(); ++i)
	{
		if (prefix ? _strnicmp(args[i].c_str(), name, strlen(name)) == 0 : _stricmp(args[i].c_str(), name) == 0)
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static const char *argumentValue(const std::vector<std::string> &args, const char *name)
{
	for (size_t i = 1; i + 1 < args.size(); ++i)
	{
		if (_stricmp(args[i].c_str(), name) == 0)
			return args[i + 1].c_str();
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
static Bool fileExists(const std::string &path)
{
	DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

//-------------------------------------------------------------------------------------------------
/** The map for -file: a .map path, or a map name looked up in the Maps folder of the game folder
	and of the user data folder. Names that are found nowhere are left to the game's archives
	(Maps\<name>\<name>.map). The game expects maps in a folder of the same name and is given the
	path without that folder (Maps\<name>.map), as on its own command line. */
//-------------------------------------------------------------------------------------------------
static std::string previewMapArgument(const std::string &map, std::string &error)
{
	std::string path;
	if (map.size() > 4 && _stricmp(map.c_str() + map.size() - 4, ".map") == 0)
	{
		path = map;
	}
	else
	{
		std::string inGame = "Maps\\" + map + "\\" + map + ".map";
		path = inGame;
		if (!fileExists(inGame))
		{
			char documents[MAX_PATH];
			if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
			{
				std::string inUserData = std::string(documents) + "\\Command and Conquer Generals Zero Hour Data\\" + inGame;
				if (fileExists(inUserData))
					path = inUserData;
			}
		}
	}

	for (size_t i = 0; i < path.size(); ++i)
	{
		if (path[i] == '/')
			path[i] = '\\';
	}
	const size_t fileStart = path.find_last_of('\\');
	const std::string folderPath = fileStart == std::string::npos ? std::string() : path.substr(0, fileStart);
	const std::string stem = path.substr(fileStart + 1, path.size() - fileStart - 1 - 4);
	const size_t folderStart = folderPath.find_last_of('\\');
	const std::string folder = folderStart == std::string::npos ? folderPath : folderPath.substr(folderStart + 1);
	if (_stricmp(folder.c_str(), stem.c_str()) != 0)
	{
		error = "the map must be in a folder of its own name: " + path;
		return path;
	}
	// The game wants at least one folder in the path.
	return (folderStart == std::string::npos ? ".\\" : "") + folderPath + ".map";
}

//-------------------------------------------------------------------------------------------------
/** The engine reads its options from __argv. The editor adds the options that make the game start
	right into the preview: windowed, no intro, no shell map, the preview map as a single player
	game (-file) and the MCP bridge. Options given on the command line win. */
//-------------------------------------------------------------------------------------------------
static std::vector<std::string> s_args;
static std::vector<char *> s_argv;

static void addEditorArguments()
{
	for (Int i = 0; i < __argc; ++i)
		s_args.push_back(__argv[i]);

	// -map <name or .map path> picks the preview map. The local player is the map's human player.
	std::string map = "FXEditorPreview";
	if (const char *value = argumentValue(s_args, "-map"))
		map = value;
	FXEditorApp::setPreviewMap(map.c_str());

	if (!hasArgument(s_args, "-win") && !hasArgument(s_args, "-fullscreen"))
		s_args.push_back("-win");
	if (!hasArgument(s_args, "-xres"))
	{
		s_args.push_back("-xres");
		s_args.push_back(std::to_string(DEFAULT_WIDTH));
	}
	if (!hasArgument(s_args, "-yres"))
	{
		s_args.push_back("-yres");
		s_args.push_back(std::to_string(DEFAULT_HEIGHT));
	}
	if (!hasArgument(s_args, "-quickstart"))
		s_args.push_back("-quickstart");
	if (!hasArgument(s_args, "-noshellmap"))
		s_args.push_back("-noshellmap");
	if (!hasArgument(s_args, "-asserts") && !hasArgument(s_args, "-ignoreAsserts"))
		s_args.push_back("-ignoreAsserts");

	// The game starts the map as a single player game: no skirmish setup, no starting units.
	if (!hasArgument(s_args, "-file"))
	{
		std::string error;
		std::string file = previewMapArgument(map, error);
		if (error.empty())
		{
			s_args.push_back("-file");
			s_args.push_back(file);
		}
		else
		{
			FXEditorApp::setStartError(error.c_str());
		}
	}

	// The game MCP bridge serves the editor's own commands.
	if (!hasArgument(s_args, "-mcpport", TRUE))
		s_args.push_back("-mcpport:" + std::to_string(FXEditorApp::DEFAULT_MCP_PORT));

	for (size_t i = 0; i < s_args.size(); ++i)
		s_argv.push_back(const_cast<char *>(s_args[i].c_str()));
	s_argv.push_back(nullptr);
	__argc = (int)s_args.size();
	__argv = s_argv.data();
}

//-------------------------------------------------------------------------------------------------
static LONG WINAPI UnHandledExceptionFilter(struct _EXCEPTION_POINTERS *e_info)
{
	DumpExceptionInfo(e_info->ExceptionRecord->ExceptionCode, e_info);
	return EXCEPTION_EXECUTE_HANDLER;
}

//-------------------------------------------------------------------------------------------------
Int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, Int nCmdShow)
{
	Int exitcode = 1;

	try
	{
		SetUnhandledExceptionFilter(UnHandledExceptionFilter);

		TheAsciiStringCriticalSection = &critSec1;
		TheUnicodeStringCriticalSection = &critSec2;
		TheDmaCriticalSection = &critSec3;
		TheMemoryPoolCriticalSection = &critSec4;
		TheDebugLogCriticalSection = &critSec5;

		addEditorArguments();

		initMemoryManager();

		CommandLine::parseCommandLineForStartup();
		// The startup options may already have been read before WinMain (by the debug log), without
		// the ones added above. The editor always runs in a window.
		TheWritableGlobalData->m_windowed = TRUE;

		if (TheGlobalData->m_headless)
		{
			MessageBox(nullptr, "The FX Editor cannot run headless.", "FX Editor", MB_OK | MB_ICONERROR);
			shutdownMemoryManager();
			return exitcode;
		}

		const Int width = TheGlobalData->m_xResolution > 0 ? TheGlobalData->m_xResolution : DEFAULT_WIDTH;
		const Int height = TheGlobalData->m_yResolution > 0 ? TheGlobalData->m_yResolution : DEFAULT_HEIGHT;
		if (!createAppWindow(hInstance, nCmdShow, width, height))
		{
			shutdownMemoryManager();
			return exitcode;
		}
		ApplicationHInstance = hInstance;

		TheVersion = NEW Version;
		TheVersion->setVersion(1, 4, 601, 0, AsciiString(""), AsciiString(""), AsciiString(__TIME__), AsciiString(__DATE__));

		exitcode = GameMain();

		delete TheVersion;
		TheVersion = nullptr;

		shutdownMemoryManager();
	}
	catch (...)
	{
	}

	TheUnicodeStringCriticalSection = nullptr;
	TheDmaCriticalSection = nullptr;
	TheMemoryPoolCriticalSection = nullptr;

	return exitcode;
}

//-------------------------------------------------------------------------------------------------
/** Called by GameMain(): the engine of the editor. */
//-------------------------------------------------------------------------------------------------
GameEngine *CreateGameEngine()
{
	FXEditorEngine *engine = NEW FXEditorEngine;
	engine->setIsActive(isWinMainActive);
	return engine;
}
