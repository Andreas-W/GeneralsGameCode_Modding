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

// FILE: SpecialPowerDesignatorUpdate.cpp ///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerDesignatorUpdate.h"
#include "GameLogic/Object.h"



//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

SpecialPowerDesignatorUpdateModuleData::SpecialPowerDesignatorUpdateModuleData()
{
	m_specialPowerTemplate = nullptr;
	m_designatorRadius = 0.0f;
	m_alwaysShowDecal = false;
	m_triggerStatusTime = 0;
	m_triggerStatusType = OBJECT_STATUS_NONE;
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/*static*/ void SpecialPowerDesignatorUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	RadiusDecalBehaviorModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
    { "SpecialPowerTemplate",				INI::parseSpecialPowerTemplate,		NULL, offsetof(SpecialPowerDesignatorUpdateModuleData, m_specialPowerTemplate) },
		{ "DesignatorRadius",					INI::parseReal,						NULL, offsetof(SpecialPowerDesignatorUpdateModuleData, m_designatorRadius) },
		{ "AlwaysShowDecal",					INI::parseBool,						NULL, offsetof(SpecialPowerDesignatorUpdateModuleData, m_alwaysShowDecal) },
		// { "WorksWhileContained",					INI::parseBool,						NULL, offsetof(SpecialPowerDesignatorUpdateModuleData, m_worksWhileContained) },
		{ "TriggerStatusTime",			INI::parseDurationUnsignedInt,	NULL,	offsetof(SpecialPowerDesignatorUpdateModuleData, m_triggerStatusTime) },
		{ "TriggerStatusType",			ObjectStatusMaskType::parseSingleBitFromINI,	NULL,	offsetof(SpecialPowerDesignatorUpdateModuleData, m_triggerStatusType) },
		{ "DecalRadius",			INI::parseReal,									NULL,	offsetof( RadiusDecalBehaviorModuleData, m_decalRadius) },
		{ 0, 0, 0, 0 }
	};

	p.add(dataFieldParse);
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpecialPowerDesignatorUpdate::SpecialPowerDesignatorUpdate( Thing *thing, const ModuleData* moduleData ) : RadiusDecalBehavior( thing, moduleData )
{
	m_targetingActive = false;
	m_statusClearFrame = 0;

	/*if (getSpecialPowerDesignatorUpdateModuleData()->m_initiallyActive)
	{
		giveSelfUpgrade();
	}
	else {
		clearDecal();
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	}*/
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
SpecialPowerDesignatorUpdate::~SpecialPowerDesignatorUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//void SpecialPowerDesignatorUpdate::createRadiusDecal( void )
//{
//	const SpecialPowerDesignatorUpdateModuleData* data = getSpecialPowerDesignatorUpdateModuleData();
//
//	if (data->m_alwaysShowDecal || m_targetingActive) {
//		RadiusDecalBehavior::createRadiusDecal();
//	}
//	else {
//		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
//	}
//}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpecialPowerDesignatorUpdate::setActive(bool status)
{
	DEBUG_LOG((">>> SpecialPowerDesignatorUpdate::setActive = %d", status));

	const SpecialPowerDesignatorUpdateModuleData* data = getSpecialPowerDesignatorUpdateModuleData();

	m_targetingActive = status;

	if (!status && !data->m_alwaysShowDecal)
		clearDecal();

	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void SpecialPowerDesignatorUpdate::triggerSpecialPower()
{
	const SpecialPowerDesignatorUpdateModuleData* data = getSpecialPowerDesignatorUpdateModuleData();
	if (data->m_triggerStatusTime > 0 && data->m_triggerStatusType != OBJECT_STATUS_NONE) {
		getObject()->setStatus(MAKE_OBJECT_STATUS_MASK(data->m_triggerStatusType));

		m_statusClearFrame = TheGameLogic->getFrame() + data->m_triggerStatusTime;
		setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
	}
}
//
////-------------------------------------------------------------------------------------------------
////-------------------------------------------------------------------------------------------------
//void SpecialPowerDesignatorUpdate::killRadiusDecal()
//{
//	clearDecal();
//	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
//}
//
//// -----------------------------------------------------------------------------------------------
//void SpecialPowerDesignatorUpdate::clearDecal()
//{
//	m_radiusDecal.clear();
//}
//
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime SpecialPowerDesignatorUpdate::update( void )
{
	const SpecialPowerDesignatorUpdateModuleData* data = getSpecialPowerDesignatorUpdateModuleData();

	UpdateSleepTime result = UPDATE_SLEEP_FOREVER;
	// First handle status
	if (m_statusClearFrame > 0 && data->m_triggerStatusType != OBJECT_STATUS_NONE) {
		if (TheGameLogic->getFrame() == m_statusClearFrame) {
			getObject()->clearStatus(MAKE_OBJECT_STATUS_MASK(data->m_triggerStatusType));
		}
		else {
			result = UPDATE_SLEEP_NONE;
		}
  }

	//if (m_statusClearFrame > 0 && data->m_triggerStatusType != OBJECT_STATUS_NONE && TheGameLogic->getFrame() == m_statusClearFrame) {
	//	getObject()->clearStatus(MAKE_OBJECT_STATUS_MASK(data->m_triggerStatusType));
	//}

  // Then decal
	if (!m_targetingActive && !data->m_alwaysShowDecal) {
		return MIN(result, UPDATE_SLEEP_FOREVER);
	}

	return MIN(RadiusDecalBehavior::update(), UPDATE_SLEEP_FOREVER);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SpecialPowerDesignatorUpdate::isValidDesignatorForSpecialPower(const SpecialPowerTemplate* templ)
{
	return isUpgradeActive() && templ == getSpecialPowerDesignatorUpdateModuleData()->m_specialPowerTemplate &&
		(getSpecialPowerDesignatorUpdateModuleData()->m_worksWhileContained || !getObject()->isDisabledByType(DISABLED_HELD));

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SpecialPowerDesignatorUpdate::crc( Xfer *xfer )
{

	// extend base class
	RadiusDecalBehavior::crc( xfer );

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SpecialPowerDesignatorUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	RadiusDecalBehavior::xfer( xfer );

	xfer->xferBool(&m_targetingActive);
	xfer->xferUnsignedInt(&m_statusClearFrame);


}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SpecialPowerDesignatorUpdate::loadPostProcess( void )
{

	// extend base class
	UpdateModule::loadPostProcess();

}  // end loadPostProcess
