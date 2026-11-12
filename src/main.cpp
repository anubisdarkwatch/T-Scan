// T-Scan — pocket Wi-Fi / BLE scanner for LilyGo boards with screens.
// Anubis Darkwatch edition. Screens: ui.h. Board hardware: board.h. This file: scanning + app logic.
// Serial 115200: CSV log of everything seen.

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "board.h"

TFT_eSPI tft;
TFT_eSprite spr(&tft);
#include "ui.h"

// ---------- config ----------
#define BLE_SCAN_S     3        // seconds per BLE scan
#define WIFI_MS_PER_CH 120      // active scan dwell per channel
#define BLE_FORGET_MS  60000    // drop BLE devices not heard for this long
#define FRAME_MS       66       // ~15 fps (sweep animation)

// ---------- shared state ----------
static WifiAP aps[MAX_AP];   static int nAp = 0;
static BleDev ble[MAX_BLE];  static int nBle = 0;
static uint8_t histW[HIST], histB[HIST]; static int histN = 0;
static SemaphoreHandle_t mtx;
static volatile bool paused = false;
static volatile uint8_t scanState = 0;
static volatile uint32_t wifiScans = 0, bleScans = 0, bleTotal = 0;
static NimBLEScan* bscan;
static Preferences prefs;
static bool flipped = false;

// ---------- BLE classification ----------
static int short16(const NimBLEUUID& u) {
  if (u.bitSize() == 16) return u.getNative()->u16.value;
  if (u.bitSize() == 32) { uint32_t v = u.getNative()->u32.value; return v <= 0xFFFF ? (int)v : -1; }
  std::string s = u.toString();
  if (s.size() == 36 && s.compare(0, 4, "0000") == 0 && s.compare(8, 28, "-0000-1000-8000-00805f9b34fb") == 0)
    return (int)strtol(s.substr(4, 4).c_str(), nullptr, 16);
  return -1;
}

static uint8_t uuidTag(const NimBLEUUID& u) {
  int v = short16(u);
  if (v == 0xFEED || v == 0xFEEC) return T_TILE;
  if (v == 0xFD5A || v == 0xFD59) return T_SMARTTAG;
  if (v >= 0x3081 && v <= 0x3083) return T_FLIPPER;
  if (v < 0 && u.toString() == "6ba1b218-15a8-461f-9fa8-5dcae273eafd") return T_MESH;
  return T_NONE;
}

static uint8_t classify(NimBLEAdvertisedDevice* d) {
  for (int i = 0; i < (int)d->getServiceUUIDCount(); i++) { uint8_t t = uuidTag(d->getServiceUUID(i)); if (t) return t; }
  for (int i = 0; i < (int)d->getServiceDataCount(); i++) { uint8_t t = uuidTag(d->getServiceDataUUID(i)); if (t) return t; }
  if (d->haveManufacturerData()) {
    std::string m = d->getManufacturerData();
    if (m.size() >= 3) {
      uint16_t cid = (uint8_t)m[0] | ((uint8_t)m[1] << 8);
      if (cid == 0x004C) {
        uint8_t ty = (uint8_t)m[2];
        if (ty == 0x12) return T_FINDMY;
        if (ty == 0x02) return T_IBEACON;
        return T_APPLE;
      }
      if (cid == 0x0006) return T_MSFT;
    }
  }
  return T_NONE;
}

static void macStr(const uint8_t* a, char* out) {  // NimBLE stores LSB first
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

class AdvCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    const uint8_t* a = d->getAddress().getNative();
    uint32_t now = millis();
    uint8_t tag = classify(d);
    std::string nm = d->getName();
    bool isNew = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    int idx = -1;
    for (int i = 0; i < nBle; i++) if (!memcmp(ble[i].addr, a, 6)) { idx = i; break; }
    if (idx < 0) {
      if (nBle < MAX_BLE) idx = nBle++;
      else { idx = 0; for (int i = 1; i < nBle; i++) if (ble[i].last < ble[idx].last) idx = i; }
      memset(&ble[idx], 0, sizeof(BleDev));
      memcpy(ble[idx].addr, a, 6);
      ble[idx].first = now;
      ble[idx].rnd = d->getAddress().getType() == BLE_ADDR_RANDOM;
      isNew = true;
      bleTotal++;
    }
    BleDev& b = ble[idx];
    b.rssi = d->getRSSI();
    b.last = now;
    if (tag) b.tag = tag;
    if (!nm.empty()) strlcpy(b.name, nm.c_str(), sizeof(b.name));
    BleDev copy = b;
    xSemaphoreGive(mtx);
    if (isNew) {
      char mac[18]; macStr(copy.addr, mac);
      Serial.printf("B,%lu,%s,%s,\"%s\",%d,%s\n", now, mac, copy.rnd ? "rnd" : "pub", copy.name, copy.rssi, TAGN[copy.tag]);
    }
  }
};

// ---------- scanning task (core 0) ----------
static void scanTask(void*) {
  for (;;) {
    if (paused) { scanState = 0; vTaskDelay(pdMS_TO_TICKS(200)); continue; }

    scanState = 1;
    int n = WiFi.scanNetworks(false, true, false, WIFI_MS_PER_CH);
    if (n >= 0) {
      static WifiAP tmp[MAX_AP];
      int k = 0;
      for (int i = 0; i < n && k < MAX_AP; i++) {
        WifiAP& w = tmp[k++];
        strlcpy(w.ssid, WiFi.SSID(i).c_str(), sizeof(w.ssid));
        memcpy(w.bssid, WiFi.BSSID(i), 6);
        w.rssi = WiFi.RSSI(i); w.ch = WiFi.channel(i); w.auth = WiFi.encryptionType(i);
      }
      for (int i = 1; i < k; i++) { WifiAP t = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j].rssi < t.rssi) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = t; }
      uint32_t now = millis();
      for (int i = 0; i < k; i++) {
        const WifiAP& w = tmp[i];
        Serial.printf("W,%lu,%02X:%02X:%02X:%02X:%02X:%02X,\"%s\",%u,%d,%u\n", now,
          w.bssid[0], w.bssid[1], w.bssid[2], w.bssid[3], w.bssid[4], w.bssid[5], w.ssid, w.ch, w.rssi, w.auth);
      }
      xSemaphoreTake(mtx, portMAX_DELAY);
      memcpy(aps, tmp, sizeof(WifiAP) * k); nAp = k;
      xSemaphoreGive(mtx);
    }
    WiFi.scanDelete();
    wifiScans++;
    if (paused) continue;

    scanState = 2;
    bscan->start(BLE_SCAN_S, false);
    bscan->clearResults();
    uint32_t now = millis();
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < nBle;) { if (now - ble[i].last > BLE_FORGET_MS) ble[i] = ble[--nBle]; else i++; }
    // history for the overview sparklines
    if (histN == HIST) { memmove(histW, histW + 1, HIST - 1); memmove(histB, histB + 1, HIST - 1); histN--; }
    histW[histN] = nAp > 255 ? 255 : nAp; histB[histN] = nBle > 255 ? 255 : nBle; histN++;
    xSemaphoreGive(mtx);
    bleScans++;
  }
}

// ---------- UI state ----------
static int page = P_WIFI;
static int scrollRow[P_COUNT] = {0};
static int listLen[P_COUNT] = {0};
static bool screenOn = true;
static uint32_t alertUntil = 0;
static int lastTrk = 0;
static uint32_t slideT0 = 0;
static bool sliding = false;
#if defined(BOARD_TEMBED_CC1101)
static bool powerMenu = false, confirmPowerOff = false;
static uint8_t powerIndex = 0;
static const char* powerNotice = nullptr;
#endif

static int bleCmp(const void* x, const void* y) {
  const BleDev* a = (const BleDev*)x; const BleDev* b = (const BleDev*)y;
  bool ta = isTracker(a->tag), tb = isTracker(b->tag);
  if (ta != tb) return ta ? -1 : 1;
  return b->rssi - a->rssi;
}

static void renderFrame() {
  static WifiAP la[MAX_AP]; static BleDev lb[MAX_BLE]; static uint8_t hw[HIST], hb[HIST];
  static float vb = 0; static int pct = -1; static uint32_t vbT = 0;
  UiData d;
  xSemaphoreTake(mtx, portMAX_DELAY);
  d.nAp = nAp;   memcpy(la, aps, sizeof(WifiAP) * nAp);
  d.nBle = nBle; memcpy(lb, ble, sizeof(BleDev) * nBle);
  d.histN = histN; memcpy(hw, histW, histN); memcpy(hb, histB, histN);
  xSemaphoreGive(mtx);
  qsort(lb, d.nBle, sizeof(BleDev), bleCmp);

  int trk = 0; for (int i = 0; i < d.nBle; i++) if (isTracker(lb[i].tag)) trk++;
  if (trk > lastTrk) alertUntil = millis() + 1800;
  lastTrk = trk;
  if (millis() - vbT > 2000 || vbT == 0) { boardBattery(vb, pct); vbT = millis(); }

  listLen[P_WIFI] = d.nAp; listLen[P_BLE] = d.nBle;
  int n = listLen[page];
  if (scrollRow[page] > 0 && scrollRow[page] >= n) scrollRow[page] = 0;

  d.ap = la; d.ble = lb; d.histW = hw; d.histB = hb;
  d.paused = paused; d.scanState = scanState;
  d.wifiScans = wifiScans; d.bleScans = bleScans; d.bleTotal = bleTotal;
  d.vbat = vb; d.battPct = pct; d.now = millis();
  d.page = page; d.scroll = scrollRow[page];
  d.alert = millis() < alertUntil;
#if defined(BOARD_TEMBED_CC1101)
  if (powerMenu) uiPowerMenu(powerIndex, confirmPowerOff, powerNotice);
  else
#endif
    uiRender(d);

  if (sliding) {                         // new page slides in from the right
    float t = (millis() - slideT0) / 180.0f;
    if (t >= 1) { sliding = false; spr.pushSprite(0, 0); }
    else { float e = 1 - (1 - t) * (1 - t) * (1 - t); spr.pushSprite((int)(UW * (1 - e)), 0); }
  } else spr.pushSprite(0, 0);
}

static void setBacklight(bool on) { digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON); }

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n# boot: start (%s)\n", BOARD_NAME);
  boardPreDisplay();
  boardInit();

  prefs.begin("tscan", false);
  flipped = prefs.getBool("flip", false);

  Serial.println("# boot: display init");
  tft.init();
  pinMode(TFT_BL, OUTPUT); setBacklight(true);
  tft.setRotation(flipped ? ROT_FLIPPED : ROT_NORMAL);
  tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(16);
  if (!spr.createSprite(UW, UH)) Serial.println("# boot: SPRITE ALLOC FAILED");

  // CRT power-on (~0.7 s), title card stays up while radios start
  for (uint32_t t0 = millis(), t = 0; t < 700; t = millis() - t0) { uiSplash(t / 700.0f); spr.pushSprite(0, 0); }
  uiSplash(1); spr.pushSprite(0, 0);
  Serial.println("# boot: display OK");

  Serial.printf("# T-Scan %s\n# W,ms,bssid,ssid,ch,rssi,auth\n# B,ms,mac,addr_type,name,rssi,tag\n", FW_VER);
  Serial.println("# boot: wifi");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.println("# boot: ble");
  NimBLEDevice::init("");
  bscan = NimBLEDevice::getScan();
  bscan->setAdvertisedDeviceCallbacks(new AdvCB(), true);
  bscan->setActiveScan(true);
  bscan->setInterval(100);
  bscan->setWindow(80);
  bscan->setMaxResults(0);

  mtx = xSemaphoreCreateMutex();
  delay(1400);
  Serial.println("# boot: scanning");
  xTaskCreatePinnedToCore(scanTask, "scan", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  bool dirty = false;
  Ev e = boardInput(flipped);
  int n = listLen[page], maxTop = n > ROWS ? n - ROWS : 0;
#if defined(BOARD_TEMBED_CC1101)
  if (powerMenu) {
    switch (e) {
      case EV_DOWN:
        if (!confirmPowerOff) powerIndex = (powerIndex + 1) % 4;
        powerNotice = nullptr; dirty = true; break;
      case EV_UP:
        if (!confirmPowerOff) powerIndex = (powerIndex + 3) % 4;
        powerNotice = nullptr; dirty = true; break;
      case EV_PAUSE: // Side key backs out of confirmation or the menu.
        if (confirmPowerOff) confirmPowerOff = false;
        else powerMenu = false;
        powerNotice = nullptr; dirty = true; break;
      case EV_NEXT: // Encoder click selects / confirms.
        dirty = true;
        if (confirmPowerOff) {
          int err = boardPowerOff();
          confirmPowerOff = false;
          powerNotice = err == 1 ? "POWER CHIP NOT DETECTED" :
                        err == 2 ? "UNPLUG USB TO POWER OFF" : "SHUTDOWN FAILED";
        } else if (powerIndex == 0) {
          paused = true;
          scanState = 0;
          setBacklight(false);
          boardDeepSleep();
        } else if (powerIndex == 1) {
          confirmPowerOff = true;
        } else if (powerIndex == 2) {
          flipped = !flipped;
          prefs.putBool("flip", flipped);
          tft.setRotation(flipped ? ROT_FLIPPED : ROT_NORMAL);
          Serial.printf("# flipped=%d\n", flipped);
          powerMenu = false;
        } else powerMenu = false;
        break;
      default: break;
    }
  } else
#endif
  switch (e) {
    case EV_NEXT:
      if (!screenOn) { screenOn = true; setBacklight(true); }
      else { page = (page + 1) % P_COUNT; sliding = true; slideT0 = millis(); }
      dirty = true; break;
    case EV_SCREEN: screenOn = !screenOn; setBacklight(screenOn); break;
    case EV_ADVANCE:                       // one-button boards: page through the list, then next page
      if (n > ROWS && scrollRow[page] + ROWS < n) scrollRow[page] += ROWS;
      else { scrollRow[page] = 0; page = (page + 1) % P_COUNT; sliding = true; slideT0 = millis(); }
      dirty = true; break;
    case EV_PAGEDOWN: scrollRow[page] = scrollRow[page] + ROWS < n ? scrollRow[page] + ROWS : 0; dirty = true; break;
    case EV_DOWN: if (scrollRow[page] < maxTop) scrollRow[page]++; dirty = true; break;
    case EV_UP:   if (scrollRow[page] > 0) scrollRow[page]--; dirty = true; break;
    case EV_PAUSE: paused = !paused; dirty = true; break;
#if defined(BOARD_TEMBED_CC1101)
    case EV_MENU:
      powerMenu = true; confirmPowerOff = false;
      powerIndex = 0; powerNotice = nullptr;
      sliding = false; dirty = true; break;
#endif
    case EV_FLIP:
      flipped = !flipped;
      prefs.putBool("flip", flipped);
      tft.setRotation(flipped ? ROT_FLIPPED : ROT_NORMAL);
      Serial.printf("# flipped=%d\n", flipped);
      dirty = true; break;
    default: break;
  }
  if (e != EV_NONE && e != EV_SCREEN && !screenOn) { screenOn = true; setBacklight(true); }

  boardLeds(screenOn, paused, scanState, millis() < alertUntil, millis());

  static uint32_t lastDraw = 0;
  uint32_t frame = sliding ? 16 : FRAME_MS;
  if (screenOn && (dirty || millis() - lastDraw >= frame)) { renderFrame(); lastDraw = millis(); }
  delay(5);
}
