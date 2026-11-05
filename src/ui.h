// ui.h — T-Scan screens in the Anubis Darkwatch look.
// Pure drawing into the global sprite `spr`; no hardware access, so the same
// code also compiles on a PC for previews (tools/preview).
#pragma once
#include "brand_fonts.h"

#define FW_VER  "v1.3"
#define MAX_AP  48
#define MAX_BLE 64
#define HIST    40

// ---------- data ----------
struct WifiAP { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t ch; uint8_t auth; };
struct BleDev { uint8_t addr[6]; char name[24]; int8_t rssi; uint8_t tag; bool rnd; uint32_t first, last; };

enum : uint8_t { T_NONE, T_FINDMY, T_APPLE, T_TILE, T_SMARTTAG, T_FLIPPER, T_MESH, T_IBEACON, T_MSFT };
static const char* TAGN[] = {"", "FINDMY", "APPLE", "TILE", "SMARTTAG", "FLIPPER", "MESH", "IBEACON", "MSFT"};
static inline bool isTracker(uint8_t t) { return t == T_FINDMY || t == T_TILE || t == T_SMARTTAG; }

enum { P_WIFI, P_CHAN, P_BLE, P_SUM, P_COUNT };

struct UiData {
  const WifiAP* ap; int nAp;           // sorted strongest first
  const BleDev* ble; int nBle;         // sorted trackers first, then strongest
  bool paused; uint8_t scanState;      // 0 idle, 1 wifi, 2 ble
  uint32_t wifiScans, bleScans, bleTotal;
  const uint8_t* histW; const uint8_t* histB; int histN;   // oldest -> newest
  float vbat; int battPct;            // battPct -1 = unknown (no fuel gauge)
  uint32_t now;
  int page, scroll;                    // scroll = first row index
  bool alert;                          // new tracker: flash the frame
};

// ---------- brand palette (RGB565) ----------
#define C_BG     0x0000  // #000000
#define C_AMB    0xFD80  // #ffb000 amber
#define C_AMB_D  0x7AA0  // #7a5400
#define C_AMB_F  0x2900  // #2e2000
#define C_GRN    0x3FE2  // #39ff14 neon green
#define C_GRN_D  0x1BC1  // #1a7a0a
#define C_GRN_F  0x0940  // #0a2a05
#define C_RED    0xF965  // #ff2d2d alert
#define C_GRID   0x18E3  // #1a1a1a

// screen size comes from the board profile; default = T-Display
#ifndef SCR_W
#define SCR_W 240
#define SCR_H 135
#endif
#ifndef UI_HINT
#define UI_HINT "L page  R scroll  hold both: flip"
#endif
static const int UW = SCR_W, UH = SCR_H;
static const int ROW_Y = 28, ROW_H = 15, ROWS = (UH - 14 - ROW_Y) / ROW_H;

// ---------- primitives ----------
static const uint8_t* curFont = nullptr;
static void useFont(const BrandFont& f) {
  if (curFont == f.data) return;
  if (curFont) spr.unloadFont();
  spr.loadFont(f.data);
  spr.gFont.spaceWidth = f.space;     // library guesses space width; use the real one
  curFont = f.data;
}

static void txt(const char* s, int x, int y, uint16_t fg, uint16_t bg = C_BG, uint8_t datum = TL_DATUM) {
  spr.setTextDatum(datum);
  spr.setTextColor(fg, bg);
  spr.drawString(s, x, y);
  spr.setTextDatum(TL_DATUM);
}

// draw s clipped to maxW px, ending in "~" when cut (UTF-8 safe)
static void fitTxt(const char* s, int x, int y, int maxW, uint16_t fg) {
  char b[48]; strlcpy(b, s, sizeof b);
  if (spr.textWidth(b) > maxW) {
    int n = strlen(b), tw = spr.textWidth("~");
    while (n > 0) {
      n--; while (n > 0 && (b[n] & 0xC0) == 0x80) n--;
      b[n] = 0;
      if (spr.textWidth(b) + tw <= maxW) break;
    }
    strcat(b, "~");
  }
  txt(b, x, y, fg);
}

static void icon(const uint8_t* a, int w, int h, int x, int y, uint16_t c) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint8_t v = pgm_read_byte(a + j * w + i);
      if (!v) continue;
      spr.drawPixel(x + i, y + j, v >= 250 ? c : spr.alphaBlend(v, c, spr.readPixel(x + i, y + j)));
    }
}

static void brackets(int x0, int y0, int x1, int y1, int L, uint16_t c) {
  spr.drawFastHLine(x0, y0, L, c);          spr.drawFastVLine(x0, y0, L, c);
  spr.drawFastHLine(x1 - L + 1, y0, L, c);  spr.drawFastVLine(x1, y0, L, c);
  spr.drawFastHLine(x0, y1, L, c);          spr.drawFastVLine(x0, y1 - L + 1, L, c);
  spr.drawFastHLine(x1 - L + 1, y1, L, c);  spr.drawFastVLine(x1, y1 - L + 1, L, c);
}

static uint16_t sigCol(int r) { return r >= -60 ? C_GRN : (r >= -75 ? C_AMB : C_RED); }

static void meter(int x, int y, int segs, int r, bool dim = false) {   // rssi -95..-35
  int lv = (r + 95) * segs / 60; if (lv < 0) lv = 0; if (lv > segs) lv = segs;
  if (r > -95 && lv == 0) lv = 1;
  for (int i = 0; i < segs; i++)
    spr.fillRect(x + i * 4, y, 3, 7, i < lv ? (dim ? C_AMB_D : sigCol(r)) : C_GRID);
}

static int pill(const char* s, int xr, int y, uint16_t c, bool filled) {  // right-aligned at xr; STM_11 loaded
  int w = spr.textWidth(s) + 8, x = xr - w;
  if (filled) { spr.fillRoundRect(x, y, w, 12, 3, c); txt(s, x + 4, y + 1, C_BG, c); }
  else        { spr.drawRoundRect(x, y, w, 12, 3, c); txt(s, x + 4, y + 1, c); }
  return w;
}

static void spark(const uint8_t* h, int n, int x, int y, int w, int ht, uint16_t c) {
  spr.drawFastHLine(x, y + ht, w, C_GRID);
  if (n < 2) return;
  int mx = 1; for (int i = 0; i < n; i++) if (h[i] > mx) mx = h[i];
  int px = x, py = y + ht - h[0] * ht / mx;
  for (int i = 1; i < n; i++) {
    int nx = x + i * (w - 1) / (HIST - 1), ny = y + ht - h[i] * ht / mx;
    spr.drawLine(px, py, nx, ny, c);
    px = nx; py = ny;
  }
  spr.fillRect(px - 1, py - 1, 3, 3, c);
}

// ---------- chrome ----------
static void header(const UiData& d, const char* title, const char* right, uint16_t rc) {
  icon(JACKAL_S, JACKAL_S_W, JACKAL_S_H, 3, 0, C_AMB);
  useFont(F_ORB_12); txt(title, 29, 5, C_AMB);
  useFont(F_STM_11); txt(right, UW - 4, 6, rc, C_BG, TR_DATUM);
  spr.drawFastHLine(0, 22, UW, d.paused ? C_RED : C_AMB_F);
  if (!d.paused && d.scanState) {                          // sweep while scanning
    const int w = 56; int p = (int)((d.now / 5) % (UW + w)) - w;
    uint16_t c = d.scanState == 1 ? C_GRN : C_AMB;
    for (int i = 0; i < w; i++) {
      int x = p + i; if (x < 0 || x >= UW) continue;
      uint8_t a = (uint8_t)(255 * i / w);
      spr.drawPixel(x, 22, spr.alphaBlend(a, c, C_AMB_F));
      spr.drawPixel(x, 21, spr.alphaBlend(a / 4, c, C_BG));
    }
  }
}

static void footer(const UiData& d, const char* right) {
  int x = UW / 2 - (3 * 8 + 14) / 2;
  for (int i = 0; i < P_COUNT; i++) {
    if (i == d.page) { spr.fillRect(x, UH - 6, 14, 3, C_AMB); x += 18; }
    else             { spr.fillRect(x, UH - 6, 4, 3, C_AMB_D); x += 8; }
  }
  useFont(F_STM_11);
  const char* st = d.paused ? "PAUSED" : d.scanState == 1 ? "SCAN WIFI" : d.scanState == 2 ? "SCAN BLE" : "IDLE";
  txt(st, 6, UH - 11, d.paused ? C_RED : C_AMB_D);
  txt(right, UW - 6, UH - 11, C_AMB_D, C_BG, TR_DATUM);
}

static void listFooter(const UiData& d, int n) {
  char b[16];
  if (n > ROWS) snprintf(b, sizeof b, "%d-%d/%d", d.scroll + 1, d.scroll + ROWS < n ? d.scroll + ROWS : n, n);
  else b[0] = 0;
  footer(d, b);
}

static void emptyMsg(const char* s) { useFont(F_STM_14); txt(s, UW / 2, UH / 2 - 1, C_AMB_D, C_BG, MC_DATUM); }

// ---------- channel maths ----------
static void chanScores(const WifiAP* a, int n, int* count, float* score) {
  for (int c = 0; c < 14; c++) { count[c] = 0; score[c] = 0; }
  for (int i = 0; i < n; i++) {
    int ch = a[i].ch; if (ch < 1 || ch > 13) continue;
    count[ch]++;
    float w = a[i].rssi + 100; if (w < 0) w = 0; if (w > 70) w = 70;
    for (int c = 1; c <= 13; c++) { int dd = abs(c - ch); if (dd <= 4) score[c] += w * (5 - dd) / 5.0f; }
  }
}
static int bestChannel(const float* s) { int b = 1; if (s[6] < s[b]) b = 6; if (s[11] < s[b]) b = 11; return b; }

// ---------- pages ----------
static void pageWifi(const UiData& d) {
  int open = 0; for (int i = 0; i < d.nAp; i++) if (d.ap[i].auth == 0) open++;
  char r[28]; snprintf(r, sizeof r, "%d APS / %d OPEN", d.nAp, open);
  header(d, "WI-FI", r, open ? C_GRN : C_AMB_D);
  if (!d.nAp) { emptyMsg(d.wifiScans ? "no networks" : "listening..."); footer(d, ""); return; }
  int last = d.scroll + ROWS < d.nAp ? d.scroll + ROWS : d.nAp;
  useFont(F_STM_14);                                            // pass 1: names
  for (int i = d.scroll; i < last; i++) {
    const WifiAP& w = d.ap[i]; int y = ROW_Y + (i - d.scroll) * ROW_H;
    bool hidden = !w.ssid[0], isOpen = w.auth == 0;
    if (isOpen) spr.fillRect(5, y + 2, 2, 11, C_GRN);
    fitTxt(hidden ? "<hidden>" : w.ssid, 10, y, UW - 112, hidden ? C_AMB_D : (isOpen ? C_GRN : C_AMB));
  }
  useFont(F_STM_11);                                            // pass 2: numbers
  for (int i = d.scroll; i < last; i++) {
    const WifiAP& w = d.ap[i]; int y = ROW_Y + (i - d.scroll) * ROW_H;
    char b[12]; snprintf(b, sizeof b, "CH%u", w.ch); txt(b, UW - 68, y + 2, C_AMB_D, C_BG, TR_DATUM);
    meter(UW - 63, y + 4, 8, w.rssi);
    snprintf(b, sizeof b, "%d", w.rssi); txt(b, UW - 6, y + 2, sigCol(w.rssi), C_BG, TR_DATUM);
  }
  listFooter(d, d.nAp);
}

static void pageChan(const UiData& d) {
  int count[14]; float score[14]; chanScores(d.ap, d.nAp, count, score);
  int best = bestChannel(score);
  char r[20]; if (d.nAp) snprintf(r, sizeof r, "BEST CH %d", best); else strcpy(r, "NO DATA");
  header(d, "SPECTRUM", r, C_GRN);
  const int base = UH - 29, top = 30, maxH = base - top - 6;
  const float step = (UW - 36) / 12.0f;
  auto chX = [step](float c) { return 18 + (int)((c - 1) * step); };   // ch1 .. ch13 across the width
  for (int c = 1; c <= 13; c += 5) for (int y = top; y < base; y += 3) spr.drawPixel(chX(c), y, C_GRID);
  if (d.nAp) for (int y = top; y < base; y += 4) spr.drawFastVLine(chX(best), y, 2, C_AMB_D);

  // weakest first so strong curves land on top
  for (int k = d.nAp - 1; k >= 0; k--) {
    const WifiAP& w = d.ap[k]; if (w.ch < 1 || w.ch > 13) continue;
    int h = (w.rssi + 95) * maxH / 60; if (h < 3) h = 3; if (h > maxH) h = maxH;
    uint16_t line = k < 3 ? C_GRN : C_GRN_D;
    int cx = chX(w.ch), px = -1, py = 0;
    for (int x = chX(w.ch - 2.2f); x <= chX(w.ch + 2.2f); x++) {
      float dx = (x - cx) / (step * 2.2f), f = 1 - dx * dx; if (f < 0) f = 0;
      int y = base - (int)(h * f);
      if (x > 6 && x < UW - 6) {
        if (y < base) spr.drawFastVLine(x, y, base - y, C_GRN_F);
        if (px >= 0) spr.drawLine(px, py, x, y, line);
      }
      px = x; py = y;
    }
  }
  spr.drawFastHLine(6, base, UW - 12, C_AMB_D);
  useFont(F_STM_11);
  for (int c = 1; c <= 13; c++) {
    char b[4]; snprintf(b, sizeof b, "%d", c);
    bool key = c == 1 || c == 6 || c == 11;
    txt(b, chX(c) + 1, base + 3, (d.nAp && c == best) ? C_GRN : key ? C_AMB : C_AMB_D, C_BG, TC_DATUM);
  }
  // label the three strongest at their peaks, skipping collisions
  int lx[3], ly[3], nl = 0;
  for (int k = 0; k < d.nAp && k < 3; k++) {
    const WifiAP& w = d.ap[k]; if (w.ch < 1 || w.ch > 13 || !w.ssid[0]) continue;
    int h = (w.rssi + 95) * maxH / 60; if (h > maxH) h = maxH;
    int x = chX(w.ch), y = base - h - 13; if (y < top - 4) y = top - 4;
    bool clash = false; for (int j = 0; j < nl; j++) if (abs(lx[j] - x) < 60 && abs(ly[j] - y) < 12) clash = true;
    if (clash) continue;
    char b[14]; strlcpy(b, w.ssid, sizeof b);
    int tw = spr.textWidth(b); int tx = x - tw / 2; if (tx < 6) tx = 6; if (tx + tw > UW - 6) tx = UW - 6 - tw;
    txt(b, tx, y, C_GRN); lx[nl] = x; ly[nl] = y; nl++;
  }
  char f[20]; snprintf(f, sizeof f, "%d APS", d.nAp); footer(d, f);
}

static void pageBle(const UiData& d) {
  int trk = 0; for (int i = 0; i < d.nBle; i++) if (isTracker(d.ble[i].tag)) trk++;
  char r[28]; snprintf(r, sizeof r, "%d DEV / %d TRK", d.nBle, trk);
  header(d, "BLE", r, trk ? C_RED : C_AMB_D);
  if (!d.nBle) { emptyMsg(d.bleScans ? "no devices" : "listening..."); footer(d, ""); return; }
  int last = d.scroll + ROWS < d.nBle ? d.scroll + ROWS : d.nBle;
  int tagW[ROWS] = {0};
  useFont(F_STM_11);                                            // pass 1: tags + numbers
  for (int i = d.scroll; i < last; i++) {
    const BleDev& b = d.ble[i]; int y = ROW_Y + (i - d.scroll) * ROW_H;
    bool stale = d.now - b.last > 15000;
    if (b.tag) {
      uint16_t c = isTracker(b.tag) ? C_RED : b.tag == T_MESH ? C_GRN : b.tag == T_FLIPPER ? C_AMB : C_GRN_D;
      tagW[i - d.scroll] = pill(TAGN[b.tag], UW - 58, y + 1, stale ? C_AMB_D : c, isTracker(b.tag) && !stale) + 4;
    }
    meter(UW - 54, y + 4, 6, b.rssi, stale);
    char s[8]; snprintf(s, sizeof s, "%d", b.rssi); txt(s, UW - 6, y + 2, stale ? C_AMB_D : sigCol(b.rssi), C_BG, TR_DATUM);
  }
  useFont(F_STM_14);                                            // pass 2: names
  for (int i = d.scroll; i < last; i++) {
    const BleDev& b = d.ble[i]; int y = ROW_Y + (i - d.scroll) * ROW_H;
    bool stale = d.now - b.last > 15000;
    char label[24];
    if (b.name[0]) strlcpy(label, b.name, sizeof label);
    else snprintf(label, sizeof label, "%02X:%02X:%02X:%02X:%02X", b.addr[4], b.addr[3], b.addr[2], b.addr[1], b.addr[0]);
    uint16_t c = stale ? C_AMB_D : isTracker(b.tag) ? C_RED : b.name[0] ? C_AMB : C_AMB_D;
    if (isTracker(b.tag) && !stale) spr.fillRect(5, y + 2, 2, 11, C_RED);
    fitTxt(label, 10, y, UW - 68 - tagW[i - d.scroll], c);
  }
  listFooter(d, d.nBle);
}

static void pageSum(const UiData& d) {
  int open = 0, trk = 0;
  for (int i = 0; i < d.nAp; i++) if (d.ap[i].auth == 0) open++;
  for (int i = 0; i < d.nBle; i++) if (isTracker(d.ble[i].tag)) trk++;
  int count[14]; float score[14]; chanScores(d.ap, d.nAp, count, score);
  char batt[20]; bool low;
  if (d.battPct >= 0) { snprintf(batt, sizeof batt, "BATT %d%%", d.battPct); low = d.battPct < 20; }
  else if (d.vbat > 4.4f) { strcpy(batt, "USB PWR"); low = false; }
  else { snprintf(batt, sizeof batt, "BATT %.2fV", d.vbat); low = d.vbat < 3.6f; }
  header(d, "OVERVIEW", batt, low ? C_RED : C_GRN);

  const int cw = (UW - 6) / 3, cx[3] = {10, 10 + cw, 10 + 2 * cw}, ly[2] = {28, 28 + (UH - 38) / 2};
  const char* lab[6] = {"WI-FI APS", "OPEN", "BEST CH", "BLE NOW", "TRACKERS", "BLE SEEN"};
  char val[6][10];
  snprintf(val[0], 10, "%d", d.nAp);  snprintf(val[1], 10, "%d", open);
  if (d.nAp) snprintf(val[2], 10, "%d", bestChannel(score)); else strcpy(val[2], "--");
  snprintf(val[3], 10, "%d", d.nBle); snprintf(val[4], 10, "%d", trk);
  snprintf(val[5], 10, "%lu", (unsigned long)d.bleTotal);
  uint16_t col[6] = {C_AMB, (uint16_t)(open ? C_GRN : C_AMB), C_GRN, C_AMB, (uint16_t)(trk ? C_RED : C_AMB), C_AMB};

  useFont(F_STM_11);
  for (int i = 0; i < 6; i++) txt(lab[i], cx[i % 3], ly[i / 3], C_AMB_D);
  useFont(F_ORB_22);
  for (int i = 0; i < 6; i++) txt(val[i], cx[i % 3], ly[i / 3] + 13, col[i]);
  for (int c = 1; c < 3; c++) for (int y = 30; y < UH - 17; y += 3) spr.drawPixel(cx[c] - 6, y, C_GRID);
  spr.drawFastHLine(10, ly[1] - 4, UW - 20, C_GRID);
  spark(d.histW, d.histN, 10, ly[0] + 35, cw - 14, 6, C_GRN_D);
  spark(d.histB, d.histN, 10, ly[1] + 35, cw - 14, 6, C_GRN_D);

  uint32_t up = d.now / 1000; char f[16];
  snprintf(f, sizeof f, "UP %lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60), (unsigned long)(up % 60));
  footer(d, f);
}

// ---------- post effects ----------
static void scanlines() {   // dim every other row: CRT texture
  uint16_t* p = (uint16_t*)spr.getPointer();
  for (int y = 1; y < UH; y += 2) {
    uint16_t* r = p + y * UW;
    for (int x = 0; x < UW; x++) {
      uint16_t v = r[x]; if (!v) continue;
      v = (v >> 8) | (v << 8);                               // sprite stores byte-swapped
      uint16_t R = (v >> 11) & 31, G = (v >> 5) & 63, B = v & 31;
      v = ((R * 3 / 4) << 11) | ((G * 3 / 4) << 5) | (B * 3 / 4);
      r[x] = (v >> 8) | (v << 8);
    }
  }
}

static void uiRender(const UiData& d) {
  spr.fillSprite(C_BG);
  switch (d.page) {
    case P_WIFI: pageWifi(d); break;
    case P_CHAN: pageChan(d); break;
    case P_BLE:  pageBle(d);  break;
    default:     pageSum(d);  break;
  }
  brackets(1, 25, UW - 2, UH - 14, 6, C_AMB_D);
  if (d.alert && (d.now / 150) % 2) { spr.drawRect(0, 0, UW, UH, C_RED); spr.drawRect(1, 1, UW - 2, UH - 2, C_RED); }
  scanlines();
}

// t: 0..1 CRT power-on, then the title card
static void uiSplash(float t) {
  const int ox = (UW - 240) / 2, oy = (UH - 135) / 2, my = UH / 2;
  spr.fillSprite(C_BG);
  brackets(3, 3, UW - 4, UH - 4, 10, C_AMB_D);
  icon(JACKAL_L, JACKAL_L_W, JACKAL_L_H, ox + 18, oy + 18, C_AMB);
  useFont(F_ORB_26); txt("T-SCAN", ox + 100, oy + 26, C_AMB);
  useFont(F_STM_11);
  txt("WI-FI / BLE RECON", ox + 101, oy + 58, C_AMB_D);
  txt(FW_VER, ox + 101, oy + 72, C_AMB_D);
  spr.drawFastHLine(ox + 18, oy + 91, 204, C_AMB_F);
  txt("the watcher in the static", UW / 2, oy + 98, C_GRN, C_BG, TC_DATUM);
  txt(UI_HINT, UW / 2, oy + 114, C_AMB_D, C_BG, TC_DATUM);
  scanlines();
  if (t < 0.35f) {                                           // beam line grows
    spr.fillSprite(C_BG);
    int w = (int)(UW * t / 0.35f);
    spr.drawFastHLine(UW / 2 - w / 2, my, w, C_AMB);
    spr.drawFastHLine(UW / 2 - w / 2, my - 1, w, C_AMB_D);
    spr.drawFastHLine(UW / 2 - w / 2, my + 1, w, C_AMB_D);
  } else if (t < 0.7f) {                                     // picture opens vertically
    int band = (int)(UH * (t - 0.35f) / 0.35f), y0 = my - band / 2;
    spr.fillRect(0, 0, UW, y0 > 0 ? y0 : 0, C_BG);
    spr.fillRect(0, y0 + band, UW, UH, C_BG);
    spr.drawFastHLine(0, y0, UW, C_AMB_D);
    spr.drawFastHLine(0, y0 + band, UW, C_AMB_D);
  }
}
