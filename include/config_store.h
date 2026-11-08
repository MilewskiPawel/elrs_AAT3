#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "pins.h"

struct AatConfig {
    uint32_t magic;        // 'AAT3'
    uint16_t version;      // 1
    uint8_t  oledRotated180; // 0=normal(0xA0,0xC8), 1=rotated 180(0xA1,0xC0)
    uint8_t  reserved;
    uint16_t azMinUs;      // servo pulse us
    uint16_t azCenterUs;
    uint16_t azMaxUs;
    uint16_t elMinUs;
    uint16_t elCenterUs;
    uint16_t elMaxUs;
    uint16_t checksum;
};

static const uint32_t AAT_CFG_MAGIC = 0x33415441UL; // 'AAT3' little-endian
static const uint16_t AAT_CFG_VER   = 1;
static const int      AAT_EE_SIZE   = 512;
static const int      AAT_EE_ADDR   = 0;

static inline uint16_t aatChecksum(const AatConfig &c) {
    const uint8_t *p = (const uint8_t *)&c;
    uint16_t sum = 0;
    for (size_t i = 0; i < offsetof(AatConfig, checksum); i++) sum += p[i];
    return (uint16_t)(0x5A5Au ^ sum);
}

static inline AatConfig aatDefaults() {
    AatConfig c{};
    c.magic = AAT_CFG_MAGIC;
    c.version = AAT_CFG_VER;
    c.oledRotated180 = (uint8_t)(OLED_MOUNT_ROTATED_180 ? 1 : 0);
    c.azMinUs = 1000; c.azCenterUs = 1500; c.azMaxUs = 2000;
    c.elMinUs = 1000; c.elCenterUs = 1500; c.elMaxUs = 2000;
    c.checksum = aatChecksum(c);
    return c;
}

static inline bool aatLoadConfig(AatConfig &out) {
    EEPROM.begin(AAT_EE_SIZE);
    EEPROM.get(AAT_EE_ADDR, out);
    if (out.magic != AAT_CFG_MAGIC || out.version != AAT_CFG_VER || out.checksum != aatChecksum(out)) {
        out = aatDefaults();
        return false;
    }
    return true;
}

static inline void aatSaveConfig(AatConfig &c) {
    c.magic = AAT_CFG_MAGIC;
    c.version = AAT_CFG_VER;
    c.checksum = aatChecksum(c);
    EEPROM.begin(AAT_EE_SIZE);
    EEPROM.put(AAT_EE_ADDR, c);
    EEPROM.commit();
}
