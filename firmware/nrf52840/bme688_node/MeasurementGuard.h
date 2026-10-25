#pragma once
#include <math.h>
#include <stdint.h>

// These checks supplement the Bosch status bits. A steady 100% RH or a warm
// heater step is not by itself an error. Pressure is compared with the last
// accepted heater-off conversion, not with a fixed sea-level pressure.
namespace measurement_guard {
constexpr uint32_t PRESSURE_REFERENCE_MS = 7200000;
constexpr float PRESSURE_JUMP_PA = 8000.0f;
constexpr uint32_t TEMPERATURE_REFERENCE_MS = 300000;
constexpr float TEMPERATURE_JUMP_C = 40.0f;
constexpr uint8_t FROZEN_GAS_FIELDS = 4;
constexpr uint8_t FROZEN_ENV_FIELDS = 5;
constexpr uint32_t FROZEN_ENV_DURATION_MS = 300000;

inline bool physical(float t, float h, float p) {
  return isfinite(t) && isfinite(h) && isfinite(p) &&
         t >= -40.0f && t <= 85.0f && h >= 0.0f && h <= 100.0f &&
         p >= 30000.0f && p <= 110000.0f;
}

struct Guard {
  bool haveEnvironment = false, haveGas = false;
  float environmentTemperature = 0, environmentHumidity = 0,
        environmentPressure = 0,
        gasTemperature = 0, gasHumidity = 0,
        gasPressure = 0, gasResistance = 0;
  uint32_t environmentAt = 0, firstRepeatedEnvironmentAt = 0;
  uint8_t repeatedEnvironmentFields = 0, repeatedGasFields = 0;

  bool pressurePlausible(float pressure, uint32_t now) const {
    return !haveEnvironment || (uint32_t)(now - environmentAt) > PRESSURE_REFERENCE_MS ||
           fabsf(pressure - environmentPressure) <= PRESSURE_JUMP_PA;
  }
  bool environmentValid(float t, float h, float p, uint32_t now) const {
    return physical(t,h,p) && pressurePlausible(p,now) &&
           (!haveEnvironment || (uint32_t)(now-environmentAt) > TEMPERATURE_REFERENCE_MS ||
            fabsf(t-environmentTemperature) <= TEMPERATURE_JUMP_C) &&
           (!haveEnvironment || t != environmentTemperature ||
            h != environmentHumidity || p != environmentPressure ||
            repeatedEnvironmentFields < FROZEN_ENV_FIELDS-1 ||
            (uint32_t)(now-firstRepeatedEnvironmentAt) < FROZEN_ENV_DURATION_MS);
  }
  void acceptEnvironment(float t, float h, float p, uint32_t now) {
    if (haveEnvironment && t == environmentTemperature &&
        h == environmentHumidity && p == environmentPressure) {
      if (repeatedEnvironmentFields < UINT8_MAX) ++repeatedEnvironmentFields;
    } else {
      repeatedEnvironmentFields = 1;
      firstRepeatedEnvironmentAt = now;
    }
    haveEnvironment = true; environmentTemperature = t;
    environmentHumidity = h; environmentPressure = p; environmentAt = now;
    haveGas = false; repeatedGasFields = 0;
  }
  bool gasValid(float t, float h, float p, float gas, uint32_t now) {
    if (!physical(t,h,p) || !isfinite(gas) || gas <= 0 ||
        !pressurePlausible(p,now)) return false;
    if (haveGas && t == gasTemperature && h == gasHumidity &&
        p == gasPressure && gas != gasResistance) {
      if (repeatedGasFields < UINT8_MAX) ++repeatedGasFields;
    } else repeatedGasFields = 1;
    haveGas = true; gasTemperature = t; gasHumidity = h;
    gasPressure = p; gasResistance = gas;
    return repeatedGasFields < FROZEN_GAS_FIELDS;
  }
};
}
