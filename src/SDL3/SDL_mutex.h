#pragma once
/* Minimal SDL3 mutex/condition-variable shim backed by libogc LWP. */
#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Mutex SDL_Mutex;
typedef struct SDL_Condition SDL_Condition;

SDL_Mutex* SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_Mutex* mutex);
void SDL_LockMutex(SDL_Mutex* mutex);
void SDL_UnlockMutex(SDL_Mutex* mutex);

SDL_Condition* SDL_CreateCondition(void);
void SDL_DestroyCondition(SDL_Condition* cond);
void SDL_SignalCondition(SDL_Condition* cond);
void SDL_BroadcastCondition(SDL_Condition* cond);
bool SDL_WaitCondition(SDL_Condition* cond, SDL_Mutex* mutex);

#ifdef __cplusplus
}
#endif
