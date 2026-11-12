#include "yesman.h"

#include "config.h"
#include "frame.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

float approach(float current, float goal, float dt, float speed) {
  float k = 1.0f - expf(-speed * dt);
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  return current + (goal - current) * k;
}

float blend(float a, float b, float t) { return a + (b - a) * t; }

int iround(float v) { return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f); }

bool reached(uint32_t now, uint32_t target) { return (int32_t)(now - target) >= 0; }

}  // namespace

YesManFace::YesManFace(Adafruit_SSD1306& display) : disp(&display) {}

void YesManFace::storeBody(const char* json) {
  bodyLen_ = 0;
  body_[0] = '\0';
  if (json == nullptr) return;
  for (size_t i = 0; json[i] != '\0' && bodyLen_ + 1 < kBodyCap; ++i) {
    unsigned char c = (unsigned char)json[i];
    if (c == '\r') continue;
    if (c == '\n' || (c >= 32 && c <= 126)) {
      body_[bodyLen_++] = (char)c;
    } else {
      body_[bodyLen_++] = '?';
    }
  }
  body_[bodyLen_] = '\0';
}

int YesManFace::countLines() const {
  if (bodyLen_ == 0) return 0;
  int lines = 1;
  int col = 0;
  for (size_t i = 0; i < bodyLen_; ++i) {
    char c = body_[i];
    if (c == '\n') {
      if (i + 1 < bodyLen_) lines++;
      col = 0;
      continue;
    }
    col++;
    if (col == kCols) {
      col = 0;
      if (i + 1 < bodyLen_ && body_[i + 1] != '\n') lines++;
    }
  }
  return lines;
}

bool YesManFace::copyLine(int want, char* out, size_t outLen) const {
  if (outLen == 0) return false;
  out[0] = '\0';
  if (want < 0 || bodyLen_ == 0) return false;

  int line = 0;
  size_t col = 0;
  for (size_t i = 0; i < bodyLen_; ++i) {
    char c = body_[i];
    if (c == '\n') {
      if (line == want) {
        out[col < outLen ? col : outLen - 1] = '\0';
        return true;
      }
      if (i + 1 >= bodyLen_) return false;
      line++;
      col = 0;
      continue;
    }
    if (line == want && col + 1 < outLen) out[col] = c;
    col++;
    if (col == (size_t)kCols) {
      if (line == want) {
        size_t end = col < outLen ? col : outLen - 1;
        out[end] = '\0';
        return true;
      }
      bool more = (i + 1 < bodyLen_ && body_[i + 1] != '\n');
      col = 0;
      if (more) line++;
    }
  }
  if (line == want) {
    out[col < outLen ? col : outLen - 1] = '\0';
    return true;
  }
  return false;
}

void YesManFace::onNotify(const char* jsonBody, uint32_t now, uint32_t holdMs) {
  storeBody(jsonBody);
  int lines = countLines();
  // Full perk shows two JSON rows. Extra rows travel past that window
  // once before the grin eases back. Speed rises for a long body so the
  // tail is not still off-screen when the hold ends.
  int extra = lines - 2;
  if (extra < 0) extra = 0;
  int px = extra * 8;
  uint32_t budget = holdMs;
  if (budget < 6000u) budget = 6000u;
  if (budget > 28000u) budget = 28000u;
  const uint32_t pauses = 1600u;
  uint32_t travelBudget = budget > pauses + 800u ? budget - pauses : 800u;
  if (px <= 0) {
    scrollPxPerMs = 0.014f;
  } else {
    scrollPxPerMs = (float)px / (float)travelBudget;
    if (scrollPxPerMs < 0.012f) scrollPxPerMs = 0.012f;
  }
  uint32_t travel = px <= 0 ? 0u : (uint32_t)((float)px / scrollPxPerMs);
  uint32_t need = pauses + travel + 500u;
  if (need < holdMs) need = holdMs;
  if (need < 4500u) need = 4500u;
  perkUntil = now + need;
  perkTarget = 1.0f;
  scrollEpoch = now;
  lookTargetX = 0.0f;
  lookTargetY = -0.15f;
  nextGlanceAt = now + 600;
}

void YesManFace::trigger(const char* text, uint32_t now, uint32_t holdMs) {
  onNotify(text, now, holdMs);
}

void YesManFace::setBanner(const char* text, uint32_t untilMs) {
  banner_[0] = '\0';
  bannerUntil = 0;
  if (text == nullptr) return;
  size_t j = 0;
  for (size_t i = 0; text[i] != '\0' && j + 1 < sizeof banner_; ++i) {
    unsigned char c = (unsigned char)text[i];
    if (c < 32 || c > 126) continue;
    banner_[j++] = (char)c;
  }
  banner_[j] = '\0';
  if (banner_[0] == '\0') return;
  bannerUntil = untilMs;
}

bool YesManFace::claim(const char* faceField, const char* jsonBody, uint32_t now,
                       uint32_t holdMs) {
  if (faceField != nullptr && faceField[0] != '\0') {
    if (strcmp(faceField, id()) == 0) {
      selected_ = true;
    } else {
      selected_ = false;
      return false;
    }
  }
  if (!selected_) return false;
  onNotify(jsonBody, now, holdMs);
  return true;
}

void YesManFace::advanceBlink(uint32_t now, bool eager) {
  uint32_t elapsed = now - blinkPhaseStart;
  switch (blinkPhase) {
    case BlinkPhase::Open:
      if (nextBlinkAt != 0 && reached(now, nextBlinkAt)) {
        blinkPhase = BlinkPhase::Closing;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Closing:
      if (elapsed >= 80) {
        blinkPhase = BlinkPhase::Shut;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Shut:
      if (elapsed >= (eager ? 40 : 70)) {
        blinkPhase = BlinkPhase::Opening;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Opening:
      if (elapsed >= 130) {
        blinkPhase = BlinkPhase::Open;
        blinkPhaseStart = now;
        uint32_t gap = eager ? (uint32_t)random(2200, 4800) : (uint32_t)random(1600, 4200);
        nextBlinkAt = now + gap;
      }
      break;
  }
}

void YesManFace::step(uint32_t now) {
  if (!started) {
    started = true;
    blinkPhase = BlinkPhase::Open;
    blinkPhaseStart = now;
    nextBlinkAt = now + (uint32_t)random(700, 1800);
    nextGlanceAt = now + 400;
    lastUpdate = now;
  }

  float dt = (float)(now - lastUpdate) / 1000.0f;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.08f) dt = 0.08f;
  lastUpdate = now;

  bool asleep = state == FaceState::Sleeping;
  slp = approach(slp, asleep ? 1.0f : 0.0f, dt, asleep ? 2.2f : 5.0f);
  if (slp < 0.001f) slp = 0.0f;
  if (slp > 0.999f) slp = 1.0f;
  talk = approach(talk, state == FaceState::Talking ? 1.0f : 0.0f, dt, 6.0f);
  breathPh += dt * (1.6f - 0.9f * slp);
  if (asleep) {
    perkTarget = 0.0f;
    lookTargetX = 0.0f;
    lookTargetY = 0.0f;
    nextGlanceAt = now + 1000;
  }

  if (perkTarget > 0.0f && reached(now, perkUntil)) perkTarget = 0.0f;
  float speed = (perkTarget > perk) ? 7.5f : 2.4f;
  perk = approach(perk, perkTarget, dt, speed);
  if (perk < 0.001f) perk = 0.0f;
  if (perk > 0.999f) perk = 1.0f;

  bool eager = perk > 0.45f || perkTarget > 0.5f;
  if (nextGlanceAt == 0 || reached(now, nextGlanceAt)) {
    if (eager) {
      lookTargetX = (float)random(-18, 19) / 100.0f;
      lookTargetY = (float)random(-12, 10) / 100.0f;
      nextGlanceAt = now + (uint32_t)random(500, 1200);
    } else {
      lookTargetX = (float)random(-55, 56) / 100.0f;
      lookTargetY = (float)random(-30, 31) / 100.0f;
      nextGlanceAt = now + (uint32_t)random(1100, 2800);
    }
  }
  float lookSpeed = eager ? 6.0f : 3.2f;
  lookX = approach(lookX, lookTargetX, dt, lookSpeed);
  lookY = approach(lookY, lookTargetY, dt, lookSpeed);
  advanceBlink(now, eager);
}

void YesManFace::pix(int x, int y) {
  if (x < 6 || x > 121 || y < 4 || y > 60) return;
  if (y >= clipBottom) return;
  disp->drawPixel(x, y, SSD1306_WHITE);
}

void YesManFace::fillEllipse(int cx, int cy, int rx, int ry) {
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  disp->fillEllipse(cx, cy, rx, ry, SSD1306_WHITE);
}

void YesManFace::screw(int x, int y) {
  disp->drawCircle(x, y, 2, SSD1306_WHITE);
  disp->drawPixel(x, y, SSD1306_WHITE);
}

void YesManFace::draw(uint32_t now) {
  disp->clearDisplay();
  disp->drawRoundRect(1, 0, 126, 64, 8, SSD1306_WHITE);
  disp->drawRoundRect(3, 2, 122, 60, 6, SSD1306_WHITE);

  int textTop = iround(blend(66.0f, 34.0f, perk));
  bool bubble = false;
  clipBottom = bubble ? textTop - 1 : 61;

  bool bannerOn = false;
  if (bannerOn && clipBottom > 53) clipBottom = 53;

  screw(10, 7);
  screw(117, 7);
  if (!bubble && !bannerOn) {
    screw(10, 56);
    screw(117, 56);
  }

  // CRT scanline and a short flicker live in the black, under the features,
  // so a resting frame still changes. Features paint over them.
  bool calm = slp > 0.5f;
  int scanLimit = clipBottom < 60 ? clipBottom : 60;
  int scan = 5 + (int)((now / 30) % 52);
  if (!calm && scan >= 5 && scan < scanLimit) {
    for (int x = 8; x < 120; x += 2) disp->drawPixel(x, scan, SSD1306_WHITE);
  }
  bool flicker = !calm && (now % 2800u) < 55u;
  if (flicker) {
    int y0 = 8 + (int)((now / 40) % 22);
    for (int y = y0; y < y0 + 2 && y < scanLimit; ++y) {
      for (int x = 8; x < 120; x += 3) disp->drawPixel(x, y, SSD1306_WHITE);
    }
  }

  int jit = flicker ? 1 : 0;
  float open = 1.0f;
  uint32_t elapsed = now - blinkPhaseStart;
  switch (blinkPhase) {
    case BlinkPhase::Open:
      open = 1.0f;
      break;
    case BlinkPhase::Closing:
      open = 1.0f - (float)elapsed / 80.0f;
      break;
    case BlinkPhase::Shut:
      open = 0.0f;
      break;
    case BlinkPhase::Opening:
      open = (float)elapsed / 130.0f;
      break;
  }
  if (open < 0.0f) open = 0.0f;
  if (open > 1.0f) open = 1.0f;
  open *= (1.0f - slp);
  int breathY = iround(sinf(breathPh) * (0.4f + 0.8f * slp));

  uint32_t kickPhase = now % 2600u;
  float kickAmp = 0.0f;
  if (kickPhase < 220u && !calm) kickAmp = sinf((float)kickPhase / 220.0f * 3.14159265f) * 1.4f;
  float twitch = sinf((float)now * 0.005f) * 1.2f * (1.0f - slp);
  float half = blend(33.0f, 40.0f, perk) + twitch;
  int hw = iround(half);
  if (hw < 12) hw = 12;
  int corner = iround(blend(40.0f, 18.0f, perk));
  float lowerDrop = blend(11.0f, 9.0f, perk) + 0.6f * sinf((float)now * 0.0033f) * (1.0f - slp);
  lowerDrop += talk * fabsf(sinf((float)now * 0.014f)) * 4.0f;
  lowerDrop -= slp * 3.0f;
  int smileBot = corner + 3 + iround(lowerDrop) + 2;
  int limit = clipBottom - 1;
  if (smileBot > limit) corner -= smileBot - limit;
  if (corner < 16) {
    int lift = 16 - corner;
    corner = 16;
    lowerDrop -= (float)lift;
    if (lowerDrop < 4.0f) lowerDrop = 4.0f;
  }

  int cx = 64 + jit;
  for (int xoff = -hw; xoff <= hw; ++xoff) {
    float t = (float)xoff / (float)hw;
    float u = 1.0f - t * t;
    int kick = 0;
    if (xoff > 0) kick = iround(kickAmp * ((float)xoff / (float)hw));
    int upper = iround((float)corner + u * 2.0f) - kick;
    int lower = iround((float)corner + 3.0f + u * lowerDrop) - kick;
    if (lower < upper + 3) lower = upper + 3;
    pix(cx + xoff, upper);
    pix(cx + xoff, upper + 1);
    pix(cx + xoff, lower);
    pix(cx + xoff, lower + 1);
    if (xoff <= -hw + 1 || xoff >= hw - 1) {
      for (int y = upper; y <= lower + 1; ++y) pix(cx + xoff, y);
    }
    int opening = lower - (upper + 2);
    if (opening >= 4 && t > -0.78f && t < 0.78f && ((xoff + hw) % 4) != 3) {
      int th = opening - 1;
      if (th > 3) th = 3;
      for (int y = upper + 2; y < upper + 2 + th; ++y) pix(cx + xoff, y);
    }
  }

  int eyeRx = iround(blend(13.0f, 14.0f, perk));
  int eyeRyFull = iround(blend(10.0f, 6.0f, perk));
  int eyeRy = iround((float)eyeRyFull * open);
  if (eyeRy < 1) eyeRy = 1;
  int eyeY = iround(blend(24.0f, 11.0f, perk)) + breathY;
  int pupilR = eyeRy >= 6 ? 3 : 2;
  float wobX = 0.35f * sinf((float)now * 0.0022f);
  float wobY = 0.22f * sinf((float)now * 0.0018f + 0.4f);
  int lookXi = iround((lookX + wobX) * 3.0f);
  int lookYi = iround((lookY + wobY) * 2.0f);

  auto eye = [&](int ecx, int ecy) {
    ecx += jit;
    if (open < 0.18f || eyeRy <= 2) {
      disp->drawFastHLine(ecx - eyeRx, ecy, eyeRx * 2 + 1, SSD1306_WHITE);
      disp->drawFastHLine(ecx - eyeRx + 1, ecy + 1, eyeRx * 2 - 1, SSD1306_WHITE);
      return;
    }
    fillEllipse(ecx, ecy, eyeRx, eyeRy);
    if (open < 0.42f) return;
    int pxx = ecx + lookXi;
    int pyy = ecy + lookYi;
    int minX = ecx - eyeRx + pupilR + 1;
    int maxX = ecx + eyeRx - pupilR - 1;
    int minY = ecy - eyeRy + pupilR + 1;
    int maxY = ecy + eyeRy - pupilR - 1;
    if (pxx < minX) pxx = minX;
    if (pxx > maxX) pxx = maxX;
    if (pyy < minY) pyy = minY;
    if (pyy > maxY) pyy = maxY;
    disp->fillCircle(pxx, pyy, pupilR, SSD1306_BLACK);
    disp->drawPixel(pxx - 1, pyy - 2, SSD1306_WHITE);
    disp->drawPixel(pxx, pyy - 2, SSD1306_WHITE);
  };
  eye(42, eyeY);
  eye(86, eyeY - 1);

  int browY = eyeY - eyeRy - 3 - iround(perk * 2.0f);
  if (browY >= 4 && perk < 0.65f) {
    int droop = iround((1.0f - perk) * 3.0f);
    disp->drawLine(30, browY + droop, 54, browY, SSD1306_WHITE);
    disp->drawLine(30, browY + droop + 1, 54, browY + 1, SSD1306_WHITE);
    disp->drawLine(74, browY, 98, browY + droop, SSD1306_WHITE);
    disp->drawLine(74, browY + 1, 98, browY + droop + 1, SSD1306_WHITE);
  }

  gFrameDrawn = true;
}

void YesManFace::updateAndDraw(uint32_t now, FaceState st) {
  state = st;
  step(now);
  if (lastDraw != 0 && (uint32_t)(now - lastDraw) < kFrameMs) return;
  lastDraw = now;
  draw(now);
}
