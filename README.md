# ELRS Backpack AAT — NodeMCU V3 (ESP8266 / ESP-12E), COM9

PlatformIO project for an **ELRS Backpack AAT**-style setup on **NodeMCU V3 (ESP8266 / ESP-12E module)**, flashed to **COM9**. Includes a **WiFi configuration AP + web UI** and the required **OLED L/R mirror fix**.

> Przełączono z docelowej konfiguracji Tianwen ESP32-C3 Pro na **NodeMCU V3 ESP8266**. Część pinów musiała zostać **przemapowana**, bo na ESP8266 GPIO6–GPIO11 to piny flash (niedostępne), a GPIO3 to UART0 RX (konflikt z debugiem Serial).

## Tryb konfiguracji (WiFi AP) — opcja 1
Po starcie ESP uruchamia **punkt dostępowy (AP)**, do którego można się podłączyć i skonfigurować moduł:
- **SSID:** `ELRS_AAT_<XXXX>` (ostatnie 4 hex `ChipId`, np. `ELRS_AAT_A79A`) — sieć **otwarta** (bez hasła)
- **IP / strona konfiguracji:** http://192.168.4.1/
- **Co można ustawić (zapisywane do EEPROM):**
  - montaż OLED: **Normalny** → po `display.begin()` komendy `0xA0, 0xC8` (fix lustra L/P), albo **Obrócony 180°** → `0xA1, 0xC0` (wymaga **Reboot**, aby ponownie wykonać `begin()` + komendy)
  - kalibracja serw (µs): AZ/EL `min / center / max`
- **Status LED (WS2812, D4, 4 diody):** AP/tryb config = **cyjan mruganie**; po inicie/OK = zieleń; boot = bursztyn.

## Finalna konfiguracja / piny (NodeMCU V3)

| Funkcja | NodeMCU | GPIO | Uwagi |
|---|---|---:|---|
| WS2812 status | **D4** | **GPIO2** | **4 diody** (GRB), sygnalizacja statusu |
| OLED SSD1306 SDA | D2 | GPIO4 | I2C, 128×64, addr **0x3C** |
| OLED SSD1306 SCL | D1 | GPIO5 | I2C |
| Serwo azymut (sygnał) | D5 | GPIO14 | 5 V zasilanie, **wspólna masa** |
| Serwo elewacja (sygnał) | D6 | GPIO12 | 5 V zasilanie, **wspólna masa** |
| Upload/monitor | — | COM9 | upload 921600, monitor 115200 |

**Dlaczego remap względem specyfikacji ESP32-C3 (SDA=5,SCL=6, AZ=3, EL=4):** na ESP8266 **GPIO6** to pin flash (niedostępny) i **GPIO3** to UART0 RX (konflikt z Serial). Użyto bezpiecznych pinów NodeMCU: I2C D2/D1, serwa D5/D6. D4/GPIO2 ma ograniczenia bootstrapa (zewnętrzny WS2812 zwykle OK; nie wymuszaj stanu niskiego przy starcie).

## OLED — fix orientacji (WYMAGANY)
Panel SSD1306 128×64 przy domyślnej inicjalizacji Adafruit_SSD1306 pokazuje **lustrzane odbicie lewo-prawo**. Poprawka wykonywana **po** `display.begin(SSD1306_SWITCHCAPVCC, 0x3C)` przez komendy SSD1306 (wartość z EEPROM / web UI).

| Montaż | Komendy po `begin()` | Znaczenie |
|---|---|---|
| **Normalny** | `0xA0`, `0xC8` | `0xA0` SEGREMAP normal (col 0→SEG0), `0xC8` COM scan normal |
| **Obrócony 180°** | `0xA1`, `0xC0` | `0xA1` SEGREMAP remap (col 127→SEG0), `0xC0` COM scan remap |

Kod: `src/main.cpp`, `applyOledOrientation()` wywoływane z `initOled()`:
```cpp
if (cfg.oledRotated180) { display.ssd1306_command(0xA1); display.ssd1306_command(0xC0); }
else                    { display.ssd1306_command(0xA0); display.ssd1306_command(0xC8); }
```

## Build / flash / log (COM9)

Z `D:\github\elrs_AAT3`:
```bash
python -m platformio run -t upload   # build + flash (COM9)
python -m platformio device monitor -p COM9 -b 115200  # log bootu
```

W logu szukaj: `[WIFI] AP started: SSID='ELRS_AAT_...' (open)` oraz `[WIFI] Config URL: http://192.168.4.1/`, a także `[OLED] Orientation: NORMAL (0xA0, 0xC8)`.

## Zmiany w plikach
- `src/main.cpp`: WiFi AP + `ESP8266WebServer` (strona `/`, zapis `/save` do EEPROM, `/reboot`), fix orientacji OLED z konfiguracji po `begin()`, WS2812×4 D4, serwa D5/D6, logi.
- `include/config_store.h`: struktura konfig + `EEPROM` (magic/version/checksum), domyślne z build_flags.
- `include/pins.h`, `platformio.ini`: piny NodeMCU V3, COM9.
- Depsy: Adafruit SSD1306 2.5.9, Adafruit GFX 1.11.9, Adafruit NeoPixel 1.12.3 (+ core `Servo`, `ESP8266WiFi`, `ESP8266WebServer`, `EEPROM`).

**Repo:** https://github.com/MilewskiPawel/elrs_AAT3
