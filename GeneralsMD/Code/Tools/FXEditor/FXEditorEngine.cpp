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

#include "FXEditorEngine.h"
#include "FXEditorApp.h"

//-------------------------------------------------------------------------------------------------
void FXEditorKeyboard::getKey(KeyboardIO *key)
{
	DirectInputKeyboard::getKey(key);
	// Read past the keys the editor interface takes, so they never reach the game.
	while (FXEditorApp::wantsKeyboard() && key->key != KEY_NONE && key->key != KEY_LOST)
		DirectInputKeyboard::getKey(key);
}

//-------------------------------------------------------------------------------------------------
FXEditorEngine::FXEditorEngine()
{
}

//-------------------------------------------------------------------------------------------------
FXEditorEngine::~FXEditorEngine()
{
	// Before the engine tears down the display and its device.
	FXEditorApp::shutdown();
}

//-------------------------------------------------------------------------------------------------
void FXEditorEngine::init()
{
	Win32GameEngine::init();
	FXEditorApp::init();
}

//-------------------------------------------------------------------------------------------------
void FXEditorEngine::update()
{
	// The interface is built before the frame is drawn; the display draws it on top (W3DDisplay::m_overlayDraw).
	FXEditorApp::update();
	Win32GameEngine::update();
}
