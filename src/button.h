#pragma once

#include <stdint.h>

void buttonBegin();

// True once per debounced press of BUTTON_PIN (active low, INPUT_PULLUP).
bool buttonPressed(uint32_t now);

// Debounced state: true while the button is held.
bool buttonHeld();
