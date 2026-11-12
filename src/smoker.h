#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "face_state.h"

// Hi-tech smoker, white on black, for a 128x64 SSD1306.
// Idle is a lit cigarette with drifting smoke. trigger() runs one
// sequence: inhale, take the cigarette out, exhale a puff, pop a
// speech bubble, then ease the cigarette back. The bubble shows
// "got a new reply" plus the raw POST body, scrolled if needed.
class Smoker {
 public:
  // Largest /notify body stored and drawn in full. Larger posts are
  // refused so the bubble never has to drop the tail of a JSON body.
  static constexpr size_t kMaxNotifyBody = 4096;

  explicit Smoker(Adafruit_SSD1306& display);

  void trigger(const char* rawBody, uint32_t now);
  void setBanner(const char* text, uint32_t untilMs);
  void updateAndDraw(uint32_t now, FaceState state);

 private:
  enum class Act : uint8_t { Idle, Inhale, Withdraw, Exhale, Bubble, Return };
  enum class Blink : uint8_t { Open, Closing, Shut, Opening };

  struct Stick {
    float x0, y0, x1, y1;
    int mouthX, mouthY;
    int bob;
  };

  struct LineSpan {
    size_t begin;
    size_t len;
    size_t next;
  };

  struct Mote {
    float x, y, vx, vy;
    uint8_t life;
    uint8_t kind;  // 0 drifts off the ember, 1 is the exhale puff
  };

  void step(uint32_t now);
  void draw(uint32_t now);
  void enter(Act next, uint32_t now);
  void setBubbleText(const char* rawBody);
  int countLines() const;
  LineSpan nextLine(size_t i) const;
  bool bannerOn(uint32_t now) const;
  Stick stick(uint32_t now) const;
  int maxScrollPx(uint32_t now) const;
  void bubbleBounds(uint32_t now, int& x, int& y, int& w, int& h, int& clipL,
                    int& clipT, int& clipR, int& clipB) const;

  void stepMotes(uint32_t now, float dt, const Stick& pose);
  void spawnMote(float x, float y, float vx, float vy, uint8_t kind, uint8_t life);
  void stepBlink(uint32_t now);
  float eyeOpen(uint32_t now) const;

  void drawCorners(uint32_t now);
  void drawHead(int bob, float closeAmt, float look, float mouthOpen, bool cigInMouth,
                uint32_t now);
  void drawBody(int bob, float inhale, float breath);
  void drawCigarette(const Stick& pose, float heat, float cigOut, uint32_t now);
  void drawMotes();
  void drawWisp(int tipX, int tipY, int mouthX, int mouthY, float pull, uint32_t now);
  void drawBanner(uint32_t now);
  void drawBubble(uint32_t now);

  static constexpr int kMotes = 20;
  static constexpr size_t kTextCap = 16 + 1 + kMaxNotifyBody;

  Adafruit_SSD1306* disp;
  Act act = Act::Idle;
  Blink blink = Blink::Open;
  Mote motes[kMotes] = {};
  uint32_t actStart = 0;
  uint32_t blinkPhaseStart = 0;
  uint32_t nextBlinkAt = 0;
  uint32_t lastUpdate = 0;
  uint32_t lastDraw = 0;
  uint32_t nextDrift = 0;
  uint32_t nextPuff = 0;
  uint32_t nextDragAt = 0;
  bool dragOnly = false;
  float slp = 1.0f;
  float breathPh = 0;
  FaceState state = FaceState::Sleeping;
  uint32_t bannerUntil = 0;
  uint32_t scrollParkedAt = 0;
  float cigShown = 0;
  float bubbleShown = 0;
  float inhaleShown = 0;
  float scroll = 0;
  int lineCount = 0;
  int puffsLeft = 0;
  bool started = false;
  bool scrollParked = false;
  char text[kTextCap] = {0};
  char bannerText[21] = {0};
};
