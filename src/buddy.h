#pragma once
#include "frame.h"
#include <Arduino_GFX_Library.h>

void buddyInit();
void buddyUpdate(const Sensors &s, float dt);
void buddyDraw(Arduino_GFX *g);
void buddySay(const char *msg, float sec, int prio = 1);
bool buddySetName(const char *name);   // greeting "Hej <name>!"; false if unchanged
void buddyBootShort();      // BOOT short press on the buddy page
bool buddyAsleep();
const char *buddyDebug();   // one-line state for serial logs
