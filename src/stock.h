#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

void stockBegin();        // starts Wi-Fi + background fetch task (core 0)
void drawStock(Arduino_GFX *g);   // draws the page (on the rotated canvas; caller flushes)
bool stockConsumeDirty(); // true once after new data arrived or live state changed
