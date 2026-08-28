/*
 * WiiInput.c -- gamepad backend for the SDL3 shim, mapped onto real Wii
 * controllers via libogc PAD (GameCube controller) and WPAD (Wiimote /
 * Wiimote+Nunchuk / Classic Controller).
 *
 * The game only ever opens a single SDL_Gamepad (see Heart/Input.c,
 * TryOpenGamepad) and polls its buttons/axes once per frame via
 * SDL_GetGamepadButton/SDL_GetGamepadAxis. We expose exactly one virtual
 * joystick (id 0) that merges whichever real controller is plugged into
 * chan 0, preferring the GameCube controller (dual analog, most faithful
 * to the original control scheme) and falling back to Wiimote+Nunchuk,
 * then a bare sideways Wiimote (digital d-pad only).
 *
 * Mapping (chosen to match the game's default bindings in
 * Heart/InputDefaults.c -- DPAD+LEFTX/LEFTY move, WEST/RIGHT_TRIGGER
 * attack, shoulders cycle weapons, SOUTH confirms, EAST/BACK cancel):
 *
 *   GameCube pad      Wiimote+Nunchuk         Sideways Wiimote   -> SDL_Gamepad
 *   ----------------   ---------------------   ----------------    -------------
 *   Control stick      Nunchuk stick           D-pad (digital)      LEFTX/LEFTY
 *   A                  A                       2                    SOUTH
 *   B                  B                       1                    EAST
 *   X                  Z (nunchuk)             A                    WEST
 *   Y                  C (nunchuk)             B                    NORTH
 *   L                  -                       -                    LEFT_SHOULDER
 *   R                  +                       +                    RIGHT_SHOULDER
 *   R analog / Z       Z (also attack)         -                    RIGHT_TRIGGER (axis)
 *   Start               Home                    Home                 START
 *   D-pad              D-pad                   -                    DPAD_*
 */

#include <gccore.h>
#include <ogc/pad.h>
#include <wiiuse/wpad.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <SDL3/SDL.h>
#include "WiiDebug.h"
#include "WiiKeyboard.h"

struct SDL_Gamepad { int dummy; };
struct SDL_Joystick { int dummy; };

static struct SDL_Gamepad sVirtualGamepad;
static struct SDL_Joystick sVirtualJoystick;
static bool sInputInitialized = false;
static bool sGamepadOpen = false;

typedef struct {
	bool buttons[SDL_GAMEPAD_BUTTON_COUNT];
	int16_t axes[SDL_GAMEPAD_AXIS_COUNT];
} GamepadState;

static GamepadState sState;

static void InitWiiInput(void)
{
	if (sInputInitialized)
		return;

	WiiCheckpoint("InitWiiInput: start");

	PAD_Init();
	WiiCheckpoint("InitWiiInput: PAD_Init done");

	WPAD_Init();
	WiiCheckpoint("InitWiiInput: WPAD_Init done");
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_0, 640, 480);

	WiiKeyboard_Init();
	WiiCheckpoint("InitWiiInput: WiiKeyboard_Init done -- complete");

	sInputInitialized = true;
}

static int16_t StickAxis(s8 raw)
{
	/* GC/Nunchuk stick axes are roughly -100..100; scale to SDL's -32768..32767. */
	int v = (int)raw * 327;
	if (v > 32767) v = 32767;
	if (v < -32768) v = -32768;
	return (int16_t)v;
}

static void PollGameCube(bool* anyConnected)
{
	/* Scan all 4 ports rather than assuming chan 0 -- on real hardware the
	   player's controller may not be in slot 1. */
	u32 connected = PAD_ScanPads();
	if (connected == 0)
		return;

	for (int chan = 0; chan < 4; chan++)
	{
		if (!(connected & (1 << chan)))
			continue;

		*anyConnected = true;

		u16 held = PAD_ButtonsHeld(chan);
		sState.buttons[SDL_GAMEPAD_BUTTON_SOUTH] |= (held & PAD_BUTTON_A) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_EAST]  |= (held & PAD_BUTTON_B) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_WEST]  |= (held & PAD_BUTTON_X) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_NORTH] |= (held & PAD_BUTTON_Y) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_START] |= (held & PAD_BUTTON_START) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]  |= (held & PAD_TRIGGER_L) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] |= (held & PAD_TRIGGER_R) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]    |= (held & PAD_BUTTON_UP) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]  |= (held & PAD_BUTTON_DOWN) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT]  |= (held & PAD_BUTTON_LEFT) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] |= (held & PAD_BUTTON_RIGHT) != 0;

		s8 sx = PAD_StickX(chan);
		s8 sy = PAD_StickY(chan);
		if (sx || sy)
		{
			sState.axes[SDL_GAMEPAD_AXIS_LEFTX] = StickAxis(sx);
			sState.axes[SDL_GAMEPAD_AXIS_LEFTY] = StickAxis((s8)(-sy)); /* GC Y is inverted vs SDL */
		}

		u8 rtrig = PAD_TriggerR(chan);
		if (rtrig > 0 || (held & PAD_TRIGGER_Z))
		{
			int v = (int)rtrig * 128;
			if (held & PAD_TRIGGER_Z) v = 32767;
			if (v > 32767) v = 32767;
			sState.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = (int16_t)v;
		}
	}
}

static void PollWiimoteChannel(int chan, bool* anyConnected)
{
	u32 type = WPAD_ERR_NO_CONTROLLER;
	WPADData* data = WPAD_Data(chan);
	if (!data || WPAD_Probe(chan, &type) != 0)
		return;

	*anyConnected = true;

	u32 held = data->btns_h;
	u32 exp = data->exp.type;

	/* HOME is a system-level "quit" request, not a game button -- return
	   straight to the Homebrew Channel rather than routing it through the
	   SDL gamepad abstraction (there's no in-game concept of this).
	   SYS_ResetSystem(SYS_RETURNTOMENU, ...) is the wrong call here: it
	   reboots into the Wii *System Menu*, not back to whatever launched
	   this .dol (confirmed on real hardware -- it kicked to the Wii Menu
	   instead of HBC). Since HBC is still resident in memory as our
	   loader, a plain exit() hands control back to it directly. */
	if (held & WPAD_BUTTON_HOME)
	{
		exit(0);
	}

	if (exp == WPAD_EXP_NUNCHUK)
	{
		/* Wiimote+Nunchuk, held sideways-free (pointed at TV): map like GC. */
		sState.buttons[SDL_GAMEPAD_BUTTON_SOUTH] |= (held & WPAD_BUTTON_A) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_EAST]  |= (held & WPAD_BUTTON_B) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_WEST]  |= (held & WPAD_NUNCHUK_BUTTON_Z) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_NORTH] |= (held & WPAD_NUNCHUK_BUTTON_C) != 0;
		/* HOME exits straight to the loader (handled separately below,
		   not routed through the SDL button abstraction -- see the
		   WPAD_BUTTON_HOME check in PollWiimoteChannel). '+' pauses,
		   matching the user's request over the initial HOME=pause guess. */
		sState.buttons[SDL_GAMEPAD_BUTTON_START] |= (held & WPAD_BUTTON_PLUS) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]  |= (held & WPAD_BUTTON_MINUS) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]    |= (held & WPAD_BUTTON_UP) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]  |= (held & WPAD_BUTTON_DOWN) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT]  |= (held & WPAD_BUTTON_LEFT) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] |= (held & WPAD_BUTTON_RIGHT) != 0;

		if (held & WPAD_NUNCHUK_BUTTON_Z)
			sState.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;

		joystick_t* js = &data->exp.nunchuk.js;
		float mag = js->mag;
		float ang = js->ang * (3.14159265f / 180.0f);
		if (mag > 0.1f)
		{
			if (mag > 1.0f) mag = 1.0f;
			sState.axes[SDL_GAMEPAD_AXIS_LEFTX] = (int16_t)(mag * 32767.0f * sinf(ang));
			sState.axes[SDL_GAMEPAD_AXIS_LEFTY] = (int16_t)(-mag * 32767.0f * cosf(ang));
		}
	}
	else
	{
		/* Bare Wiimote held sideways: d-pad becomes rotated left/right/up/down
		   (WPAD already remaps this for us when we ask for sideways orientation
		   via WPAD_SetIdealOrientation -- kept simple here: use raw d-pad). */
		/* WEST is the game's shoot/attack button (see kNeed_Attack in
		   Heart/InputDefaults.c) -- user asked for that on button 2. */
		sState.buttons[SDL_GAMEPAD_BUTTON_SOUTH] |= (held & WPAD_BUTTON_A) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_EAST]  |= (held & WPAD_BUTTON_1) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_WEST]  |= (held & WPAD_BUTTON_2) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_NORTH] |= (held & WPAD_BUTTON_B) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_START] |= (held & WPAD_BUTTON_PLUS) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER]  |= (held & WPAD_BUTTON_MINUS) != 0;
		/* Sideways grip, d-pad on the left (power-button end points left --
		   a 90 degree CCW rotation from the upright orientation): each
		   physical d-pad direction rotates 90 degrees CCW into its virtual
		   direction. Two rounds of on-hardware testing got here:
		     - 1st attempt: both axes wrong (naive un-rotated guess)
		     - 2nd attempt: left/right fixed, but up/down came out swapped
		       (RIGHT->DOWN and LEFT->UP is backwards -- it's RIGHT->UP,
		       LEFT->DOWN for a consistent single-direction rotation)
		   This is the consistent single-direction 90 degree CCW mapping. */
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_UP]    |= (held & WPAD_BUTTON_RIGHT) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_DOWN]  |= (held & WPAD_BUTTON_LEFT) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_LEFT]  |= (held & WPAD_BUTTON_UP) != 0;
		sState.buttons[SDL_GAMEPAD_BUTTON_DPAD_RIGHT] |= (held & WPAD_BUTTON_DOWN) != 0;
	}
}

static void PollWiimote(bool* anyConnected)
{
	/* Scan all 4 channels, same rationale as PollGameCube(). */
	if (WPAD_ScanPads() <= WPAD_ERR_NONE)
		return;

	for (int chan = 0; chan < 4; chan++)
		PollWiimoteChannel(chan, anyConnected);
}

/* Called once per frame from SDL_PumpEvents (see WiiPlatform.c). */
void WiiInput_Poll(void)
{
	InitWiiInput();

	memset(&sState, 0, sizeof(sState));

	bool anyConnected = false;
	PollGameCube(&anyConnected);
	PollWiimote(&anyConnected);
}

/* ----------------------------------------------------------------------
 * SDL_Gamepad API surface used by the game
 * ------------------------------------------------------------------- */

SDL_JoystickID* SDL_GetJoysticks(int* count)
{
	InitWiiInput();
	SDL_JoystickID* ids = (SDL_JoystickID*)malloc(sizeof(SDL_JoystickID));
	ids[0] = 0;
	if (count) *count = 1;
	return ids;
}

const char* SDL_GetJoystickNameForID(SDL_JoystickID id) { (void)id; return "Wii Controller"; }
bool SDL_IsGamepad(SDL_JoystickID id) { (void)id; return true; }

SDL_Gamepad* SDL_OpenGamepad(SDL_JoystickID id)
{
	(void)id;
	sGamepadOpen = true;
	return &sVirtualGamepad;
}

void SDL_CloseGamepad(SDL_Gamepad* gamepad) { (void)gamepad; sGamepadOpen = false; }
SDL_JoystickID SDL_GetGamepadID(SDL_Gamepad* gamepad) { (void)gamepad; return 0; }
const char* SDL_GetGamepadName(SDL_Gamepad* gamepad) { (void)gamepad; return "Wii Controller"; }
SDL_Joystick* SDL_GetGamepadJoystick(SDL_Gamepad* gamepad) { (void)gamepad; return &sVirtualJoystick; }

bool SDL_GetGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button)
{
	(void)gamepad;
	if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return false;
	return sState.buttons[button];
}

int16_t SDL_GetGamepadAxis(SDL_Gamepad* gamepad, SDL_GamepadAxis axis)
{
	(void)gamepad;
	if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT) return 0;
	return sState.axes[axis];
}

const char* SDL_GetGamepadStringForAxis(SDL_GamepadAxis axis)
{
	switch (axis)
	{
		case SDL_GAMEPAD_AXIS_LEFTX: return "Left Stick X";
		case SDL_GAMEPAD_AXIS_LEFTY: return "Left Stick Y";
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Z / R";
		default: return "Axis";
	}
}

bool SDL_AddGamepadMappingsFromFile(const char* file) { (void)file; return true; }

/* No force feedback hardware support wired up yet -- Wiimote rumble could
   go here (WPAD_Rumble) but the game only asks for simple on/off rumble
   through the SDL haptic API, which we don't fully emulate. */
SDL_Haptic* SDL_OpenHapticFromJoystick(SDL_Joystick* joystick) { (void)joystick; return NULL; }
void SDL_CloseHaptic(SDL_Haptic* haptic) { (void)haptic; }
bool SDL_InitHapticRumble(SDL_Haptic* haptic) { (void)haptic; return false; }

/* ----------------------------------------------------------------------
 * Keyboard/mouse -- Wii has no built-in keyboard, but a USB one works
 * (see WiiKeyboard.c, backed by libogc's wiikeyboard driver). Mouse is
 * still unsupported (nothing to plug in on real hardware).
 * ------------------------------------------------------------------- */

const bool* SDL_GetKeyboardState(int* numkeys)
{
	if (numkeys) *numkeys = SDL_SCANCODE_COUNT;
	return WiiKeyboard_GetState();
}

SDL_Scancode SDL_GetScancodeFromKey(uint32_t key, void* mod) { (void)key; (void)mod; return SDL_SCANCODE_UNKNOWN; }
const char* SDL_GetScancodeName(SDL_Scancode scancode) { (void)scancode; return ""; }
uint32_t SDL_GetMouseState(float* x, float* y) { if (x) *x = 0; if (y) *y = 0; return 0; }
void SDL_StartTextInput(SDL_Window* window) { (void)window; }
void SDL_StopTextInput(SDL_Window* window) { (void)window; }

/* ----------------------------------------------------------------------
 * Event pump -- the game polls once per frame; we translate that into one
 * WiiInput_Poll() call plus synthetic gamepad-added/removed events.
 * ------------------------------------------------------------------- */

static bool sEventQueued = false;
static bool sSentGamepadAdded = false;

void SDL_PumpEvents(void)
{
	WiiInput_Poll();

	if (!sSentGamepadAdded)
	{
		sSentGamepadAdded = true;
		sEventQueued = true;
	}
}

bool SDL_PollEvent(SDL_Event* event)
{
	if (sEventQueued)
	{
		sEventQueued = false;
		if (event)
		{
			event->type = SDL_EVENT_GAMEPAD_ADDED;
			event->gdevice.which = 0;
		}
		return true;
	}
	return false;
}
