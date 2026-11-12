#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include <esp_system.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "button.h"
#include "faces.h"
#include "pot.h"

namespace {

enum class NetState : uint8_t { NoCreds, Connecting, Online };

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1, OLED_I2C_HZ, OLED_I2C_HZ);
Faces faces(display);
WebServer server(HTTP_PORT);

NetState netState = NetState::NoCreds;
uint32_t netStamp = 0;
uint32_t lastIpLog = 0;
bool serverStarted = false;
bool mdnsStarted = false;
uint8_t oledAddr = 0;
uint16_t unread = 0;
bool notifyLedActive = false;
uint32_t notifyLedStart = 0;

void startNotifyLed(uint32_t now) {
  notifyLedActive = true;
  notifyLedStart = now;
  digitalWrite(NOTIFY_LED_PIN, HIGH);
}

// 500 ms on, 500 ms off, three times, then off. Elapsed time only — no delay().
void serviceNotifyLed(uint32_t now) {
  if (!notifyLedActive) return;
  // handleNotify samples millis() during this same loop, which can be
  // later than the stamp appLoop captured. A backwards delta would wrap
  // and kill the pulse; ignore it and leave the pin as startNotifyLed set it.
  uint32_t elapsed = now - notifyLedStart;
  if ((int32_t)elapsed < 0) return;
  if (elapsed >= NOTIFY_LED_BLINK_MS) {
    notifyLedActive = false;
    digitalWrite(NOTIFY_LED_PIN, LOW);
    return;
  }
  uint32_t phase = elapsed % (NOTIFY_LED_ON_MS + NOTIFY_LED_OFF_MS);
  digitalWrite(NOTIFY_LED_PIN, phase < NOTIFY_LED_ON_MS ? HIGH : LOW);
}

bool wifiConfigured() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0;
}

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void formatIp(char* out, size_t n) {
  IPAddress ip = WiFi.localIP();
  snprintf(out, n, "IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void logListening() {
  IPAddress ip = WiFi.localIP();
  Serial.printf("CFace listening  http://%u.%u.%u.%u:%u/notify\n", ip[0], ip[1], ip[2], ip[3],
                HTTP_PORT);
  Serial.printf("OLED address     0x%02X   SDA %d  SCL %d\n", oledAddr, OLED_SDA_PIN, OLED_SCL_PIN);
  if (mdnsStarted) {
    Serial.printf("mDNS             http://%s.local/notify\n", DEVICE_HOSTNAME);
  }
  Serial.printf("Face             %s\n", faces.faceId());
}

void handleNotify() {
  // Idle keeps a short caption. The smoker bubble keeps the raw body,
  // so that copy has to stay alive until trigger() has stored it.
  char text[64];
  text[0] = '\0';
  String body;
  // A named "face" keeps that choice. An omitted one ends a knob override
  // and returns to the boot face, or to the last POST that named one.
  bool namedFace = false;

  if (server.hasArg("plain")) {
    body = server.arg("plain");
    if (body.length() > Smoker::kMaxNotifyBody) {
      server.send(413, "application/json", "{\"ok\":false,\"error\":\"body too large\"}");
      return;
    }
    if (body.length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (err) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
      }
      const char* event = doc["event"] | "";
      if (event[0] != '\0' && strcmp(event, "prompt_complete") != 0) {
        server.send(200, "application/json", "{\"ok\":true,\"ignored\":true}");
        return;
      }
      const char* raw = doc["text"] | "";
      strncpy(text, raw, sizeof text - 1);
      text[sizeof text - 1] = '\0';

      JsonVariant faceField = doc["face"];
      if (faceField.is<const char*>()) {
        const char* id = faceField.as<const char*>();
        if (id != nullptr && id[0] != '\0') {
          namedFace = true;
          const char* before = faces.faceId();
          if (!faces.setFaceById(id)) {
            char shown[24];
            snprintf(shown, sizeof shown, "%s", id);
            Serial.printf("Unknown face \"%s\", staying on %s\n", shown, before);
          } else if (strcmp(before, faces.faceId()) != 0) {
            Serial.printf("Face             %s\n", faces.faceId());
          }
        }
      }
    }
  }

  if (!namedFace) faces.releaseKnobOverride();

  uint32_t now = millis();
  if (unread < 9999) unread++;
  faces.setUnread(unread);
  faces.trigger(text, body.c_str(), now, ALERT_HOLD_MS);
  startNotifyLed(now);
  char ok[48];
  snprintf(ok, sizeof ok, "{\"ok\":true,\"face\":\"%s\"}", faces.faceId());
  server.send(200, "application/json", ok);
}

void clearUnread() {
  unread = 0;
  faces.setUnread(0);
}

void handleClear() {
  clearUnread();
  server.send(200, "application/json", "{\"ok\":true,\"unread\":0}");
}

void handleHealth() {
  char msg[176];
  const char* face = faces.faceId();
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    snprintf(msg, sizeof msg,
             "{\"ok\":true,\"ip\":\"%u.%u.%u.%u\",\"port\":%u,\"oled\":\"0x%02X\",\"face\":\"%s\",\"unread\":%u,\"button\":%u}",
             ip[0], ip[1], ip[2], ip[3], HTTP_PORT, oledAddr, face, (unsigned)unread,
             (unsigned)buttonHeld());
  } else {
    snprintf(msg, sizeof msg,
             "{\"ok\":true,\"ip\":null,\"port\":%u,\"oled\":\"0x%02X\",\"face\":\"%s\",\"unread\":%u,\"button\":%u}", HTTP_PORT,
             oledAddr, face, (unsigned)unread, (unsigned)buttonHeld());
  }
  server.send(200, "application/json", msg);
}

void handleRoot() {
  server.send(200, "text/plain",
              "CFace\n"
              "POST /notify  {\"event\":\"prompt_complete\",\"text\":\"New reply\",\"face\":\"idle\"}\n"
              "POST /clear  (reset unread count)\n"
              "faces: idle, smoker, yesman\n"
              "GET  /health\n");
}

void ensureServer() {
  if (serverStarted) return;
  server.on("/notify", HTTP_POST, handleNotify);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  serverStarted = true;
}

void startWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  netState = NetState::Connecting;
  netStamp = millis();
  faces.setBanner("Joining WiFi", netStamp + 12000);
  Serial.printf("Joining Wi-Fi \"%s\"\n", WIFI_SSID);
}

void onWifiUp(uint32_t now) {
  netState = NetState::Online;
  ensureServer();
  mdnsStarted = MDNS.begin(DEVICE_HOSTNAME);
  char banner[21];
  formatIp(banner, sizeof banner);
  faces.setBanner(banner, now + BOOT_IP_SHOW_MS);
  logListening();
  lastIpLog = now;
}

void serviceNet(uint32_t now) {
  if (!wifiConfigured()) {
    if (netState != NetState::NoCreds) {
      netState = NetState::NoCreds;
      faces.setBanner("Set WiFi in config.h", now + 6000);
      Serial.println("Wi-Fi SSID is still the placeholder in include/config.h");
    }
    return;
  }

  if (netState == NetState::NoCreds) {
    startWifi();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (netState != NetState::Online) onWifiUp(now);
    if ((uint32_t)(now - lastIpLog) > 30000) {
      lastIpLog = now;
      logListening();
    }
    return;
  }

  if (netState == NetState::Online) {
    netState = NetState::Connecting;
    netStamp = now;
    faces.setBanner("Reconnecting", now + 2500);
    Serial.println("Wi-Fi dropped, reconnecting");
    return;
  }

  if (netState == NetState::Connecting && (uint32_t)(now - netStamp) > 15000) {
    Serial.println("Still no Wi-Fi, retrying");
    faces.setBanner("No WiFi yet", now + 2500);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    netStamp = now;
  }
}

bool startDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(OLED_I2C_HZ);
#if defined(ARDUINO_ARCH_ESP32)
  Wire.setTimeOut(50);
#endif

  uint32_t start = millis();
  uint8_t found = 0;
  do {
    if (i2cProbe(OLED_I2C_ADDR)) {
      found = OLED_I2C_ADDR;
      break;
    }
    if (i2cProbe(OLED_I2C_ADDR_FALLBACK)) {
      found = OLED_I2C_ADDR_FALLBACK;
      break;
    }
    delay(20);
  } while ((uint32_t)(millis() - start) < 400);

  if (found == 0) {
    // Bring the controller up anyway so a slow panel still gets frames.
    found = OLED_I2C_ADDR;
    Serial.printf("No ACK from OLED at 0x%02X or 0x%02X. Check wiring, then reset.\n",
                  OLED_I2C_ADDR, OLED_I2C_ADDR_FALLBACK);
  } else {
    Serial.printf("OLED ACK at 0x%02X\n", found);
  }

  oledAddr = found;
  bool ok = display.begin(SSD1306_SWITCHCAPVCC, found, false, false);
  if (!ok) {
    Serial.println("OLED begin() failed");
    return false;
  }
  display.clearDisplay();
  display.display();
  return true;
}

void appSetup() {
  Serial.begin(115200);
  delay(50);
  randomSeed(esp_random());
  pinMode(NOTIFY_LED_PIN, OUTPUT);
  digitalWrite(NOTIFY_LED_PIN, LOW);
  Serial.println();
  Serial.println("CFace booting");
  Serial.printf("Notify LED       GPIO %d\n", NOTIFY_LED_PIN);

  startDisplay();
  faces.begin(millis());
  // First frame before Wi-Fi so the panel is never a frozen splash.
  faces.updateAndDraw(millis());

  if (wifiConfigured()) {
    startWifi();
  } else {
    netState = NetState::NoCreds;
    faces.setBanner("Set WiFi in config.h", millis() + 6000);
    Serial.println("Edit WIFI_SSID and WIFI_PASSWORD in include/config.h");
  }
  potBegin(millis(), Faces::count());
  buttonBegin();
}

void appLoop() {
  uint32_t now = millis();
  serviceNet(now);
  if (serverStarted) server.handleClient();
  // Fresh millis(): handleNotify may have started the blink during this pass.
  serviceNotifyLed(millis());
  if (buttonPressed(millis())) {
    clearUnread();
    Serial.println("Unread cleared by button");
  }
  int zone = potPoll(millis());
  if (zone >= 0) faces.selectIndex(zone);
  faces.updateAndDraw(now);
}

}  // namespace

void setup() { appSetup(); }

void loop() { appLoop(); }
