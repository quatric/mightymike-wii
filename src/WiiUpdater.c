/*
 * WiiUpdater.c -- update check and in-place update via libwiiupdate.
 *
 * The check starts with the input layer; when a newer release is out a
 * popup is drawn into the frame (libogc's 8x16 console font, straight into
 * the RGB565 pixel buffer GLRender uploads) and takes the controller until
 * it is closed. A installs the update, then returns to the loader.
 */

#include <gccore.h>
#include <stdlib.h>
#include <string.h>
#include <wiiupdate.h>
#include "WiiUpdater.h"

extern const u8 console_font_8x16[];

static wiiupdate_popup sPopup;
static bool sOpen;

void WiiUpdate_Start(void)
{
	if (__system_argv && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argc > 0)
		WIIUPDATE_SetAppPath(__system_argv->argv[0]);
	WIIUPDATE_Start("quatric", "mightymike-wii", APP_VERSION);
}

bool WiiUpdate_Input(u32 buttonsDown)
{
	bool wasOpen = sOpen;
	int st = WIIUPDATE_Popup(buttonsDown, &sPopup);
	if (st == WIIUPDATE_POPUP_EXIT)
		exit(0);
	sOpen = st == WIIUPDATE_POPUP_OPEN;
	return sOpen || wasOpen;   /* also swallow the press that closed it */
}

static inline u16 RGB565(int r, int g, int b) { return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static void Fill(u16* px, int w, int h, int x, int y, int rw, int rh, u16 c)
{
	for (int j = y < 0 ? 0 : y; j < y + rh && j < h; j++)
		for (int i = x < 0 ? 0 : x; i < x + rw && i < w; i++)
			px[j * w + i] = c;
}

static void Text(u16* px, int w, int h, int cx, int y, int s, const char* str, u16 c)
{
	int x = cx - (int)strlen(str) * 8 * s / 2;
	for (; *str; str++, x += 8 * s)
	{
		const u8* glyph = console_font_8x16 + (u8)*str * 16;
		for (int row = 0; row < 16; row++)
			for (int col = 0; col < 8; col++)
				if (glyph[row] & (0x80 >> col))
					Fill(px, w, h, x + col * s, y + row * s, s, s, c);
	}
}

void WiiUpdate_Draw(void* pixels, int w, int h)
{
	if (!sOpen)
		return;
	u16* px = pixels;
	int s = w >= 1280 ? 2 : 1;   /* HQ stretch renders at twice the size */

	for (int i = 0; i < w * h; i++)
		px[i] = (px[i] >> 1) & 0x7BEF;   /* dim the game behind the popup */

	int bw = 560 * s, bh = (sPopup.progress >= 0 || sPopup.detail[0] ? 150 : 122) * s;
	int bx = (w - bw) / 2, by = (h - bh) / 2;
	Fill(px, w, h, bx - 2 * s, by - 2 * s, bw + 4 * s, bh + 4 * s, RGB565(255, 214, 0));
	Fill(px, w, h, bx, by, bw, bh, RGB565(20, 24, 60));
	Text(px, w, h, w / 2, by + 14 * s, 2 * s, sPopup.title, RGB565(255, 214, 0));
	Text(px, w, h, w / 2, by + 56 * s, s, sPopup.text, RGB565(255, 255, 255));
	int row = by + 84 * s;
	if (sPopup.progress >= 0)
	{
		Fill(px, w, h, bx + 60 * s, row + 2 * s, bw - 120 * s, 12 * s, RGB565(60, 60, 90));
		Fill(px, w, h, bx + 60 * s, row + 2 * s, (bw - 120 * s) * sPopup.progress / 100, 12 * s, RGB565(255, 214, 0));
		row += 28 * s;
	}
	else if (sPopup.detail[0])
	{
		Text(px, w, h, w / 2, row, s, sPopup.detail, RGB565(120, 200, 255));
		row += 28 * s;
	}
	Text(px, w, h, w / 2, row + 4 * s, s, sPopup.hint, RGB565(200, 200, 200));
}
