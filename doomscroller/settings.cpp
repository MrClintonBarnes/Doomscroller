#include "settings.h"
#include <cstring>

DeviceSettings g_settings;

void settings_reset_defaults() {
  g_settings.magic = SETTINGS_MAGIC;
  strncpy(g_settings.deviceName, DEFAULT_DEVICE_NAME, sizeof(g_settings.deviceName) - 1);
  g_settings.deviceName[sizeof(g_settings.deviceName) - 1] = '\0';
  g_settings.degreesPerStep = DEFAULT_DEGREES_PER_STEP;
  g_settings.accelFactor = DEFAULT_ACCEL_FACTOR;
  g_settings.deadzoneDeg = DEFAULT_DEADZONE_DEG;
  g_settings.invertScroll = DEFAULT_INVERT_SCROLL;
  g_settings.idleTimeoutSec = DEFAULT_IDLE_TIMEOUT_SEC;
}

void settings_init() {
  settings_reset_defaults();
}

bool settings_save() {
  // In-memory configuration active for session
  return true;
}
