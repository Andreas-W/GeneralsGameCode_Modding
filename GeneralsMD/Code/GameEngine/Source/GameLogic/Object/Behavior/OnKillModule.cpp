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

// FILE: OnKillModule.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   Shared filter (KillMuxData) for OnKill behavior modules.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/INI.h"
#include "GameLogic/Module/OnKillModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"		// for TheWeaponAffectsMaskNames and WEAPON_AFFECTS_* bits

//-------------------------------------------------------------------------------------------------
KillMuxData::KillMuxData()
{
	// KindOf/status masks default to empty (no restriction).
	m_requiresAllKindOfs = true;										// default: victim must match ALL required KindOfs
	m_victimRelationship = WEAPON_AFFECTS_ENEMIES;	// by default, only react to killing enemies
	m_deathTypes = DEATH_TYPE_FLAGS_ALL;
	m_damageTypes = DAMAGE_TYPE_FLAGS_ALL;
}

//-------------------------------------------------------------------------------------------------
const FieldParse* KillMuxData::getFieldParse()
{
	static const FieldParse dataFieldParse[] =
	{
		{ "KilledKindOf",					KindOfMaskType::parseFromINI,				nullptr, offsetof( KillMuxData, m_victimRequiredKindOf ) },
		{ "ForbiddenKilledKindOf",	KindOfMaskType::parseFromINI,				nullptr, offsetof( KillMuxData, m_victimForbiddenKindOf ) },
		{ "RequiresAllKindOfs",		INI::parseBool,											nullptr, offsetof( KillMuxData, m_requiresAllKindOfs ) },
		{ "RequiredKilledStatus",		ObjectStatusMaskType::parseFromINI,	nullptr, offsetof( KillMuxData, m_victimRequiredStatus ) },
		{ "ForbiddenKilledStatus",	ObjectStatusMaskType::parseFromINI,	nullptr, offsetof( KillMuxData, m_victimForbiddenStatus ) },
		{ "KilledRelationship",			INI::parseBitString32,							TheWeaponAffectsMaskNames, offsetof( KillMuxData, m_victimRelationship ) },
		{ "DeathTypes",						INI::parseDeathTypeFlags,						nullptr, offsetof( KillMuxData, m_deathTypes ) },
		{ "DamageTypes",					INI::parseDamageTypeFlags,					nullptr, offsetof( KillMuxData, m_damageTypes ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return dataFieldParse;
}

//-------------------------------------------------------------------------------------------------
Bool KillMuxData::isKillApplicable( const Object *killer, const Object *victim, const DamageInfo *damageInfo ) const
{
	if (killer == nullptr || victim == nullptr)
		return false;

	// wrong death type? punt (only when we actually have damage context)
	if (damageInfo != nullptr && !getDeathTypeFlag(m_deathTypes, damageInfo->in.m_deathType))
		return false;

	// wrong damage type? punt (only when we actually have damage context)
	if (damageInfo != nullptr && !getDamageTypeFlag(m_damageTypes, damageInfo->in.m_damageType))
		return false;

	// victim KindOf: must never have a forbidden bit, and must match the required bits either fully
	// (ALL) or partially (ANY) depending on m_requiresAllKindOfs.
	if (m_requiresAllKindOfs)
	{
		if (!victim->isKindOfMulti(m_victimRequiredKindOf, m_victimForbiddenKindOf))
			return false;
	}
	else
	{
		if (m_victimRequiredKindOf.any() && !victim->isAnyKindOf(m_victimRequiredKindOf))
			return false;
		if (victim->isAnyKindOf(m_victimForbiddenKindOf))
			return false;
	}

	// all 'required' status bits must be set.
	if (m_victimRequiredStatus.any() && !victim->getStatusBits().testForAll(m_victimRequiredStatus))
		return false;

	// all 'forbidden' status bits must be clear.
	if (m_victimForbiddenStatus.any() && victim->getStatusBits().testForAny(m_victimForbiddenStatus))
		return false;

	// relationship of the victim to the killer must be in the allowed set.
	Relationship r = killer->getRelationship(victim);
	Int need;
	if (r == ALLIES)
		need = WEAPON_AFFECTS_ALLIES;
	else if (r == ENEMIES)
		need = WEAPON_AFFECTS_ENEMIES;
	else
		need = WEAPON_AFFECTS_NEUTRALS;
	if (!(m_victimRelationship & need))
		return false;

	return true;
}
