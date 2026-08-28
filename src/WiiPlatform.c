/*
 * WiiPlatform.c -- real implementations for the SDL3 compat shim (SDL3/SDL.h)
 * used by the Wii port of Mighty Mike. Backed directly by libogc.
 *
 * Status: timing/threading/mutex/logging are real and tested in isolation.
 * Window/GL/gamepad/audio bodies are stubs to be filled in during the
 * GX + WPAD/PAD + AI integration pass (next milestone).
 */

#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <fat.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Mount the SD card (or USB, or virtual DVD FAT, depending on how the game
   was launched) before main()/Boot.cpp touches std::filesystem for the
   Data/ folder. fatInitDefault() also chdir()s into the app's own
   directory when launched from the Homebrew Channel, which is exactly the
   "Data" (relative-path) case Boot.cpp's FindGameData() falls back to. */
/* Standalone checkpoint logger: independent of GetLogFile()/SDL_Log below,
   so it works even before those are wired up, and can bracket a crash that
   happens before main() runs at all. Opens+closes every call so a partial
   log survives even if the very next line hangs or faults. */
void WiiCheckpoint(const char* msg)
{
	FILE* f = fopen("mightymike_checkpoints.txt", "a");
	if (!f)
		return;
	fprintf(f, "%s\n", msg);
	fclose(f);
}

__attribute__((constructor))
static void WiiFilesystemInit(void)
{
	bool ok = fatInitDefault();
	WiiCheckpoint(ok ? "fatInitDefault: OK" : "fatInitDefault: FAILED");

	/* Pomme's FindFolder(kPreferencesFolderType) (extern/Pomme/src/Files/Files.cpp)
	   falls into its generic non-Windows/non-Apple branch on Wii, which reads
	   HOME (or XDG_CONFIG_HOME) and fails with fnfErr if neither is set --
	   there's no such thing as a home directory on Wii, so give it one
	   relative to the app's own folder (cwd, set by fatInitDefault above). */
	setenv("HOME", ".", 1);
	WiiCheckpoint("HOME env var set");
}

#include <SDL3/SDL.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

/* ----------------------------------------------------------------------
 * Init / misc
 * ------------------------------------------------------------------- */

static char s_lastError[256] = "";
static bool s_logFileTruncated = false;

/* printf() on real Wii hardware goes nowhere without a console or USB
   Gecko attached -- mirror all logging (including DoAssert's failure
   messages, see Heart/Misc.c) to a file on the SD card so a crash that
   exits straight back to the loader still leaves a trail.

   Open+write+close on every single call (like WiiCheckpoint in this same
   file) rather than keeping one FILE* open across the run: a previous
   version kept the handle open and called fflush() per line, but a crash
   at the wrong moment still lost everything sitting in libfat's internal
   write-back cache -- fclose() is what actually forces that cache out,
   and a run that produced zero log output on a real hardware crash is
   what exposed this. */
static FILE* OpenLogFileForAppend(void)
{
	if (!s_logFileTruncated)
	{
		/* First write of this run: truncate so old runs' output doesn't
		   linger and confuse the next read. */
		s_logFileTruncated = true;
		return fopen("mightymike_log.txt", "w");
	}
	return fopen("mightymike_log.txt", "a");
}

static void LogToFile(const char* fmt, va_list ap)
{
	FILE* f = OpenLogFileForAppend();
	if (!f)
		return;
	va_list apCopy;
	va_copy(apCopy, ap);
	vfprintf(f, fmt, apCopy);
	va_end(apCopy);
	fputc('\n', f);
	fclose(f);
}

bool SDL_Init(uint32_t flags)
{
	(void)flags;
	return true;
}

void SDL_Quit(void) {}

void SDL_SetAppMetadata(const char* name, const char* version, const char* identifier)
{
	(void)name; (void)version; (void)identifier;
}

const char* SDL_GetError(void) { return s_lastError; }
void SDL_ClearError(void) { s_lastError[0] = '\0'; }

void SDL_Log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	LogToFile(fmt, ap);
	va_end(ap);
}

void SDL_LogError(int category, const char* fmt, ...)
{
	(void)category;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_lastError, sizeof(s_lastError), fmt, ap);
	va_end(ap);

	FILE* f = OpenLogFileForAppend();
	if (f)
	{
		fprintf(f, "[ERROR] %s\n", s_lastError);
		fclose(f);
	}
}

void SDL_SetLogPriorities(int priority) { (void)priority; }

bool SDL_ShowSimpleMessageBox(uint32_t flags, const char* title, const char* message, SDL_Window* window)
{
	(void)flags; (void)window;
	FILE* f = OpenLogFileForAppend();
	if (f)
	{
		fprintf(f, "[MESSAGEBOX] %s: %s\n", title, message);
		fclose(f);
	}
	return true;
}

/* Wii timer ticks are 1/TB_TIMER_CLOCK seconds; convert to milliseconds. */
uint64_t SDL_GetTicks(void)
{
	return ticks_to_millisecs(gettime());
}

void SDL_Delay(uint32_t ms)
{
	/* usleep() isn't provided by devkitPPC's newlib; use libogc's own
	   thread-sleep, which suspends this LWP for the given microseconds. */
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

int SDL_GetNumLogicalCPUCores(void)
{
	return 1; /* Wii's Broadway core is single-threaded from the game's POV */
}

void SDL_free(void* p) { free(p); }

/* ----------------------------------------------------------------------
 * Mutex / condition variable (libogc LWP)
 * ------------------------------------------------------------------- */

struct SDL_Mutex { mutex_t handle; };
struct SDL_Condition { cond_t handle; };

SDL_Mutex* SDL_CreateMutex(void)
{
	SDL_Mutex* m = (SDL_Mutex*)malloc(sizeof(SDL_Mutex));
	LWP_MutexInit(&m->handle, false);
	return m;
}

void SDL_DestroyMutex(SDL_Mutex* mutex)
{
	if (!mutex) return;
	LWP_MutexDestroy(mutex->handle);
	free(mutex);
}

void SDL_LockMutex(SDL_Mutex* mutex) { LWP_MutexLock(mutex->handle); }
void SDL_UnlockMutex(SDL_Mutex* mutex) { LWP_MutexUnlock(mutex->handle); }

SDL_Condition* SDL_CreateCondition(void)
{
	SDL_Condition* c = (SDL_Condition*)malloc(sizeof(SDL_Condition));
	LWP_CondInit(&c->handle);
	return c;
}

void SDL_DestroyCondition(SDL_Condition* cond)
{
	if (!cond) return;
	LWP_CondDestroy(cond->handle);
	free(cond);
}

void SDL_SignalCondition(SDL_Condition* cond) { LWP_CondSignal(cond->handle); }
void SDL_BroadcastCondition(SDL_Condition* cond) { LWP_CondBroadcast(cond->handle); }

bool SDL_WaitCondition(SDL_Condition* cond, SDL_Mutex* mutex)
{
	return LWP_CondWait(cond->handle, mutex->handle) == 0;
}

/* ----------------------------------------------------------------------
 * Threads (libogc LWP)
 * ------------------------------------------------------------------- */

struct SDL_Thread { lwp_t handle; };

typedef struct { SDL_ThreadFunction fn; void* data; } ThreadTrampolineArgs;

static void* ThreadTrampoline(void* argVoid)
{
	ThreadTrampolineArgs* args = (ThreadTrampolineArgs*)argVoid;
	SDL_ThreadFunction fn = args->fn;
	void* data = args->data;
	free(args);
	fn(data);
	return NULL;
}

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data)
{
	(void)name;
	SDL_Thread* t = (SDL_Thread*)malloc(sizeof(SDL_Thread));
	ThreadTrampolineArgs* args = (ThreadTrampolineArgs*)malloc(sizeof(ThreadTrampolineArgs));
	args->fn = fn;
	args->data = data;
	/* 64KB stack, default priority -- matches other libogc homebrew worker threads */
	LWP_CreateThread(&t->handle, ThreadTrampoline, args, NULL, 64 * 1024, 64);
	return t;
}

void SDL_WaitThread(SDL_Thread* thread, int* status)
{
	if (!thread) return;
	void* ret = NULL;
	LWP_JoinThread(thread->handle, &ret);
	if (status) *status = 0;
	free(thread);
}
