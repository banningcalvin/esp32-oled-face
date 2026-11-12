#include "button.h"

#include <Arduino.h>

#include "config.h"

namespace {
bool stable = true;
bool lastRaw = true;
uint32_t rawSince = 0;
}  // namespace

void buttonBegin() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(5);
  stable = lastRaw = digitalRead(BUTTON_PIN) != LOW;
  rawSince = millis();
  Serial.printf("Button           GPIO %d  reads %s at boot (idle should be HIGH)\n", BUTTON_PIN,
                stable ? "HIGH" : "LOW");
}

bool buttonHeld() { return !stable; }

bool buttonPressed(uint32_t now) {
  bool raw = digitalRead(BUTTON_PIN) != LOW;
  if (raw != lastRaw) {
    lastRaw = raw;
    rawSince = now;
    Serial.printf("Button raw       %s\n", raw ? "HIGH (released)" : "LOW (pressed)");
  }
  if (raw != stable && (int32_t)(now - rawSince) >= (int32_t)BUTTON_DEBOUNCE_MS) {
    stable = raw;
    return !stable;
  }
  return false;
}
