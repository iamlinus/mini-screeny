#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Helsingborgs IF page: table position, days to the next match (home/away), last result.
// Data from tools/companion.py over USB: "hif {json}" (see tools/football.py), sent hourly.
bool hifFeed(const char *json);
void hifDraw(Arduino_GFX *g);
bool hifConsumeDirty();     // true once after new data arrived or the status mark changed
const char *hifDebug();
