# ELRS Backpack AAT — NodeMCU V3 (ESP8266 / ESP-12E), COM9

PlatformIO project for an **ELRS Backpack AAT**-style setup on **NodeMCU V3 (ESP8266 / ESP-12E module)**, flashed to **COM9**.

> Przełączono z docelowej konfiguracji Tianwen ESP32-C3 Pro na **NodeMCU V3 ESP8266**. Część pinów musiała zostać **przemapowana**, bo na ESP8266 GPIO6–GPIO11 to piny flash (niedostępne), a GPIO3 to UART0 RX (konflikt z debugiem Serial).

## Finalna konfiguracja / piny (NodeMCU V3)

| Funkcja | NodeMCU | GPIO | Uwagi |
|---|---|---:|---|
| WS2812 status (4 diody, GRB) | **D4** | **GPIO2** | Zgodnie z wymaganiem: LEDy na D4 |
| OLED SSD1306 SDA | D2 | GPIO4 | I2C, adres `0x3C` |
| OLED SSD1306 SCL | D1 | GPIO5 | I2C |
| Serwo — azymut (sygnał) | D5 | GPIO14 | 50 Hz, attach 1000–2000 µs |
| Serwo — elewacja (sygnał) | D6 | GPIO12 | 50 Hz, attach 1000–2000 µs |
| Serial / upload / monitor | — | COM9 | 115200 monitor, upload 921600 |

**Serwa:** sygnał → D5/D6, czerwony → 5.0 V, czarny/brązowy → GND. **Wspólna masa** (ESP8266, serwa, zasilanie).

**Dlaczego remap względem specyfikacji ESP32-C3 (SDA=5,SCL=6, AZ=3, EL=4):**
- ESP8266 **GPIO6** = pin flash (SD_CLK) — użycie go powoduje awarię; SCL przeniesiony na D1/GPIO5, SDA na D2/GPIO4 (standardowe I2C NodeMCU).
- ESP8266 **GPIO3** = UART0 RX — koliduje z `Serial` 115200; serwa przeniesione na bezpieczne D5/GPIO14 (az) i D6/GPIO12 (el).
- D4/GPIO2 ma ograniczenia bootstrapa — zewnętrzny WS2812 zwykle działa, ale unikaj wymuszania stanu niskiego przy starcie.

## OLED — fix orientacji (WYMAGANY)

Panel SSD1306 128×64 przy domyślnej inicjalizacji Adafruit_SSD1306 pokazuje **lustrzane odbicie lewo-prawo**. Poprawka wykonywana **po** `display.begin(SSD1306_SWITCHCAPVCC, 0x3C)` przez komendy SSD1306.

Sterowane przez `OLED_MOUNT_ROTATED_180` w `platformio.ini` (domyślnie `0` = normalnie):

| Montaż | `OLED_MOUNT_ROTATED_180` | Komendy po `begin()` | Znaczenie |
|---|---:|---|---|
| **Normalny** (domyślnie) | `0` | `0xA0`, `0xC8` | `0xA0` SEGREMAP normal (col 0→SEG0), `0xC8` COM scan normal |
| **Obrócony 180°** | `1` | `0xA1`, `0xC0` | `0xA1` SEGREMAP remap (col 127→SEG0), `0xC0` COM scan remap |

Kod: `src/main.cpp`, `initOled()`:
```cpp
#if OLED_MOUNT_ROTATED_180
    display.ssd1306_command(0xA1); // rotated 180
    display.ssd1306_command(0xC0);
#else
    display.ssd1306_command(0xA0); // normal
    display.ssd1306_command(0xC8);
#endif
```

## WS2812 — 4 diody, sygnalizacja statusu
Wszystkie 4 diody pokazują ten sam stan (`updateStatusLeds()`):
- **BOOT**: bursztynowe mruganie
- **OK**: stała zieleń (po poprawnej inicjalizacji)
- **NO_GPS**: niebieskie mruganie (stan do integracji AAT/GPS)
- **ERROR**: czerwone mruganie

## Build / flash / log (COM9)

Z `D:\github\elrs_AAT3` (PlatformIO: `python -m platformio ...` jeśli `pio` nie jest w PATH):

```bash
python -m platformio run -t upload   # build + flash (COM9)
python -m platformio device monitor -p COM9 -b 115200  # log bootu
```

### Potwierdzony log bootu (COM9, 115200) — rzeczywisty odczyt
```text
========================================
ELRS Backpack AAT - NodeMCU V3 ESP8266
========================================
Build: Nov  8 2026 23:36:38
Pins/config (NodeMCU Dx / GPIO):
  OLED I2C: SDA=D2(GPIO4) SCL=D1(GPIO5) addr=0x3C
  OLED orientation: NORMAL      -> commands 0xA0, 0xC8
  Servo azimuth:   D5(GPIO14)
  Servo elevation: D6(GPIO12)
  WS2812: D4(GPIO2), 4 diodes
========================================
[WS2812] initialized (D4/GPIO2)
[SERVO] attached, centered to 90 deg
[OLED] Orientation fix applied: NORMAL (0xA0, 0xC8)
[BOOT] setup complete
[HB] uptime=5s status=OK az=90 el=90
...
```
*(Na początku po resecie mogą pojawić się krzaki ROM — normalne przy zmianie prędkości ROM/UART; właściwy log aplikacji jest powyżej.)*

## Zmiany w kodzie / pliki (do reflasha)
- `platformio.ini`: env `nodemcuv3_aat`, `espressif8266@4.2.1`, `board=nodemcuv2`, `upload/monitor=COM9`, `921600/115200`, `esp8266_exception_decoder`; flagi pinów (D4/GPIO2 LED, SDA4/SCL5, AZ14/EL12, `OLED_MOUNT_ROTATED_180=0`). Depsy: `Adafruit SSD1306 2.5.9`, `Adafruit GFX 1.11.9`, `Adafruit NeoPixel 1.12.3` (+ `Servo` z core 8266).
- `include/pins.h`: fallbacki dla NodeMCU V3.
- `src/main.cpp`: `Servo.h` (ESP8266), `Wire.begin(SDA,SCL)`, **fix orientacji po `begin()`** (`0xA0/0xA1` + `0xC8/0xC0`), WS2812×4 na D4, serwa D5/D6, banner/heartbeat potwierdzające boot.

**Reflash po zmianie orientacji OLED:** ustaw `-D OLED_MOUNT_ROTATED_180=0` (normalny) lub `1` (180°) w `platformio.ini` i wykonaj `python -m platformio run -t upload`. Wszystkie piny/komendy wynikają z build_flags i są wypisywane na Serial przy starcie.
