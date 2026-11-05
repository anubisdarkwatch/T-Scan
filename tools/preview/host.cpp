// Host-side preview: a minimal stand-in for TFT_eSprite that mirrors the
// TFT_eSPI behaviour ui.h relies on (VLW smooth fonts, datums, alpha blend,
// byte-swapped 16-bit buffer), then renders every screen to PPM.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

#ifndef SCR_W
#define SCR_W 240
#define SCR_H 135
#endif
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
enum { TL_DATUM, TC_DATUM, TR_DATUM, ML_DATUM, MC_DATUM, MR_DATUM, BL_DATUM, BC_DATUM, BR_DATUM };

struct FontMetrics { uint16_t gCount, yAdvance, spaceWidth; int16_t ascent, descent, maxAscent, maxDescent; };

class TFT_eSprite {
 public:
  static const int W = SCR_W, H = SCR_H;
  uint16_t buf[W * H];
  FontMetrics gFont{};
  const uint8_t* f = nullptr;
  std::vector<uint32_t> uni; std::vector<uint8_t> gh, gw, gadv; std::vector<int16_t> gdy; std::vector<int8_t> gdx; std::vector<uint32_t> gbm;
  uint16_t fg = 0xFFFF, bg = 0; uint8_t datum = TL_DATUM;

  void* getPointer() { return buf; }
  static uint16_t sw(uint16_t c) { return (c >> 8) | (c << 8); }
  void drawPixel(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) buf[y * W + x] = sw(c); }
  uint16_t readPixel(int x, int y) { return (x >= 0 && y >= 0 && x < W && y < H) ? sw(buf[y * W + x]) : 0; }
  void fillSprite(uint16_t c) { for (auto& p : buf) p = sw(c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c); drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { drawPixel(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
      int cx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0), cy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
      if (cx * cx + cy * cy <= r * r + 1) drawPixel(x + i, y + j, c);
    }
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    drawFastHLine(x + r, y, w - 2 * r, c); drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
    drawFastVLine(x, y + r, h - 2 * r, c); drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
    for (int t = 0; t < 90; t += 3) { double a = t * M_PI / 180; int dx = (int)lround((r - 0.5) * cos(a)), dy = (int)lround((r - 0.5) * sin(a));
      drawPixel(x + r - dx, y + r - dy, c); drawPixel(x + w - 1 - r + dx, y + r - dy, c);
      drawPixel(x + r - dx, y + h - 1 - r + dy, c); drawPixel(x + w - 1 - r + dx, y + h - 1 - r + dy, c); }
  }
  uint16_t alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc) {   // TFT_eSPI's formula
    uint32_t rxb = bgc & 0xF81F; rxb += ((fgc & 0xF81F) - rxb) * (alpha >> 2) >> 6;
    uint32_t xgx = bgc & 0x07E0; xgx += ((fgc & 0x07E0) - xgx) * alpha >> 8;
    return (rxb & 0xF81F) | (xgx & 0x07E0);
  }
  static int32_t rd(const uint8_t* p) { return (int32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
  void loadFont(const uint8_t* d) {
    f = d; gFont.gCount = rd(d); gFont.ascent = rd(d + 16); gFont.descent = rd(d + 20);
    gFont.maxAscent = gFont.ascent; gFont.maxDescent = gFont.descent;
    gFont.spaceWidth = (gFont.ascent + gFont.descent) / 4;
    int n = gFont.gCount; uni.resize(n); gh.resize(n); gw.resize(n); gadv.resize(n); gdy.resize(n); gdx.resize(n); gbm.resize(n);
    uint32_t bp = 24 + n * 28;
    for (int i = 0; i < n; i++) {
      const uint8_t* g = d + 24 + i * 28;
      uni[i] = rd(g); gh[i] = rd(g + 4); gw[i] = rd(g + 8); gadv[i] = rd(g + 12); gdy[i] = rd(g + 16); gdx[i] = rd(g + 20);
      uint32_t u = uni[i];
      if (gh[i] - gdy[i] > gFont.maxDescent && ((u > 0x20 && u < 0xA0 && u != 0x7F) || u > 0xFF)) gFont.maxDescent = gh[i] - gdy[i];
      gbm[i] = bp; bp += gw[i] * gh[i];
    }
    gFont.yAdvance = gFont.maxAscent + gFont.maxDescent;
  }
  void unloadFont() { f = nullptr; }
  void setTextDatum(uint8_t d) { datum = d; }
  void setTextColor(uint16_t a, uint16_t b) { fg = a; bg = b; }
  static std::vector<uint32_t> utf8(const char* s) {
    std::vector<uint32_t> o; const uint8_t* p = (const uint8_t*)s;
    while (*p) { uint32_t c = *p++;
      if (c >= 0xC0 && c < 0xE0 && *p) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
      else if (c >= 0xE0 && c < 0xF0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
      o.push_back(c); }
    return o;
  }
  int idx(uint32_t u) { for (int i = 0; i < gFont.gCount; i++) if (uni[i] == u) return i; return -1; }
  int textWidth(const char* s) {
    auto v = utf8(s); int w = 0;
    for (size_t k = 0; k < v.size(); k++) {
      if (v[k] == 0x20) { w += gFont.spaceWidth; continue; }
      int g = idx(v[k]);
      if (g < 0) { w += gFont.spaceWidth + 1; continue; }
      if (w == 0 && gdx[g] < 0) w -= gdx[g];
      w += (k + 1 < v.size()) ? gadv[g] : gdx[g] + gw[g];
    }
    return w;
  }
  void drawString(const char* s, int x, int y) {
    int w = textWidth(s), h = gFont.yAdvance;
    switch (datum) { case TC_DATUM: x -= w / 2; break; case TR_DATUM: x -= w; break;
      case ML_DATUM: y -= h / 2; break; case MC_DATUM: x -= w / 2; y -= h / 2; break; case MR_DATUM: x -= w; y -= h / 2; break;
      case BC_DATUM: x -= w / 2; y -= h; break; default: break; }
    int cx = x;
    for (uint32_t u : utf8(s)) {
      if (u == 0x20) { cx += gFont.spaceWidth; continue; }
      int g = idx(u);
      if (g < 0) { drawRect(cx + 1, y + gFont.maxAscent - gFont.ascent, gFont.spaceWidth, gFont.ascent, fg); cx += gFont.spaceWidth + 1; continue; }
      const uint8_t* bm = f + gbm[g];
      int ox = cx + gdx[g], oy = y + gFont.maxAscent - gdy[g];
      for (int j = 0; j < gh[g]; j++) for (int i = 0; i < gw[g]; i++) {
        uint8_t a = bm[j * gw[g] + i]; if (!a) continue;
        drawPixel(ox + i, oy + j, a == 0xFF ? fg : alphaBlend(a, fg, bg));
      }
      cx += gadv[g];
    }
  }
};

#ifndef BATT_PCT
#define BATT_PCT -1
#endif
TFT_eSprite spr;
#include "../../src/ui.h"

static void save(const char* path) {
  FILE* o = fopen(path, "wb"); fprintf(o, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) { uint16_t c = TFT_eSprite::sw(spr.buf[i]);
    uint8_t r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31; fputc(r, o); fputc(g, o); fputc(b, o); }
  fclose(o);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : ".";
  static WifiAP ap[] = {
    {"ATT-Home-5G2x", {}, -48, 6, 3}, {"LSU", {}, -55, 1, 5}, {"eduroam", {}, -57, 11, 5}, {"Starbucks WiFi", {}, -63, 6, 0},
    {"", {}, -66, 11, 3}, {"Darkwatch_Pi", {}, -70, 1, 3}, {"NETGEAR47", {}, -74, 3, 3}, {"xfinitywifi", {}, -79, 11, 0},
    {"DIRECT-7B-HP OfficeJet", {}, -83, 6, 3}, {"Café Ünïcode", {}, -88, 9, 3},
  };
  int nAp = sizeof ap / sizeof ap[0];
  uint32_t now = 754000;
  static BleDev ble[] = {
    {{1, 2, 3, 4, 5, 6}, "", -58, T_FINDMY, true, 0, now - 2000},
    {{9, 8, 7, 6, 5, 4}, "", -81, T_TILE, true, 0, now - 9000},
    {{1, 1, 1, 1, 1, 1}, "Anubis's iPhone", -52, T_APPLE, true, 0, now - 1000},
    {{2, 2, 2, 2, 2, 2}, "Flipper Ankh", -60, T_FLIPPER, true, 0, now - 1000},
    {{3, 3, 3, 3, 3, 3}, "Meshtastic_a1f2", -67, T_MESH, false, 0, now - 3000},
    {{4, 4, 4, 4, 4, 4}, "JBL Flip 6", -71, T_NONE, false, 0, now - 1000},
    {{0x5A, 0x3C, 0x11, 0xE0, 0x9F, 0xC4}, "", -77, T_NONE, true, 0, now - 4000},
    {{6, 6, 6, 6, 6, 6}, "", -90, T_MSFT, true, 0, now - 20000},
  };
  int nBle = sizeof ble / sizeof ble[0];
  uint8_t hw[HIST], hb[HIST];
  for (int i = 0; i < HIST; i++) { hw[i] = 7 + (i * 7 % 5) + i / 10; hb[i] = 4 + (i * 3 % 6) + (i > 25 ? 3 : 0); }
  UiData d{ap, nAp, ble, nBle, false, 1, 63, 61, 214, hw, hb, HIST, 5.02f, BATT_PCT, now, 0, 0, false};

  char p[256];
  for (int pg = 0; pg < P_COUNT; pg++) { d.page = pg; d.scanState = pg % 2 ? 2 : 1; uiRender(d); snprintf(p, sizeof p, "%s/page%d.ppm", out, pg); save(p); }
  d.page = P_BLE; d.alert = true; d.now = 754150; uiRender(d); snprintf(p, sizeof p, "%s/alert.ppm", out); save(p);
  d.alert = false; d.page = P_WIFI; d.paused = true; d.scroll = 6; uiRender(d); snprintf(p, sizeof p, "%s/paused.ppm", out); save(p);
  UiData e = d; e.nAp = 0; e.nBle = 0; e.wifiScans = 0; e.paused = false; e.scroll = 0; e.page = P_CHAN; uiRender(e); snprintf(p, sizeof p, "%s/empty.ppm", out); save(p);
  float ts[] = {0.2f, 0.55f, 1.0f};
  for (int i = 0; i < 3; i++) { uiSplash(ts[i]); snprintf(p, sizeof p, "%s/splash%d.ppm", out, i); save(p); }
  return 0;
}
