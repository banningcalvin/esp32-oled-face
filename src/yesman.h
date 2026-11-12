#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "face_state.h"

// Face id "yesman".
// An original monochrome screen-face: monitor bezel, round eyes, wide grin.
// Always moving (blink, grin twitch, scanline, flicker). On a prompt-complete
// notify the grin eases wider and a bubble shows "got a new reply" plus the
// raw JSON body, scrolled so the body is not clipped to one line.
//
// Boot selection is not decided here. Callers leave this face unselected
// unless config (or a notify "face" field) names "yesman".

class YesManFace {
 public:
  // Raw POST bodies longer than this are truncated for the bubble.
  static constexpr size_t kBodyCap = 4096;

  explicit YesManFace(Adafruit_SSD1306& display);

  static const char* id() { return "yesman"; }

  // Perk up and show jsonBody in the bubble. The pointer is copied.
  void onNotify(const char* jsonBody, uint32_t now, uint32_t holdMs);

  // Switch-friendly alias. `text` is shown in the bubble if that is all
  // the caller has; prefer onNotify() with the raw JSON body.
  void trigger(const char* text, uint32_t now, uint32_t holdMs);

  void setBanner(const char* text, uint32_t untilMs);
  void updateAndDraw(uint32_t now, FaceState state);

  bool selected() const { return selected_; }
  void setSelected(bool on) { selected_ = on; }

  // Route one POST /notify.
  // faceField "yesman" selects this face and consumes the notify.
  // faceField "idle" or any other non-empty id deselects it and returns false.
  // An empty faceField consumes the notify only when this face is already selected.
  bool claim(const char* faceField, const char* jsonBody, uint32_t now, uint32_t holdMs);

 private:
  static constexpr int kCols = 16;
  static constexpr uint32_t kFrameMs = 33;

  enum class BlinkPhase : uint8_t { Open, Closing, Shut, Opening };

  void step(uint32_t now);
  void draw(uint32_t now);
  void storeBody(const char* json);
  int countLines() const;
  bool copyLine(int want, char* out, size_t outLen) const;
  void advanceBlink(uint32_t now, bool eager);
  void pix(int x, int y);
  void fillEllipse(int cx, int cy, int rx, int ry);
  void screw(int x, int y);

  Adafruit_SSD1306* disp;
  BlinkPhase blinkPhase = BlinkPhase::Open;
  uint32_t blinkPhaseStart = 0;
  uint32_t nextBlinkAt = 0;
  uint32_t nextGlanceAt = 0;
  uint32_t lastUpdate = 0;
  uint32_t lastDraw = 0;
  uint32_t perkUntil = 0;
  uint32_t scrollEpoch = 0;
  uint32_t bannerUntil = 0;
  float lookX = 0;
  float lookY = 0;
  float lookTargetX = 0;
  float lookTargetY = 0;
  float perk = 0;
  float slp = 1.0f;
  float talk = 0;
  float breathPh = 0;
  FaceState state = FaceState::Sleeping;
  float perkTarget = 0;
  float scrollPxPerMs = 0.014f;
  bool started = false;
  bool selected_ = false;
  int clipBottom = 61;
  char body_[kBodyCap] = {0};
  uint16_t bodyLen_ = 0;
  char banner_[21] = {0};
};
