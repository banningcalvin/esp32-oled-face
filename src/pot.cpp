#include "pot.h"

#include "config.h"

#include <Arduino.h>

namespace {

constexpr int kAdcMax = 4095;

int zones = 1;
int samples[POT_AVG_SAMPLES];
int filled = 0;
int head = 0;
long sum = 0;
int current = 0;
uint32_t lastSample = 0;
bool started = false;

int zoneOf(int v) {
  int z = (int)((long)v * zones / (kAdcMax + 1));
  return z >= zones ? zones - 1 : z;
}

void push(int v) {
  if (filled < POT_AVG_SAMPLES) {
    samples[filled++] = v;
    sum += v;
  } else {
    sum += v - samples[head];
    samples[head] = v;
    head = (head + 1) % POT_AVG_SAMPLES;
  }
}

}  // namespace

void potBegin(uint32_t now, int faceCount) {
  zones = faceCount < 1 ? 1 : faceCount;
  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);
  pinMode(POT_PIN, INPUT);
  filled = head = 0;
  sum = 0;
  for (int i = 0; i < POT_AVG_SAMPLES; i++) push(analogRead(POT_PIN));
  current = zoneOf((int)(sum / filled));
  lastSample = now;
  started = true;
  Serial.printf("Potentiometer    wiper GPIO %d  %d faces\n", POT_PIN, zones);
}

int potPoll(uint32_t now) {
  if (!started || (uint32_t)(now - lastSample) < POT_SAMPLE_MS) return -1;
  lastSample = now;
  push(analogRead(POT_PIN));

  int avg = (int)(sum / filled);
  // Current zone spans [lo, hi). Move only once avg clears that span by the dead band.
  int lo = (int)((long)current * (kAdcMax + 1) / zones);
  int hi = (int)((long)(current + 1) * (kAdcMax + 1) / zones);
  if (avg >= lo - POT_DEADBAND && avg < hi + POT_DEADBAND) return -1;
  current = zoneOf(avg);
  return current;
}
