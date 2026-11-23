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

// MilesMiniaudio.cpp
// The Miles Sound System functions MilesAudioManager uses, implemented on miniaudio, so the manager
// and all its game logic (priorities, limits, fading, loops, music, speech) stay as they are.
//
// How it maps:
// - 2D samples, 3D samples and streams are "voices". A handle is the voice index + 2 stored in the
//   handle's pointer type (1 is the 3D listener), so it fits the 32-bit handle casts in the manager.
// - Samples play a private copy of the WAV image the manager hands over (PCM or IMA ADPCM).
//   Streams (music, speech) read their whole file through the file callbacks when opened.
// - End-of-sample callbacks run on a service thread holding the global lock, like the Miles timer
//   thread. Every function here takes that lock (AIL_lock / AIL_unlock expose it).
// - The single 3D provider is "Miles Fast 2D Positional Audio": pan from the camera's right
//   vector, the Miles distance rolloff (full volume up to min_dist, then min_dist / distance),
//   occlusion as a low-pass filter.
// - The delay filter exists so the manager finds it; it does nothing (the manager already
//   delays the start of sounds).

#include <windows.h>
#include "mss.h"
#include "miniaudio.h"
#include "MilesMiniaudioPcm.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace
{

//-------------------------------------------------------------------------------------------------
// Lock
//-------------------------------------------------------------------------------------------------

CRITICAL_SECTION g_lock;
bool g_lockReady = false;

void Init_Lock()
{
	if (!g_lockReady)
	{
		InitializeCriticalSection(&g_lock);
		g_lockReady = true;
	}
}

struct Lock
{
	Lock() { Init_Lock(); EnterCriticalSection(&g_lock); }
	~Lock() { LeaveCriticalSection(&g_lock); }
};

//-------------------------------------------------------------------------------------------------
// Trace: with the environment variable MILES_MINIAUDIO_TRACE set to a file name, what plays, ends
// and fails is written there.
//-------------------------------------------------------------------------------------------------

FILE *g_trace = nullptr;
bool g_traceChecked = false;

void Trace(const char *format, ...)
{
	if (!g_traceChecked)
	{
		g_traceChecked = true;
		char path[MAX_PATH];
		if (GetEnvironmentVariableA("MILES_MINIAUDIO_TRACE", path, sizeof(path)) > 0)
			g_trace = fopen(path, "w");
	}
	if (g_trace == nullptr)
		return;

	va_list args;
	va_start(args, format);
	fprintf(g_trace, "%10lu ", GetTickCount());
	vfprintf(g_trace, format, args);
	fputc('\n', g_trace);
	fflush(g_trace);
	va_end(args);
}

//-------------------------------------------------------------------------------------------------
// State
//-------------------------------------------------------------------------------------------------

enum VoiceKind
{
	VOICE_2D,
	VOICE_3D,
	VOICE_STREAM,
};

enum { USER_DATA_COUNT = 8 };
enum { LISTENER_HANDLE = 1, FIRST_VOICE_HANDLE = 2 };

struct Voice
{
	VoiceKind kind;

	// The audio data this voice plays: a WAV image for samples, the whole file for streams.
	unsigned char *data;
	size_t dataSize;

	ma_decoder decoder;
	bool hasDecoder;
	ma_sound sound;
	bool hasSound;
	ma_lpf_node lowPass;
	bool hasLowPass;

	bool playing;		// started and not paused or finished
	bool paused;

	float volume;		// 0..1
	float pan;			// 0 left, 0.5 center, 1 right
	int baseRate;		// sample rate of the data
	int rate;			// requested playback rate

	void *userData[USER_DATA_COUNT];

	AIL_sample_callback sampleDone;
	AIL_3dsample_callback sample3DDone;
	AIL_stream_callback streamDone;

	// 3D
	float position[3];
	float minDistance;
	float occlusion;

	// Streams: Miles loop count (0 = forever) and loops still to play.
	int loopCount;
	int loopsLeft;
	ma_uint64 lastCursor;
};

ma_engine g_engine;
bool g_engineReady = false;

std::vector<Voice *> g_voices;		// index = handle - FIRST_VOICE_HANDLE; null when free

float g_listenerPosition[3] = { 0.0f, 0.0f, 0.0f };
float g_listenerForward[3] = { 0.0f, 1.0f, 0.0f };

AIL_file_open_callback g_fileOpen = nullptr;
AIL_file_close_callback g_fileClose = nullptr;
AIL_file_seek_callback g_fileSeek = nullptr;
AIL_file_read_callback g_fileRead = nullptr;

HANDLE g_serviceThread = nullptr;
volatile LONG g_serviceStop = 0;

DIG_DRIVER g_digitalDriver;		// only its address is used, as the digital driver handle

char g_providerName[] = "Miles Fast 2D Positional Audio";
char g_delayFilterName[] = "Mono Delay Filter";
const HPROVIDER PROVIDER_3D = (HPROVIDER)(uintptr_t)1;
const HPROVIDER PROVIDER_DELAY = (HPROVIDER)(uintptr_t)2;

//-------------------------------------------------------------------------------------------------
// Handles
//-------------------------------------------------------------------------------------------------

template<class HANDLE_TYPE>
HANDLE_TYPE To_Handle(size_t index)
{
	return (HANDLE_TYPE)(uintptr_t)(index + FIRST_VOICE_HANDLE);
}

template<class HANDLE_TYPE>
Voice *From_Handle(HANDLE_TYPE handle)
{
	uintptr_t value = (uintptr_t)handle;
	if (value < FIRST_VOICE_HANDLE)
		return nullptr;
	size_t index = value - FIRST_VOICE_HANDLE;
	return index < g_voices.size() ? g_voices[index] : nullptr;
}

size_t Allocate_Voice(VoiceKind kind)
{
	Voice *voice = (Voice *)calloc(1, sizeof(Voice));
	voice->kind = kind;
	voice->volume = 1.0f;
	voice->pan = 0.5f;
	voice->baseRate = 22050;
	voice->rate = 22050;
	voice->minDistance = 1.0f;
	voice->loopCount = 1;
	voice->loopsLeft = 1;

	for (size_t i = 0; i < g_voices.size(); ++i)
	{
		if (g_voices[i] == nullptr)
		{
			g_voices[i] = voice;
			return i;
		}
	}
	g_voices.push_back(voice);
	return g_voices.size() - 1;
}

//-------------------------------------------------------------------------------------------------
// Voices
//-------------------------------------------------------------------------------------------------

void Destroy_Sound(Voice *voice)
{
	voice->playing = false;
	voice->paused = false;
	if (voice->hasSound)
	{
		ma_sound_uninit(&voice->sound);
		voice->hasSound = false;
	}
	if (voice->hasDecoder)
	{
		ma_decoder_uninit(&voice->decoder);
		voice->hasDecoder = false;
	}
}

void Free_Data(Voice *voice)
{
	Destroy_Sound(voice);
	free(voice->data);
	voice->data = nullptr;
	voice->dataSize = 0;
}

void Free_Voice(Voice *voice)
{
	Free_Data(voice);
	if (voice->hasLowPass)
	{
		ma_lpf_node_uninit(&voice->lowPass, nullptr);
		voice->hasLowPass = false;
	}
	free(voice);
}

void Release_Voice(uintptr_t handle)
{
	Voice *voice = From_Handle(handle);
	if (voice)
	{
		Free_Voice(voice);
		g_voices[handle - FIRST_VOICE_HANDLE] = nullptr;
	}
}

// Pan and distance rolloff of a 3D voice, relative to the listener (the camera). Game world: z up.
void Apply_3D(Voice *voice, float *gainOut, float *panOut)
{
	float dx = voice->position[0] - g_listenerPosition[0];
	float dy = voice->position[1] - g_listenerPosition[1];
	float dz = voice->position[2] - g_listenerPosition[2];
	float distance = sqrtf(dx * dx + dy * dy + dz * dz);

	// right = forward x up
	float rx = g_listenerForward[1];
	float ry = -g_listenerForward[0];
	float rightLength = sqrtf(rx * rx + ry * ry);
	if (rightLength < 1e-4f)
	{
		rx = 1.0f;
		ry = 0.0f;
		rightLength = 1.0f;
	}

	float pan = 0.0f;
	if (distance > 1e-3f)
		pan = (dx * rx + dy * ry) / (rightLength * distance);

	float gain = 1.0f;
	if (distance > voice->minDistance && distance > 0.0f)
		gain = voice->minDistance / distance;

	*gainOut = gain;
	*panOut = pan;
}

// Pushes volume, pan and pitch into the miniaudio sound.
void Apply_Settings(Voice *voice)
{
	if (!voice->hasSound)
		return;

	if (voice->kind == VOICE_3D)
	{
		float gain, pan;
		Apply_3D(voice, &gain, &pan);
		ma_sound_set_volume(&voice->sound, voice->volume * gain);
		ma_sound_set_pan(&voice->sound, pan);
	}
	else
	{
		ma_sound_set_volume(&voice->sound, voice->volume);
		ma_sound_set_pan(&voice->sound, voice->pan * 2.0f - 1.0f);
	}

	float pitch = voice->baseRate > 0 ? (float)voice->rate / (float)voice->baseRate : 1.0f;
	ma_sound_set_pitch(&voice->sound, pitch > 0.0f ? pitch : 1.0f);
}

// Occlusion 0..1 becomes the cutoff of a low-pass filter between the sound and the output.
double Occlusion_Cutoff(float occlusion)
{
	float open = 1.0f - occlusion;
	if (open < 0.0f)
		open = 0.0f;
	double cutoff = 20000.0 * open * open;
	return cutoff < 400.0 ? 400.0 : cutoff;
}

void Route_Sound(Voice *voice)
{
	if (!voice->hasSound)
		return;

	if (voice->occlusion > 0.0f)
	{
		ma_uint32 channels = ma_engine_get_channels(&g_engine);
		ma_uint32 sampleRate = ma_engine_get_sample_rate(&g_engine);
		if (!voice->hasLowPass)
		{
			ma_lpf_node_config config = ma_lpf_node_config_init(channels, sampleRate, Occlusion_Cutoff(voice->occlusion), 2);
			if (ma_lpf_node_init(ma_engine_get_node_graph(&g_engine), &config, nullptr, &voice->lowPass) != MA_SUCCESS)
				return;
			ma_node_attach_output_bus(&voice->lowPass, 0, ma_engine_get_endpoint(&g_engine), 0);
			voice->hasLowPass = true;
		}
		else
		{
			ma_lpf_config config = ma_lpf_config_init(ma_format_f32, channels, sampleRate, Occlusion_Cutoff(voice->occlusion), 2);
			ma_lpf_node_reinit(&config, &voice->lowPass);
		}
		ma_node_attach_output_bus(&voice->sound, 0, &voice->lowPass, 0);
	}
	else
	{
		ma_node_attach_output_bus(&voice->sound, 0, ma_engine_get_endpoint(&g_engine), 0);
	}
}

// (Re)creates the sound from the voice's data, from the beginning. Rebuilding instead of seeking
// avoids miniaudio's asynchronous seek leaving a stale "at end" behind.
bool Create_Sound(Voice *voice)
{
	Destroy_Sound(voice);
	if (!g_engineReady || voice->data == nullptr)
		return false;

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_memory(voice->data, voice->dataSize, &config, &voice->decoder) != MA_SUCCESS)
		return false;
	voice->hasDecoder = true;
	voice->baseRate = (int)voice->decoder.outputSampleRate;

	if (ma_sound_init_from_data_source(&g_engine, &voice->decoder, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice->sound) != MA_SUCCESS)
	{
		ma_decoder_uninit(&voice->decoder);
		voice->hasDecoder = false;
		return false;
	}
	voice->hasSound = true;

	Route_Sound(voice);
	Apply_Settings(voice);
	return true;
}

void Start_Voice(Voice *voice)
{
	if (!Create_Sound(voice))
	{
		Trace("start failed: kind %d, %u bytes", (int)voice->kind, (unsigned)voice->dataSize);
		return;
	}
	Trace("start: kind %d, rate %d/%d, volume %.2f", (int)voice->kind, voice->rate, voice->baseRate, voice->volume);

	if (voice->kind == VOICE_STREAM)
	{
		voice->loopsLeft = voice->loopCount;
		voice->lastCursor = 0;
		ma_sound_set_looping(&voice->sound, voice->loopCount == 0 || voice->loopsLeft > 1);
	}

	ma_sound_start(&voice->sound);
	voice->playing = true;
	voice->paused = false;
}

void Pause_Voice(Voice *voice)
{
	if (voice->hasSound && voice->playing)
	{
		ma_sound_stop(&voice->sound);
		voice->playing = false;
		voice->paused = true;
	}
}

void Resume_Voice(Voice *voice)
{
	if (voice->hasSound && voice->paused)
	{
		ma_sound_start(&voice->sound);
		voice->playing = true;
		voice->paused = false;
	}
}

//-------------------------------------------------------------------------------------------------
// WAV images
//-------------------------------------------------------------------------------------------------

unsigned Read_U32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
unsigned Read_U16(const unsigned char *p) { return p[0] | (p[1] << 8); }

// Size of a RIFF image from its header, or 0.
size_t Riff_Size(const void *image)
{
	const unsigned char *p = (const unsigned char *)image;
	if (p == nullptr || memcmp(p, "RIFF", 4) != 0)
		return 0;
	return (size_t)Read_U32(p + 4) + 8;
}

bool Copy_Image(Voice *voice, const void *image)
{
	Free_Data(voice);
	size_t size = Riff_Size(image);
	if (size == 0)
		return false;
	voice->data = (unsigned char *)malloc(size);
	if (voice->data == nullptr)
		return false;
	memcpy(voice->data, image, size);
	voice->dataSize = size;
	return true;
}

// Reads the whole file through the game's file callbacks.
bool Load_File(Voice *voice, const char *fileName)
{
	if (g_fileOpen == nullptr || g_fileRead == nullptr || g_fileClose == nullptr)
		return false;

	void *file = nullptr;
	if (!g_fileOpen(fileName, &file) || file == nullptr)
		return false;

	size_t capacity = 1 << 20;
	size_t size = 0;
	unsigned char *data = (unsigned char *)malloc(capacity);
	for (;;)
	{
		if (size == capacity)
		{
			capacity *= 2;
			unsigned char *bigger = (unsigned char *)realloc(data, capacity);
			if (bigger == nullptr)
			{
				free(data);
				g_fileClose(file);
				return false;
			}
			data = bigger;
		}
		unsigned long got = g_fileRead(file, data + size, (unsigned long)(capacity - size));
		if (got == 0 || got == (unsigned long)-1)
			break;
		size += got;
	}
	g_fileClose(file);

	if (size == 0)
	{
		free(data);
		return false;
	}

	Free_Data(voice);
	voice->data = data;
	voice->dataSize = size;
	return true;
}

// The data's own rate replaces any playback rate set before, as in Miles.
void Take_Rate_From_Image(Voice *voice)
{
	const unsigned char *p = voice->data;
	size_t offset = 12;
	while (offset + 8 <= voice->dataSize)
	{
		unsigned chunkSize = Read_U32(p + offset + 4);
		if (memcmp(p + offset, "fmt ", 4) == 0 && chunkSize >= 16)
		{
			voice->baseRate = (int)Read_U32(p + offset + 12);
			voice->rate = voice->baseRate;
			return;
		}
		offset += 8 + chunkSize + (chunkSize & 1);
	}
}

//-------------------------------------------------------------------------------------------------
// Service thread: end of samples and streams, stream loops
//-------------------------------------------------------------------------------------------------

void Service_Voices()
{
	Lock lock;
	// Callbacks may allocate or release voices; walk by index.
	for (size_t i = 0; i < g_voices.size(); ++i)
	{
		Voice *voice = g_voices[i];
		if (voice == nullptr || !voice->playing || !voice->hasSound)
			continue;

		if (voice->kind == VOICE_STREAM && ma_sound_is_looping(&voice->sound))
		{
			ma_uint64 cursor = 0;
			ma_sound_get_cursor_in_pcm_frames(&voice->sound, &cursor);
			if (cursor < voice->lastCursor && voice->loopCount != 0)
			{
				// wrapped around: one more loop played
				--voice->loopsLeft;
				if (voice->loopsLeft <= 1)
					ma_sound_set_looping(&voice->sound, MA_FALSE);
			}
			voice->lastCursor = cursor;
			continue;
		}

		if (!ma_sound_at_end(&voice->sound))
			continue;

		voice->playing = false;
		if (voice->kind == VOICE_STREAM && voice->loopsLeft > 0)
			voice->loopsLeft = 0;
		Trace("end: kind %d, handle %u", (int)voice->kind, (unsigned)(i + FIRST_VOICE_HANDLE));

		switch (voice->kind)
		{
		case VOICE_2D:
			if (voice->sampleDone)
				voice->sampleDone(To_Handle<HSAMPLE>(i));
			break;
		case VOICE_3D:
			if (voice->sample3DDone)
				voice->sample3DDone(To_Handle<H3DPOBJECT>(i));
			break;
		case VOICE_STREAM:
			if (voice->streamDone)
				voice->streamDone(To_Handle<HSTREAM>(i));
			break;
		}
	}
}

DWORD WINAPI Service_Thread(LPVOID)
{
	while (!g_serviceStop)
	{
		Service_Voices();
		Sleep(5);
	}
	return 0;
}

void Shutdown_Engine()
{
	if (g_serviceThread)
	{
		InterlockedExchange(&g_serviceStop, 1);
		WaitForSingleObject(g_serviceThread, INFINITE);
		CloseHandle(g_serviceThread);
		g_serviceThread = nullptr;
	}

	Lock lock;
	for (size_t i = 0; i < g_voices.size(); ++i)
	{
		if (g_voices[i])
		{
			Free_Voice(g_voices[i]);
			g_voices[i] = nullptr;
		}
	}
	g_voices.clear();

	if (g_engineReady)
	{
		ma_engine_uninit(&g_engine);
		g_engineReady = false;
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
// System
//-------------------------------------------------------------------------------------------------

extern "C" {

int __stdcall AIL_startup(void)
{
	Init_Lock();
	return 1;
}

void __stdcall AIL_shutdown(void)
{
	Shutdown_Engine();
}

int __stdcall AIL_quick_startup(int use_digital, int use_MIDI, unsigned int output_rate, int output_bits, int output_channels)
{
	Lock lock;
	if (g_engineReady)
		return 1;
	if (!use_digital)
		return 0;

	// Stereo, like the Miles 2D positional provider the game used; Windows maps it to the speakers.
	ma_engine_config config = ma_engine_config_init();
	config.channels = 2;
	if (ma_engine_init(&config, &g_engine) != MA_SUCCESS)
	{
		Trace("no audio device");
		return 0;
	}
	g_engineReady = true;
	Trace("device: %u channels, %u Hz", ma_engine_get_channels(&g_engine), ma_engine_get_sample_rate(&g_engine));

	InterlockedExchange(&g_serviceStop, 0);
	g_serviceThread = CreateThread(nullptr, 0, Service_Thread, nullptr, 0, nullptr);
	return 1;
}

void __stdcall AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls)
{
	if (pdig)
		*pdig = g_engineReady ? &g_digitalDriver : nullptr;
	if (pmdi)
		*pmdi = nullptr;
	if (pdls)
		*pdls = nullptr;
}

char *__stdcall AIL_set_redist_directory(const char *dir)
{
	return (char *)dir;
}

char *__stdcall AIL_last_error(void)
{
	static char none[] = "";
	return none;
}

void __stdcall AIL_lock(void)
{
	Init_Lock();
	EnterCriticalSection(&g_lock);
}

void __stdcall AIL_unlock(void)
{
	LeaveCriticalSection(&g_lock);
}

unsigned long __stdcall AIL_get_timer_highest_delay(void)
{
	return 0;
}

void __stdcall AIL_get_DirectSound_info(HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB)
{
	if (lplpDS)
		*lplpDS = nullptr;
	if (lplpDSB)
		*lplpDSB = nullptr;
}

void __stdcall AIL_set_file_callbacks(AIL_file_open_callback opencb, AIL_file_close_callback closecb,
	AIL_file_seek_callback seekcb, AIL_file_read_callback readcb)
{
	Lock lock;
	g_fileOpen = opencb;
	g_fileClose = closecb;
	g_fileSeek = seekcb;
	g_fileRead = readcb;
}

//-------------------------------------------------------------------------------------------------
// Providers, listener, filters
//-------------------------------------------------------------------------------------------------

int __stdcall AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name)
{
	if (*next != HPROENUM_FIRST)
		return 0;
	*dest = PROVIDER_3D;
	*name = g_providerName;
	*next = 1;
	return 1;
}

M3DRESULT __stdcall AIL_open_3D_provider(HPROVIDER lib)
{
	return lib == PROVIDER_3D ? M3D_NOERR : 1;
}

void __stdcall AIL_close_3D_provider(HPROVIDER lib)
{
}

void __stdcall AIL_set_3D_speaker_type(HPROVIDER lib, int speaker_type)
{
}

H3DPOBJECT __stdcall AIL_open_3D_listener(HPROVIDER lib)
{
	return (H3DPOBJECT)(uintptr_t)LISTENER_HANDLE;
}

void __stdcall AIL_close_3D_listener(H3DPOBJECT listener)
{
}

int __stdcall AIL_enumerate_filters(HPROENUM *next, HPROVIDER *dest, char **name)
{
	if (*next != HPROENUM_FIRST)
		return 0;
	*dest = PROVIDER_DELAY;
	*name = g_delayFilterName;
	*next = 1;
	return 1;
}

HPROVIDER __stdcall AIL_set_sample_processor(HSAMPLE sample, SAMPLESTAGE pipeline_stage, HPROVIDER provider)
{
	return nullptr;
}

void __stdcall AIL_set_filter_sample_preference(HSAMPLE sample, const char *name, const void *val)
{
}

//-------------------------------------------------------------------------------------------------
// 3D objects (listener and 3D samples)
//-------------------------------------------------------------------------------------------------

void __stdcall AIL_set_3D_position(H3DPOBJECT obj, float X, float Y, float Z)
{
	Lock lock;
	if ((uintptr_t)obj == LISTENER_HANDLE)
	{
		g_listenerPosition[0] = X;
		g_listenerPosition[1] = Y;
		g_listenerPosition[2] = Z;
		for (size_t i = 0; i < g_voices.size(); ++i)
		{
			if (g_voices[i] && g_voices[i]->kind == VOICE_3D)
				Apply_Settings(g_voices[i]);
		}
		return;
	}

	if (Voice *voice = From_Handle(obj))
	{
		voice->position[0] = X;
		voice->position[1] = Y;
		voice->position[2] = Z;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_set_3D_orientation(H3DPOBJECT obj, float X_face, float Y_face, float Z_face, float X_up, float Y_up, float Z_up)
{
	Lock lock;
	if ((uintptr_t)obj == LISTENER_HANDLE)
	{
		g_listenerForward[0] = X_face;
		g_listenerForward[1] = Y_face;
		g_listenerForward[2] = Z_face;
	}
}

H3DSAMPLE __stdcall AIL_allocate_3D_sample_handle(HPROVIDER lib)
{
	Lock lock;
	return To_Handle<H3DSAMPLE>(Allocate_Voice(VOICE_3D));
}

void __stdcall AIL_release_3D_sample_handle(H3DSAMPLE sample)
{
	Lock lock;
	Release_Voice((uintptr_t)sample);
}

int __stdcall AIL_set_3D_sample_file(H3DSAMPLE sample, const void *file_image)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (voice == nullptr || !Copy_Image(voice, file_image))
		return 0;
	Take_Rate_From_Image(voice);
	voice->occlusion = 0.0f;
	return 1;
}

void __stdcall AIL_start_3D_sample(H3DSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Start_Voice(voice);
}

void __stdcall AIL_stop_3D_sample(H3DSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Pause_Voice(voice);
}

void __stdcall AIL_resume_3D_sample(H3DSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Resume_Voice(voice);
}

AIL_3dsample_callback __stdcall AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL_3dsample_callback EOS)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (voice == nullptr)
		return nullptr;
	AIL_3dsample_callback previous = voice->sample3DDone;
	voice->sample3DDone = EOS;
	return previous;
}

void __stdcall AIL_set_3D_sample_volume(H3DSAMPLE sample, float volume)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->volume = volume;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_set_3D_sample_distances(H3DSAMPLE sample, float max_dist, float min_dist)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->minDistance = min_dist > 0.0f ? min_dist : 1.0f;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_set_3D_sample_occlusion(H3DSAMPLE sample, float occlusion)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->occlusion = occlusion < 0.0f ? 0.0f : (occlusion > 1.0f ? 1.0f : occlusion);
		Route_Sound(voice);
	}
}

int __stdcall AIL_3D_sample_playback_rate(H3DSAMPLE sample)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	return voice ? voice->rate : 0;
}

void __stdcall AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, int playback_rate)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->rate = playback_rate;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_set_3D_user_data(H3DPOBJECT obj, unsigned int index, void *value)
{
	Lock lock;
	Voice *voice = From_Handle(obj);
	if (voice && index < USER_DATA_COUNT)
		voice->userData[index] = value;
}

void *__stdcall AIL_3D_user_data(H3DSAMPLE sample, unsigned int index)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	return (voice && index < USER_DATA_COUNT) ? voice->userData[index] : nullptr;
}

//-------------------------------------------------------------------------------------------------
// 2D samples
//-------------------------------------------------------------------------------------------------

HSAMPLE __stdcall AIL_allocate_sample_handle(HDIGDRIVER dig)
{
	Lock lock;
	return To_Handle<HSAMPLE>(Allocate_Voice(VOICE_2D));
}

void __stdcall AIL_release_sample_handle(HSAMPLE sample)
{
	Lock lock;
	Release_Voice((uintptr_t)sample);
}

void __stdcall AIL_init_sample(HSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		Free_Data(voice);
		voice->volume = 1.0f;
		voice->pan = 0.5f;
		voice->baseRate = 22050;
		voice->rate = 22050;
		voice->sampleDone = nullptr;
	}
}

int __stdcall AIL_set_sample_file(HSAMPLE sample, const void *file_image, int block)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (voice == nullptr || !Copy_Image(voice, file_image))
		return 0;
	Take_Rate_From_Image(voice);
	return 1;
}

void __stdcall AIL_start_sample(HSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Start_Voice(voice);
}

void __stdcall AIL_stop_sample(HSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Pause_Voice(voice);
}

void __stdcall AIL_resume_sample(HSAMPLE sample)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
		Resume_Voice(voice);
}

AIL_sample_callback __stdcall AIL_register_EOS_callback(HSAMPLE sample, AIL_sample_callback EOS)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (voice == nullptr)
		return nullptr;
	AIL_sample_callback previous = voice->sampleDone;
	voice->sampleDone = EOS;
	return previous;
}

void __stdcall AIL_set_sample_volume_pan(HSAMPLE sample, float volume, float pan)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->volume = volume;
		voice->pan = pan;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_sample_volume_pan(HSAMPLE sample, float *volume, float *pan)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (volume)
		*volume = voice ? voice->volume : 0.0f;
	if (pan)
		*pan = voice ? voice->pan : 0.5f;
}

int __stdcall AIL_sample_playback_rate(HSAMPLE sample)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	return voice ? voice->rate : 0;
}

void __stdcall AIL_set_sample_playback_rate(HSAMPLE sample, int playback_rate)
{
	Lock lock;
	if (Voice *voice = From_Handle(sample))
	{
		voice->rate = playback_rate;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_set_sample_user_data(HSAMPLE sample, unsigned int index, void *value)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	if (voice && index < USER_DATA_COUNT)
		voice->userData[index] = value;
}

void *__stdcall AIL_sample_user_data(HSAMPLE sample, unsigned int index)
{
	Lock lock;
	Voice *voice = From_Handle(sample);
	return (voice && index < USER_DATA_COUNT) ? voice->userData[index] : nullptr;
}

//-------------------------------------------------------------------------------------------------
// Streams
//-------------------------------------------------------------------------------------------------

HSTREAM __stdcall AIL_open_stream(HDIGDRIVER dig, const char *filename, int stream_mem)
{
	Lock lock;
	size_t index = Allocate_Voice(VOICE_STREAM);
	Voice *voice = g_voices[index];
	if (!Load_File(voice, filename))
	{
		Trace("stream not found: %s", filename);
		Free_Voice(voice);
		g_voices[index] = nullptr;
		return nullptr;
	}

	// Learn the data's rate and check that it decodes.
	if (g_engineReady && !Create_Sound(voice))
	{
		Trace("stream does not decode: %s", filename);
		Free_Voice(voice);
		g_voices[index] = nullptr;
		return nullptr;
	}
	voice->rate = voice->baseRate;
	Destroy_Sound(voice);
	Trace("stream: %s (%u bytes)", filename, (unsigned)voice->dataSize);
	return To_Handle<HSTREAM>(index);
}

void __stdcall AIL_close_stream(HSTREAM stream)
{
	Lock lock;
	Release_Voice((uintptr_t)stream);
}

void __stdcall AIL_start_stream(HSTREAM stream)
{
	Lock lock;
	if (Voice *voice = From_Handle(stream))
		Start_Voice(voice);
}

void __stdcall AIL_pause_stream(HSTREAM stream, int onoff)
{
	Lock lock;
	if (Voice *voice = From_Handle(stream))
	{
		if (onoff)
			Pause_Voice(voice);
		else
			Resume_Voice(voice);
	}
}

AIL_stream_callback __stdcall AIL_register_stream_callback(HSTREAM stream, AIL_stream_callback callback)
{
	Lock lock;
	Voice *voice = From_Handle(stream);
	if (voice == nullptr)
		return nullptr;
	AIL_stream_callback previous = voice->streamDone;
	voice->streamDone = callback;
	return previous;
}

void __stdcall AIL_set_stream_loop_count(HSTREAM stream, int count)
{
	Lock lock;
	if (Voice *voice = From_Handle(stream))
	{
		voice->loopCount = count < 0 ? 1 : count;
		voice->loopsLeft = voice->loopCount;
		if (voice->hasSound)
			ma_sound_set_looping(&voice->sound, voice->loopCount == 0 || voice->loopsLeft > 1);
	}
}

int __stdcall AIL_stream_loop_count(HSTREAM stream)
{
	Lock lock;
	Voice *voice = From_Handle(stream);
	return voice ? voice->loopsLeft : 0;
}

void __stdcall AIL_set_stream_volume_pan(HSTREAM stream, float volume, float pan)
{
	Lock lock;
	if (Voice *voice = From_Handle(stream))
	{
		voice->volume = volume;
		voice->pan = pan;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_stream_volume_pan(HSTREAM stream, float *volume, float *pan)
{
	Lock lock;
	Voice *voice = From_Handle(stream);
	if (volume)
		*volume = voice ? voice->volume : 0.0f;
	if (pan)
		*pan = voice ? voice->pan : 0.5f;
}

void __stdcall AIL_stream_ms_position(HSTREAM stream, S32 *total_milliseconds, S32 *current_milliseconds)
{
	Lock lock;
	Voice *voice = From_Handle(stream);
	S32 total = 0;
	S32 current = 0;
	if (voice && voice->data)
	{
		ma_decoder decoder;
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		if (ma_decoder_init_memory(voice->data, voice->dataSize, &config, &decoder) == MA_SUCCESS)
		{
			ma_uint64 frames = 0;
			ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
			if (decoder.outputSampleRate > 0)
				total = (S32)(frames * 1000 / decoder.outputSampleRate);
			ma_decoder_uninit(&decoder);
		}
		if (voice->hasSound && voice->baseRate > 0)
		{
			ma_uint64 cursor = 0;
			ma_sound_get_cursor_in_pcm_frames(&voice->sound, &cursor);
			current = (S32)(cursor * 1000 / voice->baseRate);
		}
	}
	if (total_milliseconds)
		*total_milliseconds = total;
	if (current_milliseconds)
		*current_milliseconds = current;
}

//-------------------------------------------------------------------------------------------------
// Quick API (mission briefings)
//-------------------------------------------------------------------------------------------------

HAUDIO __stdcall AIL_quick_load_and_play(const char *filename, unsigned int loop_count, int wait_request)
{
	Lock lock;
	HSTREAM stream = AIL_open_stream(&g_digitalDriver, filename, 0);
	if (stream == nullptr)
		return nullptr;
	AIL_set_stream_loop_count(stream, (int)loop_count);
	AIL_start_stream(stream);
	return (HAUDIO)stream;
}

void __stdcall AIL_quick_set_volume(HAUDIO audio, float volume, float extravol)
{
	Lock lock;
	if (Voice *voice = From_Handle(audio))
	{
		voice->volume = volume;
		Apply_Settings(voice);
	}
}

void __stdcall AIL_quick_unload(HAUDIO audio)
{
	Lock lock;
	Release_Voice((uintptr_t)audio);
}

//-------------------------------------------------------------------------------------------------
// WAV data
//-------------------------------------------------------------------------------------------------

int __stdcall AIL_WAV_info(const void *data, AILSOUNDINFO *info)
{
	memset(info, 0, sizeof(*info));
	size_t size = Riff_Size(data);
	const unsigned char *p = (const unsigned char *)data;
	if (size < 12 || memcmp(p + 8, "WAVE", 4) != 0)
		return 0;

	info->initial_ptr = data;
	size_t offset = 12;
	while (offset + 8 <= size)
	{
		const unsigned char *chunk = p + offset;
		unsigned chunkSize = Read_U32(chunk + 4);
		if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16)
		{
			info->format = (int)Read_U16(chunk + 8);
			info->channels = (int)Read_U16(chunk + 10);
			info->rate = Read_U32(chunk + 12);
			info->block_size = Read_U16(chunk + 20);
			info->bits = (int)Read_U16(chunk + 22);
		}
		else if (memcmp(chunk, "data", 4) == 0)
		{
			info->data_ptr = chunk + 8;
			info->data_len = chunkSize;
		}
		offset += 8 + chunkSize + (chunkSize & 1);
	}

	if (info->format == WAVE_FORMAT_PCM && info->bits > 0 && info->channels > 0)
		info->samples = info->data_len / ((info->bits / 8) * info->channels);
	else if (info->block_size > 0 && info->channels > 0)
		info->samples = (info->data_len / info->block_size) * ((info->block_size - 4 * info->channels) * 2 / info->channels + 1);

	return info->data_ptr != nullptr;
}

// Decodes an IMA ADPCM WAV image into a 16-bit PCM WAV image (free it with AIL_mem_free_lock).
int __stdcall AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, unsigned long *outsize)
{
	*outdata = nullptr;
	*outsize = 0;

	size_t size = Riff_Size(info->initial_ptr);
	if (size == 0)
		return 0;

	ma_decoder decoder;
	ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
	if (ma_decoder_init_memory(info->initial_ptr, size, &config, &decoder) != MA_SUCCESS)
		return 0;

	ma_uint32 channels = decoder.outputChannels;
	ma_uint32 rate = decoder.outputSampleRate;
	ma_uint64 frames = 0;
	ma_decoder_get_length_in_pcm_frames(&decoder, &frames);

	size_t dataBytes = (size_t)frames * channels * 2;
	unsigned char *image = (unsigned char *)malloc(44 + dataBytes);
	if (image == nullptr)
	{
		ma_decoder_uninit(&decoder);
		return 0;
	}

	ma_uint64 framesRead = 0;
	ma_decoder_read_pcm_frames(&decoder, image + 44, frames, &framesRead);
	ma_decoder_uninit(&decoder);
	dataBytes = (size_t)framesRead * channels * 2;

	unsigned char *h = image;
	memcpy(h, "RIFF", 4);
	*(unsigned *)(h + 4) = (unsigned)(36 + dataBytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	*(unsigned *)(h + 16) = 16;
	*(unsigned short *)(h + 20) = WAVE_FORMAT_PCM;
	*(unsigned short *)(h + 22) = (unsigned short)channels;
	*(unsigned *)(h + 24) = rate;
	*(unsigned *)(h + 28) = rate * channels * 2;
	*(unsigned short *)(h + 32) = (unsigned short)(channels * 2);
	*(unsigned short *)(h + 34) = 16;
	memcpy(h + 36, "data", 4);
	*(unsigned *)(h + 40) = (unsigned)dataBytes;

	*outdata = image;
	*outsize = (unsigned long)(44 + dataBytes);
	return 1;
}

void __stdcall AIL_mem_free_lock(void *ptr)
{
	free(ptr);
}

} // extern "C"

//-------------------------------------------------------------------------------------------------
// PCM streams (movie audio)
//-------------------------------------------------------------------------------------------------

struct MilesMiniaudioPcmStream
{
	ma_data_source_base base;	// must be first: the stream is the data source
	ma_pcm_rb ring;
	ma_uint32 channels;
	ma_uint32 sampleRate;
	ma_sound sound;
	bool hasSound;
};

namespace
{

// Reads what is queued and fills the rest with silence, so the sound never reaches its end.
ma_result Pcm_Read(ma_data_source *dataSource, void *framesOut, ma_uint64 frameCount, ma_uint64 *framesRead)
{
	MilesMiniaudioPcmStream *stream = (MilesMiniaudioPcmStream *)dataSource;
	float *out = (float *)framesOut;
	ma_uint64 done = 0;

	while (done < frameCount)
	{
		ma_uint32 chunk = (ma_uint32)(frameCount - done);
		void *buffer = nullptr;
		if (ma_pcm_rb_acquire_read(&stream->ring, &chunk, &buffer) != MA_SUCCESS || chunk == 0)
			break;
		memcpy(out + done * stream->channels, buffer, (size_t)chunk * stream->channels * sizeof(float));
		ma_pcm_rb_commit_read(&stream->ring, chunk);
		done += chunk;
	}

	if (done < frameCount)
		memset(out + done * stream->channels, 0, (size_t)(frameCount - done) * stream->channels * sizeof(float));

	if (framesRead)
		*framesRead = frameCount;
	return MA_SUCCESS;
}

ma_result Pcm_Get_Data_Format(ma_data_source *dataSource, ma_format *format, ma_uint32 *channels, ma_uint32 *sampleRate,
	ma_channel *channelMap, size_t channelMapCap)
{
	MilesMiniaudioPcmStream *stream = (MilesMiniaudioPcmStream *)dataSource;
	*format = ma_format_f32;
	*channels = stream->channels;
	*sampleRate = stream->sampleRate;
	if (channelMap)
		ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCap, stream->channels);
	return MA_SUCCESS;
}

const ma_data_source_vtable g_pcmVTable =
{
	Pcm_Read,
	nullptr,				// seek
	Pcm_Get_Data_Format,
	nullptr,				// cursor
	nullptr,				// length
	nullptr,				// looping
	0
};

} // namespace

extern "C" {

MilesMiniaudioPcmStream *MilesMiniaudio_OpenPcmStream(unsigned sampleRate, unsigned channels)
{
	Lock lock;
	if (!g_engineReady || sampleRate == 0 || channels == 0)
		return nullptr;

	MilesMiniaudioPcmStream *stream = (MilesMiniaudioPcmStream *)calloc(1, sizeof(MilesMiniaudioPcmStream));
	stream->channels = channels;
	stream->sampleRate = sampleRate;

	ma_data_source_config config = ma_data_source_config_init();
	config.vtable = &g_pcmVTable;
	if (ma_data_source_init(&config, &stream->base) != MA_SUCCESS)
	{
		free(stream);
		return nullptr;
	}

	if (ma_pcm_rb_init(ma_format_f32, channels, sampleRate * 2, nullptr, nullptr, &stream->ring) != MA_SUCCESS)
	{
		ma_data_source_uninit(&stream->base);
		free(stream);
		return nullptr;
	}

	if (ma_sound_init_from_data_source(&g_engine, stream, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &stream->sound) != MA_SUCCESS)
	{
		ma_pcm_rb_uninit(&stream->ring);
		ma_data_source_uninit(&stream->base);
		free(stream);
		return nullptr;
	}
	stream->hasSound = true;
	ma_sound_start(&stream->sound);
	Trace("pcm stream: %u Hz, %u channels", sampleRate, channels);
	return stream;
}

void MilesMiniaudio_WritePcm(MilesMiniaudioPcmStream *stream, const float *frames, unsigned frameCount)
{
	if (stream == nullptr)
		return;

	// Single writer (the movie decoder), single reader (the audio thread): no lock needed.
	unsigned done = 0;
	while (done < frameCount)
	{
		ma_uint32 chunk = frameCount - done;
		void *buffer = nullptr;
		if (ma_pcm_rb_acquire_write(&stream->ring, &chunk, &buffer) != MA_SUCCESS || chunk == 0)
			break;
		memcpy(buffer, frames + (size_t)done * stream->channels, (size_t)chunk * stream->channels * sizeof(float));
		ma_pcm_rb_commit_write(&stream->ring, chunk);
		done += chunk;
	}
}

void MilesMiniaudio_SetPcmVolume(MilesMiniaudioPcmStream *stream, float volume)
{
	if (stream && stream->hasSound)
		ma_sound_set_volume(&stream->sound, volume);
}

void MilesMiniaudio_ClosePcmStream(MilesMiniaudioPcmStream *stream)
{
	if (stream == nullptr)
		return;

	Lock lock;
	if (stream->hasSound)
		ma_sound_uninit(&stream->sound);
	ma_pcm_rb_uninit(&stream->ring);
	ma_data_source_uninit(&stream->base);
	free(stream);
}

} // extern "C"
