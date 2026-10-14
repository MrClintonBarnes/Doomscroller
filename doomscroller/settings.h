#pragma once
#include "config.h"

extern DeviceSettings g_settings;

void settings_init();
bool settings_save();
void settings_reset_defaults();

inline TuningProfile& getActiveProfile() {
  if (g_settings.activeProfile >= NUM_PROFILES) {
    g_settings.activeProfile = 0;
  }
  return g_settings.profiles[g_settings.activeProfile];
}
