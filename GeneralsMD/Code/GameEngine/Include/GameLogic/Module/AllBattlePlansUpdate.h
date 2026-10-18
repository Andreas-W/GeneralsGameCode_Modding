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

// FILE: AllBattlePlansUpdate.h ///////////////////////////////////////////////////////////////////
// Desc:   Grants the controlling player the army bonuses of all battle plans while this object
//         exists. Acts as an additional battle plan source next to the strategy centers, so the
//         plan a strategy center runs stays untouched. Bonus values are read from the
//         BattlePlanUpdate of the given strategy center template.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/BattlePlanUpdate.h"

//-------------------------------------------------------------------------------------------------
class AllBattlePlansUpdateModuleData : public UpdateModuleData
{
public:
	AsciiString m_strategyCenterTemplateName;

	AllBattlePlansUpdateModuleData() {}

	static void buildFieldParse(MultiIniFieldParse& p)
	{
		UpdateModuleData::buildFieldParse(p);
		static const FieldParse dataFieldParse[] =
		{
			{ "StrategyCenterTemplate",	INI::parseAsciiString,	nullptr, offsetof( AllBattlePlansUpdateModuleData, m_strategyCenterTemplateName ) },
			{ nullptr, nullptr, nullptr, 0 }
		};
		p.add(dataFieldParse);
	}
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class AllBattlePlansUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AllBattlePlansUpdate, "AllBattlePlansUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AllBattlePlansUpdate, AllBattlePlansUpdateModuleData )

public:

	AllBattlePlansUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override;
	virtual void onDelete() override;
	virtual void onCapture( Player *oldOwner, Player *newOwner ) override;

private:

	void changeAllPlans( Player *player, Int delta );

	enum { PLAN_SOURCE_COUNT = PLANSTATUS_COUNT - 1 };

	BattlePlanBonusesData m_bonuses[PLAN_SOURCE_COUNT];	///< indexed by plan - 1
	Bool m_valid;			///< bonus values were found on the strategy center template
	Bool m_applied;		///< plans are currently counted at the controlling player
};
