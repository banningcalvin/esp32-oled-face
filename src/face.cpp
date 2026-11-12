#include "face.h"

#include "config.h"
#include "frame.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kTextHeight = 0;
constexpr int kLeftEyeX = 36;
constexpr int kRightEyeX = 92;
constexpr uint32_t kFrameMs = 33;

int iround(float v) {
  return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

float approach(float current, float goal, float dt, float speed) {
  float k = 1.0f - expf(-speed * dt);
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  return current + (goal - current) * k;
}

float smoothStep(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// Keep a single short ASCII line. Non-ASCII bytes are dropped so a bad
// payload cannot paint garbage over the face.
void copyLine(char* dest, size_t destLen, const char* src, const char* fallback) {
  if (destLen == 0) return;
  if (src == nullptr) src = "";

  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < destLen; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (c < 32 || c > 126) continue;
    dest[j++] = (char)c;
  }
  dest[j] = '\0';

  while (j > 0 && dest[j - 1] == ' ') {
    dest[--j] = '\0';
  }
  size_t start = 0;
  while (dest[start] == ' ') start++;
  if (start > 0) {
    memmove(dest, dest + start, strlen(dest + start) + 1);
  }

  if (dest[0] == '\0') {
    if (fallback == nullptr) fallback = "";
    strncpy(dest, fallback, destLen - 1);
    dest[destLen - 1] = '\0';
  }
}

bool reached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

}  // namespace

Face::Face(Adafruit_SSD1306& display) : disp(&display) {}

void Face::trigger(const char* text, uint32_t now, uint32_t holdMs) {
  copyLine(alertText, sizeof alertText, text, DEFAULT_ALERT_TEXT);
  alertUntil = now + holdMs;
  alertTarget = 1.0f;
  pop = 1.0f;
  // Look toward the viewer, then resume small glances from step().
  lookTargetX = 0.0f;
  lookTargetY = -0.2f;
  nextGlanceAt = now + 700;
}

void Face::setBanner(const char* text, uint32_t untilMs) {
  copyLine(bannerText, sizeof bannerText, text, "");
  bannerUntil = untilMs;
  if (bannerText[0] == '\0') bannerUntil = 0;
}

void Face::updateAndDraw(uint32_t now, FaceState st) {
  state = st;
  step(now);
  if (lastDraw != 0 && (uint32_t)(now - lastDraw) < kFrameMs) return;
  lastDraw = now;
  draw(now);
}

void Face::pickGlance(uint32_t now, bool attentive) {
  if (attentive) {
    lookTargetX = (float)random(-25, 26) / 100.0f;
    lookTargetY = (float)random(-20, 16) / 100.0f;
    nextGlanceAt = now + (uint32_t)random(700, 1600);
    return;
  }
  if (random(0, 100) < 40) {
    lookTargetX = (float)random(-85, 86) / 100.0f;
    lookTargetY = (float)random(-40, 41) / 100.0f;
  } else {
    lookTargetX = (float)random(-18, 19) / 100.0f;
    lookTargetY = (float)random(-14, 15) / 100.0f;
  }
  nextGlanceAt = now + (uint32_t)random(1200, 3200);
}

void Face::advanceBlink(uint32_t now) {
  uint32_t elapsed = now - blinkPhaseStart;
  bool attentive = alertTarget > 0.5f;
  switch (blinkPhase) {
    case BlinkPhase::Open:
      if (nextBlinkAt != 0 && reached(now, nextBlinkAt)) {
        blinkPhase = BlinkPhase::Closing;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Closing:
      if (elapsed >= 90) {
        blinkPhase = BlinkPhase::Shut;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Shut:
      if (elapsed >= (attentive ? 45 : 80)) {
        blinkPhase = BlinkPhase::Opening;
        blinkPhaseStart = now;
      }
      break;
    case BlinkPhase::Opening:
      if (elapsed >= 140) {
        blinkPhase = BlinkPhase::Open;
        blinkPhaseStart = now;
        uint32_t gap = attentive ? (uint32_t)random(3400, 7200)
                                 : (uint32_t)random(2000, 5800);
        nextBlinkAt = now + gap;
      }
      break;
  }
}

float Face::eyeOpenAmount(uint32_t now) const {
  float t;
  switch (blinkPhase) {
    case BlinkPhase::Open:
      return 1.0f;
    case BlinkPhase::Closing:
      t = (float)(now - blinkPhaseStart) / 90.0f;
      return 1.0f - smoothStep(t);
    case BlinkPhase::Shut:
      return 0.0f;
    case BlinkPhase::Opening:
      t = (float)(now - blinkPhaseStart) / 140.0f;
      return smoothStep(t);
  }
  return 1.0f;
}

void Face::step(uint32_t now) {
  if (!started) {
    started = true;
    blinkPhase = BlinkPhase::Open;
    blinkPhaseStart = now;
    nextBlinkAt = now + (uint32_t)random(1400, 3200);
    nextGlanceAt = now + 600;
    lastUpdate = now;
    lookTargetX = 0.0f;
    lookTargetY = 0.0f;
  }

  float dt = (float)(now - lastUpdate) / 1000.0f;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.08f) dt = 0.08f;
  lastUpdate = now;

  bool asleep = state == FaceState::Sleeping;
  slp = approach(slp, asleep ? 1.0f : 0.0f, dt, asleep ? 2.2f : 5.0f);
  if (slp < 0.001f) slp = 0.0f;
  if (slp > 0.999f) slp = 1.0f;
  breathPh += dt * (1.55f - 0.85f * slp);
  if (asleep) {
    alertTarget = 0.0f;
    pop = 0.0f;
    lookTargetX = 0.0f;
    lookTargetY = 0.0f;
    nextGlanceAt = now + 1000;
  }

  if (alertTarget > 0.0f && reached(now, alertUntil)) {
    alertTarget = 0.0f;
  }

  float alertSpeed = (alertTarget > alertShown) ? 8.0f : 2.3f;
  alertShown = approach(alertShown, alertTarget, dt, alertSpeed);
  if (alertShown < 0.001f) alertShown = 0.0f;
  if (alertShown > 0.999f) alertShown = 1.0f;

  if (pop > 0.0f) {
    pop -= dt * 3.2f;
    if (pop < 0.0f) pop = 0.0f;
  }

  float smileGoal = (0.32f + alertShown * 0.68f) * (1.0f - 0.45f * slp);
  smile = approach(smile, smileGoal, dt, 4.0f);

  bool attentive = alertShown > 0.45f || alertTarget > 0.5f;
  if (nextGlanceAt == 0 || reached(now, nextGlanceAt)) {
    pickGlance(now, attentive);
  }
  float lookSpeed = attentive ? 6.0f : 3.4f;
  lookX = approach(lookX, lookTargetX, dt, lookSpeed);
  lookY = approach(lookY, lookTargetY, dt, lookSpeed);

  advanceBlink(now);
}

void Face::drawEye(int cx, int cy, float openAmount, float alert, float popAmount,
                   int bob, float lookXOff, float lookYOff, uint32_t now, bool leftEye) {
  // A slow scale pulse on top of the blink, so a resting face still changes
  // every frame even between glances.
  float pulse = 0.5f + 0.5f * sinf((float)now * 0.00155f);
  float rwF = 16.0f + pulse * 0.7f + alert * 2.4f + popAmount * 1.6f;
  float rhF = (11.0f + alert * 3.4f + popAmount * 1.4f) * openAmount;
  int rw = iround(rwF);
  int rh = iround(rhF);
  if (rw < 4) rw = 4;
  if (rh < 1) rh = 1;

  int eyeY = cy + bob;
  int x = cx - rw;
  int y = eyeY - rh;
  int w = rw * 2;
  int h = rh * 2;
  int radius = rh < rw ? rh : rw;
  if (radius < 1) radius = 1;

  // Keep the white of the eye above the caption band.
  int lowest = OLED_HEIGHT - kTextHeight - 6;
  if (y + h > lowest) {
    h = lowest - y;
    if (h < 2) h = 2;
    radius = h / 2;
    if (radius < 1) radius = 1;
  }
  if (y < 0) y = 0;

  disp->fillRoundRect(x, y, w, h, radius, SSD1306_WHITE);

  // Lid almost shut: a slit only, otherwise the pupil pokes out.
  if (openAmount < 0.42f || h < 8) return;

  float pr = 2.5f + alert * 1.1f;
  if (pr > (float)(h / 2 - 2)) pr = (float)(h / 2 - 2);
  if (pr < 2.0f) pr = 2.0f;

  float maxX = (float)rw - pr - 2.0f;
  float maxY = (float)(h / 2) - pr - 2.0f;
  if (maxX < 0) maxX = 0;
  if (maxY < 0) maxY = 0;

  // Tiny continuous wobble, plus the eased glance.
  float wobbleX = 0.06f * sinf((float)now * 0.0022f + (leftEye ? 0.4f : 1.1f));
  float wobbleY = 0.08f * sinf((float)now * 0.0018f + 0.6f);
  float px = (float)cx + (lookXOff + wobbleX) * maxX;
  float py = (float)(y + h / 2) + (lookYOff + wobbleY) * maxY;
  int pupilR = iround(pr);
  disp->fillCircle(iround(px), iround(py), pupilR, SSD1306_BLACK);

  int gx = iround(px) - 1;
  int gy = iround(py) - 1;
  disp->drawPixel(gx, gy, SSD1306_WHITE);
  if (pupilR >= 3) disp->drawPixel(gx - 1, gy, SSD1306_WHITE);
}

void Face::drawSmile(int cx, int y, int halfW, int depth) {
  if (halfW < 2 || depth < 1) return;
  int lipLimit = OLED_HEIGHT - kTextHeight - 2;
  for (int x = -halfW; x <= halfW; ++x) {
    float t = (float)x / (float)halfW;
    int dy = iround((1.0f - t * t) * (float)depth);
    int py = y + dy;
    if (py > lipLimit) py = lipLimit;
    if (py < 0) continue;
    int px = cx + x;
    if (px < 0 || px >= OLED_WIDTH) continue;
    disp->drawPixel(px, py, SSD1306_WHITE);
    if (py + 1 <= lipLimit) disp->drawPixel(px, py + 1, SSD1306_WHITE);
  }
}

void Face::draw(uint32_t now) {
  disp->clearDisplay();

  float alert = alertShown;
  float open = eyeOpenAmount(now) * (1.0f - slp);
  bool talking = state == FaceState::Talking;
  // Two stacked sines so the bob does not tick like a metronome.
  float bobF = (sinf(breathPh) * 1.35f + sinf(breathPh * 1.7f) * 0.45f) * (1.0f - 0.35f * slp);
  if (talking) bobF *= 1.6f;
  int bob = iround(bobF);

  int eyeY = 22;
  drawEye(kLeftEyeX, eyeY, open, alert, pop, bob, lookX, lookY, now, true);
  drawEye(kRightEyeX, eyeY, open, alert, pop, bob, lookX, lookY, now, false);

  // Brows lift with the alert and droop a pixel at the outside while idle.
  // Brows stay at the open-eye height during a blink so they don't dive
  // into the slit. `open` still drives the eye itself in drawEye().
  float pulse = 0.5f + 0.5f * sinf((float)now * 0.00155f);
  int rw = iround(16.0f + pulse * 0.7f + alert * 2.4f + pop * 1.6f);
  int rh = iround(11.0f + alert * 3.4f + pop * 1.4f);
  if (rh < 4) rh = 4;
  int outerDroop = iround((1.0f - alert) * 2.0f);
  int lift = iround(alert * 4.0f + pop * 2.0f);
  int browBase = eyeY + bob - rh - 4 - lift;
  if (browBase < 0) browBase = 0;

  auto brow = [&](int cx, bool leftEye) {
    int yOuter = browBase + outerDroop;
    int yInner = browBase;
    int xOuter = leftEye ? cx - rw + 2 : cx + rw - 2;
    int xInner = leftEye ? cx + rw - 4 : cx - rw + 4;
    if (yOuter > OLED_HEIGHT - kTextHeight - 8) return;
    disp->drawLine(xOuter, yOuter, xInner, yInner, SSD1306_WHITE);
    disp->drawLine(xOuter, yOuter + 1, xInner, yInner + 1, SSD1306_WHITE);
  };
  brow(kLeftEyeX, true);
  brow(kRightEyeX, false);

  float chat = talking ? fabsf(sinf((float)now * 0.0155f)) : 0.0f;
  int mouthHalf = iround(14.0f + alert * 4.0f - chat * 3.0f);
  int mouthDepth = iround(2.0f + smile * 5.0f + chat * 3.0f + 0.6f * sinf(breathPh));
  if (mouthDepth < 1) mouthDepth = 1;
  int mouthY = 45 + bob;
  drawSmile(OLED_WIDTH / 2, mouthY, mouthHalf, mouthDepth);

  gFrameDrawn = true;
}
