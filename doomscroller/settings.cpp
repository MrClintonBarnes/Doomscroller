#include "settings.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <cstring>

using namespace Adafruit_LittleFS_Namespace;

#define SETTINGS_FILENAME "/settings.dat"

DeviceSettings g_settings;

void settings_reset_defaults() {
  memset(&g_settings, 0, sizeof(DeviceSettings));
  g_settings.magic = SETTINGS_MAGIC;
  g_settings.version = SETTINGS_VERSION;
  g_settings.activeProfile = 0; // Profile 0 = "Profile 1"

  strncpy(g_settings.deviceName, DEFAULT_DEVICE_NAME, sizeof(g_settings.deviceName) - 1);
  g_settings.deviceName[sizeof(g_settings.deviceName) - 1] = '\0';

  // Profile 0: Balanced / Phone (Default)
  strncpy(g_settings.profiles[0].name, "Balanced", sizeof(g_settings.profiles[0].name) - 1);
  g_settings.profiles[0].degreesPerStep = DEFAULT_DEGREES_PER_STEP; // 30.0°
  g_settings.profiles[0].accelFactor    = DEFAULT_ACCEL_FACTOR;      // 0.020
  g_settings.profiles[0].deadzoneDeg    = DEFAULT_DEADZONE_DEG;      // 0.18°
  g_settings.profiles[0].invertScroll   = DEFAULT_INVERT_SCROLL;    // false
  g_settings.profiles[0].cursorPark     = true;                     // Slide cursor to right edge on phone

  // Profile 1: Precision / PC
  strncpy(g_settings.profiles[1].name, "Precision", sizeof(g_settings.profiles[1].name) - 1);
  g_settings.profiles[1].degreesPerStep = 45.0f;                     // Slower, finer control
  g_settings.profiles[1].accelFactor    = 0.005f;                    // Low acceleration
  g_settings.profiles[1].deadzoneDeg    = 0.18f;
  g_settings.profiles[1].invertScroll   = false;
  g_settings.profiles[1].cursorPark     = false;                    // Keep cursor in place on PC

  // Profile 2: Fast / Social Media Flick
  strncpy(g_settings.profiles[2].name, "FastFlick", sizeof(g_settings.profiles[2].name) - 1);
  g_settings.profiles[2].degreesPerStep = 20.0f;                     // Faster baseline
  g_settings.profiles[2].accelFactor    = 0.040f;                    // Strong flick multiplier
  g_settings.profiles[2].deadzoneDeg    = 0.15f;
  g_settings.profiles[2].invertScroll   = false;
  g_settings.profiles[2].cursorPark     = true;
}

void settings_init() {
  InternalFS.begin();

  File file(InternalFS);
  if (file.open(SETTINGS_FILENAME, FILE_O_READ)) {
    DeviceSettings loaded;
    uint32_t bytesRead = file.read((uint8_t*)&loaded, sizeof(loaded));
    file.close();

    if (bytesRead == sizeof(DeviceSettings) && 
        loaded.magic == SETTINGS_MAGIC && 
        loaded.version == SETTINGS_VERSION) {
      memcpy(&g_settings, &loaded, sizeof(DeviceSettings));
      if (g_settings.activeProfile >= NUM_PROFILES) {
        g_settings.activeProfile = 0;
      }
      return;
    }
  }

  // File does not exist, corrupted, or older version: write default settings
  settings_reset_defaults();
  settings_save();
}

bool settings_save() {
  File file(InternalFS);
  InternalFS.remove(SETTINGS_FILENAME);
  if (file.open(SETTINGS_FILENAME, FILE_O_WRITE)) {
    uint32_t written = file.write((uint8_t*)&g_settings, sizeof(DeviceSettings));
    file.close();
    return (written == sizeof(DeviceSettings));
  }
  return false;
}
