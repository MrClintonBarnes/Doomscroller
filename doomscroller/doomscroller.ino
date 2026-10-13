#include <bluefruit.h>
#include "config.h"
#include "settings.h"
#include "sensor.h"

// ============================================================================
// Battery Hardware Definitions (Seeed XIAO nRF52840 Sense)
#ifndef PIN_VBAT
#define PIN_VBAT         32   // D32 / P0.31 - Analog input for battery voltage
#endif
#ifndef VBAT_ENABLE
#define VBAT_ENABLE      14   // D14 / P0.14 - Control pin to enable voltage divider (LOW = enable)
#endif
#define PIN_VBAT_ENABLE  VBAT_ENABLE

// ============================================================================
// Global BLE Objects (Canonical Adafruit Bluefruit Stack)
// ============================================================================
BLEDis        bledis;                 // Device Information Service
BLEBas        blebas;                 // Battery Service (0x180F)
BLEHidGeneric blehid = BLEHidGeneric(1, 0, 0); // 1 Input Report (Mouse), 0 Output, 0 Feature

// ============================================================================
// Standard BLE HID Mouse Report Descriptor (Universal Cross-Platform)
// ============================================================================
// Natively supported by macOS, Android, Windows, Linux, iOS without any drivers
// Does NOT declare a keyboard (prevents PIN / Passkey prompts on Android & macOS)
// Does NOT declare a digitizer (prevents accidental touchscreen clicks / taps)
uint8_t const hid_report_descriptor[] =
{
  0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
  0x09, 0x02,        // USAGE (Mouse)
  0xA1, 0x01,        // COLLECTION (Application)
  0x85, 0x01,        //   REPORT_ID (1)
  0x09, 0x01,        //   USAGE (Pointer)
  0xA1, 0x00,        //   COLLECTION (Physical)
  0x05, 0x09,        //     USAGE_PAGE (Button)
  0x19, 0x01,        //     USAGE_MINIMUM (Button 1)
  0x29, 0x05,        //     USAGE_MAXIMUM (Button 5)
  0x15, 0x00,        //     LOGICAL_MINIMUM (0)
  0x25, 0x01,        //     LOGICAL_MAXIMUM (1)
  0x95, 0x05,        //     REPORT_COUNT (5)
  0x75, 0x01,        //     REPORT_SIZE (1)
  0x81, 0x02,        //     INPUT (Data,Var,Abs)
  0x95, 0x01,        //     REPORT_COUNT (1)
  0x75, 0x03,        //     REPORT_SIZE (3)
  0x81, 0x03,        //     INPUT (Cnst,Var,Abs)   ; 3 bits padding
  0x05, 0x01,        //     USAGE_PAGE (Generic Desktop)
  0x09, 0x30,        //     USAGE (X)
  0x09, 0x31,        //     USAGE (Y)
  0x15, 0x81,        //     LOGICAL_MINIMUM (-127)
  0x25, 0x7F,        //     LOGICAL_MAXIMUM (127)
  0x75, 0x08,        //     REPORT_SIZE (8)
  0x95, 0x02,        //     REPORT_COUNT (2)
  0x81, 0x06,        //     INPUT (Data,Var,Rel)
  0x09, 0x38,        //     USAGE (Wheel)
  0x15, 0x81,        //     LOGICAL_MINIMUM (-127)
  0x25, 0x7F,        //     LOGICAL_MAXIMUM (127)
  0x75, 0x08,        //     REPORT_SIZE (8)
  0x95, 0x01,        //     REPORT_COUNT (1)
  0x81, 0x06,        //     INPUT (Data,Var,Rel)
  0x05, 0x0C,        //     USAGE_PAGE (Consumer Devices)
  0x0A, 0x38, 0x02,  //     USAGE (AC Pan)
  0x15, 0x81,        //     LOGICAL_MINIMUM (-127)
  0x25, 0x7F,        //     LOGICAL_MAXIMUM (127)
  0x75, 0x08,        //     REPORT_SIZE (8)
  0x95, 0x01,        //     REPORT_COUNT (1)
  0x81, 0x06,        //     INPUT (Data,Var,Rel)
  0xC0,              //   END_COLLECTION
  0xC0               // END_COLLECTION
};

// State Variables
static float    g_substepAccumulator = 0.0f;
static bool     g_telemetryStream    = false;
static uint32_t g_lastBatteryCheck   = 0;

// Forward Declarations
void printHelp();
void printStatus();
void handleSerialCLI();
void sendMouseScroll(int8_t scrollSteps);
float readBatteryVoltage();
uint8_t readBatteryPercentage();

// ============================================================================
// BLE Connection Callbacks
// ============================================================================
void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  g_sensor.resetState();
  g_substepAccumulator = 0.0f;
  Serial.println(F("[BLE] Connected to host!"));
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;
  g_sensor.enablePower(false);
  g_substepAccumulator = 0.0f;
  Serial.println(F("[BLE] Disconnected from host. Advertising resumed."));
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);

  // Configure on-board LEDs
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_RED, 1 - LED_STATE_ON);
  digitalWrite(PIN_LED_GREEN, 1 - LED_STATE_ON);
  digitalWrite(PIN_LED_BLUE, 1 - LED_STATE_ON);

  // Check hardware reset reason: detect magnetic swipe over letter "C"
  uint32_t resetReason = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = 0xFFFFFFFF; // Clear reset register flags
  bool isMagneticReset = (resetReason & POWER_RESETREAS_RESETPIN_Msk) != 0;

  // Initialize Settings in RAM
  settings_init();

  // Initialize Bluefruit BLE Peripheral (1 peripheral, 0 centrals)
  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(4); // +4 dBm maximum transmission power
  Bluefruit.setName(g_settings.deviceName);

  // Manage Bluetooth Identity and Bonds
  uint8_t resetCount = NRF_POWER->GPREGRET;
  if (isMagneticReset) {
    resetCount++;
    NRF_POWER->GPREGRET = resetCount;

    // Clear old pairing bonds so device can pair cleanly with any host
    Bluefruit.Periph.clearBonds();

    // Visual feedback for magnetic swipe: 5 fast blue flashes
    for (int i = 0; i < 5; i++) {
      digitalWrite(PIN_LED_BLUE, LED_STATE_ON);
      delay(50);
      digitalWrite(PIN_LED_BLUE, 1 - LED_STATE_ON);
      delay(50);
    }
  } else {
    // Normal boot visual confirmation: Green LED pulse
    digitalWrite(PIN_LED_GREEN, LED_STATE_ON);
    delay(100);
    digitalWrite(PIN_LED_GREEN, 1 - LED_STATE_ON);
  }

  // Set fresh random static BLE identity address
  // This completely eliminates stale pairing caches ("Doomscroller063") and Android PIN errors
  ble_gap_addr_t gap_addr;
  if (sd_ble_gap_addr_get(&gap_addr) == NRF_SUCCESS) {
    gap_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    gap_addr.addr[0] += (resetCount + 1);
    gap_addr.addr[5] |= 0xC0; // Standard BLE random static address requirement
    sd_ble_gap_addr_set(&gap_addr);
  }

  // Security configuration: unauthenticated Just-Works pairing with encryption
  // Explicitly disables Keyboard and Display caps so hosts NEVER request a passkey/PIN
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setMITM(false);

  // Connection Interval: 11.25ms (9 * 1.25ms) to 20ms (16 * 1.25ms)
  Bluefruit.Periph.setConnInterval(9, 16);

  // Connection event callbacks
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // Device Information Service
  bledis.setManufacturer("Andrew Industries");
  bledis.setModel("Doomscroller Pro");
  bledis.setSoftwareRev(FIRMWARE_VERSION);
  bledis.begin();

  // Battery Service (BLE Standard 0x180F)
  blebas.begin();

  // BLE HID Generic Service (Pure Mouse)
  uint16_t input_report_lengths[] = { 5 }; // 5 bytes: Buttons(1), X(1), Y(1), Wheel(1), Pan(1)
  blehid.setReportMap(hid_report_descriptor, sizeof(hid_report_descriptor));
  blehid.setReportLen(input_report_lengths, NULL, NULL);
  blehid.enableMouse(true);
  blehid.enableKeyboard(false);
  blehid.begin();

  // BLE Advertising configuration
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // 20ms fast mode, 152.5ms slow mode
  Bluefruit.Advertising.setFastTimeout(60);   // 60 seconds in fast mode
  Bluefruit.Advertising.start(0);             // Advertise indefinitely until connected

  // Automatic connection LED: blinks Blue LED inside USB-C during advertising, solid ON when connected
  Bluefruit.autoConnLed(true);

  // Initialize MagAlpha Sensor SPI bus
  g_sensor.begin();

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  DOOMSCROLLER UNIVERSAL FIRMWARE " FIRMWARE_VERSION));
  Serial.println(F("  Cross-Platform BLE Scroll Controller  "));
  Serial.println(F("========================================"));
  Serial.print(F("Device Name: "));
  Serial.println(g_settings.deviceName);
  if (isMagneticReset) {
    Serial.println(F("[System] Magnetic reset detected. Bonds wiped, address refreshed."));
  }
  Serial.println(F("Ready to pair."));
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. Process Serial CLI commands if connected to USB
  if (Serial.available()) {
    handleSerialCLI();
  }

  // 2. When not connected to Bluetooth, put sensor to sleep to conserve battery
  if (!Bluefruit.connected()) {
    g_sensor.enablePower(false);
    g_substepAccumulator = 0.0f;
    delay(20);
    return;
  }

  // 3. Periodic Battery Monitoring (every 60s while connected)
  if (now - g_lastBatteryCheck >= 60000 || (g_lastBatteryCheck == 0 && now >= 3000)) {
    g_lastBatteryCheck = now;
    uint8_t pct = readBatteryPercentage();
    blebas.write(pct);
  }

  // 4. Connected: sample encoder at 100Hz (10ms)
  float deltaDeg = 0.0f;
  float velocity = 0.0f;
  if (!g_sensor.update(deltaDeg, velocity)) {
    delay(ACTIVE_LOOP_INTERVAL_MS);
    return;
  }

  float absVel = fabsf(velocity);
  if (absVel > g_settings.deadzoneDeg) {
    float dir = g_settings.invertScroll ? -1.0f : 1.0f;
    float baseSteps = (deltaDeg / g_settings.degreesPerStep) * dir;

    // Dynamic flick acceleration curve
    float accelMultiplier = 1.0f + (absVel * g_settings.accelFactor * 0.01f);
    if (accelMultiplier > 8.0f) {
      accelMultiplier = 8.0f;
    }

    float totalStepDelta = baseSteps * accelMultiplier;
    g_substepAccumulator += totalStepDelta;

    int stepsToSend = (int)g_substepAccumulator;
    if (stepsToSend != 0) {
      g_substepAccumulator -= (float)stepsToSend;

      if (stepsToSend > 127) stepsToSend = 127;
      if (stepsToSend < -127) stepsToSend = -127;

      // Send standard mouse scroll wheel report (dx=0, dy=0 keeps pointer over video feed)
      sendMouseScroll((int8_t)stepsToSend);

      if (g_telemetryStream) {
        Serial.print(F("Delta: "));
        Serial.print(deltaDeg, 2);
        Serial.print(F("°\tVel: "));
        Serial.print(velocity, 1);
        Serial.print(F("°/s\tSteps: "));
        Serial.println(stepsToSend);
      }
    }
  } else {
    // Settling decay when wheel is stationary
    g_substepAccumulator *= 0.5f;
    if (fabsf(g_substepAccumulator) < 0.05f) {
      g_substepAccumulator = 0.0f;
    }
  }

  delay(ACTIVE_LOOP_INTERVAL_MS);
}

// ============================================================================
// Mouse Scroll HID Dispatcher
// ============================================================================
void sendMouseScroll(int8_t scrollSteps) {
  // Pure scroll wheel with zero X/Y displacement:
  // Keeps cursor in the active video viewport so TikTok, Instagram, and web feeds scroll 100% reliably
  // Byte 0: Buttons (0 = none)
  // Byte 1: X (0)
  // Byte 2: Y (0)
  // Byte 3: Wheel (vertical scroll)
  // Byte 4: Pan (horizontal pan, 0)
  uint8_t report[5] = { 0, 0, 0, (uint8_t)scrollSteps, 0 };
  blehid.inputReport(1, report, sizeof(report));
}

// ============================================================================
// Battery Measurement Routines
// ============================================================================
float readBatteryVoltage() {
  pinMode(PIN_VBAT_ENABLE, OUTPUT);
  digitalWrite(PIN_VBAT_ENABLE, LOW); // Enable divider
  delay(5); // Allow divider voltage to settle

  analogReference(AR_INTERNAL_3_0); // 3.0V reference
  analogReadResolution(12);         // 12-bit (0..4095)

  analogRead(PIN_VBAT);             // Discard initial sample
  delay(2);
  int raw = analogRead(PIN_VBAT);

  digitalWrite(PIN_VBAT_ENABLE, HIGH); // Disable divider to save battery

  // Voltage divider ratio: 1510k / 510k approx
  float voltage = (raw * 3.0f / 4095.0f) * (1510.0f / 510.0f);
  return voltage;
}

uint8_t readBatteryPercentage() {
  float v = readBatteryVoltage();
  if (v >= 4.20f) return 100;
  if (v <= 3.40f) return 0;
  if (v < 3.70f) {
    return (uint8_t)((v - 3.40f) / 0.30f * 15.0f);
  } else {
    return (uint8_t)(15.0f + (v - 3.70f) / (4.20f - 3.70f) * 85.0f);
  }
}

// ============================================================================
// Serial CLI & Configuration
// ============================================================================
void printHelp() {
  Serial.println();
  Serial.println(F("--- Doomscroller Serial Commands ---"));
  Serial.println(F("  status         - Show current status and settings"));
  Serial.println(F("  battery        - Read battery voltage and estimated percentage"));
  Serial.println(F("  invert         - Toggle scroll direction (invert)"));
  Serial.println(F("  speed <deg>    - Set degrees per wheel click (e.g. 'speed 30.0')"));
  Serial.println(F("  accel <gain>   - Set flick acceleration (e.g. 'accel 0.02', 0=off)"));
  Serial.println(F("  deadzone <deg> - Set deadzone degrees (e.g. 'deadzone 0.18')"));
  Serial.println(F("  name <str>     - Set Bluetooth device name (e.g. 'name Doomscroller')"));
  Serial.println(F("  stream         - Toggle live angle & velocity telemetry"));
  Serial.println(F("  unpair         - Clear all BLE bonding keys and re-advertise"));
  Serial.println(F("  defaults       - Reset settings to factory defaults"));
  Serial.println(F("  reboot         - Reboot controller"));
  Serial.println(F("  help           - Print this menu"));
  Serial.println();
}

void printStatus() {
  Serial.println();
  Serial.println(F("--- Device Status ---"));
  Serial.print(F("Firmware:         ")); Serial.println(FIRMWARE_VERSION);
  Serial.print(F("Device Name:      ")); Serial.println(g_settings.deviceName);
  Serial.print(F("BLE Status:       "));
  Serial.println(Bluefruit.connected() ? F("Connected") : F("Advertising (Pairing Mode)"));
  Serial.print(F("Battery:          "));
  float v = readBatteryVoltage();
  Serial.print(v, 2);
  Serial.print(F("V ("));
  Serial.print(readBatteryPercentage());
  Serial.println(F("%)"));
  Serial.print(F("Raw Angle:        ")); Serial.println(g_sensor.getRawAngle());
  Serial.print(F("Scroll Direction: ")); Serial.println(g_settings.invertScroll ? F("INVERTED") : F("NORMAL"));
  Serial.print(F("Degrees/Step:     ")); Serial.println(g_settings.degreesPerStep, 2);
  Serial.print(F("Accel Factor:     ")); Serial.println(g_settings.accelFactor, 3);
  Serial.print(F("Deadzone:         ")); Serial.print(g_settings.deadzoneDeg, 2); Serial.println(F("°"));
  Serial.print(F("Uptime:           ")); Serial.print(millis() / 1000); Serial.println(F("s"));
  Serial.println();
}

void handleSerialCLI() {
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  int spaceIdx = line.indexOf(' ');
  String cmd = (spaceIdx == -1) ? line : line.substring(0, spaceIdx);
  String arg = (spaceIdx == -1) ? "" : line.substring(spaceIdx + 1);
  cmd.toLowerCase();

  if (cmd == "help") {
    printHelp();
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "battery") {
    float v = readBatteryVoltage();
    uint8_t p = readBatteryPercentage();
    Serial.print(F("Battery: "));
    Serial.print(v, 3);
    Serial.print(F(" V ("));
    Serial.print(p);
    Serial.println(F("%)"));
  } else if (cmd == "invert") {
    g_settings.invertScroll = !g_settings.invertScroll;
    Serial.print(F("Scroll direction inverted: "));
    Serial.println(g_settings.invertScroll ? F("YES") : F("NO"));
  } else if (cmd == "speed" || cmd == "scale") {
    float val = arg.toFloat();
    if (val > 0.5f && val < 100.0f) {
      g_settings.degreesPerStep = val;
      Serial.print(F("Degrees per step set to: "));
      Serial.println(val, 2);
    } else {
      Serial.println(F("Usage: speed <0.5 to 100.0> (e.g. speed 30.0)"));
    }
  } else if (cmd == "accel") {
    float val = arg.toFloat();
    if (val >= 0.0f && val <= 1.0f) {
      g_settings.accelFactor = val;
      Serial.print(F("Acceleration factor set to: "));
      Serial.println(val, 3);
    } else {
      Serial.println(F("Usage: accel <0.0 to 1.0> (e.g. accel 0.02)"));
    }
  } else if (cmd == "deadzone") {
    float val = arg.toFloat();
    if (val >= 0.01f && val <= 5.0f) {
      g_settings.deadzoneDeg = val;
      Serial.print(F("Deadzone set to: "));
      Serial.print(val, 2);
      Serial.println(F("°"));
    } else {
      Serial.println(F("Usage: deadzone <0.01 to 5.0>"));
    }
  } else if (cmd == "name") {
    arg.trim();
    if (arg.length() > 0 && arg.length() < 31) {
      strncpy(g_settings.deviceName, arg.c_str(), sizeof(g_settings.deviceName) - 1);
      g_settings.deviceName[sizeof(g_settings.deviceName) - 1] = '\0';
      Serial.print(F("Device name updated to '"));
      Serial.print(g_settings.deviceName);
      Serial.println(F("'. Reboot to apply new BLE name."));
    } else {
      Serial.println(F("Usage: name <1-30 chars>"));
    }
  } else if (cmd == "stream") {
    g_telemetryStream = !g_telemetryStream;
    Serial.print(F("Live telemetry stream: "));
    Serial.println(g_telemetryStream ? F("ON") : F("OFF"));
  } else if (cmd == "unpair" || cmd == "clearbonds") {
    Bluefruit.Periph.clearBonds();
    Serial.println(F("[BLE] All pairing bonds cleared. Re-advertising for fresh pairing."));
    Bluefruit.Advertising.start(0);
  } else if (cmd == "defaults") {
    settings_reset_defaults();
    Serial.println(F("Reset to factory defaults."));
    printStatus();
  } else if (cmd == "reboot") {
    Serial.println(F("Rebooting..."));
    delay(100);
    NVIC_SystemReset();
  } else {
    Serial.print(F("Unknown command '"));
    Serial.print(cmd);
    Serial.println(F("'. Type 'help' for commands."));
  }
}
