#pragma once
#include <Arduino.h>

void stockBegin();        // starts Wi-Fi + background fetch task (core 0)
void drawStock();         // draws straight to the panel
bool stockConsumeDirty(); // true once after new data arrived or live state changed
