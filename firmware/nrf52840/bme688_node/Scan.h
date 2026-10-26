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

// One normal HP-354 scan, with the original wire text frozen at acquisition.
// Never reset or overwrite a frame that has not completed sendLine().
struct FrameBuffer {
  static constexpr uint8_t CAPACITY=STEPS;
  static constexpr uint16_t FRAME_BYTES=180;
  char frame[CAPACITY][FRAME_BYTES]={};
  uint8_t count=0, sent=0;
  bool pending() const { return sent<count; }
  bool reset() {
    if(pending())return false;
    count=sent=0;return true;
  }
};

// Per-scan counters only; never changes which fields Tracker accepts.
struct Diagnostics {
  struct Step {
    uint16_t seen=0, newData=0, gasValid=0, heatStable=0;
    uint16_t skipped=0, rejected=0, accepted=0;
    uint8_t firstStatus=0, firstMeasurement=0, lastStatus=0, lastMeasurement=0;
  } step[STEPS];
  uint16_t outOfRange=0, emptyFetches=0, fetchErrors=0;
  uint16_t waitingForZero=0, noRealGas=0, frameErrors=0;
  bool bufferFull=false, sequenceExhausted=false;
  uint32_t fetchMaxUs=0, oledMaxUs=0, adcMaxUs=0, storageMaxUs=0, processMaxUs=0;
  int8_t profileStatus=0, modeStatus=0;
  bool active=false;

  void reset() {
    for(uint8_t i=0;i<STEPS;++i)step[i]=Step{};
    outOfRange=emptyFetches=fetchErrors=0;
    waitingForZero=noRealGas=frameErrors=0;
    bufferFull=sequenceExhausted=false;
    fetchMaxUs=oledMaxUs=adcMaxUs=storageMaxUs=processMaxUs=0;
    profileStatus=modeStatus=0;
    active=true;
  }
  void observe(uint8_t gi,uint8_t mi,uint8_t status) {
    if(gi>=STEPS){++outOfRange;return;}
    Step& s=step[gi];
    if(!s.seen){s.firstStatus=status;s.firstMeasurement=mi;}
    ++s.seen;s.lastStatus=status;s.lastMeasurement=mi;
    if(status&0x80)++s.newData;
    if(status&0x20)++s.gasValid;
    if(status&0x10)++s.heatStable;
  }
};
}
