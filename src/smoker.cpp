#include "smoker.h"

#include "config.h"
#include "frame.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int kCharW = 6;
constexpr int kCharH = 8;
constexpr int kCols = 18;
constexpr uint32_t kFrameMs = 33;
constexpr uint32_t kInhaleMs = 1100;
constexpr uint32_t kWithdrawMs = 920;
constexpr uint32_t kExhaleMs = 1500;
constexpr uint32_t kReturnMs = 980;
constexpr uint32_t kBubbleHoldFitMs = 2800;
constexpr uint32_t kBubbleHoldEndMs = 1100;
constexpr float kScrollPxPerSec = 12.0f;
constexpr char kLead[] = "got a new reply";

static_assert(sizeof(kLead) == 16, "speech-bubble lead-in changed");
static_assert(Smoker::kMaxNotifyBody == 4096, "notify body cap");

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

float blend(float a, float b, float t) { return a + (b - a) * t; }

bool reached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

void copyLine(char* dest, size_t destLen, const char* src) {
  if (destLen == 0) return;
  if (src == nullptr) src = "";
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < destLen; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (c < 32 || c > 126) continue;
    dest[j++] = (char)c;
  }
  dest[j] = '\0';
  while (j > 0 && dest[j - 1] == ' ') dest[--j] = '\0';
  size_t start = 0;
  while (dest[start] == ' ') start++;
  if (start > 0) memmove(dest, dest + start, strlen(dest + start) + 1);
}

void dashH(Adafruit_SSD1306& d, int x, int y, int len, int phase) {
  for (int i = 0; i < len; ++i) {
    if (((i + phase) % 6) < 4) d.drawPixel(x + i, y, SSD1306_WHITE);
  }
}

void dashV(Adafruit_SSD1306& d, int x, int y, int len, int phase) {
  for (int i = 0; i < len; ++i) {
    if (((i + phase) % 6) < 4) d.drawPixel(x, y + i, SSD1306_WHITE);
  }
}

}  // namespace

Smoker::Smoker(Adafruit_SSD1306& display) : disp(&display) {}

void Smoker::setBubbleText(const char* rawBody) {
  size_t n = 0;
  for (size_t i = 0; kLead[i] != '\0' && n + 1 < sizeof text; ++i) text[n++] = kLead[i];
  if (rawBody != nullptr && rawBody[0] != '\0') {
    if (n + 1 < sizeof text) text[n++] = '\n';
    for (size_t i = 0; rawBody[i] != '\0' && n + 1 < sizeof text; ++i) {
      unsigned char c = (unsigned char)rawBody[i];
      if (c == '\r') {
        if (rawBody[i + 1] == '\n') continue;
        c = '\n';
      } else if (c == '\t') {
        c = ' ';
      } else if (c != '\n' && (c < 32 || c > 126)) {
        c = '?';
      }
      text[n++] = (char)c;
    }
  }
  text[n] = '\0';
  lineCount = countLines();
  static_assert(sizeof(text) >= sizeof(kLead) + 1 + kMaxNotifyBody, "bubble text cap");
}

Smoker::LineSpan Smoker::nextLine(size_t i) const {
  LineSpan span;
  span.begin = i;
  span.len = 0;
  span.next = i;
  size_t n = strlen(text);
  if (i >= n) return span;

  if (text[i] == '\n') {
    span.next = i + 1;
    return span;
  }

  size_t limit = i + (size_t)kCols;
  if (limit > n) limit = n;
  size_t stop = i;
  while (stop < limit && text[stop] != '\n') stop++;
  size_t end = stop;
  if (end == limit && end < n && text[end] != '\n') {
    size_t floor = end > 8 ? end - 8 : i;
    if (floor < i) floor = i;
    for (size_t k = end; k > floor; --k) {
      char c = text[k - 1];
      if (c == ' ' || c == ',' || c == '{' || c == '}' || c == '[' || c == ']' || c == ':') {
        end = k;
        break;
      }
    }
  }
  span.len = end - i;
  span.next = end;
  if (span.next < n && text[span.next] == '\n') span.next++;
  if (span.next <= i) span.next = i + 1;
  return span;
}

int Smoker::countLines() const {
  int count = 0;
  size_t i = 0;
  size_t n = strlen(text);
  while (i < n && count < 10000) {
    LineSpan span = nextLine(i);
    count++;
    i = span.next;
  }
  return count;
}

void Smoker::trigger(const char* rawBody, uint32_t now) {
  setBubbleText(rawBody);
  act = Act::Inhale;
  actStart = now;
  scroll = 0;
  scrollParked = false;
  scrollParkedAt = 0;
  bubbleShown = 0;
  // Inhale happens with the cigarette already at the mouth.
  cigShown = 0;
  inhaleShown = 0;
  puffsLeft = 0;
  for (int i = 0; i < kMotes; ++i) motes[i].life = 0;
  // A few wisps are already in the air so the inhale has something to pull in.
  for (int i = 0; i < 5; ++i) {
    spawnMote(104.0f + (float)i * 1.5f, 42.0f - (float)i * 3.0f, -2.0f, -3.0f, 0, 210);
  }
}

void Smoker::setBanner(const char* textIn, uint32_t untilMs) {
  copyLine(bannerText, sizeof bannerText, textIn);
  bannerUntil = untilMs;
  if (bannerText[0] == '\0') bannerUntil = 0;
}

void Smoker::enter(Act next, uint32_t now) {
  act = next;
  actStart = now;
  if (next == Act::Exhale) {
    puffsLeft = 16;
    nextPuff = now;
  }
  if (next == Act::Bubble) {
    scroll = 0;
    scrollParked = false;
    scrollParkedAt = 0;
  }
}

bool Smoker::bannerOn(uint32_t now) const {
  return bannerUntil != 0 && bannerText[0] != '\0' && !reached(now, bannerUntil);
}

void Smoker::bubbleBounds(uint32_t now, int& x, int& y, int& w, int& h, int& clipL, int& clipT,
                          int& clipR, int& clipB) const {
  // Leave the bottom strip free so the cigarette stays visible under the bubble.
  x = 2;
  y = bannerOn(now) ? 8 : 0;
  w = 124;
  h = 48;
  int limit = OLED_HEIGHT - 15;
  if (y + h > limit) h = limit - y;
  if (h < 20) h = 20;
  clipL = x + 4;
  clipT = y + 4;
  clipR = clipL + kCols * kCharW;
  clipB = y + h - 4;
  if (clipB < clipT + kCharH) clipB = clipT + kCharH;
}

int Smoker::maxScrollPx(uint32_t now) const {
  int x, y, w, h, clipL, clipT, clipR, clipB;
  bubbleBounds(now, x, y, w, h, clipL, clipT, clipR, clipB);
  int viewH = clipB - clipT;
  int totalH = lineCount * kCharH;
  if (totalH <= viewH) return 0;
  return totalH - viewH;
}

Smoker::Stick Smoker::stick(uint32_t now) const {
  float bobF = sinf((float)now * 0.00145f) * 1.15f + sinf((float)now * 0.00255f) * 0.35f;
  int bob = iround(bobF);

  float t = smoothStep(cigShown);
  Stick s;
  s.bob = bob;
  float inX0 = 67.0f;
  float inY0 = (float)(39 + bob);
  float inX1 = 108.0f;
  float inY1 = (float)(44 + bob);
  float outX0 = 96.0f;
  float outY0 = 55.0f + bobF * 0.25f;
  float outX1 = 122.0f;
  float outY1 = 60.0f + bobF * 0.25f;
  s.x0 = blend(inX0, outX0, t);
  s.y0 = blend(inY0, outY0, t);
  s.x1 = blend(inX1, outX1, t);
  s.y1 = blend(inY1, outY1, t);
  s.mouthX = 64;
  s.mouthY = 40 + bob;
  return s;
}

void Smoker::spawnMote(float x, float y, float vx, float vy, uint8_t kind, uint8_t life) {
  int slot = -1;
  uint8_t oldest = 255;
  for (int i = 0; i < kMotes; ++i) {
    if (motes[i].life == 0) {
      slot = i;
      break;
    }
    if (motes[i].life < oldest) {
      oldest = motes[i].life;
      slot = i;
    }
  }
  if (slot < 0) return;
  motes[slot].x = x;
  motes[slot].y = y;
  motes[slot].vx = vx;
  motes[slot].vy = vy;
  motes[slot].kind = kind;
  motes[slot].life = life;
}

void Smoker::stepMotes(uint32_t now, float dt, const Stick& pose) {
  if (inhaleShown < 0.82f && (nextDrift == 0 || reached(now, nextDrift))) {
    float jitter = (float)random(-30, 31) / 10.0f;
    float rise = -7.0f - (float)random(0, 90) / 10.0f;
    spawnMote(pose.x1, pose.y1, jitter, rise, 0, 220);
    nextDrift = now + (uint32_t)random(90, 190);
  }

  if (act == Act::Exhale && puffsLeft > 0 && reached(now, nextPuff)) {
    float spread = (float)random(-160, 161) / 10.0f;
    float rise = -10.0f - (float)random(0, 180) / 10.0f;
    float ox = (float)random(-2, 4);
    spawnMote((float)pose.mouthX + ox, (float)pose.mouthY, spread, rise, 1, 255);
    puffsLeft--;
    nextPuff = now + (uint32_t)random(45, 100);
  }

  for (int i = 0; i < kMotes; ++i) {
    Mote& m = motes[i];
    if (m.life == 0) continue;
    if (m.kind == 0 && inhaleShown > 0.3f) {
      float dx = (float)pose.mouthX - m.x;
      float dy = (float)pose.mouthY - m.y;
      m.vx += dx * 5.0f * dt;
      m.vy += dy * 5.0f * dt;
    } else {
      m.vy -= (m.kind == 1 ? 5.0f : 3.5f) * dt;
      m.vx += sinf(m.y * 0.18f + (float)now * 0.003f + (float)i) * 7.0f * dt;
    }
    m.x += m.vx * dt;
    m.y += m.vy * dt;
    int dec = (int)((m.kind == 1 ? 52.0f : 46.0f) * dt);
    if (dec < 1) dec = 1;
    if (m.life <= (uint8_t)dec || m.y < -6.0f || m.x < -6.0f || m.x > OLED_WIDTH + 6) {
      m.life = 0;
    } else {
      m.life = (uint8_t)(m.life - dec);
    }
  }
}

void Smoker::stepBlink(uint32_t now) {
  if (act != Act::Idle) {
    blink = Blink::Open;
    blinkPhaseStart = now;
    return;
  }
  uint32_t elapsed = now - blinkPhaseStart;
  switch (blink) {
    case Blink::Open:
      if (nextBlinkAt != 0 && reached(now, nextBlinkAt)) {
        blink = Blink::Closing;
        blinkPhaseStart = now;
      }
      break;
    case Blink::Closing:
      if (elapsed >= 80) {
        blink = Blink::Shut;
        blinkPhaseStart = now;
      }
      break;
    case Blink::Shut:
      if (elapsed >= 70) {
        blink = Blink::Opening;
        blinkPhaseStart = now;
      }
      break;
    case Blink::Opening:
      if (elapsed >= 130) {
        blink = Blink::Open;
        blinkPhaseStart = now;
        nextBlinkAt = now + (uint32_t)random(2400, 6200);
      }
      break;
  }
}

float Smoker::eyeOpen(uint32_t now) const {
  float t;
  switch (blink) {
    case Blink::Open:
      return 1.0f;
    case Blink::Closing:
      t = (float)(now - blinkPhaseStart) / 80.0f;
      return 1.0f - smoothStep(t);
    case Blink::Shut:
      return 0.0f;
    case Blink::Opening:
      t = (float)(now - blinkPhaseStart) / 130.0f;
      return smoothStep(t);
  }
  return 1.0f;
}

void Smoker::step(uint32_t now) {
  if (!started) {
    started = true;
    blink = Blink::Open;
    blinkPhaseStart = now;
    nextBlinkAt = now + (uint32_t)random(1600, 3600);
    nextDrift = now + 200;
    lastUpdate = now;
  }

  float dt = (float)(now - lastUpdate) / 1000.0f;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.08f) dt = 0.08f;
  lastUpdate = now;

  switch (act) {
    case Act::Inhale:
      if (reached(now, actStart + kInhaleMs)) enter(Act::Withdraw, now);
      break;
    case Act::Withdraw:
      if (reached(now, actStart + kWithdrawMs)) enter(Act::Exhale, now);
      break;
    case Act::Exhale:
      if (reached(now, actStart + kExhaleMs)) enter(Act::Bubble, now);
      break;
    case Act::Bubble: {
      int limit = maxScrollPx(now);
      if (scroll > (float)limit) scroll = (float)limit;
      if (bubbleShown > 0.94f) {
        if (scroll < (float)limit) {
          scroll += dt * kScrollPxPerSec;
          if (scroll > (float)limit) scroll = (float)limit;
          scrollParked = false;
        } else if (!scrollParked) {
          scrollParked = true;
          scrollParkedAt = now;
        } else {
          uint32_t need = limit <= 0 ? kBubbleHoldFitMs : kBubbleHoldEndMs;
          if (reached(now, scrollParkedAt + need)) enter(Act::Return, now);
        }
      }
      break;
    }
    case Act::Return:
      if (reached(now, actStart + kReturnMs) && bubbleShown < 0.08f && cigShown < 0.12f) {
        enter(Act::Idle, now);
      } else if (reached(now, actStart + kReturnMs + 800)) {
        enter(Act::Idle, now);
      }
      break;
    case Act::Idle:
      break;
  }

  float cigGoal = 0.0f;
  if (act == Act::Withdraw || act == Act::Exhale || act == Act::Bubble) cigGoal = 1.0f;
  cigShown = approach(cigShown, cigGoal, dt, act == Act::Withdraw ? 6.2f : 7.0f);
  if (cigShown < 0.001f) cigShown = 0.0f;
  if (cigShown > 0.999f) cigShown = 1.0f;

  float bubbleGoal = (act == Act::Bubble) ? 1.0f : 0.0f;
  bubbleShown = approach(bubbleShown, bubbleGoal, dt, bubbleGoal > bubbleShown ? 11.0f : 6.0f);
  if (bubbleShown < 0.001f) bubbleShown = 0.0f;
  if (bubbleShown > 0.999f) bubbleShown = 1.0f;

  float inhaleGoal = 0.0f;
  if (act == Act::Inhale) {
    float t = (float)(now - actStart) / (float)kInhaleMs;
    inhaleGoal = smoothStep(t);
  } else if (act == Act::Withdraw) {
    float t = (float)(now - actStart) / (float)kWithdrawMs;
    inhaleGoal = 1.0f - smoothStep(t);
  }
  inhaleShown = approach(inhaleShown, inhaleGoal, dt, 7.5f);

  Stick pose = stick(now);
  stepMotes(now, dt, pose);
  stepBlink(now);
}

void Smoker::updateAndDraw(uint32_t now) {
  step(now);
  if (lastDraw != 0 && (uint32_t)(now - lastDraw) < kFrameMs) return;
  lastDraw = now;
  draw(now);
}

void Smoker::drawCorners(uint32_t now) {
  int phase = (int)((now / 140) % 8);
  int len = 6 + (phase < 4 ? phase : 7 - phase);
  disp->drawFastHLine(0, 0, len, SSD1306_WHITE);
  disp->drawFastVLine(0, 0, len, SSD1306_WHITE);
  disp->drawFastHLine(OLED_WIDTH - len, 0, len, SSD1306_WHITE);
  disp->drawFastVLine(OLED_WIDTH - 1, 0, len, SSD1306_WHITE);
  disp->drawFastHLine(0, OLED_HEIGHT - 1, len, SSD1306_WHITE);
  disp->drawFastVLine(0, OLED_HEIGHT - len, len, SSD1306_WHITE);
  disp->drawFastHLine(OLED_WIDTH - len, OLED_HEIGHT - 1, len, SSD1306_WHITE);
  disp->drawFastVLine(OLED_WIDTH - 1, OLED_HEIGHT - len, len, SSD1306_WHITE);
  // A single pixel walks the top edge so the frame cannot sit still.
  int walk = (int)((now / 33) % (uint32_t)(OLED_WIDTH - 16));
  disp->drawPixel(8 + walk, 0, SSD1306_WHITE);
}

void Smoker::drawHead(int bob, float closeAmt, float look, float mouthOpen, bool cigInMouth,
                      uint32_t now) {
  auto Y = [&](int y) { return y + bob; };

  disp->drawLine(36, Y(9), 78, Y(9), SSD1306_WHITE);
  disp->drawLine(36, Y(9), 28, Y(20), SSD1306_WHITE);
  disp->drawLine(78, Y(9), 86, Y(20), SSD1306_WHITE);
  disp->drawLine(28, Y(20), 32, Y(36), SSD1306_WHITE);
  disp->drawLine(86, Y(20), 82, Y(36), SSD1306_WHITE);
  disp->drawLine(32, Y(36), 57, Y(45), SSD1306_WHITE);
  disp->drawLine(82, Y(36), 57, Y(45), SSD1306_WHITE);
  // Short antenna. The tip toggles so the crown keeps changing.
  disp->drawLine(57, Y(9), 57, Y(4), SSD1306_WHITE);
  if (((now / 33) % 4) != 0) disp->drawPixel(57, Y(3), SSD1306_WHITE);

  disp->drawRect(21, Y(22), 8, 11, SSD1306_WHITE);
  disp->drawFastHLine(23, Y(25), 4, SSD1306_WHITE);
  if (((now / 200) & 1) == 0) disp->drawPixel(24, Y(28), SSD1306_WHITE);

  int fullH = 11;
  int lensH = iround((float)fullH * (1.0f - closeAmt));
  if (lensH < 1) lensH = 1;
  int lensTop = Y(18) + (fullH - lensH) / 2;
  const int leftX = 34;
  const int rightX = 58;
  const int lensW = 20;
  disp->fillRect(leftX, lensTop, lensW, lensH, SSD1306_WHITE);
  disp->fillRect(rightX, lensTop, lensW, lensH, SSD1306_WHITE);
  int bridgeY = lensTop + lensH / 2;
  disp->drawFastHLine(leftX + lensW, bridgeY, rightX - (leftX + lensW), SSD1306_WHITE);

  if (lensH >= 4) {
    int shift = iround(look * 5.0f);
    int pupilW = 3;
    int pupilH = lensH - 2;
    if (pupilH < 1) pupilH = 1;
    auto pupil = [&](int lensLeft) {
      int px = lensLeft + lensW / 2 + shift - pupilW / 2;
      if (px < lensLeft + 1) px = lensLeft + 1;
      if (px + pupilW > lensLeft + lensW - 1) px = lensLeft + lensW - 1 - pupilW;
      int py = lensTop + (lensH - pupilH) / 2;
      disp->fillRect(px, py, pupilW, pupilH, SSD1306_BLACK);
    };
    pupil(leftX);
    pupil(rightX);

    int span = rightX + lensW - leftX;
    int scan = (int)((now / 40) % (uint32_t)span);
    int sx = leftX + scan;
    bool inLens = (sx >= leftX && sx < leftX + lensW) || (sx >= rightX && sx < rightX + lensW);
    if (inLens) disp->drawFastVLine(sx, lensTop + 1, lensH - 2, SSD1306_BLACK);
  }

  disp->drawLine(55, Y(30), 52, Y(36), SSD1306_WHITE);
  disp->drawLine(52, Y(36), 58, Y(36), SSD1306_WHITE);

  int my = Y(40);
  if (cigInMouth || mouthOpen < 0.18f) {
    disp->drawFastHLine(46, my, 16, SSD1306_WHITE);
  } else {
    int oh = 2 + iround(mouthOpen * 5.0f);
    int top = my - oh / 2;
    disp->drawFastHLine(46, top, 16, SSD1306_WHITE);
    disp->drawFastHLine(46, top + oh, 16, SSD1306_WHITE);
    disp->drawFastVLine(46, top, oh, SSD1306_WHITE);
    disp->drawFastVLine(61, top, oh + 1, SSD1306_WHITE);
  }

  if (closeAmt > 0.45f) {
    disp->drawLine(40, Y(33), 47, Y(37), SSD1306_WHITE);
    disp->drawLine(74, Y(33), 67, Y(37), SSD1306_WHITE);
  }
}

void Smoker::drawBody(int bob, float inhale, float breath) {
  int lift = iround(breath * 1.1f + inhale * 2.2f);
  int y = 49 + bob - lift;
  disp->drawLine(6, 63, 40, y, SSD1306_WHITE);
  disp->drawLine(40, y, 57, y + 4, SSD1306_WHITE);
  disp->drawLine(122, 63, 74, y, SSD1306_WHITE);
  disp->drawLine(74, y, 57, y + 4, SSD1306_WHITE);
  disp->drawLine(44, y + 2, 70, y + 2, SSD1306_WHITE);
  // Collar tick that slides a pixel with the breath.
  int tick = 48 + (int)((breath + inhale) * 3.0f);
  disp->drawPixel(tick, y + 3, SSD1306_WHITE);
}

void Smoker::drawCigarette(const Stick& pose, float heat, float cigOut, uint32_t now) {
  int x0 = iround(pose.x0);
  int y0 = iround(pose.y0);
  int x1 = iround(pose.x1);
  int y1 = iround(pose.y1);
  disp->drawLine(x0, y0, x1, y1, SSD1306_WHITE);
  disp->drawLine(x0, y0 + 1, x1, y1 + 1, SSD1306_WHITE);

  float dx = pose.x1 - pose.x0;
  float dy = pose.y1 - pose.y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) len = 1.0f;
  float ux = dx / len;
  float uy = dy / len;
  int fx = iround(pose.x0 + ux * 6.0f);
  int fy = iround(pose.y0 + uy * 6.0f);
  disp->drawLine(x0, y0 - 1, fx, fy - 1, SSD1306_WHITE);
  disp->drawLine(x0, y0 + 2, fx, fy + 2, SSD1306_WHITE);

  disp->drawPixel(x1, y1, SSD1306_WHITE);
  int reach = heat > 0.72f ? 2 : 1;
  const int ox[8] = {1, -1, 0, 0, 1, -1, 2, -2};
  const int oy[8] = {0, 0, 1, -1, -1, 1, 0, 0};
  uint32_t frame = now / 33;
  for (int i = 0; i < 8; ++i) {
    if (abs(ox[i]) > reach || abs(oy[i]) > reach) continue;
    uint32_t h = frame * 1103515245u + (uint32_t)i * 2246822519u;
    int thresh = 35 + iround(heat * 55.0f);
    if ((int)(h % 100u) < thresh) disp->drawPixel(x1 + ox[i], y1 + oy[i], SSD1306_WHITE);
  }

  if (cigOut > 0.18f) {
    disp->drawLine(x0 - 4, y0 - 3, x0 + 1, y0, SSD1306_WHITE);
    disp->drawLine(x0 - 4, y0 + 4, x0 + 1, y0 + 1, SSD1306_WHITE);
    disp->drawLine(x0 - 5, y0, x0 - 2, y0, SSD1306_WHITE);
  }
}

void Smoker::drawMotes() {
  for (int i = 0; i < kMotes; ++i) {
    const Mote& m = motes[i];
    if (m.life == 0) continue;
    int x = iround(m.x);
    int y = iround(m.y);
    if (m.kind == 0) {
      disp->drawPixel(x, y, SSD1306_WHITE);
      if (m.life > 160) disp->drawPixel(x + 1, y, SSD1306_WHITE);
      continue;
    }
    int rad = 2 + (int)(255 - m.life) / 34;
    if (rad > 7) rad = 7;
    disp->drawCircle(x, y, rad, SSD1306_WHITE);
    if (rad >= 3) {
      disp->drawPixel(x, y, SSD1306_WHITE);
      disp->drawPixel(x - rad / 2, y + 1, SSD1306_WHITE);
      disp->drawPixel(x + rad / 2, y - 1, SSD1306_WHITE);
    }
  }
}

void Smoker::drawWisp(int tipX, int tipY, int mouthX, int mouthY, float pull, uint32_t now) {
  for (int i = 0; i < 6; ++i) {
    uint32_t age = (now / 28 + (uint32_t)i * 41) % 200;
    float u = (float)age / 200.0f;
    int x = tipX + iround(sinf(u * 6.2832f + (float)i * 1.3f) * (1.5f + u * 7.0f));
    int y = tipY - iround(u * 18.0f);
    if (pull > 0.0f) {
      x = iround(blend((float)x, (float)mouthX, pull));
      y = iround(blend((float)y, (float)mouthY, pull));
    }
    if (((now / 33) + (uint32_t)i) % 5 == 0) continue;
    disp->drawPixel(x, y, SSD1306_WHITE);
    if (u < 0.35f && pull < 0.4f) disp->drawPixel(x, y + 1, SSD1306_WHITE);
  }
}

void Smoker::drawBanner(uint32_t now) {
  if (!bannerOn(now)) return;
  disp->fillRect(0, 0, OLED_WIDTH, 8, SSD1306_BLACK);
  disp->setTextSize(1);
  disp->setTextColor(SSD1306_WHITE);
  disp->setTextWrap(false);
  disp->setCursor(0, 0);
  disp->print(bannerText);
  if (((now / 250) % 2) == 0) disp->fillRect(OLED_WIDTH - 5, 2, 3, 3, SSD1306_WHITE);
}

void Smoker::drawBubble(uint32_t now) {
  if (bubbleShown < 0.04f) return;

  int x, y, w, h, clipL, clipT, clipR, clipB;
  bubbleBounds(now, x, y, w, h, clipL, clipT, clipR, clipB);

  float pop = bubbleShown;
  if (act == Act::Bubble) {
    float t = (float)(now - actStart) / 260.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float c1 = 1.35f;
    float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    pop = 1.0f + c3 * u * u * u + c1 * u * u;
    if (pop < 0.05f) pop = 0.05f;
  }
  int bw = iround((float)w * pop);
  int bh = iround((float)h * pop);
  if (bw < 8) bw = 8;
  if (bh < 8) bh = 8;
  if (bw > w) bw = w;
  if (bh > h) bh = h;
  int bx = x + (w - bw) / 2;
  int by = y + (h - bh) / 2;

  disp->fillRect(bx, by, bw, bh, SSD1306_BLACK);
  int phase = (int)((now / 70) % 6);
  dashH(*disp, bx, by, bw, phase);
  dashH(*disp, bx, by + bh - 1, bw, phase + 3);
  dashV(*disp, bx, by, bh, phase);
  dashV(*disp, bx + bw - 1, by, bh, phase + 3);

  if (pop > 0.72f) {
    int baseY = by + bh - 1;
    int tipY = baseY + 6;
    if (tipY >= OLED_HEIGHT) tipY = OLED_HEIGHT - 1;
    disp->fillTriangle(bx + 16, baseY, bx + 32, baseY, 46, tipY, SSD1306_BLACK);
    disp->drawLine(bx + 16, baseY, 46, tipY, SSD1306_WHITE);
    disp->drawLine(bx + 32, baseY, 46, tipY, SSD1306_WHITE);
  }

  bool showText = act == Act::Bubble && bubbleShown > 0.94f && bw + 2 >= w && bh + 2 >= h;
  if (!showText) return;

  int limit = maxScrollPx(now);
  if (limit > 0) {
    int trackT = clipT;
    int trackH = clipB - clipT;
    int thumbH = trackH / 3;
    if (thumbH < 4) thumbH = 4;
    if (thumbH > trackH) thumbH = trackH;
    int travel = trackH - thumbH;
    int thumbY = trackT;
    if (limit > 0 && travel > 0) thumbY = trackT + (int)((float)travel * (scroll / (float)limit));
    disp->drawFastVLine(clipR + 3, trackT, trackH, SSD1306_WHITE);
    disp->fillRect(clipR + 2, thumbY, 3, thumbH, SSD1306_WHITE);
  } else if (((now / 280) % 2) == 0) {
    // Short body: a caret keeps the bubble from freezing on one glyph row.
    int caretX = clipL + (int)strlen(kLead) * kCharW + 1;
    if (caretX > clipR - 2) caretX = clipR - 2;
    disp->drawFastVLine(caretX, clipT + lineCount * kCharH - 7, 6, SSD1306_WHITE);
  }

  uint8_t* buf = disp->getBuffer();
  if (buf == nullptr) return;
  static uint8_t saved[OLED_WIDTH * ((OLED_HEIGHT + 7) / 8)];
  memcpy(saved, buf, sizeof saved);

  int top = clipT - (int)scroll;
  size_t i = 0;
  size_t n = strlen(text);
  int li = 0;
  while (i < n && li < lineCount) {
    LineSpan span = nextLine(i);
    int ly = top + li * kCharH;
    if (ly < clipB && ly + kCharH > clipT) {
      int cx = clipL;
      for (size_t c = 0; c < span.len; ++c) {
        unsigned char ch = (unsigned char)text[span.begin + c];
        if (ch < 32 || ch > 126) ch = '?';
        disp->drawChar(cx, ly, ch, SSD1306_WHITE, SSD1306_BLACK, 1);
        cx += kCharW;
      }
    }
    i = span.next;
    li++;
  }

  const int pages = (OLED_HEIGHT + 7) / 8;
  for (int page = 0; page < pages; ++page) {
    int y0 = page * 8;
    for (int px = 0; px < OLED_WIDTH; ++px) {
      int index = px + page * OLED_WIDTH;
      uint8_t keep = 0;
      for (int bit = 0; bit < 8; ++bit) {
        int py = y0 + bit;
        if (px >= clipL && px < clipR && py >= clipT && py < clipB) keep |= (uint8_t)(1u << bit);
      }
      if (keep == 0) {
        buf[index] = saved[index];
      } else if (keep != 0xFFu) {
        buf[index] = (uint8_t)((buf[index] & keep) | (saved[index] & (uint8_t)~keep));
      }
    }
  }
}

void Smoker::draw(uint32_t now) {
  disp->clearDisplay();
  drawCorners(now);

  Stick pose = stick(now);
  float breath = 0.5f + 0.5f * sinf((float)now * 0.0012f);
  float look = sinf((float)now * 0.00072f) * 0.8f + sinf((float)now * 0.0018f) * 0.2f;
  if (act == Act::Inhale) look *= (1.0f - inhaleShown);

  float closeAmt = inhaleShown * 0.55f;
  float blinkClose = 1.0f - eyeOpen(now);
  if (blinkClose > closeAmt) closeAmt = blinkClose;

  float mouthOpen = 0.0f;
  if (act == Act::Exhale) {
    float t = (float)(now - actStart) / (float)kExhaleMs;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    mouthOpen = sinf(t * 3.1415926f);
  }

  bool cigInMouth = cigShown < 0.45f;
  drawHead(pose.bob, closeAmt, look, mouthOpen, cigInMouth, now);
  drawBody(pose.bob, inhaleShown, breath);

  float heat = 0.42f + 0.22f * breath + inhaleShown * 0.5f;
  if (heat > 1.0f) heat = 1.0f;
  drawCigarette(pose, heat, cigShown, now);
  drawWisp(iround(pose.x1), iround(pose.y1), pose.mouthX, pose.mouthY, inhaleShown, now);
  drawMotes();
  // Ember again so a puff ring cannot cover the coal.
  drawCigarette(pose, heat, cigShown, now);

  gFrameDrawn = true;
}
