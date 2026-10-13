#pragma once
#include "config.h"

extern DeviceSettings g_settings;

void settings_init();
bool settings_save();
void settings_reset_defaults();
