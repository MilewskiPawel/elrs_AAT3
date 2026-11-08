# Branch `AAT3plusAAT2` — ELRS Backpack AAT (base: elrs_AAT2) for NodeMCU V3 / COM9

This branch merges the ExpressLRS Backpack (AAT2) codebase into this repo and adds a NodeMCU V3 target with the required pins and OLED mirror fix.

## Target
- **Environment:** `AAT_ESP_Backpack_via_UART` (and `AAT_ESP_Backpack_via_WIFI` inherits it)
- **Board:** `nodemcuv2` (ESP8266 4M), `espressif8266@4.2.1`
- **Upload/monitor:** `COM9`, `115200`
- **Build/flash:**
  ```bash
  cd D:\github\elrs_AAT3
  python -m platformio run -e AAT_ESP_Backpack_via_UART -t upload
  python -m platformio device monitor -p COM9 -b 115200
  ```

## Final pins (NodeMCU V3)
| Function | NodeMCU | GPIO | Notes |
|---|---|---:|---|
| WS2812 status (devLED/target config) | D4 | GPIO2 | `PIN_WS2812=2`, `WS2812_COUNT=4` |
| OLED SSD1306 SDA | D2 | GPIO4 | I2C, 128x64, addr `0x3C` |
| OLED SSD1306 SCL | D1 | GPIO5 | I2C |
| Servo azimuth (signal) | D5 | GPIO14 | 5V power, common GND |
| Servo elevation (signal) | D6 | GPIO12 | 5V power, common GND |
| Button | D3/FLASH? | GPIO0 | `PIN_BUTTON=0` (target default) |

## OLED L/R mirror fix (REQUIRED)
In `src/module_aat.cpp` → `displayInit()`, **after** `_display.begin(SSD1306_SWITCHCAPVCC, 0x3C)`:
- `OLED_MOUNT_ROTATED_180=0` (default): `_display.ssd1306_command(0xA0); _display.ssd1306_command(0xC8);` (normal)
- `OLED_MOUNT_ROTATED_180=1`: `_display.ssd1306_command(0xA1); _display.ssd1306_command(0xC0);` (rotated 180°)

Set `-D OLED_MOUNT_ROTATED_180=0|1` in `targets/aat.ini` (`AAT_ESP_Backpack_via_UART`) and rebuild/flash.

## What changed vs upstream AAT2 (this branch)
- `targets/aat.ini`: NodeMCU V3 AAT UART target set to `nodemcuv2`/4M, COM9, pins (WS2812 D4/GPIO2 x4, SDA4/SCL5, AZ14/EL12), `-D OLED_MOUNT_ROTATED_180=0`.
- `src/module_aat.cpp`: OLED orientation fix (SEGREMAP+COMSCAN) after `begin()`.

## Binding / LEDs (ELRS Backpack AAT)
This is the **full ExpressLRS Backpack AAT** firmware (ESP-NOW/WiFi per Backpack behavior, `devWIFI`/`devLED`/button). Binding/peering follows Backpack rules (binding phrase/UID/MAC per your Backpack/TX config) — not the simple AP-only config from the `master` branch. Enter Backpack WiFi/update mode per standard button/boot-count behavior (`PIN_BUTTON=0`). LED behavior is controlled by `devLED` (and target defines).

## Build status
- `python -m platformio run -e AAT_ESP_Backpack_via_UART`: **SUCCESS** (RAM ~45.6%, Flash ~45.6%). `firmware.bin` produced. Upload to COM9 requires the port to be free.
