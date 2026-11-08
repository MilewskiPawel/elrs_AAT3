/*
 * ELRS Backpack AAT - NodeMCU V3 (ESP8266 / ESP-12E), COM9
 *
 * Features:
 *  - WiFi config mode (AP): ELRS_AAT_<CHIPID>, open, http://192.168.4.1/
 *    Web UI: OLED rotation (normal/180, sends 0xA0,0xC8 or 0xA1,0xC0 after begin),
 *            servo calibration (min/center/max us), reboot. Saved to EEPROM.
 *  - OLED SSD1306 128x64, I2C addr 0x3C, SDA=D2(GPIO4), SCL=D1(GPIO5)
 *    REQUIRED: after display.begin(), send SEGREMAP+COMSCAN to fix L/R mirror.
 *      Normal: 0xA0 + 0xC8   |  Rotated 180: 0xA1 + 0xC0
 *  - WS2812 status LED: D4 (GPIO2), 4 diodes (GRB)
 *  - Servos (signal; 5V; common GND): azimuth D5(GPIO14), elevation D6(GPIO12)
 *
 * Build/flash:  python -m platformio run -t upload  (COM9)
 * Serial log:   python -m platformio device monitor -p COM9 -b 115200
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#endif

#include "pins.h"
#include "config_store.h"

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static Adafruit_NeoPixel ws2812(WS2812_COUNT, PIN_WS2812, NEO_GRB + NEO_KHZ800);
static Servo servoAzim;
static Servo servoElev;
#if defined(ESP8266)
static ESP8266WebServer server(80);
#else
static WebServer server(80);
#endif

static AatConfig cfg;
static bool oledOk = false;
static String apSsid;

enum Status : uint8_t { STATUS_BOOT = 0, STATUS_OK, STATUS_NO_GPS, STATUS_ERROR, STATUS_CONFIG };
static Status g_status = STATUS_BOOT;
static uint32_t g_lastBlinkMs = 0;
static bool g_blinkOn = false;

static void setLeds(uint8_t r, uint8_t g, uint8_t b) {
    for (uint16_t i = 0; i < ws2812.numPixels(); i++) ws2812.setPixelColor(i, ws2812.Color(r, g, b));
    ws2812.show();
}

static void updateStatusLeds(uint32_t now) {
    switch (g_status) {
        case STATUS_BOOT:
            if (now - g_lastBlinkMs >= 250) { g_lastBlinkMs = now; g_blinkOn = !g_blinkOn; setLeds(g_blinkOn?40:0,g_blinkOn?20:0,0); }
            break;
        case STATUS_OK:
            setLeds(0, 30, 0); break;
        case STATUS_CONFIG:
            // config AP active: cyan blink
            if (now - g_lastBlinkMs >= 400) { g_lastBlinkMs = now; g_blinkOn = !g_blinkOn; setLeds(0, g_blinkOn?20:0, g_blinkOn?25:0); }
            break;
        case STATUS_NO_GPS:
            if (now - g_lastBlinkMs >= 500) { g_lastBlinkMs = now; g_blinkOn = !g_blinkOn; setLeds(0,0,g_blinkOn?35:0); }
            break;
        case STATUS_ERROR:
            if (now - g_lastBlinkMs >= 150) { g_lastBlinkMs = now; g_blinkOn = !g_blinkOn; setLeds(g_blinkOn?45:0,0,0); }
            break;
    }
}

static void applyOledOrientation() {
    // REQUIRED for this panel: fixes left-right mirror from stock init.
    if (!oledOk) return;
    if (cfg.oledRotated180) {
        display.ssd1306_command(0xA1); // SEGREMAP remap
        display.ssd1306_command(0xC0); // COM scan remapped
        Serial.println("[OLED] Orientation: ROTATED 180 (0xA1, 0xC0)");
    } else {
        display.ssd1306_command(0xA0); // SEGREMAP normal
        display.ssd1306_command(0xC8); // COM scan normal
        Serial.println("[OLED] Orientation: NORMAL (0xA0, 0xC8)");
    }
}

static bool initOled() {
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[OLED] SSD1306 init failed (check wiring/addr 0x3C)");
        return false;
    }
    applyOledOrientation();
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ELRS AAT 8266");
    display.println("Booting...");
    display.display();
    return true;
}

static void drawStatus() {
    if (!oledOk) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ELRS Backpack AAT");
    display.print("AP: "); display.println(apSsid);
    display.println("IP: 192.168.4.1");
    display.print("SDA:"); display.print(PIN_OLED_SDA);
    display.print(" SCL:"); display.println(PIN_OLED_SCL);
    display.print("AZ:"); display.print(PIN_SERVO_AZIM);
    display.print(" EL:"); display.print(PIN_SERVO_ELEV);
    display.print(" L:D4x"); display.println(WS2812_COUNT);
    if (cfg.oledRotated180) display.println("OLED: 180 (A1,C0)");
    else display.println("OLED: norm (A0,C8)");
    display.print("AZc:"); display.print(cfg.azCenterUs);
    display.print(" ELc:"); display.println(cfg.elCenterUs);
    display.display();
}

static void applyServoConfig() {
    servoAzim.writeMicroseconds(constrain((int)cfg.azCenterUs, (int)cfg.azMinUs, (int)cfg.azMaxUs));
    servoElev.writeMicroseconds(constrain((int)cfg.elCenterUs, (int)cfg.elMinUs, (int)cfg.elMaxUs));
}

static String htmlPage() {
    String h;
    h.reserve(3500);
    h += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>ELRS AAT Config</title><style>body{font-family:sans-serif;margin:16px;max-width:520px}"
           "label{display:block;margin-top:10px}input,select{width:100%;padding:8px;box-sizing:border-box}"
           ".row{display:flex;gap:8px}.row>div{flex:1}button{margin-top:14px;padding:10px 14px}"
           ".note{background:#f4f4f4;padding:10px;border-radius:6px;margin:10px 0}</style></head><body>");
    h += F("<h2>ELRS Backpack AAT</h2><div class='note'>NodeMCU V3 (ESP8266) &middot; COM9<br>AP: ");
    h += apSsid;
    h += F(" &middot; IP: 192.168.4.1<br>WS2812: D4(GPIO2) x4 &middot; OLED: SDA D2(GPIO4) SCL D1(GPIO5) 0x3C<br>"
           "Servos: AZ D5(GPIO14) EL D6(GPIO12)</div>");
    h += F("<form method='POST' action='/save'>");
    h += F("<label>OLED mount (fix L/R mirror after begin)</label><select name='oled'>");
    h += F("<option value='0'"); if(!cfg.oledRotated180) h += F(" selected"); h += F(">Normal &rarr; commands 0xA0, 0xC8</option>");
    h += F("<option value='1'"); if(cfg.oledRotated180) h += F(" selected"); h += F(">Rotated 180&deg; &rarr; commands 0xA1, 0xC0</option>");
    h += F("</select>");
    h += F("<h3>Servo calibration (microseconds)</h3><div class='row'>"
           "<div><label>AZ min</label><input name='azmin' type='number' min='500' max='2500' value='"); h += cfg.azMinUs; h += F("'></div>"
           "<div><label>AZ center</label><input name='azc' type='number' min='500' max='2500' value='"); h += cfg.azCenterUs; h += F("'></div>"
           "<div><label>AZ max</label><input name='azmax' type='number' min='500' max='2500' value='"); h += cfg.azMaxUs; h += F("'></div></div>");
    h += F("<div class='row'>"
           "<div><label>EL min</label><input name='elmin' type='number' min='500' max='2500' value='"); h += cfg.elMinUs; h += F("'></div>"
           "<div><label>EL center</label><input name='elc' type='number' min='500' max='2500' value='"); h += cfg.elCenterUs; h += F("'></div>"
           "<div><label>EL max</label><input name='elmax' type='number' min='500' max='2500' value='"); h += cfg.elMaxUs; h += F("'></div></div>");
    h += F("<button type='submit'>Save to EEPROM</button></form>");
    h += F("<form method='POST' action='/reboot'><button type='submit'>Reboot (apply OLED fix on boot)</button></form>");
    h += F("<p><small>After saving OLED rotation, reboot to re-run <code>display.begin()</code> + orientation commands.</small></p>");
    h += F("</body></html>");
    return h;
}

static void handleRoot() { server.send(200, "text/html; charset=utf-8", htmlPage()); }

static void handleSave() {
    if (server.hasArg("oled")) cfg.oledRotated180 = server.arg("oled").toInt() ? 1 : 0;
    if (server.hasArg("azmin")) cfg.azMinUs = (uint16_t)constrain(server.arg("azmin").toInt(), 500, 2500);
    if (server.hasArg("azc"))   cfg.azCenterUs = (uint16_t)constrain(server.arg("azc").toInt(), 500, 2500);
    if (server.hasArg("azmax")) cfg.azMaxUs = (uint16_t)constrain(server.arg("azmax").toInt(), 500, 2500);
    if (server.hasArg("elmin")) cfg.elMinUs = (uint16_t)constrain(server.arg("elmin").toInt(), 500, 2500);
    if (server.hasArg("elc"))   cfg.elCenterUs = (uint16_t)constrain(server.arg("elc").toInt(), 500, 2500);
    if (server.hasArg("elmax")) cfg.elMaxUs = (uint16_t)constrain(server.arg("elmax").toInt(), 500, 2500);
    // keep ordering sane
    if (cfg.azMinUs > cfg.azCenterUs) cfg.azMinUs = cfg.azCenterUs;
    if (cfg.azMaxUs < cfg.azCenterUs) cfg.azMaxUs = cfg.azCenterUs;
    if (cfg.elMinUs > cfg.elCenterUs) cfg.elMinUs = cfg.elCenterUs;
    if (cfg.elMaxUs < cfg.elCenterUs) cfg.elMaxUs = cfg.elCenterUs;
    aatSaveConfig(cfg);
    applyServoConfig();
    Serial.printf("[CFG] saved: oledRot=%u AZ[%u,%u,%u] EL[%u,%u,%u]\n",
        cfg.oledRotated180, cfg.azMinUs,cfg.azCenterUs,cfg.azMaxUs, cfg.elMinUs,cfg.elCenterUs,cfg.elMaxUs);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Saved. Reboot to apply OLED orientation.");
    drawStatus();
}

static void handleReboot() {
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
}

static void startConfigAp() {
#if defined(ESP8266)
    uint32_t id = ESP.getChipId() & 0xFFFF;
#else
    uint32_t id = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "ELRS_AAT_%04X", (unsigned)id);
    apSsid = buf;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str()); // open AP
    delay(100);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WIFI] AP started: SSID='%s' (open)\n", apSsid.c_str());
    Serial.printf("[WIFI] Config URL: http://%s/\n", ip.toString().c_str());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.onNotFound(handleRoot);
    server.begin();
    Serial.println("[WIFI] HTTP server started");
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("========================================");
    Serial.println("ELRS Backpack AAT - NodeMCU V3 ESP8266");
    Serial.println("Config mode: WiFi AP + Web UI");
    Serial.println("========================================");
    Serial.print("Build: "); Serial.print(__DATE__); Serial.print(" "); Serial.println(__TIME__);

    bool loaded = aatLoadConfig(cfg);
    Serial.printf("[CFG] %s EEPROM (oledRot=%u, AZc=%u, ELc=%u)\n", loaded?"loaded":"defaults", cfg.oledRotated180, cfg.azCenterUs, cfg.elCenterUs);

    ws2812.begin(); ws2812.clear(); ws2812.show();
    g_status = STATUS_BOOT;
    Serial.printf("[WS2812] D4(GPIO%d) x%d initialized\n", PIN_WS2812, WS2812_COUNT);

    servoAzim.attach(PIN_SERVO_AZIM, cfg.azMinUs, cfg.azMaxUs);
    servoElev.attach(PIN_SERVO_ELEV, cfg.elMinUs, cfg.elMaxUs);
    applyServoConfig();
    Serial.printf("[SERVO] AZ D5(GPIO%d) EL D6(GPIO%d) centered\n", PIN_SERVO_AZIM, PIN_SERVO_ELEV);

    oledOk = initOled(); // applies orientation fix from cfg after begin()
    startConfigAp();
    drawStatus();
    g_status = STATUS_CONFIG;
    Serial.println("[BOOT] setup complete - connect to AP and open http://192.168.4.1/");
}

void loop() {
    uint32_t now = millis();
    server.handleClient();
    updateStatusLeds(now);

    static uint32_t lastLog = 0;
    if (now - lastLog >= 5000) {
        lastLog = now;
        Serial.printf("[HB] up=%lus AP=%s clients=%d oledRot=%u az=%d el=%d\n",
            (unsigned long)(now/1000), apSsid.c_str(), WiFi.softAPgetStationNum(),
            cfg.oledRotated180, servoAzim.read(), servoElev.read());
    }
    delay(5);
}
