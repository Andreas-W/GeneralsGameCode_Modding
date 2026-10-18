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

// SkirmishLaunch.h
// Starts a skirmish without the skirmish menu, for games that are set up on the command line
// (AI matches, the MCP bridge).

#pragma once

#include "Common/AsciiString.h"

#include <vector>

struct SkirmishSlot
{
	AsciiString faction;	///< player template name or side; empty or "Observer" for an observer
	Int slotState;				///< SLOT_PLAYER for the local player, SLOT_EASY_AI .. SLOT_BRUTAL_AI for AI players
	Int team;							///< -1 for none
};

namespace SkirmishLaunch
{
	/// The map cache key for a map key or part of a multiplayer map's path; empty if none matches.
	AsciiString findMap(const AsciiString &name);

	/// Number of start positions of a map from the cache, or -1 when unknown.
	Int countStartPositions(const AsciiString &mapKey);

	/// SlotState of an AI difficulty name (easy, normal, hard), or -1.
	Int slotStateForDifficulty(const AsciiString &difficulty);

	/// Fills the skirmish game info: slot 0 is the local player, the others are AI players.
	/// Returns false with a reason when a faction is unknown.
	Bool prepare(const AsciiString &mapKey, const std::vector<SkirmishSlot> &slots, Int seed, Int startingCash, AsciiString &error);

	/// Starts the prepared game. maxFps is the frame rate limit of the game (1000 for none).
	void launch(const AsciiString &mapKey, Int seed, Int maxFps);
}
