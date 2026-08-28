#pragma once
/* Standalone boot-checkpoint logger, defined in WiiPlatform.c. Writes
   immediately (open+write+close every call) so a partial trail survives
   even if the game hangs or faults on the very next line -- used to
   bisect where in boot a hard-hang/exception is happening on real
   hardware, where we have no debugger or console attached. */
void WiiCheckpoint(const char* msg);
