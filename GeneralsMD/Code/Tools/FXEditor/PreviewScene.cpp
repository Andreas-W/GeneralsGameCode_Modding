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

#include "Precompiled/PreRTS.h"

#include "PreviewScene.h"

#include "Common/AudioEventRTS.h"
#include "Common/FramePacer.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"

namespace
{

const char *const KIND_NAMES[PREVIEW_KIND_COUNT] = { "particle", "fxlist", "sound" };

/// The game fades in from black at its start; the preview is ready after that.
const UnsignedInt READY_FRAME = 45;

/// Logic frames for a time in seconds.
UnsignedInt framesFor(Real seconds)
{
	const Real frames = seconds * LOGICFRAMES_PER_SECOND;
	return frames < 1.0f ? 1 : (UnsignedInt)frames;
}

} // namespace

//-------------------------------------------------------------------------------------------------
const char *previewKindName(PreviewKind kind)
{
	return (kind >= 0 && kind < PREVIEW_KIND_COUNT) ? KIND_NAMES[kind] : "unknown";
}

//-------------------------------------------------------------------------------------------------
Bool previewKindFromName(const char *name, PreviewKind &kind)
{
	for (Int i = 0; i < PREVIEW_KIND_COUNT; ++i)
	{
		if (stricmp(name, KIND_NAMES[i]) == 0)
		{
			kind = (PreviewKind)i;
			return TRUE;
		}
	}
	if (stricmp(name, "particlesystem") == 0 || stricmp(name, "particle_system") == 0)
	{
		kind = PREVIEW_PARTICLE_SYSTEM;
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
PreviewSettings::PreviewSettings() :
	height(0.0f),
	repeat(TRUE),
	repeatSeconds(3.0f),
	move(FALSE),
	moveRadius(60.0f),
	moveSpeed(60.0f),
	angle(0.0f),
	followCamera(TRUE)
{
}

//-------------------------------------------------------------------------------------------------
PreviewScene::PreviewScene() :
	m_ready(FALSE),
	m_speed(1.0f),
	m_playing(FALSE),
	m_kind(PREVIEW_PARTICLE_SYSTEM),
	m_startFrame(0),
	m_lastFireFrame(0),
	m_playCount(0),
	m_moveAngle(0.0f),
	m_lastMoveFrame(0),
	m_firstSystemID(0xFFFFFFFF),
	m_attachObjectID(INVALID_ID),
	m_soundHandle(0),
	m_resumeKind(PREVIEW_PARTICLE_SYSTEM)
{
	m_center.zero();
	m_cameraTarget.zero();
}

//-------------------------------------------------------------------------------------------------
/** The preview game has started: a revealed scene around the middle of the map. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::prepareScene()
{
	Player *local = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (local == nullptr)
		return;

	// The middle of the map (the start position may be any of the map's).
	Region3D extent;
	TheTerrainLogic->getExtent(&extent);
	Coord3D center;
	center.x = (extent.lo.x + extent.hi.x) * 0.5f;
	center.y = (extent.lo.y + extent.hi.y) * 0.5f;
	center.z = TheTerrainLogic->getGroundHeight(center.x, center.y);
	m_center = center;
	TheTacticalView->lookAt(&m_center);
	m_cameraTarget = TheTacticalView->getPosition();

	// Nothing hides the effects: no shroud, no starting units (of a skirmish), no control bar.
	ThePartitionManager->revealMapForPlayerPermanently(local->getPlayerIndex());
	if (TheGameLogic->isInSkirmishGame())
	{
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		{
			if (obj->getControllingPlayer() == local)
				TheGameLogic->destroyObject(obj);
		}
	}
	HideControlBar(TRUE);

	m_ready = TRUE;

	if (!m_resumeName.isEmpty())
	{
		const AsciiString name = m_resumeName;
		m_resumeName.clear();
		AsciiString error;
		play(m_resumeKind, name, error);
	}
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::leaveGame()
{
	if (m_playing)
	{
		m_resumeKind = m_kind;
		m_resumeName = m_name;
	}
	if (m_ready)
		stop();
	m_systemIDs.clear();
	m_attachObjectID = INVALID_ID;
	m_attachTemplate.clear();
	m_ready = FALSE;
	m_playing = FALSE;
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::update()
{
	if (!m_ready)
	{
		if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && TheGameLogic->getFrame() >= READY_FRAME && TheTacticalView)
			prepareScene();
		return;
	}
	if (!TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
	{
		leaveGame();
		return;
	}
	if (!m_playing)
		return;

	const UnsignedInt now = TheGameLogic->getFrame();

	// Moving emitters follow a circle around the center, in logic time.
	if (m_settings.move && now != m_lastMoveFrame)
	{
		const UnsignedInt frames = now > m_lastMoveFrame ? now - m_lastMoveFrame : 0;
		m_lastMoveFrame = now;
		if (m_settings.moveRadius > 1.0f)
			m_moveAngle += (m_settings.moveSpeed / LOGICFRAMES_PER_SECOND) * frames / m_settings.moveRadius;

		Coord3D pos = getEmitterPosition();
		if (m_kind == PREVIEW_PARTICLE_SYSTEM)
		{
			for (size_t i = 0; i < m_systemIDs.size(); ++i)
			{
				ParticleSystem *sys = TheParticleSystemManager->findParticleSystem((ParticleSystemID)m_systemIDs[i]);
				if (sys)
					sys->setPosition(&pos);
			}
		}
		if (Object *obj = TheGameLogic->findObjectByID(m_attachObjectID))
		{
			pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
			obj->setPosition(&pos);
			obj->setOrientation(m_moveAngle + PI / 2);
		}
	}

	if (m_settings.repeat && now >= m_lastFireFrame + framesFor(m_settings.repeatSeconds))
		fire();
}

//-------------------------------------------------------------------------------------------------
Coord3D PreviewScene::getEmitterPosition() const
{
	Coord3D pos = m_center;
	if (m_settings.move)
	{
		pos.x += m_settings.moveRadius * Cos(m_moveAngle);
		pos.y += m_settings.moveRadius * Sin(m_moveAngle);
	}
	pos.z = (TheTerrainLogic ? TheTerrainLogic->getGroundHeight(pos.x, pos.y) : m_center.z) + m_settings.height;
	return pos;
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::setCenter(const Coord3D &pos)
{
	m_center = pos;
	if (TheTerrainLogic)
		m_center.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	// Only camera moves from now on take the point along.
	if (TheTacticalView)
		m_cameraTarget = TheTacticalView->getPosition();
}

//-------------------------------------------------------------------------------------------------
/** Each start of the effect takes the camera's target as the preview point, if the camera moved
	since the point was last set. What already runs stays where it started. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::followCamera()
{
	if (TheTacticalView == nullptr || !m_settings.followCamera)
		return;
	const Coord3D &target = TheTacticalView->getPosition();
	const Real dx = target.x - m_cameraTarget.x;
	const Real dy = target.y - m_cameraTarget.y;
	if (dx * dx + dy * dy < 0.25f)
		return;
	setCenter(target);
	if (Object *obj = TheGameLogic->findObjectByID(m_attachObjectID))
	{
		Coord3D pos = getEmitterPosition();
		pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
		obj->setPosition(&pos);
	}
}

//-------------------------------------------------------------------------------------------------
Bool PreviewScene::play(PreviewKind kind, const AsciiString &name, AsciiString &error)
{
	if (!m_ready)
	{
		error = "the preview game is not running yet";
		return FALSE;
	}
	switch (kind)
	{
		case PREVIEW_PARTICLE_SYSTEM:
			if (TheParticleSystemManager->findTemplate(name) == nullptr)
			{
				error.format("unknown particle system '%s'", name.str());
				return FALSE;
			}
			break;
		case PREVIEW_FXLIST:
			if (TheFXListStore->findFXList(name.str()) == nullptr)
			{
				error.format("unknown FXList '%s'", name.str());
				return FALSE;
			}
			break;
		case PREVIEW_SOUND:
			if (TheAudio->findAudioEventInfo(name) == nullptr)
			{
				error.format("unknown audio event '%s'", name.str());
				return FALSE;
			}
			break;
		default:
			error = "unknown kind";
			return FALSE;
	}

	stop();
	clearScorches();
	m_kind = kind;
	m_name = name;
	m_playing = TRUE;
	m_playCount = 0;
	m_startFrame = TheGameLogic->getFrame();
	m_lastMoveFrame = m_startFrame;
	markFirstSystem();
	fire();
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::restart()
{
	if (!m_playing || !m_ready)
		return;
	killPreviewSystems();
	clearScorches();
	m_playCount = 0;
	m_startFrame = TheGameLogic->getFrame();
	markFirstSystem();
	fire();
}

//-------------------------------------------------------------------------------------------------
/** Scorch marks stay on the terrain for good; a new or restarted preview starts on clean ground.
	Repeats of one preview keep theirs, as in the game. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::clearScorches()
{
	if (TheTerrainRenderObject)
		TheTerrainRenderObject->clearAllScorches();
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::stop()
{
	killPreviewSystems();
	if (m_soundHandle != 0 && TheAudio)
	{
		TheAudio->removeAudioEvent(m_soundHandle);
		m_soundHandle = 0;
	}
	removeAttachObject();
	m_playing = FALSE;
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::clearAllParticles()
{
	clearScorches();
	m_systemIDs.clear();
	if (TheParticleSystemManager)
	{
		ParticleSystemManager::ParticleSystemList &list = TheParticleSystemManager->getAllParticleSystems();
		while (!list.empty())
			deleteInstance(list.front());
	}
}

//-------------------------------------------------------------------------------------------------
/** Removes the particle systems made since the current preview started, with their particles. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::killPreviewSystems()
{
	m_systemIDs.clear();
	if (TheParticleSystemManager == nullptr)
		return;
	std::vector<ParticleSystem *> victims;
	ParticleSystemManager::ParticleSystemList &list = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = list.begin(); it != list.end(); ++it)
	{
		if ((UnsignedInt)(*it)->getSystemID() >= m_firstSystemID)
			victims.push_back(*it);
	}
	for (size_t i = 0; i < victims.size(); ++i)
		deleteInstance(victims[i]);
}

//-------------------------------------------------------------------------------------------------
/** System ids only grow: the systems made from now on belong to the preview. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::markFirstSystem()
{
	UnsignedInt maxID = 0;
	ParticleSystemManager::ParticleSystemList &list = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = list.begin(); it != list.end(); ++it)
	{
		if ((UnsignedInt)(*it)->getSystemID() > maxID)
			maxID = (UnsignedInt)(*it)->getSystemID();
	}
	m_firstSystemID = maxID + 1;
}

//-------------------------------------------------------------------------------------------------
Object *PreviewScene::ensureAttachObject()
{
	if (m_settings.attachTo.isEmpty())
	{
		removeAttachObject();
		return nullptr;
	}
	Object *obj = TheGameLogic->findObjectByID(m_attachObjectID);
	if (obj && m_attachTemplate == m_settings.attachTo)
		return obj;
	removeAttachObject();

	const ThingTemplate *tmpl = TheThingFactory->findTemplate(m_settings.attachTo);
	Player *local = ThePlayerList->getLocalPlayer();
	if (tmpl == nullptr || local == nullptr || local->getDefaultTeam() == nullptr)
		return nullptr;
	obj = TheThingFactory->newObject(tmpl, local->getDefaultTeam());
	if (obj == nullptr)
		return nullptr;
	Coord3D pos = getEmitterPosition();
	pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);
	obj->setPosition(&pos);
	obj->setOrientation(m_settings.angle * PI / 180.0f);
	m_attachObjectID = obj->getID();
	m_attachTemplate = m_settings.attachTo;
	return obj;
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::removeAttachObject()
{
	if (Object *obj = TheGameLogic ? TheGameLogic->findObjectByID(m_attachObjectID) : nullptr)
		TheGameLogic->destroyObject(obj);
	m_attachObjectID = INVALID_ID;
	m_attachTemplate.clear();
}

//-------------------------------------------------------------------------------------------------
/** Starts the previewed thing once. */
//-------------------------------------------------------------------------------------------------
void PreviewScene::fire()
{
	m_lastFireFrame = TheGameLogic->getFrame();
	m_playCount++;
	followCamera();

	const Coord3D pos = getEmitterPosition();
	Matrix3D mtx(1);
	mtx.Rotate_Z(m_settings.angle * PI / 180.0f);
	mtx.Set_Translation(Vector3(pos.x, pos.y, pos.z));
	Object *attach = ensureAttachObject();

	switch (m_kind)
	{
		case PREVIEW_PARTICLE_SYSTEM:
		{
			const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(m_name);
			if (tmpl == nullptr)
				break;
			// A system that lives forever is started once; repeating would only pile up copies.
			for (size_t i = 0; i < m_systemIDs.size(); ++i)
			{
				ParticleSystem *old = TheParticleSystemManager->findParticleSystem((ParticleSystemID)m_systemIDs[i]);
				if (old && old->isSystemForever())
					return;
			}
			ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(tmpl);
			if (sys == nullptr)
				break;
			if (attach)
			{
				sys->attachToObject(attach);
			}
			else
			{
				sys->setLocalTransform(&mtx);
				sys->setPosition(&pos);
			}
			m_systemIDs.push_back((UnsignedInt)sys->getSystemID());
			break;
		}
		case PREVIEW_FXLIST:
		{
			const FXList *fx = TheFXListStore->findFXList(m_name.str());
			if (attach)
				FXList::doFXObj(fx, attach);
			else
				FXList::doFXPos(fx, &pos, &mtx);
			break;
		}
		case PREVIEW_SOUND:
		{
			AudioEventRTS sound(m_name);
			if (attach)
				sound.setObjectID(attach->getID());
			else
				sound.setPosition(&pos);
			m_soundHandle = TheAudio->addAudioEvent(&sound);
			break;
		}
		default:
			break;
	}
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::getStats(PreviewStats &stats) const
{
	memset(&stats, 0, sizeof(stats));
	if (TheParticleSystemManager == nullptr)
		return;
	stats.totalParticles = TheParticleSystemManager->getParticleCount();
	stats.fieldParticles = TheParticleSystemManager->getFieldParticleCount();
	stats.systems = TheParticleSystemManager->getParticleSystemCount();
	stats.maxParticles = TheGlobalData->m_maxParticleCount;
	stats.onScreenParticles = TheParticleSystemManager->getOnScreenParticleCount();
	stats.playCount = m_playCount;
	if (TheGameLogic && m_playing)
		stats.framesSinceStart = TheGameLogic->getFrame() - m_lastFireFrame;

	ParticleSystemManager::ParticleSystemList &list = TheParticleSystemManager->getAllParticleSystems();
	for (ParticleSystemManager::ParticleSystemListIt it = list.begin(); it != list.end(); ++it)
	{
		if (m_playing && (UnsignedInt)(*it)->getSystemID() >= m_firstSystemID)
		{
			stats.previewSystems++;
			stats.previewParticles += (*it)->getParticleCount();
		}
	}
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::setPaused(Bool paused)
{
	if (TheGameLogic)
		TheGameLogic->setGamePaused(paused, FALSE, FALSE);
}

//-------------------------------------------------------------------------------------------------
Bool PreviewScene::isPaused() const
{
	return TheGameLogic && TheGameLogic->isGamePaused();
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::step(Int frames)
{
	if (TheGameLogic == nullptr || frames < 1)
		return;
	const UnsignedInt target = TheGameLogic->getFrame() + frames;
	TheGameLogic->setGamePaused(FALSE, FALSE, FALSE);
	TheGameLogic->setGamePausedInFrame(target, FALSE);
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::setSpeed(Real speed)
{
	if (speed < 0.05f)
		speed = 0.05f;
	m_speed = speed;
	// The logic runs at its own rate; the view keeps drawing at full rate and interpolates particles.
	if (fabs(speed - 1.0f) < 0.001f)
	{
		TheFramePacer->enableLogicTimeScale(FALSE);
	}
	else
	{
		Int fps = (Int)(LOGICFRAMES_PER_SECOND * speed + 0.5f);
		if (fps < 1)
			fps = 1;
		TheFramePacer->setLogicTimeScaleFps(fps);
		TheFramePacer->enableLogicTimeScale(TRUE);
	}
}

//-------------------------------------------------------------------------------------------------
void PreviewScene::lookAtCenter()
{
	if (TheTacticalView)
	{
		TheTacticalView->lookAt(&m_center);
		m_cameraTarget = TheTacticalView->getPosition();
	}
}

//-------------------------------------------------------------------------------------------------
Bool PreviewScene::isSoundPlaying() const
{
	return m_soundHandle != 0 && TheAudio && TheAudio->isCurrentlyPlaying(m_soundHandle);
}
