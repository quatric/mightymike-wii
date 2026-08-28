#pragma once
/*
 * Minimal SDL3 compatibility shim for the Wii (devkitPPC/libogc).
 * Implements only the subset of the SDL3 API actually used by Mighty Mike:
 * window/render/GL stub types, gamepad input mapped to WPAD/PAD, threading
 * and mutexes mapped to libogc LWP, plus a handful of misc utility calls.
 *
 * This header intentionally does NOT try to be a general-purpose SDL3
 * replacement -- see WiiPlatform.c for the actual implementations.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Real SDL3's umbrella <SDL3/SDL.h> transitively pulls in SDL_mutex.h and
   SDL_thread.h; some game/Pomme files rely on that instead of including
   them directly, so match that behavior here. (These headers #include
   "SDL.h" themselves, but the #pragma once guards make that harmless.) */
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------
 * Basic types
 * ------------------------------------------------------------------- */

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef void* SDL_GLContext;
typedef uint32_t SDL_WindowFlags;
typedef uint32_t SDL_DisplayID;

typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_FRect { float x, y, w, h; } SDL_FRect;

static inline bool SDL_RectsEqual(const SDL_Rect* a, const SDL_Rect* b)
{
	return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

/* ----------------------------------------------------------------------
 * Init / misc
 * ------------------------------------------------------------------- */

#define SDL_INIT_VIDEO		0x00000020u
#define SDL_INIT_GAMEPAD	0x00002000u

bool SDL_Init(uint32_t flags);
void SDL_Quit(void);
void SDL_SetAppMetadata(const char* name, const char* version, const char* identifier);

const char* SDL_GetError(void);
void SDL_ClearError(void);

#define SDL_LOG_CATEGORY_APPLICATION	0
#define SDL_LOG_PRIORITY_VERBOSE		1
#define SDL_LOG_PRIORITY_INFO			2

void SDL_Log(const char* fmt, ...);
void SDL_LogError(int category, const char* fmt, ...);
void SDL_SetLogPriorities(int priority);

#define SDL_MESSAGEBOX_ERROR	0x00000010u
#define SDL_MESSAGEBOX_WARNING	0x00000020u
bool SDL_ShowSimpleMessageBox(uint32_t flags, const char* title, const char* message, SDL_Window* window);

uint64_t SDL_GetTicks(void);
void SDL_Delay(uint32_t ms);

int SDL_GetNumLogicalCPUCores(void);

#define SDL_abs(x) ((x) < 0 ? -(x) : (x))
#define SDL_clamp(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define SDL_memcpy memcpy
#define SDL_memset memset
#define SDL_strlen strlen
#define SDL_snprintf snprintf
void SDL_free(void* p);

/* ----------------------------------------------------------------------
 * Window / renderer / GL (Wii has no real window system; these are
 * thin wrappers driving GX + opengx directly -- see WiiPlatform.c)
 * ------------------------------------------------------------------- */

#define SDL_WINDOW_OPENGL			0x00000002u
#define SDL_WINDOW_RESIZABLE		0x00000020u
#define SDL_WINDOW_MAXIMIZED		0x00000080u
#define SDL_WINDOW_HIGH_PIXEL_DENSITY	0x00002000u

#define SDL_WINDOWPOS_CENTERED_DISPLAY(x) 0

SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags);
void SDL_DestroyWindow(SDL_Window* window);
void SDL_SetWindowTitle(SDL_Window* window, const char* title);
void SDL_SetWindowSize(SDL_Window* window, int w, int h);
void SDL_SetWindowPosition(SDL_Window* window, int x, int y);
bool SDL_SetWindowFullscreen(SDL_Window* window, bool fullscreen);
void SDL_GetWindowSize(SDL_Window* window, int* w, int* h);
void SDL_GetWindowSizeInPixels(SDL_Window* window, int* w, int* h);
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* window);
void SDL_MaximizeWindow(SDL_Window* window);
void SDL_RestoreWindow(SDL_Window* window);
void SDL_SyncWindow(SDL_Window* window);
void SDL_ShowCursor(void);
void SDL_HideCursor(void);

SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window* window);
SDL_DisplayID* SDL_GetDisplays(int* count);
bool SDL_GetDisplayUsableBounds(SDL_DisplayID display, SDL_Rect* rect);

SDL_GLContext SDL_GL_CreateContext(SDL_Window* window);
void SDL_GL_DestroyContext(SDL_GLContext ctx);
bool SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext ctx);
void SDL_GL_SwapWindow(SDL_Window* window);
bool SDL_GL_SetSwapInterval(int interval);
void* SDL_GL_GetProcAddress(const char* proc);
void SDL_GL_GetDrawableSize(SDL_Window* window, int* w, int* h);
bool SDL_GL_SetAttribute(int attr, int value);
#define SDL_GL_CONTEXT_PROFILE_MASK			0
#define SDL_GL_CONTEXT_PROFILE_COMPATIBILITY	0

#define SDL_PIXELFORMAT_RGB	1
#define SDL_PIXELFORMAT_RGBA	2
#define SDL_TEXTUREACCESS_STREAMING	1
#define SDL_SCALEMODE_NEAREST	0
#define SDL_SCALEMODE_LINEAR	1
#define SDL_LOGICAL_PRESENTATION_LETTERBOX			1
#define SDL_LOGICAL_PRESENTATION_INTEGER_SCALE		2

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, const char* name);
void SDL_DestroyRenderer(SDL_Renderer* renderer);
const char* SDL_GetRendererName(SDL_Renderer* renderer);
SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, int format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture* texture);
bool SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch);
void SDL_SetTextureScaleMode(SDL_Texture* texture, int scaleMode);
void SDL_RenderClear(SDL_Renderer* renderer);
void SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* srcrect, const SDL_FRect* dstrect);
void SDL_RenderPresent(SDL_Renderer* renderer);
void SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync);
bool SDL_SetRenderLogicalPresentation(SDL_Renderer* renderer, int w, int h, int mode);
bool SDL_RenderSetIntegerScale(SDL_Renderer* renderer, bool enable);

/* ----------------------------------------------------------------------
 * Events (mapped onto Wii PAD/WPAD polling each frame)
 * ------------------------------------------------------------------- */

typedef enum {
	SDL_EVENT_QUIT = 0x100,
	SDL_EVENT_WINDOW_CLOSE_REQUESTED,
	SDL_EVENT_WINDOW_RESIZED,
	SDL_EVENT_TEXT_INPUT,
	SDL_EVENT_MOUSE_WHEEL,
	SDL_EVENT_GAMEPAD_ADDED,
	SDL_EVENT_GAMEPAD_REMOVED,
} SDL_EventType;

typedef struct SDL_Event {
	uint32_t type;
	union {
		struct { int32_t which; } gdevice;
		struct { float x, y; } wheel;
		struct { const char* text; } text;
	};
} SDL_Event;

bool SDL_PollEvent(SDL_Event* event);
void SDL_PumpEvents(void);

/* ----------------------------------------------------------------------
 * Keyboard (Wii has no keyboard; stubbed as "always up")
 * ------------------------------------------------------------------- */

typedef int SDL_Scancode;
#define SDL_SCANCODE_COUNT 512
enum {
	SDL_SCANCODE_UNKNOWN = 0,
	SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E,
	SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J,
	SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O,
	SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
	SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X, SDL_SCANCODE_Y, SDL_SCANCODE_Z,
	SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE,
	SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
	SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE,
	SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
	SDL_SCANCODE_DELETE, SDL_SCANCODE_LEFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT, SDL_SCANCODE_LGUI,
	SDL_SCANCODE_RCTRL, SDL_SCANCODE_RALT, SDL_SCANCODE_RGUI,
	SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_PLUS,
	SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_KP_DECIMAL,
	SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
	SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
	SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_CAPSLOCK,
	SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT,
	SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN,
	SDL_SCANCODE_INSERT, SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
	SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
	SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
};

const bool* SDL_GetKeyboardState(int* numkeys);
SDL_Scancode SDL_GetScancodeFromKey(uint32_t key, void* mod);
const char* SDL_GetScancodeName(SDL_Scancode scancode);
uint32_t SDL_GetMouseState(float* x, float* y);

#define SDL_BUTTON_LEFT		1
#define SDL_BUTTON_MIDDLE	2
#define SDL_BUTTON_RIGHT	3
#define SDL_BUTTON_MASK(x) (1u << ((x) - 1))

void SDL_StartTextInput(SDL_Window* window);
void SDL_StopTextInput(SDL_Window* window);

/* ----------------------------------------------------------------------
 * Gamepad (mapped to a single WPAD/PAD-backed virtual gamepad)
 * ------------------------------------------------------------------- */

typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_Joystick SDL_Joystick;
typedef uint32_t SDL_JoystickID;

typedef enum {
	SDL_GAMEPAD_BUTTON_INVALID = -1,
	SDL_GAMEPAD_BUTTON_SOUTH = 0,
	SDL_GAMEPAD_BUTTON_EAST,
	SDL_GAMEPAD_BUTTON_WEST,
	SDL_GAMEPAD_BUTTON_NORTH,
	SDL_GAMEPAD_BUTTON_START,
	SDL_GAMEPAD_BUTTON_LEFT_STICK,
	SDL_GAMEPAD_BUTTON_RIGHT_STICK,
	SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
	SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
	SDL_GAMEPAD_BUTTON_DPAD_UP,
	SDL_GAMEPAD_BUTTON_DPAD_DOWN,
	SDL_GAMEPAD_BUTTON_DPAD_LEFT,
	SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
	SDL_GAMEPAD_BUTTON_BACK,
	SDL_GAMEPAD_BUTTON_GUIDE,
	SDL_GAMEPAD_BUTTON_COUNT,
} SDL_GamepadButton;

typedef enum {
	SDL_GAMEPAD_AXIS_INVALID = -1,
	SDL_GAMEPAD_AXIS_LEFTX = 0,
	SDL_GAMEPAD_AXIS_LEFTY,
	SDL_GAMEPAD_AXIS_RIGHTX,
	SDL_GAMEPAD_AXIS_RIGHTY,
	SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
	SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
	SDL_GAMEPAD_AXIS_COUNT,
} SDL_GamepadAxis;

SDL_JoystickID* SDL_GetJoysticks(int* count);
const char* SDL_GetJoystickNameForID(SDL_JoystickID id);
bool SDL_IsGamepad(SDL_JoystickID id);
SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID id);
void SDL_CloseGamepad(SDL_Gamepad* gamepad);
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad* gamepad);
const char* SDL_GetGamepadName(SDL_Gamepad* gamepad);
SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad* gamepad);
bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button);
int16_t SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis);
const char* SDL_GetGamepadStringForAxis(SDL_GamepadAxis axis);
bool SDL_AddGamepadMappingsFromFile(const char* file);

typedef struct SDL_Haptic SDL_Haptic;
SDL_Haptic* SDL_OpenHapticFromJoystick(SDL_Joystick* joystick);
void SDL_CloseHaptic(SDL_Haptic* haptic);
bool SDL_InitHapticRumble(SDL_Haptic* haptic);

/* ----------------------------------------------------------------------
 * IO streams (only used for tiny writes -- backed by stdio)
 * ------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
 * Audio (backed by AI double-buffered DMA -- see WiiAudio.c)
 * ------------------------------------------------------------------- */

typedef uint32_t SDL_AudioDeviceID;
typedef struct SDL_AudioStream SDL_AudioStream;

typedef struct SDL_AudioSpec {
	int format;
	int channels;
	int freq;
} SDL_AudioSpec;

#define SDL_AUDIO_S16	0x8010
#define SDL_INIT_AUDIO	0x00000010u
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)

bool SDL_InitSubSystem(uint32_t flags);
void SDL_QuitSubSystem(uint32_t flags);

#define SDLCALL

SDL_AudioStream* SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec* spec,
	void (*callback)(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount),
	void* userdata);
bool SDL_GetAudioStreamFormat(SDL_AudioStream* stream, SDL_AudioSpec* srcSpec, SDL_AudioSpec* dstSpec);
SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream* stream);
void SDL_CloseAudioDevice(SDL_AudioDeviceID device);
size_t SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, size_t len);
bool SDL_ResumeAudioDevice(SDL_AudioDeviceID device);

typedef struct SDL_IOStream SDL_IOStream;
SDL_IOStream* SDL_IOFromFile(const char* file, const char* mode);
bool SDL_CloseIO(SDL_IOStream* stream);
size_t SDL_WriteIO(SDL_IOStream* stream, const void* data, size_t size);
bool SDL_WriteU8(SDL_IOStream* stream, uint8_t value);

#ifdef __cplusplus
}
#endif
