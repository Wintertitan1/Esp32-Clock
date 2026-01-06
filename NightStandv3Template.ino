#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();

// ===================== USER SETTINGS =====================
const char* WIFI_SSID = "YOUR WIFI NAME"; //FILL IN THIS LINE
const char* WIFI_PASS = "YOUR WIFI PASSWORD"; //FILL IN THIS LINE

// OpenWeatherMap
const char* OWM_API_KEY = "YOUR OPEN WEATHER APP KEY"; //GO TO OPENWEATHERAPPS WEBSITE MAKE AN ACCOUNT AND COPY YOUR FREE KEY
const char* OWM_ZIP     = "YOUR ZIP CODE"; 
const char* OWM_COUNTRY = "YOUR COUNTRY"; //EXAMPLE: "US"

// Central Time with DST
const char* TZ_INFO = "CST6CDT,M3.2.0/2,M11.1.0/2";

// Pins
static const int PIN_BL  = 4;
static const int PIN_LDR = 34;

// PWM
static const int PWM_FREQ = 5000;
static const int PWM_BITS = 10;

// LDR calibration
static const int LDR_DARK   = 400;   // covered/dark
static const int LDR_BRIGHT = 3700;  // bright lights

// Weather refresh
const unsigned long WEATHER_PERIOD_MS = 15UL * 60UL * 1000UL;
// =========================================================

struct WeatherData {
  float tempF = NAN;
  String main;  // Clear, Clouds, Rain, Snow, etc.
  bool valid = false;
};
WeatherData weather;

unsigned long lastWeatherMs = 0;
float ldrFiltered = 0.0f;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int ldrToBrightness(float ldr) {
  float x = (ldr - LDR_DARK) / float(LDR_BRIGHT - LDR_DARK);
  if (x < 0) x = 0;
  if (x > 1) x = 1;

  const int minB = 3;
  const int maxB = 900;

  const float gamma = 2.6f;
  float y = powf(x, gamma);

  return (int)(minB + y * (maxB - minB));
}

void setBacklight(int duty) {
  duty = clampi(duty, 0, (1 << PWM_BITS) - 1);
  ledcWrite(PIN_BL, duty);
}

// ===================== WIFI (auto reconnect + indicator) =====================
bool wifiConnected = false;
unsigned long lastWifiAttemptMs = 0;
unsigned long wifiBackoffMs = 2000;   // grows up to 60s

void wifiStart() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void drawWifiIndicator(bool connected) {
  // top-left small dot + optional text, without clearing whole screen
  tft.fillRect(0, 0, 60, 18, TFT_BLACK);

  uint16_t col = connected ? TFT_GREEN : TFT_RED;
  tft.fillCircle(8, 9, 5, col);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(connected ? "WiFi" : "NoWiFi", 18, 2, 2);
}

void wifiService() {
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected != wifiConnected) {
    wifiConnected = connected;
    drawWifiIndicator(wifiConnected);
    if (wifiConnected) wifiBackoffMs = 2000; // reset backoff on success
  }

  if (!wifiConnected) {
    unsigned long now = millis();
    if (now - lastWifiAttemptMs >= wifiBackoffMs) {
      lastWifiAttemptMs = now;
      wifiStart();
      wifiBackoffMs = (wifiBackoffMs < 60000UL) ? (wifiBackoffMs * 2UL) : 60000UL;
    }
  }
}

// ===================== TIME =====================
void setupTime() {
  // Reliable TZ + DST on ESP32
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
}

bool waitForValidTime(uint32_t timeoutMs = 15000) {
  unsigned long start = millis();
  tm ti;
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&ti, 500)) {
      if (ti.tm_year >= 120) return true; // >= 2020
    }
    delay(200);
  }
  return false;
}

// ===================== UI HELPERS =====================
void drawCenteredText(const String& s, int y, int font, uint16_t fg, uint16_t bg) {
  int w = tft.textWidth(s, font);
  int x = (240 - w) / 2;
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, font);
}

// ===================== DRAWING =====================
// Flicker fix:
// - Draw full time only when MINUTE changes
// - Blink colon by drawing/erasing ONLY the colon region
int lastMinuteDrawn = -1;
int lastDayDrawn = -1;
bool colonOn = true;

void drawTimeMinute(const tm &ti) {
  int hour12 = ti.tm_hour % 12;
  if (hour12 == 0) hour12 = 12;

  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%d", hour12);
  snprintf(mm, sizeof(mm), "%02d", ti.tm_min);

  String full = String(hh) + ":" + String(mm);
  int fullW = tft.textWidth(full, 8);
  int x0 = (240 - fullW) / 2;
  int y0 = 24; // top zone

  // Clear only the time area (not the whole top zone)
  tft.fillRect(0, 18, 240, 85, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Draw HH : MM
  tft.drawString(hh, x0, y0, 8);
  int wHH = tft.textWidth(String(hh), 8);

  tft.drawString(":", x0 + wHH, y0, 8);
  int wColon = tft.textWidth(":", 8);

  tft.drawString(mm, x0 + wHH + wColon, y0, 8);
}

void drawColonBlink(const tm &ti) {
  int hour12 = ti.tm_hour % 12;
  if (hour12 == 0) hour12 = 12;

  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), "%d", hour12);
  snprintf(mm, sizeof(mm), "%02d", ti.tm_min);

  String full = String(hh) + ":" + String(mm);
  int fullW = tft.textWidth(full, 8);
  int x0 = (240 - fullW) / 2;
  int y0 = 20;

  int wHH = tft.textWidth(String(hh), 8);
  int colonX = x0 + wHH;
  int colonY = y0;
  int colonW = tft.textWidth(":", 8);

  // Clear colon area only
  tft.fillRect(colonX - 2, colonY, colonW + 4, 85, TFT_BLACK);

  if (colonOn) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(":", colonX, colonY, 8);
  }
}

void drawDateIfNeeded(const tm &ti) {
  if (ti.tm_mday != lastDayDrawn) {
    lastDayDrawn = ti.tm_mday;

    // Clear date strip only
    tft.fillRect(0, 110, 240, 30, TFT_BLACK);

    char bufDate[32];
    strftime(bufDate, sizeof(bufDate), "%a %b %d", &ti);
    drawCenteredText(String(bufDate), 112, 4, TFT_LIGHTGREY, TFT_BLACK);
  }
}

void drawWeather(const WeatherData &w) {
  tft.fillRect(0, 140, 240, 100, TFT_BLACK);

  if (!w.valid || isnan(w.tempF)) {
    drawCenteredText("Weather --", 190, 4, TFT_ORANGE, TFT_BLACK);
    return;
  }

  // Draw number only (big, centered)
  char numBuf[8];
  snprintf(numBuf, sizeof(numBuf), "%.0f", w.tempF);
  drawCenteredText(String(numBuf), 140, 8, TFT_WHITE, TFT_BLACK);

  // Draw the "F" separately so it never gets clipped
  // (position is slightly to the right of center; adjust +/− if you want)
  tft.setTextDatum(ML_DATUM);                 // middle-left anchor
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("F", 180, 190, 4);           // smaller font for the unit

  // Conditions lower
  drawCenteredText(w.main, 230, 4, TFT_CYAN, TFT_BLACK);
}


// ===================== WEATHER FETCH =====================
bool fetchWeatherZip(WeatherData &out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String("https://api.openweathermap.org/data/2.5/weather?zip=") +
               OWM_ZIP + "," + OWM_COUNTRY +
               "&appid=" + OWM_API_KEY +
               "&units=imperial";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, url)) return false;

  int code = https.GET();
  if (code != 200) {
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  StaticJsonDocument<2048> doc;
  auto err = deserializeJson(doc, payload);
  if (err) return false;

  out.tempF = doc["main"]["temp"] | NAN;
  out.main  = doc["weather"][0]["main"].as<String>();
  out.valid = (!isnan(out.tempF) && out.main.length() > 0);
  return out.valid;
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);
  delay(500);

  ledcAttach(PIN_BL, PWM_FREQ, PWM_BITS);
  setBacklight(200);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  ldrFiltered = analogRead(PIN_LDR);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Start WiFi (non-blocking)
  wifiStart();
  drawWifiIndicator(false);

  // Time
  setupTime();
  waitForValidTime();

  // Initial draw
  tm ti;
  if (getLocalTime(&ti, 500)) {
    lastMinuteDrawn = -1;
    lastDayDrawn = -1;
    drawTimeMinute(ti);
    drawDateIfNeeded(ti);
  }

  // Initial weather
  if (fetchWeatherZip(weather)) {
    drawWeather(weather);
  } else {
    weather.valid = false;
    drawWeather(weather);
  }
  lastWeatherMs = millis();
}

void loop() {
  // auto dim
  int raw = analogRead(PIN_LDR);
  ldrFiltered = 0.95f * ldrFiltered + 0.05f * raw;
  setBacklight(ldrToBrightness(ldrFiltered));

  // WiFi maintenance + indicator (does not block UI)
  wifiService();

  // time + blink colon
  tm ti;
  if (getLocalTime(&ti, 200) && ti.tm_year >= 120) {

    // redraw time only when minute changes
    if (ti.tm_min != lastMinuteDrawn) {
      lastMinuteDrawn = ti.tm_min;
      drawTimeMinute(ti);
    }

    // redraw date only when day changes
    drawDateIfNeeded(ti);

    // blink colon once per second
    static int lastBlinkSec = -1;
    if (ti.tm_sec != lastBlinkSec) {
      lastBlinkSec = ti.tm_sec;
      colonOn = !colonOn;
      drawColonBlink(ti);
    }
  }

  // weather refresh (only if time passed; will refresh when WiFi is back)
  if (millis() - lastWeatherMs > WEATHER_PERIOD_MS) {
    if (fetchWeatherZip(weather)) {
      drawWeather(weather);
    } else {
      // keep last weather, but show OFFLINE if wifi down
      drawWeather(weather);
    }
    lastWeatherMs = millis();
  }

  delay(20);
}
