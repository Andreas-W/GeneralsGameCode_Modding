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

// SciencePurchaseSlots.h
// How many sciences the purchase screens offer per rank. Kept apart from the control bar so that
// game logic can size its own tables by it without pulling in the user interface.

#pragma once

enum {
			 MAX_PURCHASE_SCIENCE_RANK_1 = 7,
			 MAX_PURCHASE_SCIENCE_RANK_3 = 21,
			 MAX_PURCHASE_SCIENCE_RANK_8 = 7,
			};

/// Every science a player can purchase: one per slot of the purchase screens.
enum { MAX_PURCHASE_SCIENCES = MAX_PURCHASE_SCIENCE_RANK_1 + MAX_PURCHASE_SCIENCE_RANK_3 + MAX_PURCHASE_SCIENCE_RANK_8 };
