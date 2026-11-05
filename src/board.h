// board.h — per-board hardware: screen size, power-up, inputs, battery, LEDs.
// Select with -DBOARD_TDISPLAY, -DBOARD_TEMBED_CC1101 or -DBOARD_TDONGLE_S3 (see platformio.ini).
#pragma once
#include <Arduino.h>

// EV_ADVANCE: scroll the list a screenful, or move to the next page when at the end
enum Ev : uint8_t { EV_NONE, EV_NEXT, EV_UP, EV_DOWN, EV_PAGEDOWN, EV_ADVANCE, EV_PAUSE, EV_SCREEN, EV_FLIP };

// shared debounced button: returns 0 none, 1 short (on release), 2 long (once, while held)
struct Btn { uint8_t pin; bool down; uint32_t t0; bool longFired; };
static int pollBtn(Btn& b, uint32_t longMs = 800) {
  bool d = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (d && !b.down) { b.down = true; b.t0 = now; b.longFired = false; }
  else if (d && b.down && !b.longFired && now - b.t0 > longMs) { b.longFired = true; return 2; }
  else if (!d && b.down) { b.down = false; if (!b.longFired && now - b.t0 > 30) return 1; }
  return 0;
}

// =====================================================================
#if defined(BOARD_TEMBED_CC1101)
// LilyGo T-Embed CC1101 — ESP32-S3, 1.9" ST7789 320x170, rotary encoder,
// side key, 8x WS2812, BQ27220 fuel gauge. Pins from LilyGo's utilities.h.
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

#define BOARD_NAME   "T-Embed CC1101"
#define ROT_NORMAL   3
#define ROT_FLIPPED  1
#define PIN_PWR_EN   15
#define PIN_ENC_A    4
#define PIN_ENC_B    5
#define PIN_ENC_KEY  0
#define PIN_SIDE_KEY 6
#define PIN_LED      14
#define NUM_LEDS     8
#define I2C_SDA      8
#define I2C_SCL      18
#define BQ27220_ADDR 0x55
// other devices on the shared SPI bus: keep them deselected
#define CS_SD        13
#define CS_CC1101    12
#define CS_NRF24     44
#define CE_NRF24     43

static Adafruit_NeoPixel leds(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);
static Btn bKnob = {PIN_ENC_KEY, false, 0, false}, bSide = {PIN_SIDE_KEY, false, 0, false};
static volatile int32_t encRaw = 0;
static volatile uint8_t encPrev = 0;
static int32_t encLast = 0;
static bool gaugeOk = false;

static const int8_t ENC_TABLE[4][4] = {{0, -1, 1, 0}, {1, 0, 0, -1}, {-1, 0, 0, 1}, {0, 1, -1, 0}};
static void IRAM_ATTR encISR() {
  uint8_t cur = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  encRaw += ENC_TABLE[encPrev][cur];
  encPrev = cur;
}

static void boardPreDisplay() {
  pinMode(PIN_PWR_EN, OUTPUT); digitalWrite(PIN_PWR_EN, HIGH);   // powers display + peripherals
  for (int p : {CS_SD, CS_CC1101, CS_NRF24}) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
  pinMode(CE_NRF24, OUTPUT); digitalWrite(CE_NRF24, LOW);
  delay(20);
}

static void boardInit() {
  pinMode(PIN_ENC_A, INPUT_PULLUP); pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_KEY, INPUT_PULLUP); pinMode(PIN_SIDE_KEY, INPUT_PULLUP);
  encPrev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(PIN_ENC_A, encISR, CHANGE);
  attachInterrupt(PIN_ENC_B, encISR, CHANGE);
  leds.begin(); leds.setBrightness(40); leds.clear(); leds.show();
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(BQ27220_ADDR);
  gaugeOk = Wire.endTransmission() == 0;
}

static int gaugeRead(uint8_t cmd) {
  Wire.beginTransmission(BQ27220_ADDR); Wire.write(cmd);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(BQ27220_ADDR, 2) != 2) return -1;
  int lo = Wire.read(), hi = Wire.read();
  return lo | (hi << 8);
}

// volts, and percent (-1 if no gauge)
static void boardBattery(float& v, int& pct) {
  if (!gaugeOk) { v = 0; pct = -1; return; }
  int mv = gaugeRead(0x08), soc = gaugeRead(0x2C);
  v = mv > 0 ? mv / 1000.0f : 0;
  pct = (soc >= 0 && soc <= 100) ? soc : -1;
}

static Ev boardInput(bool /*flipped*/) {
  int k = pollBtn(bKnob), s = pollBtn(bSide);
  if (k == 1) return EV_NEXT;
  if (k == 2) return EV_SCREEN;
  if (s == 1) return EV_PAUSE;
  if (s == 2) return EV_FLIP;
  int32_t d = (encRaw - encLast) / 2;          // 2 transitions per detent
  if (d) { encLast += d * 2; return d > 0 ? EV_DOWN : EV_UP; }
  return EV_NONE;
}

// LED ring: amber comet while scanning Wi-Fi, green on BLE, dim red when paused, red flash on tracker
static void boardLeds(bool on, bool paused, uint8_t scanState, bool alert, uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 40) return;
  last = now;
  leds.clear();
  if (on) {
    if (alert && (now / 150) % 2) for (int i = 0; i < NUM_LEDS; i++) leds.setPixelColor(i, 255, 20, 20);
    else if (paused) leds.setPixelColor(0, 60, 0, 0);
    else if (scanState) {
      uint8_t r = scanState == 1 ? 255 : 57, g = scanState == 1 ? 176 : 255, b = scanState == 1 ? 0 : 20;
      int head = (now / 90) % NUM_LEDS;
      for (int t = 0; t < 3; t++) {                    // head + fading tail
        int i = (head - t + NUM_LEDS) % NUM_LEDS, k = t == 0 ? 255 : t == 1 ? 70 : 18;
        leds.setPixelColor(i, r * k / 255, g * k / 255, b * k / 255);
      }
    }
  }
  leds.show();
}

// =====================================================================
#elif defined(BOARD_TDONGLE_S3)
// LilyGo T-Dongle S3 (and S3 Plus) — ESP32-S3, 0.96" ST7735 160x80, one BOOT
// button, one APA102 RGB LED, USB powered. Pins from LilyGo's T-Dongle-S3 repo.
#define BOARD_NAME   "T-Dongle S3"
#define ROT_NORMAL   1
#define ROT_FLIPPED  3
#define PIN_BTN      0
#define PIN_LED_DI   40
#define PIN_LED_CI   39

static bool btnDown = false;
static uint32_t btnT0 = 0;

static void apaByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) { digitalWrite(PIN_LED_DI, (b >> i) & 1); digitalWrite(PIN_LED_CI, 1); digitalWrite(PIN_LED_CI, 0); }
}
static void apaSet(uint8_t r, uint8_t g, uint8_t b, uint8_t bright = 6) {   // bright 0..31
  for (int i = 0; i < 4; i++) apaByte(0x00);                 // start frame
  apaByte(0xE0 | (bright & 31)); apaByte(b); apaByte(g); apaByte(r);
  for (int i = 0; i < 4; i++) apaByte(0xFF);                 // end frame
}

static void boardPreDisplay() {}

static void boardInit() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_DI, OUTPUT); pinMode(PIN_LED_CI, OUTPUT);
  apaSet(0, 0, 0, 0);
}

static void boardBattery(float& v, int& pct) { v = 5.0f; pct = -1; }   // USB powered

// one button: click = next, hold 1 s = pause, hold 3 s = flip (acts on release)
static Ev boardInput(bool) {
  bool d = digitalRead(PIN_BTN) == LOW;
  uint32_t now = millis();
  if (d && !btnDown) { btnDown = true; btnT0 = now; }
  else if (!d && btnDown) {
    btnDown = false;
    uint32_t held = now - btnT0;
    if (held >= 3000) return EV_FLIP;
    if (held >= 800)  return EV_PAUSE;
    if (held >= 30)   return EV_ADVANCE;
  }
  return EV_NONE;
}

// LED: amber pulse while scanning Wi-Fi, green on BLE, dim red paused, red flash on tracker;
// while the button is held it shows what release will do (amber = pause, magenta = flip)
static void boardLeds(bool, bool paused, uint8_t scanState, bool alert, uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 40) return;
  last = now;
  if (btnDown) {
    uint32_t held = now - btnT0;
    if (held >= 3000) { apaSet(255, 0, 255, 8); return; }
    if (held >= 800)  { apaSet(255, 176, 0, 8); return; }
  }
  if (alert && (now / 150) % 2) { apaSet(255, 20, 20, 12); return; }
  if (paused) { apaSet(60, 0, 0, 2); return; }
  if (!scanState) { apaSet(0, 0, 0, 0); return; }
  uint8_t br = 1 + (uint8_t)(((now / 60) % 20) < 10 ? (now / 60) % 10 : 9 - (now / 60) % 10) / 2;   // gentle pulse
  if (scanState == 1) apaSet(255, 176, 0, br); else apaSet(57, 255, 20, br);
}

// =====================================================================
#else  // BOARD_TDISPLAY (default) — LilyGo TTGO T-Display, ESP32, 240x135
#define BOARD_NAME   "T-Display"
#define ROT_NORMAL   1
#define ROT_FLIPPED  3
#define BTN_L   0
#define BTN_R   35
#define ADC_EN  14
#define ADC_PIN 34

static Btn bl = {BTN_L, false, 0, false}, br = {BTN_R, false, 0, false};
static bool comboActive = false, comboDone = false;
static uint32_t comboT0 = 0;

static void boardPreDisplay() {}

static void boardInit() {
  pinMode(BTN_L, INPUT_PULLUP);
  pinMode(BTN_R, INPUT);                 // GPIO35 has an external pull-up
  pinMode(ADC_EN, OUTPUT); digitalWrite(ADC_EN, HIGH);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);
}

static void boardBattery(float& v, int& pct) { v = analogReadMilliVolts(ADC_PIN) * 2 / 1000.0f; pct = -1; }

static Ev boardInput(bool flipped) {
  bool l = digitalRead(BTN_L) == LOW, r = digitalRead(BTN_R) == LOW;
  if (l && r) {                                       // both held ~1 s: flip
    if (!comboActive) { comboActive = true; comboDone = false; comboT0 = millis(); }
    bl.longFired = br.longFired = true;
    if (!comboDone && millis() - comboT0 > 1000) { comboDone = true; return EV_FLIP; }
  } else if (comboActive && !l && !r) comboActive = false;
  int a = pollBtn(bl), b = pollBtn(br);
  if (comboActive) return EV_NONE;
  int L = flipped ? b : a, R = flipped ? a : b;       // keep "left" on your left
  if (L == 1) return EV_NEXT;
  if (L == 2) return EV_SCREEN;
  if (R == 1) return EV_PAGEDOWN;
  if (R == 2) return EV_PAUSE;
  return EV_NONE;
}

static void boardLeds(bool, bool, uint8_t, bool, uint32_t) {}
#endif
