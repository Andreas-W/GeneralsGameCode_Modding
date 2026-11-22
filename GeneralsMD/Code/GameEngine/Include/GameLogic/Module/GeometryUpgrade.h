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

// FILE: GeometryUpgrade.h ////////////////////////////////////////////////////////////////////////
// Desc: Replaces the object's whole geometry (primary and additional shapes) when upgraded
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/Geometry.h"
#include "GameLogic/Module/UpgradeModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class GeometryUpgradeModuleData : public UpgradeModuleData
{
public:

	GeometryUpgradeModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	GeometryInfo m_geometry;		///< the geometry the object gets
};

//-------------------------------------------------------------------------------------------------
class GeometryUpgrade : public UpgradeModule
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( GeometryUpgrade, "GeometryUpgrade" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( GeometryUpgrade, GeometryUpgradeModuleData );

public:

	GeometryUpgrade( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

protected:
	virtual void upgradeImplementation() override; ///< Here's the actual work of Upgrading
	virtual Bool isSubObjectsUpgrade() override { return false; }

};
