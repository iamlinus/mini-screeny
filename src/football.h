#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Football page: table position, days to the next match (home/away), last result for the team in
// config.toml. Data from tools/companion.py over USB: "fb {json}" (see tools/football.py), sent hourly.
bool footballFeed(const char *json);
void footballDraw(Arduino_GFX *g);
bool footballConsumeDirty();     // true once after new data arrived or the status mark changed
const char *footballDebug();
