/*
 * ELRS Backpack AAT - NodeMCU V3 (ESP8266 / ESP-12E), target COM9
 *
 * Hardware:
 *  - OLED SSD1306 128x64, I2C addr 0x3C
 *      SDA = D2 (GPIO4)
 *      SCL = D1 (GPIO5)
 *    NOTE (remap from Tianwen ESP32-C3 config): GPIO6 is a flash pin on ESP8266
 *    and cannot be used; pins moved to safe NodeMCU I2C defaults.
 *    Stock Adafruit_SSD1306 init shows a left-right mirror on this panel.
 *    Fix: after display.begin(), send SSD1306 commands:
 *      Normal mount:      0xA0 (SEGREMAP normal, col 0->SEG0) + 0xC8 (COM scan normal)
 *      Rotated 180 deg:   0xA1 (SEGREMAP remap)              + 0xC0 (COM scan remap)
 *    Controlled by OLED_MOUNT_ROTATED_180 in platformio.ini.
 *  - Servos (signal; 5V power; common GND):
 *      azimuth   = D5 (GPIO14)
 *      elevation = D6 (GPIO12)
 *    NOTE (remap from C3 config): GPIO3 is UART0 RX on 8266 and conflicts with
 *    Serial debug; servos moved to safe pins.
 *  - WS2812 status LED: D4 (GPIO2), 4 diodes (GRB).
 *
 * Build/flash (PlatformIO): pio run -t upload   (COM9)
 * Serial monitor:           pio device monitor  (COM9, 115200)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

#if defined(ESP8266)
#include <Servo.h>
#elif defined(ESP32)
#include <ESP32Servo.h>
#else
#include <Servo.h>
#endif

#include "pins.h"

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static Adafruit_NeoPixel ws2812(WS2812_COUNT, PIN_WS2812, NEO_GRB + NEO_KHZ800);
static Servo servoAzim;
static Servo servoElev;

// Simple status states for WS2812 indication
enum Status : uint8_t {
    STATUS_BOOT = 0,
    STATUS_OK,
    STATUS_NO_GPS,
    STATUS_ERROR
};
static Status g_status = STATUS_BOOT;
static uint32_t g_lastBlinkMs = 0;
static bool g_blinkOn = false;

static void setLeds(uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < ws2812.numPixels(); i++) {
        ws2812.setPixelColor(i, ws2812.Color(r, g, b));
    }
    ws2812.show();
}

static void updateStatusLeds(uint32_t now) {
    // All 4 diodes show the same status color/pattern.
    switch (g_status) {
        case STATUS_BOOT:
            // Boot: amber blink
            if (now - g_lastBlinkMs >= 250) {
                g_lastBlinkMs = now;
                g_blinkOn = !g_blinkOn;
                setLeds(g_blinkOn ? 40 : 0, g_blinkOn ? 20 : 0, 0);
            }
            break;
        case STATUS_OK:
            // OK: solid green
            setLeds(0, 30, 0);
            break;
        case STATUS_NO_GPS:
            // Waiting/no GPS: blue blink
            if (now - g_lastBlinkMs >= 500) {
                g_lastBlinkMs = now;
                g_blinkOn = !g_blinkOn;
                setLeds(0, 0, g_blinkOn ? 35 : 0);
            }
            break;
        case STATUS_ERROR:
            // Error: red blink
            if (now - g_lastBlinkMs >= 150) {
                g_lastBlinkMs = now;
                g_blinkOn = !g_blinkOn;
                setLeds(g_blinkOn ? 45 : 0, 0, 0);
            }
            break;
    }
}

static bool initOled() {
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[OLED] SSD1306 init failed (check wiring/addr 0x3C)");
        return false;
    }

    // --- Orientation fix (REQUIRED for this panel) ---
    // Stock library default init produces a left-right mirror.
    // Send SEGREMAP + COMSCAN after begin().
#if OLED_MOUNT_ROTATED_180
    // Rotated 180 degrees: 0xA1 + 0xC0
    display.ssd1306_command(0xA1); // SEGREMAP: column 127 mapped to SEG0 (remapped)
    display.ssd1306_command(0xC0); // COM output scan direction: remapped
    Serial.println("[OLED] Orientation fix applied: ROTATED 180 (0xA1, 0xC0)");
#else
    // Mounted normally: 0xA0 + 0xC8
    display.ssd1306_command(0xA0); // SEGREMAP: column 0 mapped to SEG0 (normal)
    display.ssd1306_command(0xC8); // COM output scan direction: normal
    Serial.println("[OLED] Orientation fix applied: NORMAL (0xA0, 0xC8)");
#endif

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ELRS AAT 8266");
    display.println("Booting...");
    display.display();
    return true;
}

static void drawStatus(bool oledOk) {
    if (!oledOk) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ELRS Backpack AAT");
    display.println("NodeMCU V3 ESP8266");
    display.println();
    display.print("SDA:"); display.print(PIN_OLED_SDA);
    display.print(" SCL:"); display.println(PIN_OLED_SCL);
    display.print("AZ:"); display.print(PIN_SERVO_AZIM);
    display.print(" EL:"); display.println(PIN_SERVO_ELEV);
    display.print("WS2812:D4/"); display.print(PIN_WS2812);
    display.print(" x"); display.println(WS2812_COUNT);
#if OLED_MOUNT_ROTATED_180
    display.println("OLED: 180deg (A1,C0)");
#else
    display.println("OLED: normal (A0,C8)");
#endif
    display.display();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("========================================");
    Serial.println("ELRS Backpack AAT - NodeMCU V3 ESP8266");
    Serial.println("========================================");
    Serial.print("Build: "); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);
    Serial.println("Pins/config (NodeMCU Dx / GPIO):");
    Serial.printf("  OLED I2C: SDA=D2(GPIO%d) SCL=D1(GPIO%d) addr=0x%02X\n", PIN_OLED_SDA, PIN_OLED_SCL, OLED_ADDR);
#if OLED_MOUNT_ROTATED_180
    Serial.println("  OLED orientation: ROTATED 180 -> commands 0xA1, 0xC0");
#else
    Serial.println("  OLED orientation: NORMAL      -> commands 0xA0, 0xC8");
#endif
    Serial.printf("  Servo azimuth:   D5(GPIO%d)\n", PIN_SERVO_AZIM);
    Serial.printf("  Servo elevation: D6(GPIO%d)\n", PIN_SERVO_ELEV);
    Serial.printf("  WS2812: D4(GPIO%d), %d diodes\n", PIN_WS2812, WS2812_COUNT);
    Serial.println("========================================");

    // WS2812 init (D4/GPIO2)
    ws2812.begin();
    ws2812.clear();
    ws2812.show();
    g_status = STATUS_BOOT;
    Serial.println("[WS2812] initialized (D4/GPIO2)");

    // Servos
    servoAzim.attach(PIN_SERVO_AZIM, 1000, 2000);
    servoElev.attach(PIN_SERVO_ELEV, 1000, 2000);
    servoAzim.write(90);
    servoElev.write(90);
    Serial.println("[SERVO] attached, centered to 90 deg");

    // OLED (includes required mirror/orientation fix after begin)
    bool oledOk = initOled();

    drawStatus(oledOk);
    g_status = STATUS_OK; // boot complete
    Serial.println("[BOOT] setup complete");
}

void loop() {
    uint32_t now = millis();
    updateStatusLeds(now);

    // Periodic heartbeat on serial to confirm the firmware is running
    static uint32_t lastLog = 0;
    if (now - lastLog >= 5000) {
        lastLog = now;
        Serial.printf("[HB] uptime=%lus status=OK az=%d el=%d\n",
                      (unsigned long)(now / 1000),
                      servoAzim.read(), servoElev.read());
    }
    delay(10);
}
