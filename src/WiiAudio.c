/*
 * WiiAudio.c -- SDL3 audio-stream shim backed directly by the Wii's AI
 * (Audio Interface) DMA engine, double-buffered.
 *
 * Pomme's cmixer (extern/Pomme/src/SoundMixer/cmixer.cpp) drives audio in a
 * "pull" model: it opens one SDL_AudioStream with a callback, and SDL is
 * expected to invoke that callback whenever it needs more PCM data, which
 * cmixer answers by calling SDL_PutAudioStreamData(). We reproduce that
 * contract using AUDIO_RegisterDMACallback: each time the AI finishes
 * playing one buffer, we ask cmixer's callback to fill the *other* buffer,
 * then queue it for DMA next.
 *
 * PPC is big-endian and so is the AI hardware's expected sample format, and
 * SDL_AUDIO_S16 is native-endian -- so mixed samples need no byte-swapping
 * here, unlike on a little-endian target.
 *
 * IMPORTANT: AUDIO_RegisterDMACallback() fires from interrupt context. The
 * first version of this file called straight into cmixer's callback (heap
 * allocation + floating-point mixing) from inside that IRQ, which is unsafe
 * -- it crashed silently on real hardware (worked in Dolphin, which is more
 * forgiving about IRQ-context work). The IRQ handler here now does only the
 * DMA buffer swap and posts a semaphore; all the real mixing work happens
 * in a dedicated LWP thread that wakes up on that semaphore.
 */

#include <gccore.h>
#include <ogc/audio.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>
#include <malloc.h>
#include <string.h>

#include <SDL3/SDL.h>
#include "WiiDebug.h"

#define WII_AUDIO_FREQ		48000
#define WII_AUDIO_CHANNELS	2
#define BUFFER_FRAMES		512										/* frames per DMA buffer */
#define BUFFER_BYTES		(BUFFER_FRAMES * WII_AUDIO_CHANNELS * 2)	/* 16-bit samples */

typedef void (*SDL_AudioStreamCallback)(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);

struct SDL_AudioStream {
	SDL_AudioStreamCallback callback;
	void* userdata;
	int freq;
	int channels;
};

static struct SDL_AudioStream sStream;
static bool sStreamOpen = false;

static u8* sBuf[2];
static int sPlaying = 0;	/* which buffer AI is currently consuming */
static bool sRunning = false;

/* Where SDL_PutAudioStreamData() writes to -- always the buffer NOT
   currently queued for DMA playback. */
static u8* sFillCursor = NULL;
static int sFillRemaining = 0;

static sem_t sFillSem;
static lwp_t sFillThread;
static bool sFillThreadStarted = false;

static void FillSilence(u8* buf)
{
	memset(buf, 0, BUFFER_BYTES);
}

/* Does the actual mixing work -- only ever called from sFillThreadFunc,
   never from IRQ context. */
static void FillBuffer(int bufIndex)
{
	sFillCursor = sBuf[bufIndex];
	sFillRemaining = BUFFER_BYTES;

	FillSilence(sBuf[bufIndex]); /* in case the callback doesn't fill it all */

	if (sStream.callback)
		sStream.callback(sStream.userdata, &sStream, BUFFER_BYTES, BUFFER_BYTES);

	DCFlushRange(sBuf[bufIndex], BUFFER_BYTES);
}

static void* FillThreadFunc(void* arg)
{
	(void)arg;
	for (;;)
	{
		LWP_SemWait(sFillSem);
		/* Fill whichever buffer AI is NOT currently playing. */
		FillBuffer(sPlaying ^ 1);
	}
	return NULL;
}

/* Runs in IRQ context -- keep this minimal. Just swap to the
   already-filled buffer and wake the fill thread to prepare the next one. */
static void AiDmaCallback(void)
{
	sPlaying ^= 1;
	AUDIO_InitDMA((u32)sBuf[sPlaying], BUFFER_BYTES);

	LWP_SemPost(sFillSem);
}

bool SDL_InitSubSystem(uint32_t flags)
{
	(void)flags;
	return true;
}

void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }

SDL_AudioStream* SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec* spec,
	void (*callback)(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount),
	void* userdata)
{
	(void)device;

	WiiCheckpoint("SDL_OpenAudioDeviceStream: called");

	sStream.freq = spec && spec->freq ? spec->freq : WII_AUDIO_FREQ;
	sStream.channels = spec && spec->channels ? spec->channels : WII_AUDIO_CHANNELS;
	sStream.callback = callback;
	sStream.userdata = userdata;

	if (!sStreamOpen)
	{
		sBuf[0] = (u8*)memalign(32, BUFFER_BYTES);
		sBuf[1] = (u8*)memalign(32, BUFFER_BYTES);
		FillSilence(sBuf[0]);
		FillSilence(sBuf[1]);

		LWP_SemInit(&sFillSem, 0, 1);

		AUDIO_Init(NULL);
		AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
		AUDIO_RegisterDMACallback(AiDmaCallback);

		sStreamOpen = true;
		WiiCheckpoint("SDL_OpenAudioDeviceStream: AI subsystem initialized");
	}

	return &sStream;
}

bool SDL_GetAudioStreamFormat(SDL_AudioStream* stream, SDL_AudioSpec* srcSpec, SDL_AudioSpec* dstSpec)
{
	SDL_AudioSpec spec;
	spec.format = 0x8010; /* SDL_AUDIO_S16 (native-endian 16-bit) */
	spec.channels = stream ? stream->channels : WII_AUDIO_CHANNELS;
	spec.freq = stream ? stream->freq : WII_AUDIO_FREQ;
	if (srcSpec) *srcSpec = spec;
	if (dstSpec) *dstSpec = spec;
	return true;
}

SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream* stream)
{
	(void)stream;
	return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID device)
{
	(void)device;
	/* Keep the DMA running -- cmixer calls this right after probing the
	   hardware frequency (see GetHardwareFrequency() in cmixer.cpp), then
	   reopens a real stream immediately after. Actually tearing down AI
	   here would kill audio permanently, so this is intentionally a no-op
	   beyond that one probe path. */
}

size_t SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, size_t len)
{
	(void)stream;
	if ((int)len > sFillRemaining)
		len = sFillRemaining;
	if (sFillCursor && len > 0)
	{
		memcpy(sFillCursor, data, len);
		sFillCursor += len;
		sFillRemaining -= (int)len;
	}
	return len;
}

bool SDL_ResumeAudioDevice(SDL_AudioDeviceID device)
{
	(void)device;
	WiiCheckpoint("SDL_ResumeAudioDevice: called");
	if (!sRunning)
	{
		sRunning = true;
		sPlaying = 0;

		if (!sFillThreadStarted)
		{
			LWP_CreateThread(&sFillThread, FillThreadFunc, NULL, NULL, 32 * 1024, 80);
			sFillThreadStarted = true;
			WiiCheckpoint("SDL_ResumeAudioDevice: fill thread created");
		}

		FillBuffer(1); /* pre-fill buf[1] directly (not via IRQ) before we start on buf[0] */
		WiiCheckpoint("SDL_ResumeAudioDevice: first buffer filled, starting DMA");
		AUDIO_InitDMA((u32)sBuf[0], BUFFER_BYTES);
		AUDIO_StartDMA();
		WiiCheckpoint("SDL_ResumeAudioDevice: DMA started");
	}
	return true;
}
