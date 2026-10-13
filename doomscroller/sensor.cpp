#include "sensor.h"

ScrollSensor g_sensor;

ScrollSensor::ScrollSensor()
  : _velFilter(6),
    _lastRawAngle(0),
    _lastSampleTime(0),
    _powered(false),
    _firstSample(true)
{
}

void ScrollSensor::begin() {
  pinMode(PIN_MPS_ENABLE, OUTPUT);
  enablePower(true);
  delay(5); // Allow sensor startup

  _magAlpha.begin(SPI_SCLK_FREQUENCY, MA_SPI_MODE_3, PIN_SPI_CS);

  // Configure sensor hardware registers
  _magAlpha.writeRegister(MA780_REG_BCT, 0);      // BCT side-shaft config
  _magAlpha.writeRegister(MA780_REG_Y_TRIM, 2);  // Enable Y trimming
  _magAlpha.writeRegister(MA780_REG_FILTER, 0xB0); // 78Hz internal digital filter
  _magAlpha.writeRegister(MA780_REG_DIR, 128);   // Reverse direction

  resetState();
  Serial.println("[Sensor] MagAlpha MA780 initialized (SPI 5MHz, Mode 3, 78Hz HW filter).");
}

void ScrollSensor::enablePower(bool on) {
  if (on != _powered) {
    digitalWrite(PIN_MPS_ENABLE, on ? HIGH : LOW);
    _powered = on;
    if (on) {
      delayMicroseconds(250); // MA780 startup time is ~220us
    }
  }
}

void ScrollSensor::resetState() {
  if (!_powered) {
    enablePower(true);
  }
  _lastRawAngle = _magAlpha.readAngleRaw();
  _lastSampleTime = millis();
  _firstSample = true;
}

uint16_t ScrollSensor::getRawAngle() {
  if (!_powered) enablePower(true);
  return _magAlpha.readAngleRaw();
}

bool ScrollSensor::update(float& outDeltaDeg, float& outVelocityDegPerSec) {
  if (!_powered) enablePower(true);

  uint32_t now = millis();
  uint32_t dtMs = now - _lastSampleTime;
  if (dtMs < 1) dtMs = 1;

  uint16_t currentRaw = _magAlpha.readAngleRaw();

  if (_firstSample) {
    _lastRawAngle = currentRaw;
    _lastSampleTime = now;
    _firstSample = false;
    outDeltaDeg = 0.0f;
    outVelocityDegPerSec = 0.0f;
    return false;
  }

  // Integer two's-complement subtraction automatically handles circular 16-bit wraparound (0 <-> 65535)
  int16_t diffRaw = (int16_t)(currentRaw - _lastRawAngle);
  _lastRawAngle = currentRaw;
  _lastSampleTime = now;

  // 65536 counts = 360 degrees
  float deltaDeg = (float)diffRaw * (360.0f / 65536.0f);

  // Calculate velocity in degrees per second
  float instVelocity = (deltaDeg / (float)dtMs) * 1000.0f;

  _velFilter.push(instVelocity);
  float filteredVel = _velFilter.get();

  outDeltaDeg = deltaDeg;
  outVelocityDegPerSec = filteredVel;
  return true;
}

bool ScrollSensor::checkIdleWakeup(float thresholdDeg) {
  enablePower(true);
  uint16_t currentRaw = _magAlpha.readAngleRaw();
  int16_t diffRaw = (int16_t)(currentRaw - _lastRawAngle);
  float diffDeg = fabsf((float)diffRaw * (360.0f / 65536.0f));

  if (diffDeg > thresholdDeg) {
    // Movement detected! Keep sensor powered and reset timing
    _lastRawAngle = currentRaw;
    _lastSampleTime = millis();
    _firstSample = true;
    return true;
  }

  // No movement; power down sensor to preserve battery
  enablePower(false);
  return false;
}
