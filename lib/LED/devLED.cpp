#include <Arduino.h>
#include "common.h"
#include "device.h"

#if defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#elif defined(PLATFORM_ESP32)
#include <WiFi.h>
#endif

#if defined(TARGET_RX)
extern bool connectionHasModelMatch;
#endif

extern unsigned long rebootTime;

#if defined(PIN_WS2812)
#include <Adafruit_NeoPixel.h>
#ifndef WS2812_BRIGHTNESS
#define WS2812_BRIGHTNESS 40
#endif
#ifndef WS2812_COUNT
#define WS2812_COUNT 1
#endif
static Adafruit_NeoPixel _ws2812(WS2812_COUNT, PIN_WS2812, NEO_GRB + NEO_KHZ800);
#endif

constexpr uint8_t LEDSEQ_WIFI_UPDATE[] = { 2, 3 };     // 20ms on, 30ms off
constexpr uint8_t LEDSEQ_BINDING[] = { 10, 10, 10, 100 };   // 2x 100ms blink, 1s pause

#ifndef NO_BINDING_TIMEOUT
#define NO_BINDING_TIMEOUT 120000UL
#endif

static bool blipLED;

#ifdef LED_INVERTED
static uint8_t _pin_inverted = true;
#else
static uint8_t _pin_inverted = false;
#endif

#define UNDEF_PIN (-1)

#ifndef PIN_LED
#define PIN_LED UNDEF_PIN
#endif

static uint8_t _pin = -1;
static const uint8_t *_durations;
static uint8_t _count;
static uint8_t _counter = 0;

#if defined(PIN_WS2812)
static inline uint32_t cOff() { return 0; }
static inline uint32_t cRed() { return _ws2812.Color(WS2812_BRIGHTNESS, 0, 0); }
static inline uint32_t cGreen() { return _ws2812.Color(0, WS2812_BRIGHTNESS, 0); }

static void setPixel(uint16_t i, uint32_t c)
{
    if (i < _ws2812.numPixels())
        _ws2812.setPixelColor(i, c);
}
#endif

static void writeLED(bool on)
{
#if defined(PIN_WS2812)
    uint32_t c = on ? _ws2812.Color(WS2812_BRIGHTNESS, WS2812_BRIGHTNESS, WS2812_BRIGHTNESS) : 0;
    for (uint16_t i = 0; i < _ws2812.numPixels(); i++)
        _ws2812.setPixelColor(i, c);
    _ws2812.show();
#else
    if (_pin == (uint8_t)UNDEF_PIN)
        return;
    digitalWrite(_pin, (on ? HIGH : LOW) ^ _pin_inverted);
#endif
}

#if defined(PIN_WS2812) && (WS2812_COUNT == 4)
static connectionState_e lastState4 = starting;
static bool wasBinding4 = false;
static bool bindFailed4 = false;
static bool bindSuccess4 = false;

static void updateLeds4()
{
    connectionState_e st = connectionState;
    uint32_t now = millis();

    // track binding transitions
    if (st == binding)
    {
        wasBinding4 = true;
        bindFailed4 = false;
        bindSuccess4 = false;
    }
    else if (lastState4 == binding && st != binding)
    {
        // left binding mode
        if (wasBinding4)
        {
            unsigned long dur = now - bindingStart;
            if (dur >= (NO_BINDING_TIMEOUT - 500UL))
                bindFailed4 = true;   // timeout -> red per request
            else
                bindSuccess4 = true;  // completed/left early -> green (best-effort)
        }
        wasBinding4 = false;
    }
    if (st == binding)
    {
        // re-entering binding clears previous result
        bindSuccess4 = false;
    }
    lastState4 = st;

    bool isProblem = (st >= FAILURE_STATES) || bindFailed4;

    // LED 1 (idx 0): zielona gdy jest aktywny AP
    bool isApActive = (st == wifiUpdate);
#if defined(PLATFORM_ESP8266) || defined(PLATFORM_ESP32)
    WiFiMode_t currentMode = WiFi.getMode();
    if (currentMode == WIFI_AP || currentMode == WIFI_AP_STA)
    {
        isApActive = true;
    }
#endif
    setPixel(0, isApActive ? cGreen() : cOff());

    // LED 2 (idx 1): mruga na zielono gdy jest w trybie bindowania
    if (st == binding)
    {
        setPixel(1, ((now / 250) % 2) ? cGreen() : cOff());
    }
    else
    {
        setPixel(1, cOff());
    }

    // LED 3 (idx 2): gdy jest problem pali sie na czerwono
    setPixel(2, isProblem ? cRed() : cOff());

    // LED 4 (idx 3): wylaczona
    setPixel(3, cOff());

    _ws2812.show();
}
#endif

static uint16_t updateLED()
{
#if !defined(PIN_WS2812)
    if (_pin == (uint8_t)UNDEF_PIN)
        return DURATION_NEVER;
#endif
    if (_counter % 2 == 1)
        writeLED(false);
    else
        writeLED(true);
    if (_counter >= _count)
    {
        _counter = 0;
    }
    return _durations[_counter++] * 10;
}

static uint16_t flashLED(uint8_t pin, const uint8_t durations[], uint8_t count)
{
    _counter = 0;
    _pin = pin;
    _durations = durations;
    _count = count;
    return updateLED();
}

static void initialize()
{
#if defined(PIN_WS2812)
    _ws2812.begin();
    _ws2812.setBrightness(255);
#if (WS2812_COUNT == 4)
    for (uint16_t i = 0; i < _ws2812.numPixels(); i++) setPixel(i, cOff());
    _ws2812.show();
#else
    writeLED(true);
#endif
#else
    if (PIN_LED != UNDEF_PIN)
    {
        pinMode(PIN_LED, OUTPUT);
        _pin = PIN_LED;
        writeLED(true);
    }
#endif
}

static int timeout()
{
#if defined(PIN_WS2812) && (WS2812_COUNT == 4)
    updateLeds4();
    return 100; // refresh for blink/connection status
#endif
#if !defined(PIN_WS2812)
    if (_pin == (uint8_t)UNDEF_PIN)
        return DURATION_NEVER;
#endif
    if (connectionState == running && blipLED)
    {
        blipLED = false;
        writeLED(true);
        return DURATION_NEVER;
    }
    return updateLED();
}

static int event()
{
#if defined(PIN_WS2812) && (WS2812_COUNT == 4)
    updateLeds4();
    return 100;
#endif
#if !defined(PIN_WS2812)
    if (_pin == (uint8_t)UNDEF_PIN && PIN_LED == UNDEF_PIN)
        return DURATION_NEVER;
#endif
    if (connectionState == running && blipLED)
    {
        writeLED(false);
        return 50; // 50ms off
    }
    if (connectionState == binding)
    {
        return flashLED(PIN_LED, LEDSEQ_BINDING, sizeof(LEDSEQ_BINDING));
    }
    if (connectionState == wifiUpdate)
    {
        return flashLED(PIN_LED, LEDSEQ_WIFI_UPDATE, sizeof(LEDSEQ_WIFI_UPDATE));
    }
    return DURATION_NEVER;
}

void turnOffLED()
{
#if defined(PIN_WS2812) && (WS2812_COUNT == 4)
    for (uint16_t i = 0; i < _ws2812.numPixels(); i++) setPixel(i, cOff());
    _ws2812.show();
    return;
#endif
#if !defined(PIN_WS2812)
    if (PIN_LED == UNDEF_PIN && _pin == (uint8_t)UNDEF_PIN)
        return;
#endif
    writeLED(false);
}

void blinkLED()
{
#if defined(PIN_WS2812) && (WS2812_COUNT == 4)
    // no-op for custom 4-led mode; external blip not used
    (void)blipLED;
    return;
#endif
    blipLED = true;
    devicesTriggerEvent();
}

device_t LED_device = {
    .initialize = initialize,
    .start = event,
    .event = event,
    .timeout = timeout
};
