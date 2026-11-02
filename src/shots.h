#pragma once
#include "frame.h"
#include <Arduino_GFX_Library.h>

// iShots: a shot glass whose liquid stays level with the real world. Tilt to drink, stand up to refill.
void shotsInit();
void shotsUpdate(const Sensors &s, float dt);
void shotsDraw(Arduino_GFX *g);
void shotsNext();            // BOOT: next shot type
const char *shotsDebug();
