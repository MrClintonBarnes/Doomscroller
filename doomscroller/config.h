#pragma once
#include <Arduino.h>

// ============================================================================
// Hardware Pin Definitions (Seeed XIAO nRF52840)
// ============================================================================
#define PIN_SPI_CS          0     // D0 / P0.02 - MagAlpha SPI Chip Select
#define PIN_MPS_ENABLE      1     // D1 / P0.03 - MagAlpha Sensor Power Enable (HIGH=ON, LOW=OFF)

// LEDs on Seeed XIAO nRF52840 (Active LOW)
#define PIN_LED_RED         11    // P0.26 - Red LED
#define PIN_LED_BLUE        12    // P0.06 - Blue LED (used for pairing indicator)
#define PIN_LED_GREEN       13    // P0.30 - Green LED

// ============================================================================
// SPI & MagAlpha Encoder Parameters
// ============================================================================
#define SPI_SCLK_FREQUENCY  5000000   // 5 MHz SPI clock
#define MA780_REG_BCT       2         // Register 2: BCT side-shaft configuration (0x00)
#define MA780_REG_Y_TRIM    3         // Register 3: Y trimming enable (0x02)
#define MA780_REG_FILTER    14        // Register 14: Filter cutoff 78Hz (0xB0)
#define MA780_REG_DIR       9         // Register 9: Invert direction bit (0x80 / 128)

// ============================================================================
// Default Tuning Parameters
// ============================================================================
#define DEFAULT_DEVICE_NAME         "Doomscroller"
#define DEFAULT_DEGREES_PER_STEP    30.0f     // Degrees of rotation per 1 mouse wheel notch (~12 notches/rev)
#define DEFAULT_ACCEL_FACTOR        0.02f     // Dynamic acceleration multiplier for fast flicks (0.0 = linear)
#define DEFAULT_DEADZONE_DEG        0.18f     // Deadzone threshold in degrees per sample to ignore resting noise
#define DEFAULT_INVERT_SCROLL       false     // Set true to flip scroll direction (or use "invert" via Serial/BLE)
#define DEFAULT_CURSOR_PARK         true      // Default to auto-parking cursor on right edge (ideal for phones)
#define DEFAULT_IDLE_TIMEOUT_SEC    15        // Seconds without motion before entering ultra-low power idle
#define IDLE_POLL_INTERVAL_MS       100       // How often to check for motion when idle (sub-100ms wake latency)
#define ACTIVE_LOOP_INTERVAL_MS     10        // Active sampling period: 10ms (100Hz), matched to BLE interval

// Version
#define FIRMWARE_VERSION            "v2.2-Universal"

// ============================================================================
// Multi-Profile Persistent Settings (InternalFS Flash Storage)
// ============================================================================
#define NUM_PROFILES 3

struct TuningProfile {
  char  name[16];                     // Friendly profile name (e.g. "Balanced", "Precision", "FastFlick")
  float degreesPerStep;               // Degrees of rotation per mouse wheel notch
  float accelFactor;                  // Dynamic acceleration multiplier
  float deadzoneDeg;                  // Motion deadzone in degrees
  bool  invertScroll;                 // True = invert scroll direction
  bool  cursorPark;                   // True = slide cursor to right edge on connect (Phone), False = keep in place (PC)
};

struct DeviceSettings {
  uint32_t      magic;                // Magic validation bytes (0x444F4F4D = "DOOM")
  uint8_t       version;              // Settings schema version
  uint8_t       activeProfile;        // Currently active profile index (0 to NUM_PROFILES - 1)
  char          deviceName[32];       // Bluetooth advertised name
  TuningProfile profiles[NUM_PROFILES]; // Profile storage slots
};

#define SETTINGS_MAGIC    0x444F4F4D  // "DOOM"
#define SETTINGS_VERSION  3           // Version 3 includes cursorPark
