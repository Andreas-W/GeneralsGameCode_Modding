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

// What the FX Editor plays in the preview game: one particle system, FXList or sound at a time,
// at the preview point or on a unit, once or again and again, standing still or moving in a circle.

#pragma once

#include "Common/AsciiString.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"

#include <vector>

class Object;

enum PreviewKind
{
	PREVIEW_PARTICLE_SYSTEM,
	PREVIEW_FXLIST,
	PREVIEW_SOUND,
	PREVIEW_KIND_COUNT
};

const char *previewKindName(PreviewKind kind);	///< "particle", "fxlist", "sound"
Bool previewKindFromName(const char *name, PreviewKind &kind);

struct PreviewSettings
{
	Real height;				///< above the ground at the preview point
	Bool repeat;				///< play again every repeatSeconds
	Real repeatSeconds;
	Bool move;					///< move the emitter around the preview point
	Real moveRadius;
	Real moveSpeed;			///< world units per second
	AsciiString attachTo;	///< thing template to play on (doFXObj / attached particle system); empty: on the point
	Real angle;					///< direction the emitter faces, degrees
	Bool followCamera;		///< each start of the effect takes the camera's target as the preview point

	PreviewSettings();
};

struct PreviewStats
{
	UnsignedInt totalParticles;
	UnsignedInt fieldParticles;
	UnsignedInt systems;
	UnsignedInt maxParticles;
	UnsignedInt previewParticles;		///< in systems of the previewed template (and its slaves)
	UnsignedInt previewSystems;
	UnsignedInt playCount;					///< how often it was started since the last play()
	UnsignedInt framesSinceStart;		///< logic frames since it was last started
	Int onScreenParticles;
};

class PreviewScene
{
public:
	PreviewScene();

	/// Called every editor update. Prepares the scene once the preview game runs, repeats and moves.
	void update();

	Bool isReady() const { return m_ready; }

	/// Plays something by name; replaces what played before and removes the scorch marks.
	/// False with a reason if unknown.
	Bool play(PreviewKind kind, const AsciiString &name, AsciiString &error);
	/// Starts the current one again (after an edit), killing its particles and scorch marks first.
	void restart();
	/// Stops repeating and removes the particle systems it made.
	void stop();
	/// Removes every particle system and scorch mark in the game.
	void clearAllParticles();
	/// The game ends (another map is loaded): stops, and plays the same again once the next game is ready.
	void leaveGame();

	Bool isPlaying() const { return m_playing; }
	PreviewKind getKind() const { return m_kind; }
	const AsciiString &getName() const { return m_name; }

	PreviewSettings &settings() { return m_settings; }
	const Coord3D &getCenter() const { return m_center; }
	void setCenter(const Coord3D &pos);
	/// Where the emitter is now (the center, or on its circle when moving).
	Coord3D getEmitterPosition() const;

	void getStats(PreviewStats &stats) const;
	/// The sound played last, while it plays.
	Bool isSoundPlaying() const;

	// Time control of the game logic.
	void setPaused(Bool paused);
	Bool isPaused() const;
	void step(Int frames);
	void setSpeed(Real speed);	///< 1 = normal; slower or faster runs the logic at another rate, the view stays smooth
	Real getSpeed() const { return m_speed; }

	void lookAtCenter();

private:
	void prepareScene();
	void fire();
	void killPreviewSystems();
	void clearScorches();
	void markFirstSystem();
	Object *ensureAttachObject();
	void removeAttachObject();
	void followCamera();

	Bool m_ready;
	Coord3D m_center;
	Coord3D m_cameraTarget;			///< where the camera looked when the preview point was last set
	PreviewSettings m_settings;
	Real m_speed;

	Bool m_playing;
	PreviewKind m_kind;
	AsciiString m_name;
	UnsignedInt m_startFrame;
	UnsignedInt m_lastFireFrame;
	UnsignedInt m_playCount;
	Real m_moveAngle;					///< radians on the circle
	UnsignedInt m_lastMoveFrame;
	std::vector<UnsignedInt> m_systemIDs;	///< particle systems this scene created
	UnsignedInt m_firstSystemID;		///< systems from this id on were made by the current preview (directly or by an FXList)
	ObjectID m_attachObjectID;
	AsciiString m_attachTemplate;
	UnsignedInt m_soundHandle;	///< AudioHandle of the sound played last
	PreviewKind m_resumeKind;		///< what played when the last game ended, played again in the next
	AsciiString m_resumeName;
};
