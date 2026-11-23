/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Electronic Arts Inc.
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

// MilesMiniaudioPcm.h
// Plays PCM that is decoded elsewhere (movie audio) through the same miniaudio device as the game's
// sounds. The stream plays silence while nothing is queued, so it never ends by itself.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MilesMiniaudioPcmStream MilesMiniaudioPcmStream;

/// Opens a stream of interleaved 32-bit float frames. Returns null without an audio device.
MilesMiniaudioPcmStream *MilesMiniaudio_OpenPcmStream(unsigned sampleRate, unsigned channels);

/// Queues frames; what does not fit into about two seconds of buffer is dropped.
void MilesMiniaudio_WritePcm(MilesMiniaudioPcmStream *stream, const float *frames, unsigned frameCount);

/// Volume 0..1.
void MilesMiniaudio_SetPcmVolume(MilesMiniaudioPcmStream *stream, float volume);

void MilesMiniaudio_ClosePcmStream(MilesMiniaudioPcmStream *stream);

#ifdef __cplusplus
}
#endif
