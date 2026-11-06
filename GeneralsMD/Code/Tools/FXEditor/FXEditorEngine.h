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

// The game engine of the FX Editor: the Win32 game engine with a game client whose keyboard
// ignores keys while the editor interface has the focus, and an update that runs the editor.

#pragma once

#include "Win32Device/Common/Win32GameEngine.h"
#include "Win32Device/GameClient/Win32DIKeyboard.h"
#include "W3DDevice/GameClient/W3DGameClient.h"

//-------------------------------------------------------------------------------------------------
/** Drops all key events while the editor interface takes keyboard input, so typing into a field
	does not trigger game hotkeys. */
//-------------------------------------------------------------------------------------------------
class FXEditorKeyboard : public DirectInputKeyboard
{
public:
	virtual void getKey(KeyboardIO *key) override;
};

//-------------------------------------------------------------------------------------------------
class FXEditorGameClient : public W3DGameClient
{
protected:
	virtual Keyboard *createKeyboard() override { return NEW FXEditorKeyboard; }
};

//-------------------------------------------------------------------------------------------------
class FXEditorEngine : public Win32GameEngine
{
public:
	FXEditorEngine();
	virtual ~FXEditorEngine() override;

	virtual void init() override;
	virtual void update() override;

protected:
	virtual GameClient *createGameClient() override { return NEW FXEditorGameClient; }
};
