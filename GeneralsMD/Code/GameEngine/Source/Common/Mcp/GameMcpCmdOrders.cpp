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

// GameMcpCmdOrders.cpp
// Order commands of the MCP bridge. Orders go to the logic as the player's own command messages
// would, so they are carried out on the next logic frame, recorded in the replay and checked like
// the player's clicks. They work for the units of any player.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#ifdef RTS_GAME_MCP

#include "GameMcpCommands.h"

#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"

namespace
{

GameMessage *appendOrder(GameMessage::Type type, const Player *player)
{
	GameMessage *msg = newInstance(GameMessage)(type);
	msg->friend_setPlayerIndex(player->getPlayerIndex());
	TheCommandList->appendMessage(msg);
	return msg;
}

/// Makes the objects the selection of their owner, which the order that follows applies to.
Player *selectObjects(const std::vector<Object *> &objects)
{
	Player *owner = objects[0]->getControllingPlayer();
	if (owner == nullptr) {
		gameMcpFail("object %u has no owner", (unsigned int)objects[0]->getID());
	}
	for (size_t i = 1; i < objects.size(); i++) {
		if (objects[i]->getControllingPlayer() != owner) {
			gameMcpFail("all objects of one order must belong to one player (%u belongs to %s, %u to %s)",
				(unsigned int)objects[0]->getID(), gameMcpPlayerName(owner).c_str(),
				(unsigned int)objects[i]->getID(), gameMcpPlayerName(objects[i]->getControllingPlayer()).c_str());
		}
	}
	GameMessage *msg = appendOrder(GameMessage::MSG_CREATE_SELECTED_GROUP_NO_SOUND, owner);
	msg->appendBooleanArgument(TRUE);
	for (size_t i = 0; i < objects.size(); i++) {
		msg->appendObjectIDArgument(objects[i]->getID());
	}
	return owner;
}

Player *selectOne(Object *object)
{
	std::vector<Object *> objects(1, object);
	return selectObjects(objects);
}

McpJson queued(const std::vector<Object *> &objects)
{
	McpJson ids = McpJson::makeArray();
	for (size_t i = 0; i < objects.size(); i++) {
		ids.push((unsigned int)objects[i]->getID());
	}
	return McpJson::makeObject().set("queued", true).set("ids", ids).set("frame", TheGameLogic->getFrame())
		.set("note", "carried out on the next logic frame (step or resume a paused game)");
}

McpJson cmdMove(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	const Coord3D pos = gameMcpArgPosition(args);
	Player *owner = selectObjects(objects);
	const bool attackMove = gameMcpArgBool(args, "attack_move", false);
	GameMessage *msg = appendOrder(attackMove ? GameMessage::MSG_DO_ATTACKMOVETO : GameMessage::MSG_DO_MOVETO, owner);
	msg->appendLocationArgument(pos);
	return queued(objects);
}

McpJson cmdAttack(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	if (args.has("target")) {
		Object *target = gameMcpRequireObject(gameMcpArgInt(args, "target"));
		Player *owner = selectObjects(objects);
		const bool force = gameMcpArgBool(args, "force", false);
		GameMessage *msg = appendOrder(force ? GameMessage::MSG_DO_FORCE_ATTACK_OBJECT : GameMessage::MSG_DO_ATTACK_OBJECT, owner);
		msg->appendObjectIDArgument(target->getID());
	} else {
		const Coord3D pos = gameMcpArgPosition(args);
		Player *owner = selectObjects(objects);
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_FORCE_ATTACK_GROUND, owner);
		msg->appendLocationArgument(pos);
	}
	return queued(objects);
}

McpJson cmdStop(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	appendOrder(GameMessage::MSG_DO_STOP, selectObjects(objects));
	return queued(objects);
}

McpJson cmdGuard(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	const Int mode = gameMcpArgInt(args, "mode", GUARDMODE_NORMAL);
	if (args.has("target")) {
		Object *target = gameMcpRequireObject(gameMcpArgInt(args, "target"));
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_GUARD_OBJECT, selectObjects(objects));
		msg->appendObjectIDArgument(target->getID());
		msg->appendIntegerArgument(mode);
	} else {
		const Coord3D pos = gameMcpArgPosition(args);
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_GUARD_POSITION, selectObjects(objects));
		msg->appendLocationArgument(pos);
		msg->appendIntegerArgument(mode);
	}
	return queued(objects);
}

McpJson cmdSell(const McpJson &args)
{
	std::vector<Object *> objects;
	gameMcpRequireObjects(args, objects);
	appendOrder(GameMessage::MSG_SELL, selectObjects(objects));
	return queued(objects);
}

McpJson cmdBuild(const McpJson &args)
{
	Object *dozer = gameMcpRequireObject(gameMcpArgInt(args, "dozer"));
	const ThingTemplate *tmpl = gameMcpRequireTemplate(gameMcpArgString(args, "template"));
	const Coord3D pos = gameMcpArgPosition(args);
	const Real angle = (Real)(gameMcpArgNumber(args, "angle", 0) * PI / 180.0);
	GameMessage *msg = appendOrder(GameMessage::MSG_DOZER_CONSTRUCT, selectOne(dozer));
	msg->appendIntegerArgument(tmpl->getTemplateID());
	msg->appendLocationArgument(pos);
	msg->appendRealArgument(angle);
	return queued(std::vector<Object *>(1, dozer));
}

McpJson cmdProduce(const McpJson &args)
{
	Object *factory = gameMcpRequireObject(gameMcpArgInt(args, "factory"));
	const ThingTemplate *tmpl = gameMcpRequireTemplate(gameMcpArgString(args, "template"));
	ProductionUpdateInterface *production = factory->getProductionUpdateInterface();
	if (production == nullptr) {
		gameMcpFail("object %u (%s) cannot produce anything", (unsigned int)factory->getID(), factory->getTemplate()->getName().str());
	}
	const Int count = gameMcpArgInt(args, "count", 1);
	Player *owner = selectOne(factory);
	for (Int i = 0; i < count; i++) {
		GameMessage *msg = appendOrder(GameMessage::MSG_QUEUE_UNIT_CREATE, owner);
		msg->appendIntegerArgument(tmpl->getTemplateID());
		msg->appendIntegerArgument(production->requestUniqueUnitID());
	}
	return queued(std::vector<Object *>(1, factory));
}

McpJson cmdUpgrade(const McpJson &args)
{
	Object *object = gameMcpRequireObject(gameMcpArgInt(args, "id"));
	const std::string name = gameMcpArgString(args, "upgrade");
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade(name.c_str());
	if (upgrade == nullptr) {
		gameMcpFail("unknown upgrade '%s'", name.c_str());
	}
	GameMessage *msg = appendOrder(GameMessage::MSG_QUEUE_UPGRADE, selectOne(object));
	msg->appendObjectIDArgument(object->getID());
	msg->appendIntegerArgument(upgrade->getUpgradeNameKey());
	return queued(std::vector<Object *>(1, object));
}

McpJson cmdSpecialPower(const McpJson &args)
{
	Object *source = gameMcpRequireObject(gameMcpArgInt(args, "id"));
	const SpecialPowerTemplate *power = nullptr;
	UnsignedInt options = (UnsignedInt)gameMcpArgInt(args, "options", 0);
	if (args.has("button")) {
		// A command button carries the power and the options the UI would send.
		const std::string buttonName = gameMcpArgString(args, "button");
		const CommandButton *button = TheControlBar ? TheControlBar->findCommandButton(buttonName.c_str()) : nullptr;
		if (button == nullptr || button->getSpecialPowerTemplate() == nullptr) {
			gameMcpFail("no command button '%s' with a special power", buttonName.c_str());
		}
		power = button->getSpecialPowerTemplate();
		if (!args.has("options")) {
			options = button->getOptions();
		}
	} else {
		const std::string name = gameMcpArgString(args, "power");
		power = TheSpecialPowerStore->findSpecialPowerTemplate(name.c_str());
		if (power == nullptr) {
			gameMcpFail("unknown special power '%s' (see objects.get section special_powers)", name.c_str());
		}
	}

	Player *owner = selectOne(source);
	if (args.has("target")) {
		Object *target = gameMcpRequireObject(gameMcpArgInt(args, "target"));
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_SPECIAL_POWER_AT_OBJECT, owner);
		msg->appendIntegerArgument(power->getID());
		msg->appendObjectIDArgument(target->getID());
		msg->appendIntegerArgument(options);
		msg->appendObjectIDArgument(source->getID());
	} else if (args.has("x")) {
		const Coord3D pos = gameMcpArgPosition(args);
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_SPECIAL_POWER_AT_LOCATION, owner);
		msg->appendIntegerArgument(power->getID());
		msg->appendLocationArgument(pos);
		msg->appendRealArgument((Real)(gameMcpArgNumber(args, "angle", 0) * PI / 180.0));
		msg->appendObjectIDArgument(INVALID_ID);
		msg->appendIntegerArgument(options);
		msg->appendObjectIDArgument(source->getID());
	} else {
		GameMessage *msg = appendOrder(GameMessage::MSG_DO_SPECIAL_POWER, owner);
		msg->appendIntegerArgument(power->getID());
		msg->appendIntegerArgument(options);
		msg->appendObjectIDArgument(source->getID());
	}
	return queued(std::vector<Object *>(1, source));
}

McpJson cmdPurchaseScience(const McpJson &args)
{
	Player *player = gameMcpPlayerArg(args, "player");
	const std::string name = gameMcpArgString(args, "science");
	const ScienceType science = TheScienceStore->getScienceFromInternalName(name.c_str());
	if (science == SCIENCE_INVALID) {
		gameMcpFail("unknown science '%s'", name.c_str());
	}
	GameMessage *msg = appendOrder(GameMessage::MSG_PURCHASE_SCIENCE, player);
	msg->appendIntegerArgument(science);
	return McpJson::makeObject().set("queued", true).set("player", gameMcpPlayerName(player)).set("frame", TheGameLogic->getFrame());
}

} // namespace

void gameMcpRegisterOrderCommands()
{
	gameMcpRegisterCommand("orders.move", cmdMove, "{ids, x, y, attack_move?} Moves units to a position.");
	gameMcpRegisterCommand("orders.attack", cmdAttack, "{ids, target, force?} | {ids, x, y} Attacks an object, or fires at the ground.");
	gameMcpRegisterCommand("orders.stop", cmdStop, "{ids} Stops units.");
	gameMcpRegisterCommand("orders.guard", cmdGuard, "{ids, target | x, y, mode=0} Guards an object or position (mode 0 normal, 1 without pursuit, 2 flying units only).");
	gameMcpRegisterCommand("orders.sell", cmdSell, "{ids} Sells structures.");
	gameMcpRegisterCommand("orders.build", cmdBuild, "{dozer, template, x, y, angle=0} A dozer or worker builds a structure.");
	gameMcpRegisterCommand("orders.produce", cmdProduce, "{factory, template, count=1} Queues units in a production building.");
	gameMcpRegisterCommand("orders.upgrade", cmdUpgrade, "{id, upgrade} Queues an upgrade in an object.");
	gameMcpRegisterCommand("orders.special_power", cmdSpecialPower,
		"{id, power | button, target | x, y, angle?, options?} Uses a special power of an object, at an object, a position or without a target.");
	gameMcpRegisterCommand("orders.purchase_science", cmdPurchaseScience, "{player?, science} Buys a general's power with science points.");
}

#endif // RTS_GAME_MCP
