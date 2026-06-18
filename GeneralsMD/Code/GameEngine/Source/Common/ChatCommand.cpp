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

// FILE: ChatCommand.cpp ////////////////////////////////////////////////////////////////////////
// Desc: Parsing and storage for ChatCommand blocks defined in the optional ChatCommands.ini.
//////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ChatCommand.h"
#include "Common/INI.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Upgrade.h"
#include "Common/GameAudio.h"
#include "Common/MiscAudio.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/KindOf.h"
#include "Common/ModelState.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Mouse.h"
#include "GameClient/View.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/WeaponSetType.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"

//-------------------------------------------------------------------------------------------------
ChatCommandStore* TheChatCommandStore = nullptr;

const FieldParse ChatCommand::s_fieldParseTable[] =
{
	{ "Money",			INI::parseInt,			nullptr,	offsetof( ChatCommand, m_money ) },
	{ "Rank",			INI::parseUnsignedInt,	nullptr,	offsetof( ChatCommand, m_rank ) },
	{ "ReadyTimers",	INI::parseBool,			nullptr,	offsetof( ChatCommand, m_readyTimers ) },
	{ "SpawnUnit",				INI::parseAsciiString,	nullptr,	offsetof( ChatCommand, m_spawnUnit ) },
	{ "ToggleUnitRequirements",	INI::parseBool,			nullptr,	offsetof( ChatCommand, m_toggleUnitRequirements ) },
	{ "ToggleInfiniteEnergy",	INI::parseBool,			nullptr,	offsetof( ChatCommand, m_toggleInfiniteEnergy ) },
	{ "GrantAllUpgrades",		INI::parseBool,			nullptr,	offsetof( ChatCommand, m_grantAllUpgrades ) },
	{ "PromoteUnit",			INI::parseInt,			nullptr,	offsetof( ChatCommand, m_promoteUnit ) },
	{ "GiveSalvage",			INI::parseInt,			nullptr,	offsetof( ChatCommand, m_giveSalvage ) },
	{ NULL, NULL, 0, 0 }  // keep this last
};

//-------------------------------------------------------------------------------------------------
// Set a weapon-salvager's crate-upgrade tier (0=none, 1=ONE, 2=TWO) to match newTier.
static void setWeaponSalvageTier( Object *obj, Int newTier )
{
	obj->clearWeaponSetFlag( WEAPONSET_CRATEUPGRADE_ONE );
	obj->clearWeaponSetFlag( WEAPONSET_CRATEUPGRADE_TWO );
	if (newTier >= 2)
		obj->setWeaponSetFlag( WEAPONSET_CRATEUPGRADE_TWO );
	else if (newTier == 1)
		obj->setWeaponSetFlag( WEAPONSET_CRATEUPGRADE_ONE );
}

//-------------------------------------------------------------------------------------------------
// Set an armor-salvager's crate-upgrade tier (0=none, 1=ONE, 2=TWO), including model visuals.
static void setArmorSalvageTier( Object *obj, Int newTier )
{
	obj->clearArmorSetFlag( ARMORSET_CRATE_UPGRADE_ONE );
	obj->clearArmorSetFlag( ARMORSET_CRATE_UPGRADE_TWO );
	obj->clearModelConditionState( MODELCONDITION_ARMORSET_CRATEUPGRADE_ONE );
	obj->clearModelConditionState( MODELCONDITION_ARMORSET_CRATEUPGRADE_TWO );
	if (newTier >= 2)
	{
		obj->setArmorSetFlag( ARMORSET_CRATE_UPGRADE_TWO );
		obj->setModelConditionState( MODELCONDITION_ARMORSET_CRATEUPGRADE_TWO );
	}
	else if (newTier == 1)
	{
		obj->setArmorSetFlag( ARMORSET_CRATE_UPGRADE_ONE );
		obj->setModelConditionState( MODELCONDITION_ARMORSET_CRATEUPGRADE_ONE );
	}
}

//-------------------------------------------------------------------------------------------------
void ChatCommand::execute() const
{
	// Money: positive grants cash, negative removes it. The local player's funds can never
	// go below zero (withdraw caps at the current balance), so a large removal just empties it.
	if (m_money != 0)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		Money *money = player ? player->getMoney() : nullptr;
		if (money)
		{
			// Suppress the built-in deposit/withdraw sound; play the cash-hack sound instead (below).
			if (m_money > 0)
				money->deposit( (UnsignedInt)m_money, FALSE );
			else
				money->withdraw( (UnsignedInt)(-m_money), FALSE );

			// Play the same sound the Cash Hack special power uses, for the local player.
			if (TheAudio && TheAudio->getMiscAudio())
			{
				AudioEventRTS sound = TheAudio->getMiscAudio()->m_moneyDepositSound;
				sound.setPlayerIndex( player->getPlayerIndex() );
				TheAudio->addAudioEvent( &sound );
			}
		}
	}

	// Rank: grant additional rank levels. setRankLevel() clamps to the maximum rank,
	// so over-granting just tops the player out.
	if (m_rank > 0)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if (player)
			player->setRankLevel( player->getRankLevel() + (Int)m_rank );
	}

	// ReadyTimers: set every special power timer the player owns to ready (available now).
	if (m_readyTimers)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if (player)
		{
			const UnsignedInt now = TheGameLogic->getFrame();
			for (Player::PlayerTeamList::const_iterator pt = player->getPlayerTeams()->begin();
				pt != player->getPlayerTeams()->end(); ++pt)
			{
				for (DLINK_ITERATOR<Team> teamIt = (*pt)->iterate_TeamInstanceList(); !teamIt.done(); teamIt.advance())
				{
					Team *team = teamIt.cur();
					if (!team)
						continue;

					for (DLINK_ITERATOR<Object> objIt = team->iterate_TeamMemberList(); !objIt.done(); objIt.advance())
					{
						Object *obj = objIt.cur();
						if (!obj)
							continue;

						for (BehaviorModule **b = obj->getBehaviorModules(); b && *b; ++b)
						{
							SpecialPowerModuleInterface *sp = (*b)->getSpecialPower();
							if (sp)
							{
								// Ready the per-object timer (superweapons) and the player's shared
								// timer (generals' shortcut powers read their readiness from there).
								sp->setReadyFrame( now );
								const SpecialPowerTemplate *temp = sp->getSpecialPowerTemplate();
								if (temp)
									player->expressSpecialPowerReadyFrame( temp, now );
							}
						}
					}
				}
			}
		}
	}

	// SpawnUnit: create one instance of the named ObjectTemplate for the local player,
	// placed on the terrain under the mouse cursor.
	if (!m_spawnUnit.isEmpty())
	{
		const ThingTemplate *tmpl = TheThingFactory ? TheThingFactory->findTemplate( m_spawnUnit ) : nullptr;
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		Team *team = player ? player->getDefaultTeam() : nullptr;
		if (tmpl && team && TheMouse && TheTacticalView)
		{
			Coord3D pos;
			const MouseIO *mouseIO = TheMouse->getMouseStatus();
			TheTacticalView->screenToTerrain( &mouseIO->pos, &pos );

			Object *obj = TheThingFactory->newObject( tmpl, team );
			if (obj)
			{
				obj->setPosition( &pos );
				obj->setOrientation( 0.0f );
			}
		}
		else if (!tmpl)
		{
			DEBUG_LOG((">>> CHAT COMMAND SpawnUnit: ThingTemplate '%s' not found.", m_spawnUnit.str()));
		}
	}

	// ToggleUnitRequirements: flip ignoring of unit/building build prereqs for the local player
	// (science prereqs still apply). Mark the control bar dirty so build buttons re-evaluate.
	if (m_toggleUnitRequirements)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if (player)
		{
			player->toggleIgnoreUnitPrereqs();
			if (TheControlBar)
				TheControlBar->markUIDirty();
		}
	}

	// ToggleInfiniteEnergy: flip infinite power for the local player. The setter refreshes
	// power-dependent objects; mark the control bar dirty so the power UI updates.
	if (m_toggleInfiniteEnergy)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if (player)
		{
			player->toggleInfinitePower();
			if (TheControlBar)
				TheControlBar->markUIDirty();
		}
	}

	// GrantAllUpgrades: complete every player-type upgrade for the local player.
	if (m_grantAllUpgrades)
	{
		Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
		if (player && TheUpgradeCenter)
		{
			for (UpgradeTemplate *tmpl = TheUpgradeCenter->firstUpgradeTemplate();
				tmpl != nullptr; tmpl = tmpl->friend_getNext())
			{
				if (tmpl->getUpgradeType() != UPGRADE_TYPE_PLAYER)
					continue;
				if (player->hasUpgradeComplete( tmpl ))
					continue;
				player->addUpgrade( tmpl, UPGRADE_STATUS_COMPLETE );
			}
		}
	}

	// PromoteUnit: change veterancy of the player's currently selected (trainable) units by
	// m_promoteUnit levels (negative demotes), clamped to the valid range.
	if (m_promoteUnit != 0 && TheInGameUI)
	{
		const DrawableList *selected = TheInGameUI->getAllSelectedLocalDrawables();
		if (selected)
		{
			for (DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it)
			{
				Drawable *draw = *it;
				Object *obj = draw ? draw->getObject() : nullptr;
				ExperienceTracker *exp = obj ? obj->getExperienceTracker() : nullptr;
				if (!exp || !exp->isTrainable())
					continue;

				Int newLevel = (Int)exp->getVeterancyLevel() + m_promoteUnit;
				if (newLevel < LEVEL_FIRST)
					newLevel = LEVEL_FIRST;
				else if (newLevel > LEVEL_LAST)
					newLevel = LEVEL_LAST;

				exp->setVeterancyLevel( (VeterancyLevel)newLevel );
			}
		}
	}

	// GiveSalvage: change the crate-upgrade tier of the player's selected salvagers by
	// m_giveSalvage steps (negative removes), clamped 0..2. Weapon tier only applies to
	// weapon-salvagers, armor tier only to armor-salvagers; other units are unaffected.
	if (m_giveSalvage != 0 && TheInGameUI)
	{
		const DrawableList *selected = TheInGameUI->getAllSelectedLocalDrawables();
		if (selected)
		{
			for (DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it)
			{
				Drawable *draw = *it;
				Object *obj = draw ? draw->getObject() : nullptr;
				if (!obj)
					continue;

				Bool changed = FALSE;

				if (obj->isKindOf( KINDOF_WEAPON_SALVAGER ))
				{
					Int cur = obj->testWeaponSetFlag( WEAPONSET_CRATEUPGRADE_TWO ) ? 2
							: obj->testWeaponSetFlag( WEAPONSET_CRATEUPGRADE_ONE ) ? 1 : 0;
					Int next = cur + m_giveSalvage;
					next = next < 0 ? 0 : (next > 2 ? 2 : next);
					if (next != cur)
					{
						setWeaponSalvageTier( obj, next );
						changed = TRUE;
					}
				}

				if (obj->isKindOf( KINDOF_ARMOR_SALVAGER ))
				{
					Int cur = obj->testArmorSetFlag( ARMORSET_CRATE_UPGRADE_TWO ) ? 2
							: obj->testArmorSetFlag( ARMORSET_CRATE_UPGRADE_ONE ) ? 1 : 0;
					Int next = cur + m_giveSalvage;
					next = next < 0 ? 0 : (next > 2 ? 2 : next);
					if (next != cur)
					{
						setArmorSalvageTier( obj, next );
						changed = TRUE;
					}
				}

				// play the salvage crate pickup sound on the unit when its tier changed.
				if (changed && TheAudio && TheAudio->getMiscAudio())
				{
					AudioEventRTS sound = TheAudio->getMiscAudio()->m_crateSalvage;
					sound.setObjectID( obj->getID() );
					TheAudio->addAudioEvent( &sound );
				}
			}
		}
	}

	// Notify the player that the command ran.
	if (TheInGameUI)
	{
		UnicodeString uName;
		uName.translate( m_name );
		UnicodeString msg;
		msg.format( L"Chat command executed: %s", uName.str() );
		TheInGameUI->message( msg );
	}
}

//-------------------------------------------------------------------------------------------------
ChatCommandStore::~ChatCommandStore()
{
	clear();
}

//-------------------------------------------------------------------------------------------------
void ChatCommandStore::clear()
{
	for (std::vector<ChatCommand*>::iterator it = m_commands.begin(); it != m_commands.end(); ++it)
		delete *it;
	m_commands.clear();
}

//-------------------------------------------------------------------------------------------------
void ChatCommandStore::reset()
{
	// Chat commands are static definitions loaded once from ChatCommands.ini; they must persist
	// across game resets (like ThingTemplates), so do not clear them here.
}

//-------------------------------------------------------------------------------------------------
const ChatCommand* ChatCommandStore::findChatCommand( const AsciiString& name ) const
{
	for (std::vector<ChatCommand*>::const_iterator it = m_commands.begin(); it != m_commands.end(); ++it)
	{
		if ((*it)->getName().compareNoCase(name) == 0)
			return *it;
	}
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void ChatCommandStore::parseChatCommandDefinition( INI* ini )
{
	// read the command name that follows the "ChatCommand" keyword
	AsciiString name;
	name.set( ini->getNextToken() );

	if (!TheChatCommandStore)
		return;

	// command names must be unique; a duplicate would shadow the earlier definition at dispatch.
	if (TheChatCommandStore->findChatCommand( name ) != nullptr)
	{
		DEBUG_CRASH(("[LINE: %d - FILE: '%s'] Duplicate ChatCommand '%s'", ini->getLineNum(), ini->getFilename().str(), name.str()));
		throw INI_INVALID_DATA;
	}

	ChatCommand* command = new ChatCommand;
	command->setName( name );

	// consume the block to its "End" token; no attributes are defined yet
	ini->initFromINI( command, command->getFieldParse() );

	TheChatCommandStore->m_commands.push_back( command );

	DEBUG_LOG((">>> ADDED CHAT COMMAND '%s'", command->getName().str()));
}

//-------------------------------------------------------------------------------------------------
/*static*/ void INI::parseChatCommandDefinition( INI* ini )
{
	ChatCommandStore::parseChatCommandDefinition( ini );
}
