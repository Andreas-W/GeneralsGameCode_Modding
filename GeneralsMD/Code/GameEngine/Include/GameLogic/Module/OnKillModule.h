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

// FILE: OnKillModule.h ////////////////////////////////////////////////////////////////////////////
// Desc:   Behavior interface + shared filter for modules that react when the owning object kills
//         another object (the "killer" side, as opposed to DieModule which is the victim side).
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/Module.h"
#include "Common/KindOf.h"
#include "Common/ObjectStatusTypes.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BehaviorModule.h"

class Object;
struct FieldParse;

//-------------------------------------------------------------------------------------------------
/** Implemented by modules that want to be notified whenever their owning object kills another. */
//-------------------------------------------------------------------------------------------------
class OnKillModuleInterface
{
public:
	virtual void onKilledObject( Object *victim, const DamageInfo *damageInfo ) = 0;
};

//-------------------------------------------------------------------------------------------------
/** Shared filter describing which kills a OnKill module should react to. Checks the KILLED object
	(victim) by KindOf and ObjectStatus, its player relationship to the killer, and (when a DamageInfo
	is available) the death type. Does NOT inherit from ModuleData (mirrors DieMuxData). */
//-------------------------------------------------------------------------------------------------
class KillMuxData
{
public:
	KindOfMaskType				m_victimRequiredKindOf;		///< victim must match these KindOf bits (ALL or ANY, per m_requiresAllKindOfs)
	KindOfMaskType				m_victimForbiddenKindOf;	///< victim must have none of these KindOf bits
	Bool									m_requiresAllKindOfs;			///< if true victim needs ALL required KindOfs, else ANY of them
	ObjectStatusMaskType	m_victimRequiredStatus;		///< victim must have all of these status bits
	ObjectStatusMaskType	m_victimForbiddenStatus;	///< victim must have none of these status bits
	Int										m_victimRelationship;			///< bitmask (WEAPON_AFFECTS_ALLIES/ENEMIES/NEUTRALS) of allowed killer->victim relationships
	DeathTypeFlags				m_deathTypes;							///< only these death types trigger (checked only when a DamageInfo is present)
	DamageTypeFlags				m_damageTypes;						///< only these damage types trigger (checked only when a DamageInfo is present)

	KillMuxData();
	static const FieldParse* getFieldParse();

	Bool isKillApplicable( const Object *killer, const Object *victim, const DamageInfo *damageInfo ) const;
};
