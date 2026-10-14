#pragma once
#include "config.h"
#include <MagAlpha.h>
#include <MovingAveragePlus.h>

class ScrollSensor {
public:
  ScrollSensor();
  void begin();
  void enablePower(bool on);
  bool isPowered() const { return _powered; }

  // Called in active loop (every ~10ms). Returns true if valid new sample was processed.
  // outDeltaDeg: signed degrees turned in this sample (positive = forward, negative = backward)
  // outVelocityDegPerSec: filtered velocity in deg/sec
  bool update(float& outDeltaDeg, float& outVelocityDegPerSec);

  // Quick check for wake-up while in low-power idle.
  // Returns true if movement exceeded threshold.
  bool checkIdleWakeup(float thresholdDeg = 1.0f);

  // Resets tracking state (e.g. on wakeup or connect)
  void resetState();

  // Re-applies MagAlpha hardware registers (BCT, Y_TRIM, FILTER, DIR)
  void configureRegisters();

  // Get current raw angle
  uint16_t getRawAngle();

private:
  MagAlpha _magAlpha;
  MovingAveragePlus<float> _velFilter;
  uint16_t _lastRawAngle;
  uint32_t _lastSampleTime;
  bool _powered;
  bool _firstSample;
};

extern ScrollSensor g_sensor;
