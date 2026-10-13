#pragma once
#include "config.h"

class BatteryMonitor {
public:
  static void init() {
    pinMode(PIN_VBAT_ENABLE, OUTPUT);
    digitalWrite(PIN_VBAT_ENABLE, HIGH); // Keep disabled by default to save power
  }

  static float readVoltage() {
    digitalWrite(PIN_VBAT_ENABLE, LOW); // Enable divider
    delayMicroseconds(60); // Allow voltage to stabilize across 1M divider

    analogReference(AR_INTERNAL); // 0.6V internal reference * 6 = 3.6V full scale
    analogReadResolution(12);     // 12-bit ADC (0..4095)
    
    // Discard first reading to allow S&H capacitor to charge
    analogRead(PIN_VBAT_SENSE);
    int raw = analogRead(PIN_VBAT_SENSE);

    digitalWrite(PIN_VBAT_ENABLE, HIGH); // Disable divider

    // 1M + 1M divider -> input voltage is 2x measured
    // Reference 3.6V, 4095 counts
    float voltage = (float)raw * (3.6f / 4095.0f) * 2.0f;
    return voltage;
  }

  static uint8_t readPercentage() {
    float v = readVoltage();
    // LiPo curve approximation: 4.2V = 100%, 3.3V = 0%
    if (v >= 4.20f) return 100;
    if (v <= 3.30f) return 0;
    return (uint8_t)((v - 3.30f) / (4.20f - 3.30f) * 100.0f);
  }
};
