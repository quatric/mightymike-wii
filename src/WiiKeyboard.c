/*
 * WiiKeyboard.c -- USB keyboard support for the SDL3 shim, backed by
 * libogc's wiikeyboard (USB HID boot-protocol keyboard driver).
 *
 * A dedicated LWP thread owns USB_Initialize()/USBKeyboard_Scan() and
 * feeds a press/release event callback, which we use to maintain a plain
 * bool[] keystate array --
 * exactly what SDL_GetKeyboardState() is expected to return, so the game's
 * existing keyboard bindings (Heart/InputDefaults.c) work completely
 * unmodified once a USB keyboard is plugged into the Wii.
 *
 * Keycodes are USB HID keyboard usage IDs (the boot-protocol scancode
 * space); mapped here to our own SDL_Scancode enum values one at a time
 * rather than assuming numeric equality with real SDL3's scancodes (which
 * happen to match HID usage IDs, but our shim's enum is just declared in
 * source order and isn't guaranteed to line up).
 */

#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/usb.h>
#include <wiikeyboard/usbkeyboard.h>
#include <unistd.h>
#include <string.h>

#include <SDL3/SDL.h>

static bool sKeyState[SDL_SCANCODE_COUNT];

static SDL_Scancode HidKeycodeToScancode(u8 keyCode)
{
	switch (keyCode)
	{
		case 0x04: return SDL_SCANCODE_A;
		case 0x05: return SDL_SCANCODE_B;
		case 0x06: return SDL_SCANCODE_C;
		case 0x07: return SDL_SCANCODE_D;
		case 0x08: return SDL_SCANCODE_E;
		case 0x09: return SDL_SCANCODE_F;
		case 0x0A: return SDL_SCANCODE_G;
		case 0x0B: return SDL_SCANCODE_H;
		case 0x0C: return SDL_SCANCODE_I;
		case 0x0D: return SDL_SCANCODE_J;
		case 0x0E: return SDL_SCANCODE_K;
		case 0x0F: return SDL_SCANCODE_L;
		case 0x10: return SDL_SCANCODE_M;
		case 0x11: return SDL_SCANCODE_N;
		case 0x12: return SDL_SCANCODE_O;
		case 0x13: return SDL_SCANCODE_P;
		case 0x14: return SDL_SCANCODE_Q;
		case 0x15: return SDL_SCANCODE_R;
		case 0x16: return SDL_SCANCODE_S;
		case 0x17: return SDL_SCANCODE_T;
		case 0x18: return SDL_SCANCODE_U;
		case 0x19: return SDL_SCANCODE_V;
		case 0x1A: return SDL_SCANCODE_W;
		case 0x1B: return SDL_SCANCODE_X;
		case 0x1C: return SDL_SCANCODE_Y;
		case 0x1D: return SDL_SCANCODE_Z;
		case 0x1E: return SDL_SCANCODE_1;
		case 0x1F: return SDL_SCANCODE_2;
		case 0x20: return SDL_SCANCODE_3;
		case 0x21: return SDL_SCANCODE_4;
		case 0x22: return SDL_SCANCODE_5;
		case 0x23: return SDL_SCANCODE_6;
		case 0x24: return SDL_SCANCODE_7;
		case 0x25: return SDL_SCANCODE_8;
		case 0x26: return SDL_SCANCODE_9;
		case 0x27: return SDL_SCANCODE_0;
		case 0x28: return SDL_SCANCODE_RETURN;
		case 0x29: return SDL_SCANCODE_ESCAPE;
		case 0x2A: return SDL_SCANCODE_BACKSPACE;
		case 0x2B: return SDL_SCANCODE_TAB;
		case 0x2C: return SDL_SCANCODE_SPACE;
		case 0x2D: return SDL_SCANCODE_MINUS;
		case 0x2E: return SDL_SCANCODE_EQUALS;
		case 0x2F: return SDL_SCANCODE_LEFTBRACKET;
		case 0x30: return SDL_SCANCODE_RIGHTBRACKET;
		case 0x31: return SDL_SCANCODE_BACKSLASH;
		case 0x33: return SDL_SCANCODE_SEMICOLON;
		case 0x34: return SDL_SCANCODE_APOSTROPHE;
		case 0x35: return SDL_SCANCODE_GRAVE;
		case 0x36: return SDL_SCANCODE_COMMA;
		case 0x37: return SDL_SCANCODE_PERIOD;
		case 0x38: return SDL_SCANCODE_SLASH;
		case 0x39: return SDL_SCANCODE_CAPSLOCK;
		case 0x3A: return SDL_SCANCODE_F1;
		case 0x3B: return SDL_SCANCODE_F2;
		case 0x3C: return SDL_SCANCODE_F3;
		case 0x3D: return SDL_SCANCODE_F4;
		case 0x3E: return SDL_SCANCODE_F5;
		case 0x3F: return SDL_SCANCODE_F6;
		case 0x40: return SDL_SCANCODE_F7;
		case 0x41: return SDL_SCANCODE_F8;
		case 0x42: return SDL_SCANCODE_F9;
		case 0x43: return SDL_SCANCODE_F10;
		case 0x44: return SDL_SCANCODE_F11;
		case 0x45: return SDL_SCANCODE_F12;
		case 0x49: return SDL_SCANCODE_INSERT;
		case 0x4A: return SDL_SCANCODE_HOME;
		case 0x4B: return SDL_SCANCODE_PAGEUP;
		case 0x4C: return SDL_SCANCODE_DELETE;
		case 0x4D: return SDL_SCANCODE_END;
		case 0x4E: return SDL_SCANCODE_PAGEDOWN;
		case 0x4F: return SDL_SCANCODE_RIGHT;
		case 0x50: return SDL_SCANCODE_LEFT;
		case 0x51: return SDL_SCANCODE_DOWN;
		case 0x52: return SDL_SCANCODE_UP;
		case 0x54: return SDL_SCANCODE_KP_DIVIDE;
		case 0x55: return SDL_SCANCODE_KP_MULTIPLY;
		case 0x56: return SDL_SCANCODE_KP_MINUS;
		case 0x57: return SDL_SCANCODE_KP_PLUS;
		case 0x58: return SDL_SCANCODE_KP_ENTER;
		case 0x63: return SDL_SCANCODE_KP_DECIMAL;
		case 0xE0: return SDL_SCANCODE_LCTRL;
		case 0xE1: return SDL_SCANCODE_LSHIFT;
		case 0xE2: return SDL_SCANCODE_LALT;
		case 0xE3: return SDL_SCANCODE_LGUI;
		case 0xE4: return SDL_SCANCODE_RCTRL;
		case 0xE5: return SDL_SCANCODE_RSHIFT;
		case 0xE6: return SDL_SCANCODE_RALT;
		case 0xE7: return SDL_SCANCODE_RGUI;
		default: return SDL_SCANCODE_UNKNOWN;
	}
}

static void KeyboardEventHandler(USBKeyboard_event event)
{
	if (event.type != USBKEYBOARD_PRESSED && event.type != USBKEYBOARD_RELEASED)
		return; /* USBKEYBOARD_DISCONNECTED: nothing to clean up here */

	SDL_Scancode sc = HidKeycodeToScancode(event.keyCode);
	if (sc == SDL_SCANCODE_UNKNOWN)
		return;

	sKeyState[sc] = (event.type == USBKEYBOARD_PRESSED);
}

static lwp_t sKeyboardThread = LWP_THREAD_NULL;
static volatile bool sKeyboardThreadRunning = false;

static void* KeyboardThreadFunc(void* arg)
{
	(void)arg;
	while (sKeyboardThreadRunning)
	{
		if (!USBKeyboard_IsConnected())
		{
			USBKeyboard_Open(KeyboardEventHandler);
		}
		USBKeyboard_Scan();
		usleep(400);
	}
	return NULL;
}

void WiiKeyboard_Init(void)
{
	memset(sKeyState, 0, sizeof(sKeyState));

	USB_Initialize();
	USBKeyboard_Initialize();

	sKeyboardThreadRunning = true;
	LWP_CreateThread(&sKeyboardThread, KeyboardThreadFunc, NULL, NULL, 16 * 1024, 68);
}

const bool* WiiKeyboard_GetState(void)
{
	return sKeyState;
}
