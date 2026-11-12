#pragma once

#include <stdint.h>

// Potentiometer face selector on POT_PIN. potBegin() takes the face count
// and the first reading, which is only a starting point: nothing is applied
// until the knob moves into another zone.
void potBegin(uint32_t now, int faceCount);

// Non-blocking. Reads one sample every POT_SAMPLE_MS and returns the new zone
// index when the smoothed position has moved into a different zone, else -1.
int potPoll(uint32_t now);
