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
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <time.h>
#include <LittleFS.h>

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

  // === Schedule check -- once per second ===
  if (scheduleEnabled && millis() - lastScheduleCheck >= 1000) {
    lastScheduleCheck = millis();
    checkSchedule();
  }

  // === LED update -- only when timer flag is set ===
  if (animFlag) {
    animFlag = false;
    if (orbOn) {
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

  if (DEBUG_ENABLED) {
    Serial.printf("Schedule loaded: %s %02d:%02d - %02d:%02d\n",
      scheduleEnabled ? "ON" : "OFF", startHour, startMinute, stopHour, stopMinute);
  }
}

void saveSchedule() {
  File f = LittleFS.open("/schedule.json", "w");
  if (!f) {
    if (DEBUG_ENABLED) Serial.println("Failed to save schedule");
    return;
  }
  f.printf("{\"en\":%d,\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d}",
    scheduleEnabled ? 1 : 0, startHour, startMinute, stopHour, stopMinute);
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
    resetAnimationState();
    if (DEBUG_ENABLED) Serial.println("Schedule: turning ON");
  } else if (!inWindow && !scheduledOff) {
    // Window just closed -- turn off
    scheduledOff = true;
    orbOn = false;
    allColor(0);
    if (DEBUG_ENABLED) Serial.println("Schedule: turning OFF");
  }
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
  <div style='text-align:center'>
    <a class='btn b0' onclick='setMode(0)' id='m0'>Rainbow Cycle</a>
    <a class='btn b1' onclick='setMode(1)' id='m1'>Solid Color</a>
    <a class='btn b2' onclick='setMode(2)' id='m2'>Rainbow</a>
    <a class='btn b3' onclick='setMode(3)' id='m3'>Theater Chase</a>
    <a class='btn b4' onclick='setMode(4)' id='m4'>Theater Rainbow</a>
  </div>
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

  function setMode(m){
    fetch('/mode?m='+m).then(function(){
      for(var i=0;i<5;i++){
        var el=document.getElementById('m'+i);
        if(i==m) el.classList.add('active');
        else el.classList.remove('active');
      }
    });
  }

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
      for(var i=0;i<5;i++){
        var el=document.getElementById('m'+i);
        if(i==d.mode) el.classList.add('active');
        else el.classList.remove('active');
      }
      // Schedule
      document.getElementById('schedEn').checked=d.schedEn;
      document.getElementById('schedStart').value=pad2(d.sh)+':'+pad2(d.sm);
      document.getElementById('schedStop').value=pad2(d.eh)+':'+pad2(d.em);
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
      allColor(0);
    } else {
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
