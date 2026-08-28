#pragma once
/* USB keyboard support, defined in WiiKeyboard.c. */

void WiiKeyboard_Init(void);
const bool* WiiKeyboard_GetState(void);
