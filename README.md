# T-Scan v1.2 — LilyGo T-Display Wi-Fi / BLE scanner (Anubis Darkwatch edition)

Pocket recon screen for the classic LilyGo TTGO T-Display (ESP32, 1.14" ST7789).
Nearby Wi-Fi, a channel spectrum, BLE devices with tracker flags, and an
overview dashboard. Listens and logs only — it never connects to anything.

![T-Scan screens](docs/screens.png)

Brand look: black, amber `#ffb000` + neon green `#39ff14`, Orbitron Black +
Share Tech Mono, jackal mark, HUD brackets, CRT scanlines, CRT power-on boot.

## Flash it (browser, no installs)
1. Plug the board into USB-C. Chrome or Edge on desktop.
2. Open https://espressif.github.io/esptool-js/ → **Connect** → pick the board's COM port.
3. Flash address **0x0**, file `T-Scan-v1.2-merged.bin` → **Program**.
4. When it finishes, unplug and replug USB (or press the tiny reset button on the
   board's top edge — the two front buttons are not reset).

If Connect fails: hold the left front button, tap reset, release, try again.

## Controls
| Button | Short | Hold (~1 s) |
|---|---|---|
| Left | Next page (slides in) | Screen off / on |
| Right | Scroll 6 rows | Pause / resume scanning |
| **Both** | — | **Rotate 180°** (remembered after power-off) |

Left/right follow the screen, so after flipping, the left button is still the one on your left.

## Screens
- **Header** — jackal, page title, live counts. The sweep along the header rule
  shows what's scanning: green = Wi-Fi, amber = BLE; solid red = paused.
- **Wi-Fi** — strongest first: SSID, channel, signal meter, dBm.
  Green name + green side mark = open network. Dim `<hidden>` = hidden SSID.
- **Spectrum** — every AP drawn as a curve across the channels it actually
  occupies (±2), height = signal. Strongest three are labelled. Dashed amber
  line + `BEST CH` = least crowded of 1 / 6 / 11.
- **BLE** — trackers first, then by signal. Pills: `FINDMY` / `TILE` /
  `SMARTTAG` (solid red = tracker), `FLIPPER`, `MESH` (Meshtastic), `APPLE`,
  `IBEACON`, `MSFT`. Dim = not heard for 15 s; gone after 60 s.
- **Overview** — APs, open, best channel, BLE now, trackers, total BLE seen,
  with sparklines of the last ~40 scan cycles. Battery or `USB PWR` up top.
- **Tracker alert** — the frame flashes red when a new tracker shows up.

Signal colours: green ≥ −60 dBm, amber ≥ −75, red weaker.

Phones and tags rotate random MACs every ~15 min, so "BLE seen" overcounts on
long sessions. One FindMy tag near you for hours that isn't yours is worth a
look; a few at a venue is normal.

## Serial log
115200 baud, CSV: `W,ms,bssid,ssid,ch,rssi,auth` per AP per scan,
`B,ms,mac,addr_type,name,rssi,tag` per new BLE address. Boot stages print as `# boot: …`.

## Build from source
- Firmware: PlatformIO, `pio run`. Pins and display config in `platformio.ini`;
  scan timing at the top of `src/main.cpp`; all drawing in `src/ui.h`.
- Fonts: `python3 tools/mkfonts.py <fonts dir> src/brand_fonts.h` (fontsource
  packages from npm: `@fontsource/orbitron`, `@fontsource/share-tech-mono`,
  `@fontsource/noto-sans-egyptian-hieroglyphs`; all OFL).
- PC previews of every screen: `g++ -std=c++17 -Itools/preview/stub -include cstring
  tools/preview/host.cpp -o host && ./host outdir` (writes PPMs).
