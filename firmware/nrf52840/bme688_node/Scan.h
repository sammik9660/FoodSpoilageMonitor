#pragma once
#include <stdint.h>

namespace gas_scan {
constexpr uint8_t STEPS=10;
constexpr uint32_t PROFILE_ID=354;
constexpr uint32_t POLL_MS=140;
// Bosch parallel_mode example: heater temperatures and gas-wait multipliers.
static uint16_t temperature[STEPS]={320,100,100,100,200,200,200,320,320,320};
static uint16_t multiplier[STEPS]={5,2,10,30,5,5,5,5,5,5};

struct Tracker {
  uint32_t scan=0, complete=0, incomplete=0;
  uint16_t mask=0;
  uint8_t lastStep=0, lastMeasurement=0;
  bool started=false, broken=false, haveLast=false;

  void resetForRecovery() {
    if(started)++incomplete;
    started=false; broken=false; haveLast=false; mask=0;
  }
  void invalidate() { if(started)broken=true; }

  // Call only for a real gas-result field, never a parallel-mode dummy TPHG
  // conversion. MI counts ALL TPHG conversions, so it can jump by the heater
  // multiplier between valid gas results; subtraction handles 8-bit rollover.
  bool observe(uint8_t step, uint8_t measurement, bool valid) {
    if(step>=STEPS)return false;
    if(step==0) {
      if(started && (mask!=0x3ff || broken))++incomplete;
      if(scan==UINT32_MAX)return false;
      ++scan; started=true; mask=0; broken=false; haveLast=false;
    }
    if(!started)return false;
    if(haveLast && (step!=(uint8_t)(lastStep+1) ||
                    (uint8_t)(measurement-lastMeasurement)==0 ||
                    (uint8_t)(measurement-lastMeasurement)>127)) broken=true;
    if(mask & (uint16_t)(1u<<step)) broken=true;
    if(valid) mask |= (uint16_t)(1u<<step);
    else broken=true;
    lastStep=step; lastMeasurement=measurement; haveLast=true;
    if(step==STEPS-1) {
      if(mask==0x3ff && !broken)++complete;
      else ++incomplete;
      // Wait for a fresh step 0; no later field can repair this scan.
      started=false;
    }
    return true;
  }
};
}
