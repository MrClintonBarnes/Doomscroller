#include "settings.h"
#include <InternalFileSystem.h>
#include <cstring>

DeviceSettings g_settings;
static const char* SETTINGS_FILE = "/settings.bin";

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

  if (!InternalFS.begin()) {
    Serial.println("[Settings] Warning: InternalFS.begin() failed, using defaults.");
    return;
  }

  if (InternalFS.exists(SETTINGS_FILE)) {
    Adafruit_LittleFS_Namespace::File file = InternalFS.open(SETTINGS_FILE, Adafruit_LittleFS_Namespace::FILE_O_READ);
    if (file) {
      DeviceSettings loaded;
      size_t readBytes = file.read((uint8_t*)&loaded, sizeof(DeviceSettings));
      file.close();

      if (readBytes == sizeof(DeviceSettings) && loaded.magic == SETTINGS_MAGIC) {
        g_settings = loaded;
        Serial.print("[Settings] Loaded preferences for: ");
        Serial.println(g_settings.deviceName);
        return;
      }
    }
  }

  Serial.println("[Settings] No valid saved settings found. Writing defaults.");
  settings_save();
}

bool settings_save() {
  if (InternalFS.exists(SETTINGS_FILE)) {
    InternalFS.remove(SETTINGS_FILE);
  }

  Adafruit_LittleFS_Namespace::File file = InternalFS.open(SETTINGS_FILE, Adafruit_LittleFS_Namespace::FILE_O_WRITE);
  if (!file) {
    Serial.println("[Settings] Failed to open settings file for writing!");
    return false;
  }

  g_settings.magic = SETTINGS_MAGIC;
  size_t written = file.write((uint8_t*)&g_settings, sizeof(DeviceSettings));
  file.close();

  if (written == sizeof(DeviceSettings)) {
    Serial.println("[Settings] Settings saved successfully.");
    return true;
  } else {
    Serial.println("[Settings] Error writing settings to flash.");
    return false;
  }
}
