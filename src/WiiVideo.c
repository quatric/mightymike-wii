/*
 * WiiVideo.c -- GX/VIDEO backend for the SDL3 window/GL-context shim.
 *
 * Wii has no real windowing system: "the window" is just the TV output,
 * always fullscreen, and there's exactly one of it. SDL_Window is an opaque
 * handle wrapping the current display mode; the "GL context" wraps opengx,
 * which translates fixed-function GL calls onto the GX fifo.
 */

#include <gccore.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <ogc/lwp_watchdog.h>
#include <opengx.h>

#include <SDL3/SDL.h>
#include "WiiDebug.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

struct SDL_Window {
	int w, h;
};

static GXRModeObj* sRMode = NULL;
static void* sXfb[2] = { NULL, NULL };
static int sWhichFb = 0;
static void* sGxFifo = NULL;
static bool sGxInitialized = false;
static bool sVsync = true;

static void InitGX(void)
{
	if (sGxInitialized)
		return;

	WiiCheckpoint("InitGX: start");

	VIDEO_Init();
	WiiCheckpoint("InitGX: VIDEO_Init done");

	sRMode = VIDEO_GetPreferredMode(NULL);
	WiiCheckpoint("InitGX: VIDEO_GetPreferredMode done");

	sXfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(sRMode));
	sXfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(sRMode));
	WiiCheckpoint("InitGX: framebuffers allocated");

	VIDEO_Configure(sRMode);
	VIDEO_SetNextFramebuffer(sXfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	WiiCheckpoint("InitGX: VIDEO_Configure/Flush done, waiting for vsync");
	VIDEO_WaitVSync();
	if (sRMode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	WiiCheckpoint("InitGX: first vsync(s) done");

	sGxFifo = memalign(32, DEFAULT_FIFO_SIZE);
	memset(sGxFifo, 0, DEFAULT_FIFO_SIZE);
	WiiCheckpoint("InitGX: GX fifo allocated, calling GX_Init");
	GX_Init(sGxFifo, DEFAULT_FIFO_SIZE);
	WiiCheckpoint("InitGX: GX_Init done");

	GXColor background = { 0, 0, 0, 0xff };
	GX_SetCopyClear(background, GX_MAX_Z24);

	GX_SetViewport(0, 0, sRMode->fbWidth, sRMode->efbHeight, 0, 1);
	GX_SetDispCopyYScale((f32)sRMode->xfbHeight / (f32)sRMode->efbHeight);
	GX_SetScissor(0, 0, sRMode->fbWidth, sRMode->efbHeight);
	GX_SetDispCopySrc(0, 0, sRMode->fbWidth, sRMode->efbHeight);
	GX_SetDispCopyDst(sRMode->fbWidth, sRMode->xfbHeight);
	GX_SetCopyFilter(sRMode->aa, sRMode->sample_pattern, GX_TRUE, sRMode->vfilter);
	GX_SetFieldMode(sRMode->field_rendering,
		((sRMode->viHeight == 2 * sRMode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

	if (sRMode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetDispCopyGamma(GX_GM_1_0);
	WiiCheckpoint("InitGX: GX config done, calling ogx_initialize");

	ogx_initialize();
	WiiCheckpoint("InitGX: ogx_initialize done");
	ogx_enable_double_buffering(1);
	WiiCheckpoint("InitGX: ogx_enable_double_buffering done -- InitGX complete");

	sGxInitialized = true;
}

SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags)
{
	(void)title; (void)flags;

	WiiCheckpoint("SDL_CreateWindow: called");
	InitGX();
	WiiCheckpoint("SDL_CreateWindow: InitGX returned");

	SDL_Window* window = (SDL_Window*)malloc(sizeof(SDL_Window));
	/* Report the real TV resolution -- the game letterboxes/scales its
	   fixed-size playfield into whatever we report here. */
	window->w = sRMode->fbWidth;
	window->h = sRMode->efbHeight;
	return window;
}

void SDL_DestroyWindow(SDL_Window* window)
{
	free(window);
}

void SDL_SetWindowTitle(SDL_Window* window, const char* title) { (void)window; (void)title; }
void SDL_SetWindowSize(SDL_Window* window, int w, int h) { (void)window; (void)w; (void)h; }
void SDL_SetWindowPosition(SDL_Window* window, int x, int y) { (void)window; (void)x; (void)y; }

bool SDL_SetWindowFullscreen(SDL_Window* window, bool fullscreen)
{
	(void)window; (void)fullscreen;
	return true; /* always fullscreen on a TV */
}

void SDL_GetWindowSize(SDL_Window* window, int* w, int* h)
{
	if (w) *w = window->w;
	if (h) *h = window->h;
}

void SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h)
{
	SDL_GetWindowSize(window, w, h);
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* window)
{
	(void)window;
	return SDL_WINDOW_MAXIMIZED;
}

void SDL_MaximizeWindow(SDL_Window* window) { (void)window; }
void SDL_RestoreWindow(SDL_Window* window) { (void)window; }
void SDL_SyncWindow(SDL_Window* window) { (void)window; }
void SDL_ShowCursor(void) {}
void SDL_HideCursor(void) {}

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window) { (void)window; return 1; }

SDL_DisplayID* SDL_GetDisplays(int* count)
{
	/* Real SDL3's SDL_GetDisplays() returns a heap-allocated array that the
	   caller is expected to SDL_free() -- see GetPreferredSDLDisplayID() in
	   Heart/Window.c, which does exactly that. Returning a pointer to a
	   static here (as an earlier version of this function did) meant that
	   free() call corrupted the heap by trying to free a non-heap address,
	   crashing later inside an unrelated _free_r call (confirmed via a
	   DSI stack trace back to SetOptimalWindowSize -> GetPreferredSDLDisplayID). */
	SDL_DisplayID* ids = (SDL_DisplayID*)malloc(sizeof(SDL_DisplayID));
	ids[0] = 1;
	if (count) *count = 1;
	return ids;
}

bool SDL_GetDisplayUsableBounds(SDL_DisplayID display, SDL_Rect* rect)
{
	(void)display;
	if (rect)
	{
		rect->x = 0;
		rect->y = 0;
		rect->w = sRMode ? sRMode->fbWidth : 640;
		rect->h = sRMode ? sRMode->efbHeight : 480;
	}
	return true;
}

/* ----------------------------------------------------------------------
 * "GL context" -- opengx has no real context object; there's only ever
 * one, tied to the GX state machine we set up in InitGX().
 * ------------------------------------------------------------------- */

SDL_GLContext SDL_GL_CreateContext(SDL_Window* window)
{
	(void)window;
	InitGX();
	return (SDL_GLContext)1; /* non-null sentinel */
}

void SDL_GL_DestroyContext(SDL_GLContext ctx) { (void)ctx; }

bool SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext ctx)
{
	(void)window; (void)ctx;
	return true; /* only one context ever exists */
}

bool SDL_GL_SetSwapInterval(int interval)
{
	sVsync = interval != 0;
	return true;
}

void* SDL_GL_GetProcAddress(const char* proc)
{
	return ogx_get_proc_address(proc);
}

void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h)
{
	SDL_GetWindowSize(window, w, h);
}

bool SDL_GL_SetAttribute(int attr, int value) { (void)attr; (void)value; return true; }

/* Frame-timing instrumentation: logs measured frame time to
   mightymike_checkpoints.txt every 60 frames, split into "CPU-side work
   since the last swap" (game logic + framebuffer conversion + texture
   upload, all of which happens in the caller before this function runs)
   vs "GX copy/vsync" (this function's own GX_CopyDisp/VIDEO_WaitVSync
   cost) -- so a reported perf issue can be attributed to a side instead
   of guessed at. */
static u64 sLastSwapTicks = 0;
static u32 sFrameCounter = 0;
static u64 sAccumCpuTicks = 0;
static u64 sAccumGxTicks = 0;

void SDL_GL_SwapWindow(SDL_Window* window)
{
	(void)window;

	static bool loggedFirst = false;
	if (!loggedFirst)
	{
		WiiCheckpoint("SDL_GL_SwapWindow: first call");
		loggedFirst = true;
		sLastSwapTicks = gettime();
	}

	u64 nowBefore = gettime();
	sAccumCpuTicks += nowBefore - sLastSwapTicks;

	ogx_prepare_swap_buffers();

	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(sXfb[sWhichFb], GX_TRUE);
	GX_DrawDone();

	VIDEO_SetNextFramebuffer(sXfb[sWhichFb]);
	VIDEO_Flush();
	if (sVsync)
		VIDEO_WaitVSync();

	sWhichFb ^= 1;

	u64 nowAfter = gettime();
	sAccumGxTicks += nowAfter - nowBefore;
	sLastSwapTicks = nowAfter;

	sFrameCounter++;
	if (sFrameCounter >= 60)
	{
		u32 cpuMs = (u32)ticks_to_millisecs(sAccumCpuTicks);
		u32 gxMs = (u32)ticks_to_millisecs(sAccumGxTicks);
		u32 totalMs = cpuMs + gxMs;
		char buf[128];
		snprintf(buf, sizeof(buf),
			"perf: %lu frames, avg %lums/frame (cpu %lums + gx/vsync %lums) = ~%lu fps",
			(unsigned long)sFrameCounter,
			(unsigned long)(totalMs / sFrameCounter),
			(unsigned long)(cpuMs / sFrameCounter),
			(unsigned long)(gxMs / sFrameCounter),
			(unsigned long)(totalMs > 0 ? (1000 * sFrameCounter) / totalMs : 0));
		WiiCheckpoint(buf);

		sFrameCounter = 0;
		sAccumCpuTicks = 0;
		sAccumGxTicks = 0;
	}
}
