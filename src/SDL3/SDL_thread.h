#pragma once
/* Minimal SDL3 thread shim backed by libogc LWP. */
#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void* data);

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data);
void SDL_WaitThread(SDL_Thread* thread, int* status);

#ifdef __cplusplus
}
#endif
