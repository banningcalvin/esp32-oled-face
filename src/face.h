#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "face_state.h"

// Always-moving face for a 128x64 SSD1306.
// Idle breathes, blinks, and glances. trigger() eases into a wider,
// perkier expression, holds it, then eases back. Bottom text stays
// in the lowest 8 pixels so it cannot cover the eyes.
class Face {
 public:
  explicit Face(Adafruit_SSD1306& display);

  // Wake the alert expression. Empty text uses DEFAULT_ALERT_TEXT.
  // A new trigger restarts the hold and replaces the bottom line.
  void trigger(const char* text, uint32_t now, uint32_t holdMs);

  // Status line that does not change the expression (boot IP, Wi-Fi).
  // Shown only while the alert expression is down, until untilMs.
  void setBanner(const char* text, uint32_t untilMs);

  // Advance motion and paint one frame. Safe to call as fast as loop() runs;
  // the panel itself is refreshed about 30 times a second.
  void updateAndDraw(uint32_t now, FaceState state);

 private:
  enum class BlinkPhase : uint8_t { Open, Closing, Shut, Opening };

  void step(uint32_t now);
  void draw(uint32_t now);
  void advanceBlink(uint32_t now);
  void pickGlance(uint32_t now, bool attentive);
  float eyeOpenAmount(uint32_t now) const;
  void drawEye(int cx, int cy, float openAmount, float alert, float pop, int bob,
               float lookXOff, float lookYOff, uint32_t now, bool leftEye);
  void drawSmile(int cx, int y, int halfW, int depth);

  Adafruit_SSD1306* disp;
  BlinkPhase blinkPhase = BlinkPhase::Open;
  uint32_t blinkPhaseStart = 0;
  uint32_t nextBlinkAt = 0;
  uint32_t nextGlanceAt = 0;
  uint32_t lastUpdate = 0;
  uint32_t lastDraw = 0;
  uint32_t alertUntil = 0;
  uint32_t bannerUntil = 0;
  float lookX = 0;
  float lookY = 0;
  float lookTargetX = 0;
  float lookTargetY = 0;
  float smile = 0.35f;
  float alertShown = 0;
  float alertTarget = 0;
  float pop = 0;
  float slp = 1.0f;
  float breathPh = 0;
  FaceState state = FaceState::Sleeping;
  bool started = false;
  char alertText[21] = {0};
  char bannerText[21] = {0};
};
