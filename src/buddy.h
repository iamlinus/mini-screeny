#pragma once
#include "frame.h"
#include <Arduino_GFX_Library.h>

void buddyInit();
void buddyUpdate(const Sensors &s, float dt);
void buddyDraw(Arduino_GFX *g);
void buddySay(const char *msg, float sec, int prio = 1);
void buddyAlert(const char *msg, float sec);   // wakes him, beats any chatter, keeps him awake while it shows
bool buddySetName(const char *name);   // greeting "Hej <name>!"; false if unchanged
bool buddyAsleep();
const char *buddyDebug();   // one-line state for serial logs
