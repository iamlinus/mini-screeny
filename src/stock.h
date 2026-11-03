#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Kjell Group stock page. Data comes from tools/companion.py over USB: "stk {json}" (Avanza quote fields).
bool stockFeed(const char *json);   // false on bad JSON / missing "last"
void drawStock(Arduino_GFX *g);     // draws the page (on the rotated canvas; caller flushes)
bool stockConsumeDirty();           // true once after new data arrived or the live dot changed
const char *stockDebug();
