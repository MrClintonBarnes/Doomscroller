#include <bluefruit.h>
#include "config.h"
#include "settings.h"
#include "sensor.h"

// ============================================================================
// Battery Hardware Definitions (Seeed XIAO nRF52840 Sense)
// ============================================================================
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
BLEUart       bleuart;                // Nordic UART Service (NUS) for wireless Web Bluetooth

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
static bool     g_pendingParkEdge    = false;
static uint32_t g_connectTime        = 0;
static uint32_t g_lastActivityTime   = 0;
static bool     g_isIdle             = false;

// Forward Declarations
void printHelp(Stream& io);
void printStatus(Stream& io);
void printJsonStatus(Stream& io);
void handleStreamCLI(Stream& io);
void processCommand(Stream& io, String line);
void switchProfile(uint8_t newProfile, Stream& io);
void updateBleAddress();
void sendMouseScroll(int8_t scrollSteps);
void parkMouseEdge();
void parkMouseCenter();
float readBatteryVoltage();
uint8_t readBatteryPercentage();

// ============================================================================
// BLE Connection Callbacks
// ============================================================================
void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  g_sensor.resetState();
  g_substepAccumulator = 0.0f;
  g_pendingParkEdge    = true;
  g_connectTime        = millis();
  g_lastActivityTime   = millis();
  g_isIdle             = false;
  Serial.println(F("[BLE] Connected to host!"));
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;
  g_sensor.enablePower(false);
  g_substepAccumulator = 0.0f;
  g_pendingParkEdge    = false;
  g_isIdle             = false;
  Serial.println(F("[BLE] Disconnected from host. Advertising resumed."));
}

// ============================================================================
// Bluetooth Identity Management: Per-Profile Unique MAC
// ============================================================================
// Assigns a unique MAC offset to each profile slot. When the user switches
// to Profile 2 (PC), the Phone (paired to Profile 1) won't hijack the connection!
void updateBleAddress() {
  uint8_t resetCount = NRF_POWER->GPREGRET;
  ble_gap_addr_t gap_addr;
  if (sd_ble_gap_addr_get(&gap_addr) == NRF_SUCCESS) {
    gap_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    gap_addr.addr[0] += (resetCount * 3) + g_settings.activeProfile + 1;
    gap_addr.addr[5] |= 0xC0; // Standard BLE random static address requirement
    sd_ble_gap_addr_set(&gap_addr);
  }
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

  // Initialize Settings from InternalFS flash
  settings_init();

  // Configure high-bandwidth peripheral mode for simultaneous HID + BLE UART
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
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

  // Set fresh per-profile static BLE identity address
  updateBleAddress();

  // Security configuration: unauthenticated Just-Works pairing with encryption
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setMITM(false);

  // Connection Interval: 11.25ms to 20ms
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

  // Nordic UART Service (for Web Bluetooth Configurator)
  bleuart.begin();

  // BLE HID Generic Service (Pure Mouse)
  uint16_t input_report_lengths[] = { 5 }; // Buttons(1), X(1), Y(1), Wheel(1), Pan(1)
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
  Bluefruit.ScanResponse.addService(bleuart); // Nordic UART Service in Scan Response
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // 20ms fast, 152.5ms slow
  Bluefruit.Advertising.setFastTimeout(60);   // 60 seconds fast
  Bluefruit.Advertising.start(0);

  // Automatic connection LED
  Bluefruit.autoConnLed(true);

  // Initialize MagAlpha Sensor SPI bus
  g_sensor.begin();

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  DOOMSCROLLER UNIVERSAL FIRMWARE " FIRMWARE_VERSION));
  Serial.println(F("  Cross-Platform BLE & Web Bluetooth    "));
  Serial.println(F("========================================"));
  Serial.print(F("Device Name:    ")); Serial.println(g_settings.deviceName);
  Serial.print(F("Active Profile: [")); Serial.print(g_settings.activeProfile + 1);
  Serial.print(F("] ")); Serial.println(getActiveProfile().name);
  if (isMagneticReset) {
    Serial.println(F("[System] Magnetic reset detected. Bonds wiped, address refreshed."));
  }
  Serial.println(F("Ready to connect."));
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. Process Serial CLI commands if connected to USB
  if (Serial.available()) {
    handleStreamCLI(Serial);
  }

  // 2. Process BLE UART commands if connected over Web Bluetooth
  if (bleuart.available()) {
    handleStreamCLI(bleuart);
  }

  // 3. When not connected to Bluetooth, put sensor to sleep to conserve battery
  if (!Bluefruit.connected()) {
    g_sensor.enablePower(false);
    g_substepAccumulator = 0.0f;
    g_isIdle = false;
    delay(20);
    return;
  }

  // 4. Auto-slide cursor to right edge ~1500ms after connection if enabled for active profile
  if (g_pendingParkEdge && (now - g_connectTime >= 1500)) {
    g_pendingParkEdge = false;
    if (getActiveProfile().cursorPark) {
      parkMouseEdge();
    }
  }

  // 5. Periodic Battery Monitoring (every 60s while connected)
  if (now - g_lastBatteryCheck >= 60000 || (g_lastBatteryCheck == 0 && now >= 3000)) {
    g_lastBatteryCheck = now;
    uint8_t pct = readBatteryPercentage();
    blebas.write(pct);
  }

  // 6. Connected Idle Sleep: after 15 seconds of inactivity, enter ultra-low power sleep
  TuningProfile& prof = getActiveProfile();
  if (g_isIdle) {
    // Poll sensor every 100ms with checkIdleWakeup
    // MagAlpha is powered OFF between polls, CPU enters low-power sleep
    if (g_sensor.checkIdleWakeup(prof.deadzoneDeg * 1.5f)) {
      // Motion detected! Wake up immediately
      g_isIdle = false;
      g_lastActivityTime = now;
      g_sensor.enablePower(true);
      g_sensor.resetState();
    } else {
      delay(IDLE_POLL_INTERVAL_MS);
      return;
    }
  }

  // 7. Active Sampling at 100Hz (10ms)
  float deltaDeg = 0.0f;
  float velocity = 0.0f;
  if (!g_sensor.update(deltaDeg, velocity)) {
    delay(ACTIVE_LOOP_INTERVAL_MS);
    return;
  }

  float absVel = fabsf(velocity);
  if (absVel > prof.deadzoneDeg) {
    g_lastActivityTime = now;
    float dir = prof.invertScroll ? -1.0f : 1.0f;
    float baseSteps = (deltaDeg / prof.degreesPerStep) * dir;

    // Dynamic flick acceleration curve
    float accelMultiplier = 1.0f + (absVel * prof.accelFactor * 0.01f);
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

      sendMouseScroll((int8_t)stepsToSend);

      if (g_telemetryStream) {
        Serial.print(F("Delta: ")); Serial.print(deltaDeg, 2);
        Serial.print(F("°\tVel: ")); Serial.print(velocity, 1);
        Serial.print(F("°/s\tSteps: ")); Serial.println(stepsToSend);
        if (Bluefruit.connected()) {
          bleuart.print(F("Delta: ")); bleuart.print(deltaDeg, 2);
          bleuart.print(F("°\tVel: ")); bleuart.print(velocity, 1);
          bleuart.print(F("°/s\tSteps: ")); bleuart.println(stepsToSend);
        }
      }
    }
  } else {
    // Settling decay when wheel is stationary
    g_substepAccumulator *= 0.5f;
    if (fabsf(g_substepAccumulator) < 0.05f) {
      g_substepAccumulator = 0.0f;
    }

    // Check if 15 seconds have elapsed without motion
    if (now - g_lastActivityTime > ((uint32_t)DEFAULT_IDLE_TIMEOUT_SEC * 1000)) {
      g_isIdle = true;
      g_sensor.enablePower(false);
    }
  }

  delay(ACTIVE_LOOP_INTERVAL_MS);
}

// ============================================================================
// Mouse Scroll HID Dispatcher
// ============================================================================
void sendMouseScroll(int8_t scrollSteps) {
  uint8_t report[5] = { 0, 0, 0, (uint8_t)scrollSteps, 0 };
  blehid.inputReport(1, report, sizeof(report));
}

// ============================================================================
// Cursor Positioning: slides cursor to right margin (out of center, inside video)
// ============================================================================
void parkMouseEdge() {
  if (!Bluefruit.connected()) return;
  int sent = 0;
  for (int attempt = 0; attempt < 40 && sent < 18; attempt++) {
    uint8_t report[5] = { 0, 120, 0, 0, 0 };
    if (blehid.inputReport(1, report, sizeof(report))) {
      sent++;
    }
    delay(15);
  }
}

void parkMouseCenter() {
  if (!Bluefruit.connected()) return;
  int sent = 0;
  for (int attempt = 0; attempt < 30 && sent < 8; attempt++) {
    uint8_t report[5] = { 0, (uint8_t)-120, 0, 0, 0 };
    if (blehid.inputReport(1, report, sizeof(report))) {
      sent++;
    }
    delay(15);
  }
}

// ============================================================================
// Battery Measurement Routines
// ============================================================================
float readBatteryVoltage() {
  pinMode(PIN_VBAT_ENABLE, OUTPUT);
  digitalWrite(PIN_VBAT_ENABLE, LOW); // Enable divider
  delay(5);

  analogReference(AR_INTERNAL_3_0); // 3.0V reference
  analogReadResolution(12);         // 12-bit (0..4095)

  analogRead(PIN_VBAT);             // Discard initial sample
  delay(2);
  int raw = analogRead(PIN_VBAT);

  digitalWrite(PIN_VBAT_ENABLE, HIGH); // Disable divider to save battery

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
// Profile Channel Switching
// ============================================================================
void switchProfile(uint8_t newProfile, Stream& io) {
  if (newProfile >= NUM_PROFILES) return;
  bool changed = (g_settings.activeProfile != newProfile);
  g_settings.activeProfile = newProfile;
  settings_save();

  io.print(F("Active profile switched to ["));
  io.print(newProfile + 1);
  io.print(F("] "));
  io.print(getActiveProfile().name);
  io.println(F(" (Saved to flash)"));

  if (changed) {
    // Disconnect current host to allow the paired device on the target profile to connect
    if (Bluefruit.connected()) {
      io.println(F("[BLE] Switching Bluetooth identity channel..."));
      delay(50);
      Bluefruit.disconnect(Bluefruit.connHandle());
    }
    updateBleAddress();
    Bluefruit.Advertising.start(60);
  }
}

// ============================================================================
// CLI & Configuration (Universal Stream: Works identically over USB & BLE)
// ============================================================================
void printHelp(Stream& io) {
  io.println();
  io.println(F("--- Doomscroller Commands ---"));
  io.println(F("  status         - Show current status and all profile settings"));
  io.println(F("  profile <1-3>  - Switch active tuning profile (e.g. 'profile 1')"));
  io.println(F("  pname <str>    - Rename active profile (e.g. 'pname Social')"));
  io.println(F("  parkmode       - Toggle auto-parking cursor on right edge"));
  io.println(F("  speed <deg>    - Set degrees per click for active profile"));
  io.println(F("  accel <gain>   - Set flick acceleration for active profile"));
  io.println(F("  deadzone <deg> - Set deadzone degrees for active profile"));
  io.println(F("  invert         - Toggle scroll direction for active profile"));
  io.println(F("  name <str>     - Set Bluetooth device name"));
  io.println(F("  save           - Explicitly save settings to flash"));
  io.println(F("  edge           - Slide mouse cursor to right margin"));
  io.println(F("  center         - Move mouse cursor back towards center"));
  io.println(F("  battery        - Read battery voltage and estimated percentage"));
  io.println(F("  json           - Output machine-readable JSON status"));
  io.println(F("  disconnect     - Disconnect from currently connected host"));
  io.println(F("  pair           - Disconnect and enter pairing mode for new device"));
  io.println(F("  dfu            - Reboot into UF2 bootloader drive for flashing"));
  io.println(F("  stream         - Toggle live angle & velocity telemetry"));
  io.println(F("  unpair         - Clear all BLE bonding keys and re-advertise"));
  io.println(F("  defaults       - Reset all settings to factory defaults"));
  io.println(F("  reboot         - Reboot controller"));
  io.println(F("  help           - Print this menu"));
  io.println();
}

void printStatus(Stream& io) {
  io.println();
  io.println(F("--- Device Status ---"));
  io.print(F("Firmware:         ")); io.println(FIRMWARE_VERSION);
  io.print(F("Device Name:      ")); io.println(g_settings.deviceName);
  io.print(F("BLE Status:       "));
  io.println(Bluefruit.connected() ? F("Connected") : F("Advertising (Pairing Mode)"));
  io.print(F("Battery:          "));
  float v = readBatteryVoltage();
  io.print(v, 2);
  io.print(F("V ("));
  io.print(readBatteryPercentage());
  io.println(F("%)"));
  io.print(F("Raw Angle:        ")); io.println(g_sensor.getRawAngle());
  io.print(F("Active Profile:   "));
  io.print(g_settings.activeProfile + 1);
  io.print(F(" / "));
  io.print(NUM_PROFILES);
  io.print(F(" ["));
  io.print(getActiveProfile().name);
  io.println(F("]"));

  io.println(F("--- Active Profile Tuning ---"));
  TuningProfile& cur = getActiveProfile();
  io.print(F("  Degrees/Step:   ")); io.println(cur.degreesPerStep, 2);
  io.print(F("  Accel Factor:   ")); io.println(cur.accelFactor, 3);
  io.print(F("  Deadzone:       ")); io.print(cur.deadzoneDeg, 2); io.println(F("°"));
  io.print(F("  Scroll Invert:  ")); io.println(cur.invertScroll ? F("YES (Inverted)") : F("NO (Normal)"));
  io.print(F("  Cursor Auto-Park: ")); io.println(cur.cursorPark ? F("YES (Slide to edge)") : F("NO (Keep in place)"));

  io.println(F("--- All Profiles ---"));
  for (int i = 0; i < NUM_PROFILES; i++) {
    io.print(i == g_settings.activeProfile ? F(" *[") : F("  ["));
    io.print(i + 1);
    io.print(F("] "));
    io.print(g_settings.profiles[i].name);
    io.print(F(": "));
    io.print(g_settings.profiles[i].degreesPerStep, 1);
    io.print(F("°/step, accel="));
    io.print(g_settings.profiles[i].accelFactor, 3);
    io.print(F(", deadzone="));
    io.print(g_settings.profiles[i].deadzoneDeg, 2);
    io.print(F("°, invert="));
    io.print(g_settings.profiles[i].invertScroll ? F("YES") : F("NO"));
    io.print(F(", park="));
    io.println(g_settings.profiles[i].cursorPark ? F("YES") : F("NO"));
  }
  io.print(F("Uptime:           ")); io.print(millis() / 1000); io.println(F("s"));
  io.println();
}

void printJsonStatus(Stream& io) {
  float v = readBatteryVoltage();
  uint8_t pct = readBatteryPercentage();
  io.print(F("{\"version\":"));
  io.print(g_settings.version);
  io.print(F(",\"fw\":\"" FIRMWARE_VERSION "\""));
  io.print(F(",\"name\":\""));
  io.print(g_settings.deviceName);
  io.print(F("\",\"connected\":"));
  io.print(Bluefruit.connected() ? F("true") : F("false"));
  io.print(F(",\"batteryV\":"));
  io.print(v, 2);
  io.print(F(",\"batteryPct\":"));
  io.print(pct);
  io.print(F(",\"rawAngle\":"));
  io.print(g_sensor.getRawAngle());
  io.print(F(",\"activeProfile\":"));
  io.print(g_settings.activeProfile);
  io.print(F(",\"profiles\":["));
  for (int i = 0; i < NUM_PROFILES; i++) {
    if (i > 0) io.print(',');
    io.print(F("{\"id\":"));
    io.print(i);
    io.print(F(",\"name\":\""));
    io.print(g_settings.profiles[i].name);
    io.print(F("\",\"speed\":"));
    io.print(g_settings.profiles[i].degreesPerStep, 2);
    io.print(F(",\"accel\":"));
    io.print(g_settings.profiles[i].accelFactor, 3);
    io.print(F(",\"deadzone\":"));
    io.print(g_settings.profiles[i].deadzoneDeg, 2);
    io.print(F(",\"invert\":"));
    io.print(g_settings.profiles[i].invertScroll ? F("true") : F("false"));
    io.print(F(",\"cursorPark\":"));
    io.print(g_settings.profiles[i].cursorPark ? F("true") : F("false"));
    io.print(F("}"));
  }
  io.println(F("]}"));
}

void handleStreamCLI(Stream& io) {
  String line = io.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  processCommand(io, line);
}

void processCommand(Stream& io, String line) {
  int spaceIdx = line.indexOf(' ');
  String cmd = (spaceIdx == -1) ? line : line.substring(0, spaceIdx);
  String arg = (spaceIdx == -1) ? "" : line.substring(spaceIdx + 1);
  cmd.toLowerCase();

  if (cmd == "help") {
    printHelp(io);
  } else if (cmd == "status") {
    printStatus(io);
  } else if (cmd == "json") {
    printJsonStatus(io);
  } else if (cmd == "profile") {
    arg.trim();
    if (arg.length() > 0) {
      int pNum = arg.toInt();
      if (pNum >= 1 && pNum <= NUM_PROFILES) {
        switchProfile((uint8_t)(pNum - 1), io);
      } else if (pNum == 0 && arg == "0") {
        switchProfile(0, io);
      } else {
        io.println(F("Usage: profile <1 to 3> (e.g. 'profile 1')"));
      }
    } else {
      io.print(F("Current profile: ["));
      io.print(g_settings.activeProfile + 1);
      io.print(F("] "));
      io.println(getActiveProfile().name);
    }
  } else if (cmd == "pname") {
    arg.trim();
    if (arg.length() > 0 && arg.length() < 16) {
      strncpy(getActiveProfile().name, arg.c_str(), sizeof(getActiveProfile().name) - 1);
      getActiveProfile().name[sizeof(getActiveProfile().name) - 1] = '\0';
      settings_save();
      io.print(F("Active profile renamed to '"));
      io.print(getActiveProfile().name);
      io.println(F("' (Saved to flash)"));
    } else {
      io.println(F("Usage: pname <1-15 chars>"));
    }
  } else if (cmd == "parkmode" || cmd == "cursorpark") {
    TuningProfile& prof = getActiveProfile();
    arg.toLowerCase();
    if (arg == "on" || arg == "true" || arg == "1") {
      prof.cursorPark = true;
    } else if (arg == "off" || arg == "false" || arg == "0") {
      prof.cursorPark = false;
    } else {
      prof.cursorPark = !prof.cursorPark;
    }
    settings_save();
    io.print(F("Cursor auto-parking for ["));
    io.print(prof.name);
    io.print(F("]: "));
    io.println(prof.cursorPark ? F("ENABLED (Saved to flash)") : F("DISABLED (Saved to flash)"));
  } else if (cmd == "battery") {
    float v = readBatteryVoltage();
    uint8_t p = readBatteryPercentage();
    io.print(F("Battery: "));
    io.print(v, 3);
    io.print(F(" V ("));
    io.print(p);
    io.println(F("%)"));
  } else if (cmd == "invert") {
    TuningProfile& prof = getActiveProfile();
    prof.invertScroll = !prof.invertScroll;
    settings_save();
    io.print(F("Scroll direction for ["));
    io.print(prof.name);
    io.print(F("]: "));
    io.println(prof.invertScroll ? F("INVERTED (Saved to flash)") : F("NORMAL (Saved to flash)"));
  } else if (cmd == "speed" || cmd == "scale") {
    float val = arg.toFloat();
    if (val > 0.5f && val < 100.0f) {
      getActiveProfile().degreesPerStep = val;
      settings_save();
      io.print(F("Degrees per step for ["));
      io.print(getActiveProfile().name);
      io.print(F("] set to: "));
      io.print(val, 2);
      io.println(F(" (Saved to flash)"));
    } else {
      io.println(F("Usage: speed <0.5 to 100.0> (e.g. speed 30.0)"));
    }
  } else if (cmd == "accel") {
    float val = arg.toFloat();
    if (val >= 0.0f && val <= 1.0f) {
      getActiveProfile().accelFactor = val;
      settings_save();
      io.print(F("Acceleration factor for ["));
      io.print(getActiveProfile().name);
      io.print(F("] set to: "));
      io.print(val, 3);
      io.println(F(" (Saved to flash)"));
    } else {
      io.println(F("Usage: accel <0.0 to 1.0> (e.g. accel 0.02)"));
    }
  } else if (cmd == "deadzone") {
    float val = arg.toFloat();
    if (val >= 0.01f && val <= 5.0f) {
      getActiveProfile().deadzoneDeg = val;
      settings_save();
      io.print(F("Deadzone for ["));
      io.print(getActiveProfile().name);
      io.print(F("] set to: "));
      io.print(val, 2);
      io.println(F("° (Saved to flash)"));
    } else {
      io.println(F("Usage: deadzone <0.01 to 5.0>"));
    }
  } else if (cmd == "name") {
    arg.trim();
    if (arg.length() > 0 && arg.length() < 31) {
      strncpy(g_settings.deviceName, arg.c_str(), sizeof(g_settings.deviceName) - 1);
      g_settings.deviceName[sizeof(g_settings.deviceName) - 1] = '\0';
      settings_save();
      io.print(F("Device name updated to '"));
      io.print(g_settings.deviceName);
      io.println(F("'. Reboot to apply new BLE name. (Saved to flash)"));
    } else {
      io.println(F("Usage: name <1-30 chars>"));
    }
  } else if (cmd == "save") {
    if (settings_save()) {
      io.println(F("Settings successfully saved to flash (/settings.dat)."));
    } else {
      io.println(F("ERROR: Failed to write settings to flash!"));
    }
  } else if (cmd == "disconnect") {
    io.println(F("[BLE] Disconnecting from host..."));
    if (Bluefruit.connected()) {
      delay(50);
      Bluefruit.disconnect(Bluefruit.connHandle());
    }
  } else if (cmd == "pair") {
    io.println(F("[BLE] Disconnecting and starting fresh pairing mode (60s)..."));
    if (Bluefruit.connected()) {
      delay(50);
      Bluefruit.disconnect(Bluefruit.connHandle());
    }
    uint8_t count = NRF_POWER->GPREGRET + 1;
    NRF_POWER->GPREGRET = count;
    updateBleAddress();
    Bluefruit.Advertising.start(60);
  } else if (cmd == "edge" || cmd == "park") {
    parkMouseEdge();
    io.println(F("Cursor moved to right edge."));
  } else if (cmd == "center") {
    parkMouseCenter();
    io.println(F("Cursor moved to center."));
  } else if (cmd == "dfu" || cmd == "bootloader") {
    io.println(F("Rebooting into UF2 bootloader drive..."));
    delay(100);
    enterUf2Dfu();
  } else if (cmd == "stream") {
    g_telemetryStream = !g_telemetryStream;
    io.print(F("Live telemetry stream: "));
    io.println(g_telemetryStream ? F("ON") : F("OFF"));
  } else if (cmd == "unpair" || cmd == "clearbonds") {
    Bluefruit.Periph.clearBonds();
    io.println(F("[BLE] All pairing bonds cleared. Re-advertising for fresh pairing."));
    Bluefruit.Advertising.start(0);
  } else if (cmd == "defaults") {
    settings_reset_defaults();
    settings_save();
    io.println(F("Reset to factory defaults and saved to flash."));
    printStatus(io);
  } else if (cmd == "reboot") {
    io.println(F("Rebooting..."));
    delay(100);
    NVIC_SystemReset();
  } else {
    io.print(F("Unknown command '"));
    io.print(cmd);
    io.println(F("'. Type 'help' for commands."));
  }
}
