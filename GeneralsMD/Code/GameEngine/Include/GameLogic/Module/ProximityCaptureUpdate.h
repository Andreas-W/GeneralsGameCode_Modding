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

// FILE: ProximityCaptureUpdate.h /////////////////////////////////////////////////////////////////////////////
// Author: 	Matthew D. Campbell, April 2002
// Desc:  Reacts when an enemy is within range
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpdateModule.h"
#include "Common/KindOf.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class ProximityCaptureUpdateModuleData : public UpdateModuleData
{
public:
	UnsignedInt					m_captureTickDelay;	///< Time delay between evaluation ticks
	Real								m_captureRate;   ///< How much to capture per tick
	Real								m_uncapRate;   ///< How much progress to remove
	Real								m_uncapRateNeutral;   ///< How much progress to remove when neutral;
	Real								m_recoverRate;   ///< How much progress to recover when no units are around;
	Real								m_captureRadius;
	KindOfMaskType			m_requiredKindOf;						///< Must be set on target
	KindOfMaskType			m_forbiddenKindOf;	///< Must be clear on target
	Bool								m_isCountAirborne;					///< Affect Airborne targets
	Bool								m_requiresAllKindOfs;					///< requires ALL requiredKindOfs or just one of them

	Real								m_unitValueContentionDelta;   ///< unit values between players need to differ by this much to trigger capture
	Real								m_unitValueCountFactor;					///< value factor for the number of units (i.e. equal value for each unit)
	//Real								m_unitValueBuildCostFactor;   ///< value factor for sellValue of the unit

	Bool								m_showProgressBar;

	ProximityCaptureUpdateModuleData()
	{
		m_captureTickDelay = LOGICFRAMES_PER_SECOND;
		m_captureRate = 1.0 / 30.0f;
		m_uncapRate = 1.0 / 30.0f;
		m_uncapRateNeutral = -1.0f;
		m_recoverRate = -1.0f;

		m_unitValueContentionDelta = 0.01;
		m_unitValueCountFactor = 1.0;
		//m_unitValueBuildCostFactor = 0.0;
	}

	static void buildFieldParse(MultiIniFieldParse& p)
	{
    UpdateModuleData::buildFieldParse(p);
		static const FieldParse dataFieldParse[] =
		{
			{ "CaptureTickDelay",		INI::parseDurationUnsignedInt,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_captureTickDelay) },
			{ "CaptureRadius",		INI::parseReal,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_captureRadius) },
			{ "CaptureProgressPerTick",		INI::parsePercentToReal,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_captureRate) },
			{ "CaptureProgressRemovePerTick",		INI::parsePercentToReal,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_uncapRate) },
			{ "CaptureProgressRemovePerTickFromNeutral",		INI::parsePercentToReal,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_uncapRateNeutral) },
			{ "CaptureProgressRecoverPerTick",		INI::parsePercentToReal,		NULL, offsetof( ProximityCaptureUpdateModuleData, m_recoverRate) },
			{ "RequiredKindOf",		KindOfMaskType::parseFromINI,		NULL, offsetof(ProximityCaptureUpdateModuleData, m_requiredKindOf) },
		  { "RequiresAllKindOfs", INI::parseBool, NULL, offsetof(ProximityCaptureUpdateModuleData, m_requiresAllKindOfs) },
		  { "ForbiddenKindOf",	KindOfMaskType::parseFromINI,		NULL, offsetof(ProximityCaptureUpdateModuleData, m_forbiddenKindOf) },
			{ "AllowAirborne", INI::parseBool, NULL, offsetof(ProximityCaptureUpdateModuleData, m_isCountAirborne) },
			{ "UnitValueContentionDelta",		INI::parseReal,		NULL, offsetof(ProximityCaptureUpdateModuleData, m_unitValueContentionDelta) },
			{ "UnitValueCountFactor",		INI::parseReal,		NULL, offsetof(ProximityCaptureUpdateModuleData, m_unitValueCountFactor) },
			//{ "UnitValueBuildCostFactor",		INI::parseReal,		NULL, offsetof(ProximityCaptureUpdateModuleData, m_unitValueBuildCostFactor) },
			{ "ShowProgressBar", INI::parseBool, NULL, offsetof(ProximityCaptureUpdateModuleData, m_showProgressBar) },
			{ 0, 0, 0, 0 }
		};
    p.add(dataFieldParse);
	}
};

//-------------------------------------------------------------------------------------------------
/** EnemyNear update */
//-------------------------------------------------------------------------------------------------
class ProximityCaptureUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ProximityCaptureUpdate, "ProximityCaptureUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ProximityCaptureUpdate, ProximityCaptureUpdateModuleData )

public:

	ProximityCaptureUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update();

	Bool getProgressBarInfo(Real& progress, Int& type, RGBAColorInt& color, RGBAColorInt& colorBG);

protected:

	Int checkDominantPlayer( void );

	void handleCaptureProgress(Int dominantPlayer);

	Real getValueForUnit(const Object* obj) const;

private:

	Int m_capturingPlayer;
	Int m_dominantPlayerPrev;
	Bool m_isContested;
	Real m_captureProgress;

	void startCapture(Int playerId);
	void finishCapture(Int playerId);
	void startUncap(Int playerId);
	void finishUncap(Int playerId);

};
