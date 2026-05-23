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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: SpecialPowerDesignatorUpdate.h /////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __SpecialPowerDesignatorUpdate_H_
#define __SpecialPowerDesignatorUpdate_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/RadiusDecalBehavior.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////
class SpecialPowerTemplate;
class Thing;
class FXList;

//-------------------------------------------------------------------------------------------------
class SpecialPowerDesignatorUpdateModuleData : public RadiusDecalBehaviorModuleData
{
public:
	SpecialPowerTemplate* m_specialPowerTemplate;
	Real m_designatorRadius;
	Bool m_alwaysShowDecal;
	ObjectStatusTypes m_triggerStatusType;
	UnsignedInt m_triggerStatusTime;
	const FXList* m_triggerFX;

	SpecialPowerDesignatorUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class SpecialPowerDesignatorUpdate : public RadiusDecalBehavior
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SpecialPowerDesignatorUpdate, "SpecialPowerDesignatorUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SpecialPowerDesignatorUpdate, SpecialPowerDesignatorUpdateModuleData )

public:

	SpecialPowerDesignatorUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// UpdateModuleInterface
	virtual UpdateSleepTime update();

	Real getDesignatorRadius() { return getSpecialPowerDesignatorUpdateModuleData()->m_designatorRadius; }

	Bool isValidDesignatorForSpecialPower(const SpecialPowerTemplate* templ);

	void triggerSpecialPower();

protected:

private:

	UnsignedInt m_statusClearFrame;

};

#endif // __SpecialPowerDesignatorUpdate_H_

