// IO-Orb: NeoPixel fiber optic orb controller
// Hardware: ESP-12S on protoboard, 24 NeoPixel strip
// Features: 5 animation modes, WiFi web interface, OTA updates
// Non-blocking design -- WiFi gets priority, LEDs update on a timer
//
// Architecture: The loop() runs WiFi services as fast as possible.
// A Ticker fires at the current speed setting to set a flag. When
// the flag is set, loop() runs ONE animation frame then goes back
// to servicing WiFi.

#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <time.h>
#include <LittleFS.h>

// Read the internal VDD rail (ESP.getVcc()) instead of the A0 pin. A0 is unused
// (only the archived pot sketches read it). Powers the NG-1 glitch guard, which
// treats a VCC dip as a supply-transient event. Must be at file scope.
ADC_MODE(ADC_VCC);

// ---- Pin Configuration ----
#define PIXEL_PIN     2     // GPIO2 -- NeoPixel data
#define PIXEL_COUNT   24
#define RAINBOW_CYCLE_LENGTH 1280

// ---- Animation Modes ----
#define MODE_RAINBOW_CYCLE    0
#define MODE_SOLID_COLOR      1
#define MODE_RAINBOW          2
#define MODE_THEATER_CHASE    3
#define MODE_THEATER_RAINBOW  4
#define NUM_MODES             5

// ---- WiFi Configuration ----
#include "config.h"
const char* wifi_ssid = WIFI_SSID;
const char* wifi_pass = WIFI_PASS;

// ---- Debug ----
#define DEBUG_ENABLED true

// ---- NeoPixel Strip ----
Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ---- Web Server ----
ESP8266WebServer server(80);

// ---- Animation Timer ----
Ticker animTicker;
volatile bool animFlag = false;

void IRAM_ATTR onAnimTimer() {
  animFlag = true;
}

// ---- Animation State ----
struct AnimationState {
  uint16_t step;
  uint8_t chasePos;
};
AnimationState animState;

// ---- Global State ----
int showType = MODE_RAINBOW_CYCLE;
int hueColor = 0;          // hue (0-255) for solid color, theater chase
int speedVal = 80;          // animation speed in ms per frame (30=fast, 500=slow)
int brightnessVal = 128;    // brightness (0-255)
bool orbOn = true;

// ---- Schedule State ----
bool scheduleEnabled = false;
int startHour = 18;           // default: 6:00 PM
int startMinute = 0;
int stopHour = 23;            // default: 11:00 PM
int stopMinute = 0;
bool scheduledOff = false;    // true when schedule has turned orb off
unsigned long lastScheduleCheck = 0;

// ---- Weather-Alarm State (WATG-1/2/3) ----
// At a set alarm time the orb does a gentle 30-min throb whose COLOR
// encodes today's weather (yellow=sunny, blue=cloudy, grey=rain/fog),
// fetched keyless over plain HTTP from Open-Meteo.
#define ALARM_THROB_MS   (30UL * 60UL * 1000UL)  // 30-minute throb window
#define WEATHER_PREFETCH_MIN  30                 // fetch this many min before alarm
#define THROB_TICK_MS    40                      // fast ticker while throbbing (smooth sine)
#define THROB_PERIOD_DEFAULT 3000                // ms per throb cycle (runtime-adjustable via /setthrob)
#define THROB_PERIOD_MIN 1000                    // 1 s = brisk
#define THROB_PERIOD_MAX 8000                    // 8 s = very slow/gentle
#define THROB_FLOOR      0.15f                    // never fully dark -- keep a gentle glow

bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;
int throbPeriodMs = THROB_PERIOD_DEFAULT;  // breathing period, set via web app

// runtime (not persisted)
bool alarmActive = false;              // currently inside the throb window
unsigned long alarmStartMs = 0;        // millis() when the throb window began
bool savedOrbOn = true;                // orbOn to restore when the throb ends
int lastAlarmFireYday = -1;            // tm_yday of last fire -> fire once/day
int lastWeatherFetchYday = -1;         // tm_yday of last prefetch -> fetch once/day

uint32_t weatherColor = 0;             // resolved throb color from last fetch
bool weatherValid = false;             // true once a fetch has succeeded

// ---- NeoPixel off-state glitch guard (NG-1) ----
// A nearby inductive load (fan) injects a supply transient the idle WS2812s
// latch as a dim glow while the orb is off (we stop refreshing). We can't read
// the strip, so we detect the CONDUCTED transient as a VCC dip -> an EVENT
// (counted + timestamped) that re-asserts dark. A radiated glitch leaves no VCC
// trace, so a bounded safety re-dark runs nightly as a fallback.
#define VCC_SAMPLE_MS         20        // sample VCC this often (catch brief dips)
#define VCC_WARMUP_SAMPLES    250       // ~5 s to settle the baseline before arming
#define VCC_DIP_MV            120       // a sample this far below baseline = event
#define VCC_EVENT_DEBOUNCE_MS 800       // ignore repeat events within this window
#define SAFETY_DARK_START_MIN (21*60)   // 21:00 PST -- nightly safety re-dark window
#define SAFETY_DARK_STOP_MIN  (23*60)   // 23:00 PST -- window end
#define SAFETY_DARK_INTERVAL_MS 15000UL // re-dark cadence inside the window

unsigned long lastVccSampleMs = 0;
uint16_t vccBaseline = 3000;           // rolling mV baseline (EMA), seeded ~3.0 V
uint16_t vccMin = 4000;                // lowest sample seen since boot (telemetry)
uint16_t vccLast = 0;                  // most recent sample
uint16_t vccSamples = 0;               // warmup counter (saturates)
uint16_t glitchEvents = 0;             // # of detected supply-dip events
unsigned long lastGlitchMs = 0;        // millis() of last event (0 = none)
unsigned long lastSafetyDarkMs = 0;    // last nightly safety re-dark

// ---- Forward Declarations ----
void startShow(int mode);
void resetAnimationState();
uint32_t Wheel(byte WheelPos);
void handleRoot();
void handleSetMode();
void handleSetBrightness();
void handleSetColor();
void handleSetSpeed();
void handleSetPower();
void handleStatus();
void handleGetSchedule();
void handleSetSchedule();
void allColor(uint32_t c);
void updateTickerSpeed();
void loadSchedule();
void saveSchedule();
void checkSchedule();
void checkAlarm();
void startAlarmThrob();
void endAlarmThrob();
void renderAlarmThrob();
bool fetchWeather();
uint32_t weatherCodeToColor(int code);
void handleGetAlarm();
void handleSetAlarm();
void handleSetThrob();
void handleFlashInfo();
void pollGlitchGuard();
void maybeSafetyDark();

// ---- Update ticker to match current speed ----
void updateTickerSpeed() {
  animTicker.detach();
  animTicker.attach_ms(speedVal, onAnimTimer);
}

void setup() {
  if (DEBUG_ENABLED) {
    Serial.begin(115200);
    Serial.println("\nIO-Orb starting...");
  }

  strip.begin();
  strip.setBrightness(brightnessVal);
  strip.show();

  resetAnimationState();

  // WiFi
  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("io-orb");
  WiFi.begin(wifi_ssid, wifi_pass);

  if (DEBUG_ENABLED) Serial.print("Connecting");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    if (DEBUG_ENABLED) Serial.print(".");
    tries++;
  }

  if (DEBUG_ENABLED) {
    Serial.println();
    Serial.print("Status: "); Serial.println(WiFi.status());
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("Subnet: "); Serial.println(WiFi.subnetMask());
    Serial.print("Gateway: "); Serial.println(WiFi.gatewayIP());
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());
    Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  }

  // NTP -- US Pacific with automatic DST
  configTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  if (DEBUG_ENABLED) Serial.println("NTP configured (Pacific time)");

  // LittleFS -- persistent storage for schedule
  if (LittleFS.begin()) {
    if (DEBUG_ENABLED) Serial.println("LittleFS mounted");
    loadSchedule();
  } else {
    if (DEBUG_ENABLED) Serial.println("LittleFS mount failed");
  }

  // OTA
  ArduinoOTA.setHostname("io-orb");
  ArduinoOTA.setPassword(wifi_pass);
  ArduinoOTA.begin();

  // mDNS
  MDNS.begin("io-orb");

  // Web server
  server.on("/", handleRoot);
  server.on("/mode", handleSetMode);
  server.on("/brightness", handleSetBrightness);
  server.on("/color", handleSetColor);
  server.on("/speed", handleSetSpeed);
  server.on("/power", handleSetPower);
  server.on("/status", handleStatus);
  server.on("/schedule", handleGetSchedule);
  server.on("/setschedule", handleSetSchedule);
  server.on("/alarm", handleGetAlarm);
  server.on("/setalarm", handleSetAlarm);
  server.on("/setthrob", handleSetThrob);
  server.on("/flashinfo", handleFlashInfo);
  server.begin();

  // Start animation timer at default speed
  animTicker.attach_ms(speedVal, onAnimTimer);
  if (DEBUG_ENABLED) { Serial.print("Animation speed: "); Serial.print(speedVal); Serial.println("ms"); }
}

void loop() {
  // === WiFi services -- run EVERY loop iteration ===
  ArduinoOTA.handle();
  MDNS.update();
  server.handleClient();
  yield();

  // === NG-1: watch VDD for a supply-transient event (self-rate-limited) ===
  pollGlitchGuard();

  // === Schedule + alarm check -- once per second ===
  if (millis() - lastScheduleCheck >= 1000) {
    lastScheduleCheck = millis();
    // Expire the throb window first so the restored state is authoritative
    if (alarmActive && millis() - alarmStartMs >= ALARM_THROB_MS) {
      endAlarmThrob();
    }
    // Schedule yields to an active alarm throb (the alarm overrides on/off)
    if (scheduleEnabled && !alarmActive) checkSchedule();
    if (alarmEnabled) checkAlarm();
    maybeSafetyDark();   // NG-1 fallback: nightly re-dark for radiated glitches
  }

  // === LED update -- only when timer flag is set ===
  if (animFlag) {
    animFlag = false;
    if (alarmActive) {
      renderAlarmThrob();     // throb overrides the normal animation
    } else if (orbOn) {
      startShow(showType);
    }
    yield();
  }
}

// ---- Reset animation state ----
void resetAnimationState() {
  animState.step = 0;
  animState.chasePos = 0;
}

// ---- Animation Dispatcher ----
void startShow(int mode) {
  switch (mode) {
    case MODE_RAINBOW_CYCLE:    rainbowCycle(); break;
    case MODE_SOLID_COLOR:      colorSet();     break;
    case MODE_RAINBOW:          rainbow();      break;
    case MODE_THEATER_CHASE:    theaterChase(); break;
    case MODE_THEATER_RAINBOW:  theaterChaseRainbow(); break;
  }
}

// ---- Solid color from hueColor ----
void colorSet() {
  uint32_t c = Wheel((uint8_t)hueColor);
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

// ---- Rainbow ----
void rainbow() {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, Wheel((i + animState.step) & 255));
  }
  strip.show();
  animState.step = (animState.step + 1) % 256;
}

// ---- Rainbow Cycle ----
void rainbowCycle() {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + animState.step) & 255));
  }
  strip.show();
  animState.step = (animState.step + 1) % RAINBOW_CYCLE_LENGTH;
}

// ---- Theater Chase ----
void theaterChase() {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);
  }
  for (uint16_t i = 0; i < strip.numPixels(); i += 3) {
    uint16_t idx = i + animState.chasePos;
    if (idx < strip.numPixels()) {
      strip.setPixelColor(idx, Wheel((uint8_t)hueColor));
    }
  }
  strip.show();
  animState.chasePos = (animState.chasePos + 1) % 3;
}

// ---- Theater Chase Rainbow ----
// Color rotates every frame (not just every 3rd) for visible rainbow effect
void theaterChaseRainbow() {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);
  }
  for (uint16_t i = 0; i < strip.numPixels(); i += 3) {
    uint16_t idx = i + animState.chasePos;
    if (idx < strip.numPixels()) {
      strip.setPixelColor(idx, Wheel((idx * 10 + animState.step) & 255));
    }
  }
  strip.show();
  animState.chasePos = (animState.chasePos + 1) % 3;
  animState.step = (animState.step + 3) % 256;  // advance color every frame, 3x faster
}

// ---- Color Wheel: 0-255 -> R->G->B->R ----
uint32_t Wheel(byte WheelPos) {
  if (WheelPos < 85) {
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
    WheelPos -= 170;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

// ---- Fill all pixels ----
void allColor(uint32_t c) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
  }
  strip.show();
}

// ==========================================================
// NeoPixel off-state glitch guard (NG-1)
// ==========================================================

// Sample VDD; a sample well below the rolling baseline is a conducted supply
// transient (the fan). Count it, and re-assert dark when we are supposed to be
// off (a throb/animation already refreshes every frame, so a glitch there is
// overwritten anyway). Self-rate-limited to VCC_SAMPLE_MS.
void pollGlitchGuard() {
  unsigned long nowMs = millis();
  if (nowMs - lastVccSampleMs < VCC_SAMPLE_MS) return;
  lastVccSampleMs = nowMs;

  uint16_t v = ESP.getVcc();
  vccLast = v;
  if (v < vccMin) vccMin = v;
  vccBaseline = (uint16_t)(((uint32_t)vccBaseline * 15 + v) / 16);   // slow EMA

  if (vccSamples < VCC_WARMUP_SAMPLES) { vccSamples++; return; }     // let baseline settle

  if (v + VCC_DIP_MV < vccBaseline &&
      (lastGlitchMs == 0 || nowMs - lastGlitchMs > VCC_EVENT_DEBOUNCE_MS)) {
    glitchEvents++;
    lastGlitchMs = nowMs;
    if (!orbOn && !alarmActive) allColor(0);   // clear any latched glow
    if (DEBUG_ENABLED) Serial.printf("NG-1: dip %u mV (base %u) event #%u\n",
                                     (unsigned)(vccBaseline - v), vccBaseline, glitchEvents);
  }
}

// Fallback for RADIATED glitches the VCC probe can't see: while off and NTP-
// synced, between 21:00-23:00 PST re-push the dark frame every ~15 s. Bounded to
// the window on purpose - not a 24/7 re-blit.
void maybeSafetyDark() {
  if (orbOn || alarmActive) return;
  time_t now = time(nullptr);
  if (now < 100000) return;                    // need NTP
  struct tm* t = localtime(&now);
  int nowMin = t->tm_hour * 60 + t->tm_min;
  if (nowMin < SAFETY_DARK_START_MIN || nowMin >= SAFETY_DARK_STOP_MIN) return;
  if (millis() - lastSafetyDarkMs < SAFETY_DARK_INTERVAL_MS) return;
  lastSafetyDarkMs = millis();
  allColor(0);
}

// ==========================================================
// Schedule Persistence & Enforcement
// ==========================================================

void loadSchedule() {
  File f = LittleFS.open("/schedule.json", "r");
  if (!f) {
    if (DEBUG_ENABLED) Serial.println("No schedule file, using defaults");
    return;
  }
  String data = f.readString();
  f.close();

  // Minimal JSON parsing -- format: {"en":1,"sh":18,"sm":0,"eh":23,"em":0}
  int idx;
  idx = data.indexOf("\"en\":");
  if (idx >= 0) scheduleEnabled = data.substring(idx + 5).toInt() == 1;
  idx = data.indexOf("\"sh\":");
  if (idx >= 0) startHour = constrain(data.substring(idx + 5).toInt(), 0, 23);
  idx = data.indexOf("\"sm\":");
  if (idx >= 0) startMinute = constrain(data.substring(idx + 5).toInt(), 0, 59);
  idx = data.indexOf("\"eh\":");
  if (idx >= 0) stopHour = constrain(data.substring(idx + 5).toInt(), 0, 23);
  idx = data.indexOf("\"em\":");
  if (idx >= 0) stopMinute = constrain(data.substring(idx + 5).toInt(), 0, 59);
  // Weather-alarm fields (absent in pre-alarm files -> keep defaults)
  idx = data.indexOf("\"aen\":");
  if (idx >= 0) alarmEnabled = data.substring(idx + 6).toInt() == 1;
  idx = data.indexOf("\"ah\":");
  if (idx >= 0) alarmHour = constrain(data.substring(idx + 5).toInt(), 0, 23);
  idx = data.indexOf("\"am\":");
  if (idx >= 0) alarmMinute = constrain(data.substring(idx + 5).toInt(), 0, 59);
  idx = data.indexOf("\"tp\":");
  if (idx >= 0) throbPeriodMs = constrain(data.substring(idx + 5).toInt(), THROB_PERIOD_MIN, THROB_PERIOD_MAX);

  if (DEBUG_ENABLED) {
    Serial.printf("Schedule loaded: %s %02d:%02d - %02d:%02d | Alarm: %s %02d:%02d\n",
      scheduleEnabled ? "ON" : "OFF", startHour, startMinute, stopHour, stopMinute,
      alarmEnabled ? "ON" : "OFF", alarmHour, alarmMinute);
  }
}

void saveSchedule() {
  File f = LittleFS.open("/schedule.json", "w");
  if (!f) {
    if (DEBUG_ENABLED) Serial.println("Failed to save schedule");
    return;
  }
  f.printf("{\"en\":%d,\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d,\"aen\":%d,\"ah\":%d,\"am\":%d,\"tp\":%d}",
    scheduleEnabled ? 1 : 0, startHour, startMinute, stopHour, stopMinute,
    alarmEnabled ? 1 : 0, alarmHour, alarmMinute, throbPeriodMs);
  f.close();
  if (DEBUG_ENABLED) Serial.println("Schedule saved");
}

void checkSchedule() {
  time_t now = time(nullptr);
  if (now < 100000) return;  // NTP not synced yet

  struct tm* t = localtime(&now);
  int nowMinutes = t->tm_hour * 60 + t->tm_min;
  int startMinutes = startHour * 60 + startMinute;
  int stopMinutes = stopHour * 60 + stopMinute;

  // Determine if we're inside the active window
  bool inWindow;
  if (startMinutes <= stopMinutes) {
    // Same-day window (e.g., 08:00 - 23:00)
    inWindow = (nowMinutes >= startMinutes && nowMinutes < stopMinutes);
  } else {
    // Cross-midnight window (e.g., 20:00 - 06:00)
    inWindow = (nowMinutes >= startMinutes || nowMinutes < stopMinutes);
  }

  if (inWindow && scheduledOff) {
    // Window just opened -- turn on
    scheduledOff = false;
    orbOn = true;
    strip.setBrightness(brightnessVal);
    resetAnimationState();
    if (DEBUG_ENABLED) Serial.println("Schedule: turning ON");
  } else if (!inWindow && !scheduledOff) {
    // Window just closed -- turn off
    scheduledOff = true;
    orbOn = false;
    strip.setBrightness(0);
    allColor(0);
    if (DEBUG_ENABLED) Serial.println("Schedule: turning OFF");
  }
}

// ==========================================================
// Weather Alarm (WATG-1/2/3)
// ==========================================================

// WMO weathercode -> throb color. Mapping resolved in TODOS WATG-D2.
// RGB values are first-pass; calibrate on the actual fiber bundle.
uint32_t weatherCodeToColor(int code) {
  if (code <= 1)  return strip.Color(255, 190, 0);  // 0-1 clear     -> yellow (sunny)
  if (code <= 3)  return strip.Color(0, 70, 255);   // 2-3 cloudy    -> blue
  return strip.Color(90, 90, 90);                    // fog/rain/etc  -> grey
}

// Fetch current conditions from Open-Meteo over PLAIN HTTP (keyless, no
// TLS -- see PLATFORM_PHYSICS). Blocking, but called at most once/day
// from the 1 Hz tick, never from the render path. Returns true on success.
bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
             + WEATHER_LAT + "&longitude=" + WEATHER_LON + "&current_weather=true";
  if (!http.begin(client, url)) return false;
  http.setTimeout(8000);
  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    if (DEBUG_ENABLED) Serial.printf("Weather fetch HTTP %d\n", status);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  // The JSON carries "weathercode" TWICE: first as a units string inside
  // "current_weather_units", then as the real value inside
  // "current_weather". Anchor on the current_weather object so we read the
  // number, not the "wmo code" units string.
  int cw = payload.indexOf("\"current_weather\":");
  if (cw < 0) return false;
  int idx = payload.indexOf("\"weathercode\":", cw);
  if (idx < 0) return false;
  int wcode = payload.substring(idx + 14).toInt();

  weatherColor = weatherCodeToColor(wcode);
  weatherValid = true;
  if (DEBUG_ENABLED) Serial.printf("Weather: code=%d -> color set\n", wcode);
  return true;
}

// 1 Hz: prefetch weather ~30 min before the alarm, then fire the throb at
// the alarm minute. Both are once-per-day and guarded on NTP sync.
void checkAlarm() {
  time_t now = time(nullptr);
  if (now < 100000) return;  // NTP not synced yet -- never fire on a ~1970 clock

  struct tm* t = localtime(&now);
  int nowMinutes = t->tm_hour * 60 + t->tm_min;
  int alarmMinutes = alarmHour * 60 + alarmMinute;
  int prefetchMinutes = (alarmMinutes - WEATHER_PREFETCH_MIN + 1440) % 1440;

  // Prefetch window (once/day)
  if (t->tm_yday != lastWeatherFetchYday && nowMinutes == prefetchMinutes) {
    lastWeatherFetchYday = t->tm_yday;
    fetchWeather();
  }

  // Alarm fire (once/day)
  if (!alarmActive && t->tm_yday != lastAlarmFireYday && nowMinutes == alarmMinutes) {
    lastAlarmFireYday = t->tm_yday;
    startAlarmThrob();
  }
}

void startAlarmThrob() {
  // If the prefetch was missed (e.g. booted after it), grab weather now.
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (t->tm_yday != lastWeatherFetchYday) {
    lastWeatherFetchYday = t->tm_yday;
    fetchWeather();
  }
  savedOrbOn = orbOn;           // remember state to restore after the window
  alarmActive = true;
  alarmStartMs = millis();
  orbOn = true;                 // ensure LEDs run during the throb
  strip.setBrightness(255);     // throb dims via color scaling, not global brightness
  animTicker.attach_ms(THROB_TICK_MS, onAnimTimer);  // fast, smooth sine
  if (DEBUG_ENABLED) Serial.println("Alarm: throb START");
}

void endAlarmThrob() {
  alarmActive = false;
  updateTickerSpeed();                 // restore the user's animation speed
  strip.setBrightness(brightnessVal);  // restore brightness
  orbOn = savedOrbOn;
  if (orbOn) {
    resetAnimationState();             // resume the normal animation
  } else {
    allColor(0);                       // was off -> push an explicit dark frame (55797f7)
  }
  if (DEBUG_ENABLED) Serial.println("Alarm: throb END");
}

// One throb frame: a slow sine on brightness at the weather color,
// millis()-phased so it is smooth regardless of tick rate, floored so it
// never goes fully dark. Scales the COLOR (not strip.setBrightness) to
// keep hue resolution.
void renderAlarmThrob() {
  float period = (float)throbPeriodMs;
  float phase = (float)(millis() % (unsigned long)period) / period;
  float s = (sinf(phase * 2.0f * PI - PI / 2.0f) + 1.0f) * 0.5f;  // 0..1, starts low
  float scale = THROB_FLOOR + (1.0f - THROB_FLOOR) * s;
  uint32_t c = weatherValid ? weatherColor : strip.Color(120, 120, 120);  // neutral fallback
  uint8_t r = (uint8_t)(((c >> 16) & 0xFF) * scale);
  uint8_t g = (uint8_t)(((c >> 8) & 0xFF) * scale);
  uint8_t b = (uint8_t)((c & 0xFF) * scale);
  uint32_t scaled = strip.Color(r, g, b);
  for (uint16_t i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i, scaled);
  strip.show();
}

// ==========================================================
// Web Server Handlers
// ==========================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<title>IO-Orb</title>
<style>
  body { font-family: -apple-system, sans-serif; background: #1a1a2e; color: #eee;
         max-width: 420px; margin: 0 auto; padding: 20px; }
  h1 { text-align: center; color: #e94560; }
  .card { background: #16213e; border-radius: 12px; padding: 16px; margin: 12px 0; }
  .pwr { display: block; width: 100%; padding: 16px; border: none; border-radius: 12px;
         font-size: 20px; font-weight: bold; cursor: pointer; color: #fff;
         transition: background 0.2s; }
  .pwr:active { transform: scale(0.97); }
  .pwr-on { background: #e94560; }
  .pwr-off { background: #333; }
  .btn { display: inline-block; padding: 12px 20px; margin: 4px; border: none;
         border-radius: 8px; color: #fff; font-size: 15px; cursor: pointer;
         text-decoration: none; text-align: center; min-width: 80px; }
  .btn:active { transform: scale(0.95); }
  .b0 { background: linear-gradient(135deg, #ff6b6b, #feca57, #48dbfb, #ff9ff3); }
  .b1 { background: #e94560; }
  .b2 { background: linear-gradient(135deg, #ff6b6b, #48dbfb); }
  .b3 { background: #0f3460; }
  .b4 { background: linear-gradient(135deg, #0f3460, #e94560); }
  .active { outline: 3px solid #fff; outline-offset: 2px; }
  #modeSel { width: 100%; padding: 12px; font-size: 16px; border-radius: 8px;
         background: #0f3460; color: #eee; border: 1px solid #444; -webkit-appearance: none;
         appearance: none; text-align: center; }
  .dim { opacity: 0.35; pointer-events: none; }
  .slider-wrap { margin: 14px 0; }
  .slider-wrap label { display: block; margin-bottom: 6px; font-size: 14px; }
  input[type=range] { width: 100%; height: 28px; -webkit-appearance: none; border-radius: 8px;
         outline: none; }
  input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 28px; height: 28px;
         border-radius: 50%; background: #fff; cursor: pointer; border: 2px solid #333; }
  #hueSlider { background: #333; }
  #speedSlider { background: linear-gradient(to right, #e94560, #16213e);
         accent-color: #e94560; }
  #throbSlider { background: linear-gradient(to right, #feca57, #0f3460);
         accent-color: #feca57; }
  #brightSlider { background: linear-gradient(to right, #222, #fff);
         accent-color: #e94560; }
  .swatch { width: 48px; height: 48px; border-radius: 50%; border: 3px solid #fff;
         display: inline-block; vertical-align: middle; margin-left: 12px; }
  #status { text-align: center; font-size: 13px; color: #888; margin-top: 16px; }
  .sched-row { display: flex; align-items: center; justify-content: space-between;
         margin: 10px 0; }
  .sched-row label { font-size: 14px; }
  input[type=time] { background: #0f3460; color: #eee; border: 1px solid #444;
         border-radius: 8px; padding: 8px 12px; font-size: 16px; }
  .toggle { position: relative; width: 50px; height: 28px; }
  .toggle input { opacity: 0; width: 0; height: 0; }
  .toggle .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
         background: #333; border-radius: 28px; transition: 0.3s; }
  .toggle .slider:before { content: ''; position: absolute; height: 22px; width: 22px;
         left: 3px; bottom: 3px; background: #fff; border-radius: 50%; transition: 0.3s; }
  .toggle input:checked + .slider { background: #e94560; }
  .toggle input:checked + .slider:before { transform: translateX(22px); }
  #devTime { text-align: center; font-size: 12px; color: #666; margin-top: 8px; }
</style>
</head>
<body>
<h1>IO-Orb</h1>

<div class='card'>
  <button class='pwr pwr-on' id='pwr' onclick='togglePower()'>ON</button>
</div>

<div class='card' id='modes'>
  <select id='modeSel' onchange='setMode(this.value)'>
    <option value='0'>Rainbow Cycle</option>
    <option value='1'>Solid Color</option>
    <option value='2'>Rainbow</option>
    <option value='3'>Theater Chase</option>
    <option value='4'>Theater Rainbow</option>
  </select>
</div>

<div class='card' id='sliders'>
  <div class='slider-wrap'>
    <label>Color <span class='swatch' id='colorSwatch'></span></label>
    <input type='range' min='0' max='255' value='0' id='hueSlider'>
  </div>
  <div class='slider-wrap'>
    <label>Speed: <span id='sv'>80</span>ms</label>
    <input type='range' min='30' max='500' value='80' id='speedSlider'>
  </div>
  <div class='slider-wrap'>
    <label>Brightness: <span id='bv'>128</span></label>
    <input type='range' min='5' max='255' value='128' id='brightSlider'>
  </div>
</div>

<div class='card' id='schedCard'>
  <div class='sched-row'>
    <label>Schedule</label>
    <label class='toggle'><input type='checkbox' id='schedEn' onchange='sendSchedule()'><span class='slider'></span></label>
  </div>
  <div class='sched-row'>
    <label>Start</label>
    <input type='time' id='schedStart' value='18:00' onchange='sendSchedule()'>
  </div>
  <div class='sched-row'>
    <label>Stop</label>
    <input type='time' id='schedStop' value='23:00' onchange='sendSchedule()'>
  </div>
  <div id='devTime'></div>
</div>

<div class='card' id='alarmCard'>
  <div class='sched-row'>
    <label>Weather Alarm</label>
    <label class='toggle'><input type='checkbox' id='alarmEn' onchange='sendAlarm()'><span class='slider'></span></label>
  </div>
  <div class='sched-row'>
    <label>Alarm time</label>
    <input type='time' id='alarmTime' value='07:00' onchange='sendAlarm()'>
  </div>
  <div class='slider-wrap'>
    <label>Throb speed: <span id='tv'>3.0</span>s / breath</label>
    <input type='range' min='1000' max='8000' step='250' value='3000' id='throbSlider'
      oninput="document.getElementById('tv').textContent=(this.value/1000).toFixed(1)"
      onchange="fetch('/setthrob?ms='+this.value)">
  </div>
  <div id='alarmHint' style='font-size:12px;color:#888;margin-top:8px'>
    Gentle throb at alarm time in today's weather color:
    <span style='color:#ffbe00'>&#9679; sunny</span>
    <span style='color:#3a7bff'>&#9679; cloudy</span>
    <span style='color:#9a9a9a'>&#9679; rain/fog</span>
  </div>
</div>

<div id='status'>IO-Orb</div>

<script>
  var isOn = true;

  // Hue to RGB using same Wheel() math as firmware
  function hueToRgb(h) {
    var r, g, b;
    if (h < 85) { r=h*3; g=255-h*3; b=0; }
    else if (h < 170) { h-=85; r=255-h*3; g=0; b=h*3; }
    else { h-=170; r=0; g=h*3; b=255-h*3; }
    return 'rgb('+r+','+g+','+b+')';
  }

  // Build hue slider gradient from Wheel() math so bar matches swatch exactly
  (function(){
    var stops=[];
    for(var i=0;i<=16;i++){
      var h=Math.round(i*255/16);
      stops.push(hueToRgb(h)+' '+((i*100/16).toFixed(1))+'%');
    }
    document.getElementById('hueSlider').style.background=
      'linear-gradient(to right,'+stops.join(',')+')';
  })();

  function updateUI(){
    var p=document.getElementById('pwr');
    p.textContent=isOn?'TURN ME OFF':'TURN ME ON';
    p.className=isOn?'pwr pwr-on':'pwr pwr-off';
    document.getElementById('modes').className=isOn?'card':'card dim';
    document.getElementById('sliders').className=isOn?'card':'card dim';
  }

  function togglePower(){
    isOn=!isOn;
    fetch('/power?on='+(isOn?'1':'0'));
    updateUI();
  }

  function setMode(m){ fetch('/mode?m='+m); }

  // Poll sliders using requestAnimationFrame + throttled sends
  // rAF runs during touch on iOS, setInterval does not
  var lastHue=-1, lastSpd=-1, lastBrt=-1;
  var lastSend=0, lastSync=0;
  function pollSliders(){
    requestAnimationFrame(pollSliders);
    var now=Date.now();
    // Sync full status every 5s via rAF (more reliable than setInterval on mobile)
    if(now-lastSync>=5000){ lastSync=now; syncStatus(); }
    var h=parseInt(document.getElementById('hueSlider').value);
    // Always update swatch immediately (local, no network)
    if(h!==lastHue){
      document.getElementById('colorSwatch').style.background=hueToRgb(h);
    }
    var s=parseInt(document.getElementById('speedSlider').value);
    if(s!==lastSpd) document.getElementById('sv').textContent=s;
    var b=parseInt(document.getElementById('brightSlider').value);
    if(b!==lastBrt) document.getElementById('bv').textContent=b;
    // Throttle network sends to every 150ms
    if(now-lastSend<150) return;
    if(h!==lastHue){ lastHue=h; lastSend=now; fetch('/color?c='+h); }
    else if(s!==lastSpd){ lastSpd=s; lastSend=now; fetch('/speed?s='+s); }
    else if(b!==lastBrt){ lastBrt=b; lastSend=now; fetch('/brightness?b='+b); }
  }
  requestAnimationFrame(pollSliders);

  function sendSchedule(){
    var en=document.getElementById('schedEn').checked?1:0;
    var st=document.getElementById('schedStart').value.split(':');
    var et=document.getElementById('schedStop').value.split(':');
    fetch('/setschedule?en='+en+'&sh='+st[0]+'&sm='+st[1]+'&eh='+et[0]+'&em='+et[1]);
  }

  function sendAlarm(){
    var en=document.getElementById('alarmEn').checked?1:0;
    var at=document.getElementById('alarmTime').value.split(':');
    fetch('/setalarm?aen='+en+'&ah='+at[0]+'&am='+at[1]);
  }

  // Pad number to 2 digits
  function pad2(n){ return n<10?'0'+n:''+n; }

  function syncStatus(){
    fetch('/status').then(r=>r.json()).then(d=>{
      isOn=d.power;
      lastHue=d.color;
      document.getElementById('hueSlider').value=d.color;
      document.getElementById('colorSwatch').style.background=hueToRgb(d.color);
      lastSpd=d.speed;
      document.getElementById('speedSlider').value=d.speed;
      document.getElementById('sv').textContent=d.speed;
      lastBrt=d.brightness;
      document.getElementById('brightSlider').value=d.brightness;
      document.getElementById('bv').textContent=d.brightness;
      document.getElementById('modeSel').value=d.mode;
      // Schedule
      document.getElementById('schedEn').checked=d.schedEn;
      document.getElementById('schedStart').value=pad2(d.sh)+':'+pad2(d.sm);
      document.getElementById('schedStop').value=pad2(d.eh)+':'+pad2(d.em);
      // Weather alarm
      document.getElementById('alarmEn').checked=d.alarmEn;
      document.getElementById('alarmTime').value=pad2(d.ah)+':'+pad2(d.am);
      document.getElementById('throbSlider').value=d.throbMs;
      document.getElementById('tv').textContent=(d.throbMs/1000).toFixed(1);
      if(d.time) document.getElementById('devTime').textContent='Device time: '+d.time;
      updateUI();
    });
  }
  // Load on open + refresh on tab focus/wake
  syncStatus();
  document.addEventListener('visibilitychange', function(){ if(!document.hidden) syncStatus(); });
  window.addEventListener('focus', syncStatus);
  window.addEventListener('pageshow', syncStatus);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleSetMode() {
  if (server.hasArg("m")) {
    int m = server.arg("m").toInt();
    if (m >= 0 && m < NUM_MODES) {
      showType = m;
      resetAnimationState();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSetBrightness() {
  if (server.hasArg("b")) {
    int b = server.arg("b").toInt();
    brightnessVal = constrain(b, 5, 255);
    strip.setBrightness(brightnessVal);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetColor() {
  if (server.hasArg("c")) {
    int c = server.arg("c").toInt();
    hueColor = constrain(c, 0, 255);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetSpeed() {
  if (server.hasArg("s")) {
    int s = server.arg("s").toInt();
    speedVal = constrain(s, 30, 500);
    updateTickerSpeed();
  }
  server.send(200, "text/plain", "OK");
}

void handleSetPower() {
  if (server.hasArg("on")) {
    orbOn = server.arg("on").toInt() == 1;
    if (!orbOn) {
      strip.setBrightness(0);
      allColor(0);
    } else {
      strip.setBrightness(brightnessVal);
      resetAnimationState();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"mode\":";
  json += showType;
  json += ",\"brightness\":";
  json += brightnessVal;
  json += ",\"color\":";
  json += hueColor;
  json += ",\"speed\":";
  json += speedVal;
  json += ",\"power\":";
  json += orbOn ? "true" : "false";
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",\"schedEn\":";
  json += scheduleEnabled ? "true" : "false";
  json += ",\"sh\":";
  json += startHour;
  json += ",\"sm\":";
  json += startMinute;
  json += ",\"eh\":";
  json += stopHour;
  json += ",\"em\":";
  json += stopMinute;
  // Weather alarm
  json += ",\"alarmEn\":";
  json += alarmEnabled ? "true" : "false";
  json += ",\"ah\":";
  json += alarmHour;
  json += ",\"am\":";
  json += alarmMinute;
  json += ",\"alarmActive\":";
  json += alarmActive ? "true" : "false";
  json += ",\"throbMs\":";
  json += throbPeriodMs;
  // NG-1 glitch guard telemetry
  json += ",\"glitchEvents\":";
  json += glitchEvents;
  json += ",\"vcc\":";
  json += vccLast;
  json += ",\"vccBaseline\":";
  json += vccBaseline;
  json += ",\"vccMin\":";
  json += vccMin;
  // Current device time
  time_t now = time(nullptr);
  if (now > 100000) {
    struct tm* t = localtime(&now);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
    json += ",\"time\":\"";
    json += buf;
    json += "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleGetSchedule() {
  String json = "{\"enabled\":";
  json += scheduleEnabled ? "true" : "false";
  json += ",\"startHour\":";
  json += startHour;
  json += ",\"startMinute\":";
  json += startMinute;
  json += ",\"stopHour\":";
  json += stopHour;
  json += ",\"stopMinute\":";
  json += stopMinute;
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSchedule() {
  if (server.hasArg("en"))
    scheduleEnabled = server.arg("en").toInt() == 1;
  if (server.hasArg("sh"))
    startHour = constrain(server.arg("sh").toInt(), 0, 23);
  if (server.hasArg("sm"))
    startMinute = constrain(server.arg("sm").toInt(), 0, 59);
  if (server.hasArg("eh"))
    stopHour = constrain(server.arg("eh").toInt(), 0, 23);
  if (server.hasArg("em"))
    stopMinute = constrain(server.arg("em").toInt(), 0, 59);

  // Reset schedule state so it re-evaluates immediately
  scheduledOff = false;

  saveSchedule();

  if (DEBUG_ENABLED) {
    Serial.printf("Schedule set: %s %02d:%02d - %02d:%02d\n",
      scheduleEnabled ? "ON" : "OFF", startHour, startMinute, stopHour, stopMinute);
  }
  server.send(200, "text/plain", "OK");
}

void handleGetAlarm() {
  String json = "{\"enabled\":";
  json += alarmEnabled ? "true" : "false";
  json += ",\"hour\":";
  json += alarmHour;
  json += ",\"minute\":";
  json += alarmMinute;
  json += ",\"active\":";
  json += alarmActive ? "true" : "false";
  json += "}";
  server.send(200, "application/json", json);
}

// Flash-layout canary: exposes the ACTUAL chip flash size vs the size this
// image was BUILT for. When they differ, the build FQBN/flash layout does not
// match the hardware -- the exact condition that bricks an OTA (RF-cal / FS
// land in the wrong sectors). flash.sh preflights this before any OTA and
// aborts on match=false. (The 2026-07-10 brick was actually a WiFi SSID case
// error, not layout; this canary covers the layout failure mode. See BUILD_LOG.)
void handleFlashInfo() {
  uint32_t real = ESP.getFlashChipRealSize();  // queried from the chip
  uint32_t conf = ESP.getFlashChipSize();      // what the image was built to assume
  String json = "{\"realSize\":";
  json += real;
  json += ",\"configuredSize\":";
  json += conf;
  json += ",\"match\":";
  json += (real == conf) ? "true" : "false";
  json += ",\"freeSketchSpace\":";
  json += ESP.getFreeSketchSpace();
  json += ",\"sketchSize\":";
  json += ESP.getSketchSize();
  json += ",\"flashMode\":";
  json += (int)ESP.getFlashChipMode();
  json += ",\"freeHeap\":";
  json += ESP.getFreeHeap();
  json += ",\"maxFreeBlock\":";
  json += ESP.getMaxFreeBlockSize();
  json += ",\"coreVersion\":\"";
  json += ESP.getCoreVersion();
  json += "\",\"sdkVersion\":\"";
  json += ESP.getSdkVersion();
  json += "\"}";
  server.send(200, "application/json", json);
}

void handleSetAlarm() {
  if (server.hasArg("aen"))
    alarmEnabled = server.arg("aen").toInt() == 1;
  if (server.hasArg("ah"))
    alarmHour = constrain(server.arg("ah").toInt(), 0, 23);
  if (server.hasArg("am"))
    alarmMinute = constrain(server.arg("am").toInt(), 0, 59);

  // Allow re-fire today if the alarm was edited to a still-future time
  lastAlarmFireYday = -1;
  lastWeatherFetchYday = -1;

  saveSchedule();  // alarm lives in the same file as the schedule

  if (DEBUG_ENABLED) {
    Serial.printf("Alarm set: %s %02d:%02d\n",
      alarmEnabled ? "ON" : "OFF", alarmHour, alarmMinute);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetThrob() {
  if (server.hasArg("ms")) {
    throbPeriodMs = constrain(server.arg("ms").toInt(), THROB_PERIOD_MIN, THROB_PERIOD_MAX);
    saveSchedule();  // persisted in the same file
    if (DEBUG_ENABLED) Serial.printf("Throb period set: %d ms\n", throbPeriodMs);
  }
  server.send(200, "text/plain", "OK");
}
