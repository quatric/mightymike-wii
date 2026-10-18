#pragma once

#include <stdbool.h>
#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the background update check (once, with the input layer). */
void WiiUpdate_Start(void);
/* Feed the WPAD buttons pressed this frame; true while the popup has the controls. */
bool WiiUpdate_Input(u32 buttonsDown);
/* Draw the popup, if open, into the RGB565 frame about to be uploaded. */
void WiiUpdate_Draw(void* pixels, int w, int h);

#ifdef __cplusplus
}
#endif
