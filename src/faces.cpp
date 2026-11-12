#include "faces.h"

#include "config.h"
#include "frame.h"

#include <math.h>
#include <string.h>

Faces::Faces(Adafruit_SSD1306& display) : disp(&display), idle(display), smoker(display), yesman(display) {}

void Faces::begin(uint32_t now) {
  (void)now;
  which = Which::Idle;
  if (!setFaceById(BOOT_FACE)) {
    Serial.printf("BOOT_FACE \"%s\" is unknown, using idle\n", BOOT_FACE);
  }
  Serial.printf("Boot face        %s\n", faceId());
}

bool Faces::setFaceById(const char* id) {
  if (id == nullptr || id[0] == '\0') return false;
  Which next;
  if (strcmp(id, FACE_IDLE) == 0) next = Which::Idle;
  else if (strcmp(id, FACE_SMOKER) == 0) next = Which::Smoker;
  else if (strcmp(id, FACE_YESMAN) == 0) next = Which::YesMan;
  else return false;
  which = next;
  latched = next;
  knobOverride = false;
  return true;
}

namespace {
// Registration order. Keep in step with setFaceById and faceId.
constexpr int kCount = 3;
}  // namespace

int Faces::count() { return kCount; }

int Faces::index() const {
  if (which == Which::Smoker) return 1;
  if (which == Which::YesMan) return 2;
  return 0;
}

void Faces::selectIndex(int idx) {
  static constexpr Which kOrder[kCount] = {Which::Idle, Which::Smoker, Which::YesMan};
  if (idx < 0) idx = 0;
  if (idx >= kCount) idx = kCount - 1;
  knobOverride = true;
  if (kOrder[idx] == which) return;
  which = kOrder[idx];
  Serial.printf("Face             %s\n", faceId());
}

void Faces::releaseKnobOverride() {
  if (!knobOverride) return;
  knobOverride = false;
  if (which == latched) return;
  which = latched;
  Serial.printf("Face             %s\n", faceId());
}

const char* Faces::faceId() const {
  if (which == Which::Smoker) return FACE_SMOKER;
  if (which == Which::YesMan) return FACE_YESMAN;
  return FACE_IDLE;
}

bool gFrameDrawn = false;

namespace {
constexpr uint32_t kWakeMs = 800;
bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
constexpr int kStripH = 16;
constexpr int kScrollPxPerSec = 45;
constexpr size_t kFitChars = 21;
}  // namespace

void Faces::setUnread(uint16_t n, uint32_t now) {
  if (n == 0) {
    unread = 0;
    state = FaceState::Sleeping;
    pending = false;
    msgUntil = 0;
    wakeUntil = 0;
    return;
  }
  if (unread == 0) {
    state = FaceState::Awake;
    wakeUntil = now + kWakeMs;
  }
  unread = n;
}

void Faces::trigger(const char* caption, const char* rawBody, uint32_t now, uint32_t holdMs) {
  if (wakeUntil != 0 && !reached(now, wakeUntil)) {
    pendCaption = caption != nullptr ? caption : "";
    pendBody = rawBody != nullptr ? rawBody : "";
    pendHold = holdMs;
    pending = true;
    return;
  }
  pending = false;
  fire(now, caption, rawBody, holdMs);
}

void Faces::fire(uint32_t now, const char* caption, const char* rawBody, uint32_t holdMs) {
  size_t n = 0;
  if (which == Which::Idle) {
    const char* t = (caption != nullptr && caption[0] != '\0') ? caption : DEFAULT_ALERT_TEXT;
    snprintf(msg, sizeof msg, "%s", t);
    n = strlen(msg);
  } else {
    n = (size_t)snprintf(msg, sizeof msg, "got a new reply ");
    bool gap = false;
    for (const char* p = rawBody; p != nullptr && *p != '\0' && n + 1 < sizeof msg; ++p) {
      unsigned char ch = (unsigned char)*p;
      if (ch <= ' ' || ch > 126) {
        gap = true;
        continue;
      }
      if (gap) msg[n++] = ' ';
      gap = false;
      if (n + 1 < sizeof msg) msg[n++] = (char)ch;
    }
    msg[n] = '\0';
  }
  uint32_t hold = holdMs;
  if (n > kFitChars) {
    uint32_t loop = ((uint32_t)n * 6u + OLED_WIDTH) * 1000u / kScrollPxPerSec;
    if (loop > hold) hold = loop;
  }
  msgStart = now;
  msgUntil = now + hold;
  state = FaceState::Talking;

  if (which == Which::Smoker) smoker.trigger(rawBody, now);
  else if (which == Which::YesMan) yesman.onNotify(rawBody, now, holdMs);
  else idle.trigger(caption, now, holdMs);
}

void Faces::setBanner(const char* text, uint32_t untilMs) {
  snprintf(banner, sizeof banner, "%s", text != nullptr ? text : "");
  bannerUntil = banner[0] != '\0' ? untilMs : 0;
}

void Faces::drawStrip(uint32_t now) {
  disp->setTextSize(1);
  disp->setTextColor(SSD1306_WHITE);
  disp->setTextWrap(false);
  const char* line = nullptr;
  bool alert = msgUntil != 0 && (int32_t)(msgUntil - now) > 0;
  if (alert) line = msg;
  else if (bannerUntil != 0 && (int32_t)(bannerUntil - now) > 0) line = banner;
  if (line != nullptr) {
    disp->fillRect(0, 0, OLED_WIDTH, kStripH, SSD1306_BLACK);
    int len = (int)strlen(line);
    int x = 0;
    if (len > (int)kFitChars) {
      int loop = len * 6 + OLED_WIDTH;
      x = OLED_WIDTH - (int)(((now - msgStart) * kScrollPxPerSec / 1000u) % (uint32_t)loop);
    }
    disp->setCursor(x, 4);
    disp->print(line);
  }
}

void Faces::drawBubbleIcon(uint32_t now) {
  if (which == Which::Idle || msgUntil == 0 || (int32_t)(msgUntil - now) <= 0) return;
  const int x = 106, y = 18;
  disp->fillRect(x, y, 18, 12, SSD1306_BLACK);
  disp->drawRoundRect(x, y, 18, 10, 3, SSD1306_WHITE);
  disp->drawLine(x + 4, y + 10, x + 4, y + 12, SSD1306_WHITE);
  disp->drawLine(x + 4, y + 12, x + 7, y + 9, SSD1306_WHITE);
  int dots = (int)((now / 250) % 4);
  for (int i = 0; i < 3; ++i) {
    if (i < dots) disp->fillRect(x + 4 + i * 4, y + 4, 2, 2, SSD1306_WHITE);
  }
}

void Faces::drawZs(uint32_t now) {
  const uint32_t period = 3600;
  for (int i = 0; i < 3; ++i) {
    uint32_t t = (now + (uint32_t)i * (period / 3)) % period;
    float p = (float)t / (float)period;
    int x = 104 + (int)(p * 8.0f) + (int)(2.0f * sinf(p * 6.28f));
    int y = 46 - (int)(p * 26.0f);
    disp->drawChar(x, y, 'z', SSD1306_WHITE, SSD1306_BLACK, p < 0.5f ? 1 : 2);
  }
}

void Faces::updateAndDraw(uint32_t now) {
  gFrameDrawn = false;
  if (pending && reached(now, wakeUntil)) {
    pending = false;
    fire(now, pendCaption.c_str(), pendBody.c_str(), pendHold);
  }
  if (state == FaceState::Talking && msgUntil != 0 && reached(now, msgUntil)) {
    state = unread > 0 ? FaceState::Awake : FaceState::Sleeping;
  }
  if (which == Which::Smoker) smoker.updateAndDraw(now, state);
  else if (which == Which::YesMan) yesman.updateAndDraw(now, state);
  else idle.updateAndDraw(now, state);
  if (!gFrameDrawn) return;

  drawStrip(now);
  if (state == FaceState::Sleeping) drawZs(now);
  drawBubbleIcon(now);
  disp->display();
}
