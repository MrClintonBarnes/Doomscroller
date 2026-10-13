#include <bluefruit.h>
#include "utility/bonding.h"
#include "config.h"
#include "settings.h"
#include "sensor.h"
#include "battery.h"

// ============================================================================
// Global BLE Objects
// ============================================================================
BLEDis bledis;          // Device Information Service
BLEBas blebas;          // Battery Service
BLEHidAdafruit blehid;  // Standard Composite HID (Mouse, Consumer, Keyboard)

// ============================================================================
// State Variables
// ============================================================================
enum DeviceState {
  STATE_ADVERTISING,
  STATE_CONNECTED_ACTIVE,
  STATE_CONNECTED_IDLE
};

static DeviceState g_state = STATE_ADVERTISING;
static uint32_t g_lastMotionTime = 0;
static uint32_t g_lastBatteryCheck = 0;
static uint32_t g_lastLedBlink = 0;
static float g_substepAccumulator = 0.0f;
static bool g_telemetryStream = false;

// Forward Declarations
void printHelp();
void printStatus();
void handleSerialCLI();

// ============================================================================
// BLE Callbacks
// ============================================================================
void connect_callback(uint16_t conn_handle) {
  resumeLoop(); // Guarantee the Arduino loop task is running

  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  char peerName[32] = {0};
  if (conn) {
    conn->getPeerName(peerName, sizeof(peerName));
  }

  Serial.println();
  Serial.print(F("[BLE] Connected to: "));
  Serial.println(peerName[0] ? peerName : "Central device");

  // Visual confirmation: flash Green LED for 250ms
  digitalWrite(PIN_LED_BLUE, HIGH);   // Blue OFF
  digitalWrite(PIN_LED_GREEN, LOW);  // Green ON (active LOW)
  delay(250);
  digitalWrite(PIN_LED_GREEN, HIGH); // Green OFF

  // Reset state
  g_sensor.resetState();
  g_substepAccumulator = 0.0f;
  g_lastMotionTime = millis();
  g_state = STATE_CONNECTED_ACTIVE;

  // Send initial battery level
  uint8_t batt = BatteryMonitor::readPercentage();
  blebas.write(batt);
  Serial.print(F("[Battery] Initial level: "));
  Serial.print(batt);
  Serial.println(F("%"));
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  Serial.println();
  Serial.print(F("[BLE] Disconnected (reason: 0x"));
  Serial.print(reason, HEX);
  Serial.println(F("). Entering pairing mode."));

  g_state = STATE_ADVERTISING;
  g_sensor.enablePower(false); // Sleep sensor when not connected
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize LEDs (active LOW on Seeed XIAO nRF52840)
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_RED, HIGH);
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_BLUE, HIGH);

  // Initialize Hardware Modules
  BatteryMonitor::init();
  settings_init();
  g_sensor.begin();

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  DOOMSCROLLER UNIVERSAL FIRMWARE " FIRMWARE_VERSION));
  Serial.println(F("  Cross-Platform BLE Scroll Controller  "));
  Serial.println(F("  (macOS / Android / iOS / Windows / Linux)"));
  Serial.println(F("========================================"));
  Serial.print(F("Device Name: "));
  Serial.println(g_settings.deviceName);
  Serial.print(F("Battery:     "));
  Serial.print(BatteryMonitor::readVoltage(), 2);
  Serial.print(F("V ("));
  Serial.print(BatteryMonitor::readPercentage());
  Serial.println(F("%)"));
  Serial.println(F("Type 'help' in serial monitor for options."));
  Serial.println(F("----------------------------------------"));

  // Configure BLE
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(1, 1);
  Bluefruit.setTxPower(4); // +4 dBm for strong, reliable signal
  Bluefruit.setName(g_settings.deviceName);

  // Register Connection Callbacks
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
  // Connection interval: 11.25ms to 15.0ms for ultra-low latency scrolling
  Bluefruit.Periph.setConnInterval(9, 12);

  // Check boot/reset source
  uint32_t resetReason = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = 0xFFFFFFFF; // Clear reset flags
  if (resetReason & POWER_RESETREAS_RESETPIN_Msk) {
    Serial.println(F("[System] Boot source: Magnetic Hall Sensor / Pin Reset (letter 'C')"));
  }

  // Device Information Service
  bledis.setManufacturer("Andrew Industries");
  bledis.setModel("Doomscroller Universal");
  bledis.setSoftwareRev(FIRMWARE_VERSION);
  bledis.begin();

  // Battery Service
  blebas.begin();
  blebas.write(BatteryMonitor::readPercentage());

  // Standard BLE HID Service (Mouse, Consumer, Keyboard)
  blehid.begin();

  // Advertising configuration
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addService(blebas);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 160); // 20ms to 100ms fast advertising
  Bluefruit.Advertising.setFastTimeout(0);     // Advertise indefinitely without sleeping permanently
  Bluefruit.Advertising.start(0);

  Serial.println(F("[BLE] Advertising started. Ready to pair."));
  g_lastMotionTime = millis();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // 1. Process Serial CLI commands
  if (Serial.available()) {
    handleSerialCLI();
  }

  // 2. Battery Monitoring (check every 60s)
  if (now - g_lastBatteryCheck >= 60000) {
    g_lastBatteryCheck = now;
    uint8_t batt = BatteryMonitor::readPercentage();
    if (Bluefruit.connected()) {
      blebas.write(batt);
    }
  }

  // 3. State Machine & Scrolling
  if (g_state == STATE_ADVERTISING) {
    // Pulse Blue LED inside USB-C port to indicate discovery/pairing mode
    if (now - g_lastLedBlink >= 600) {
      g_lastLedBlink = now;
      digitalWrite(PIN_LED_BLUE, LOW);  // LED ON (active low)
      delay(80);
      digitalWrite(PIN_LED_BLUE, HIGH); // LED OFF
    }
    delay(40);
    return;
  }

  if (g_state == STATE_CONNECTED_IDLE) {
    // Check for wheel movement every 100ms
    if (g_sensor.checkIdleWakeup(1.0f)) {
      Serial.println(F("[Power] Wakeup from idle."));
      g_state = STATE_CONNECTED_ACTIVE;
      g_lastMotionTime = now;
      g_substepAccumulator = 0.0f;
    } else {
      delay(IDLE_POLL_INTERVAL_MS);
      return;
    }
  }

  // Active connected state: sample at ~100Hz (10ms)
  float deltaDeg = 0.0f;
  float velocity = 0.0f;
  if (!g_sensor.update(deltaDeg, velocity)) {
    delay(ACTIVE_LOOP_INTERVAL_MS);
    return;
  }

  // Deadzone filter: ignore micro-tremors and mechanical resting backlash
  float absDelta = fabsf(deltaDeg);
  float absVel = fabsf(velocity);

  if (absDelta >= g_settings.deadzoneDeg || absVel >= (g_settings.deadzoneDeg * 60.0f)) {
    g_lastMotionTime = now;

    // Apply scroll direction:
    // In standard HID Mouse: positive wheel value = scroll UP, negative = scroll DOWN.
    // If invertScroll is true, flip sign.
    float dir = g_settings.invertScroll ? -1.0f : 1.0f;
    float baseSteps = (deltaDeg / g_settings.degreesPerStep) * dir;

    // Dynamic flick acceleration curve:
    // Gentle rotation: 1.0x (linear, precise).
    // Fast flick: progressive multiplier for effortless long feed scrolling.
    float accelMultiplier = 1.0f + (absVel * g_settings.accelFactor * 0.01f);
    if (accelMultiplier > 8.0f) {
      accelMultiplier = 8.0f; // Safe upper bound
    }

    float totalStepDelta = baseSteps * accelMultiplier;
    g_substepAccumulator += totalStepDelta;

    int stepsToSend = (int)g_substepAccumulator;
    if (stepsToSend != 0) {
      g_substepAccumulator -= (float)stepsToSend;

      // Clamp to int8 limits (-127 to 127)
      if (stepsToSend > 127) stepsToSend = 127;
      if (stepsToSend < -127) stepsToSend = -127;

      // Send standard BLE HID Mouse scroll wheel report
      blehid.mouseScroll((int8_t)stepsToSend);

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
    // Settling decay: slowly bleed off remainder when wheel is stopped
    g_substepAccumulator *= 0.5f;
    if (fabsf(g_substepAccumulator) < 0.05f) {
      g_substepAccumulator = 0.0f;
    }

    // Check for idle timeout
    if ((now - g_lastMotionTime) >= (uint32_t)(g_settings.idleTimeoutSec * 1000)) {
      Serial.println(F("[Power] Idle timeout reached. Entering low-power standby."));
      g_sensor.enablePower(false);
      g_state = STATE_CONNECTED_IDLE;
    }
  }

  delay(ACTIVE_LOOP_INTERVAL_MS);
}

// ============================================================================
// Serial CLI & Configuration
// ============================================================================
void printHelp() {
  Serial.println();
  Serial.println(F("--- Doomscroller Serial Commands ---"));
  Serial.println(F("  status         - Show current status and settings"));
  Serial.println(F("  invert         - Toggle scroll direction (invert)"));
  Serial.println(F("  speed <deg>    - Set degrees per wheel click (e.g. 'speed 10.0')"));
  Serial.println(F("  accel <gain>   - Set flick acceleration (e.g. 'accel 0.05', 0=off)"));
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
  if (Bluefruit.connected()) {
    Serial.println(g_state == STATE_CONNECTED_IDLE ? F("Connected (Idle)") : F("Connected (Active)"));
  } else {
    Serial.println(F("Advertising (Pairing Mode)"));
  }
  Serial.print(F("Battery:          "));
  Serial.print(BatteryMonitor::readVoltage(), 2);
  Serial.print(F("V ("));
  Serial.print(BatteryMonitor::readPercentage());
  Serial.println(F("%)"));
  Serial.print(F("Raw Angle:        ")); Serial.println(g_sensor.getRawAngle());
  Serial.print(F("Scroll Direction: ")); Serial.println(g_settings.invertScroll ? F("INVERTED") : F("NORMAL"));
  Serial.print(F("Degrees/Step:     ")); Serial.println(g_settings.degreesPerStep, 2);
  Serial.print(F("Accel Factor:     ")); Serial.println(g_settings.accelFactor, 3);
  Serial.print(F("Deadzone:         ")); Serial.print(g_settings.deadzoneDeg, 2); Serial.println(F("°"));
  Serial.print(F("Idle Timeout:     ")); Serial.print(g_settings.idleTimeoutSec); Serial.println(F("s"));
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
  } else if (cmd == "invert") {
    g_settings.invertScroll = !g_settings.invertScroll;
    settings_save();
    Serial.print(F("Scroll direction inverted: "));
    Serial.println(g_settings.invertScroll ? F("YES") : F("NO"));
  } else if (cmd == "speed" || cmd == "scale") {
    float val = arg.toFloat();
    if (val > 0.5f && val < 100.0f) {
      g_settings.degreesPerStep = val;
      settings_save();
      Serial.print(F("Degrees per step set to: "));
      Serial.println(val, 2);
    } else {
      Serial.println(F("Usage: speed <0.5 to 100.0> (e.g. speed 10.0)"));
    }
  } else if (cmd == "accel") {
    float val = arg.toFloat();
    if (val >= 0.0f && val <= 1.0f) {
      g_settings.accelFactor = val;
      settings_save();
      Serial.print(F("Acceleration factor set to: "));
      Serial.println(val, 3);
    } else {
      Serial.println(F("Usage: accel <0.0 to 1.0> (e.g. accel 0.05)"));
    }
  } else if (cmd == "deadzone") {
    float val = arg.toFloat();
    if (val >= 0.01f && val <= 5.0f) {
      g_settings.deadzoneDeg = val;
      settings_save();
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
      settings_save();
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
    bond_clear_all();
    Serial.println(F("[BLE] All pairing bonds cleared. Re-advertising for fresh pairing."));
    Bluefruit.Advertising.start(0);
  } else if (cmd == "defaults") {
    settings_reset_defaults();
    settings_save();
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
