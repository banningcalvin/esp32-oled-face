#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "face.h"
#include "smoker.h"
#include "yesman.h"

// Selects the face that is on the glass. "idle" is the original face.
// "smoker" is the cigarette face. "yesman" is the screen grin.
// Unknown ids do not change the current one.
class Faces {
 public:
  explicit Faces(Adafruit_SSD1306& display);

  void begin(uint32_t now);

  // True when id is a known face. Applying the current id is a success
  // and does not restart an animation; the caller triggers separately.
  bool setFaceById(const char* id);
  const char* faceId() const;

  // caption is the short idle bottom line. rawBody is the POST body the
  // smoker and yesman bubbles scroll. holdMs is the idle and yesman hold.
  void trigger(const char* caption, const char* rawBody, uint32_t now, uint32_t holdMs);
  void setBanner(const char* text, uint32_t untilMs);
  void setUnread(uint16_t n) { unread = n; }
  void updateAndDraw(uint32_t now);

  // Number of registered faces, in registration order: idle, smoker, yesman.
  static int count();

  // Registration index of the face on the glass.
  int index() const;

  // Select by registration index (clamped). The choice shows immediately and
  // lasts until a notify that does not name a face.
  void selectIndex(int idx);

  // Drop a knob choice and show the latched face again: BOOT_FACE,
  // or the last POST /notify that named a face.
  void releaseKnobOverride();

 private:
  enum class Which : uint8_t { Idle, Smoker, YesMan };

  void drawStrip(uint32_t now);
  void drawBubbleIcon(uint32_t now);

  Adafruit_SSD1306* disp;
  Face idle;
  Smoker smoker;
  YesManFace yesman;
  Which which = Which::Idle;
  Which latched = Which::Idle;
  bool knobOverride = false;
  uint16_t unread = 0;
  uint32_t msgStart = 0;
  uint32_t msgUntil = 0;
  uint32_t bannerUntil = 0;
  char msg[200] = {0};
  char banner[24] = {0};
};
