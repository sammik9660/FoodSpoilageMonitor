#pragma once
#include <stdint.h>
namespace progress_guard {
// Unsigned elapsed subtraction remains valid across the millis() rollover.
struct Pending {
  bool watching=false;
  uint8_t sent=0;
  uint32_t progressedAt=0,lastAttempt=0,attempts=0;
  bool poll(uint32_t now,bool pending,uint8_t completed,bool connected) {
    if(!pending){watching=false;attempts=0;return false;}
    if(!watching||completed!=sent){watching=true;sent=completed;
      progressedAt=lastAttempt=now;attempts=0;return false;}
    const uint32_t backoff=attempts==0?120000:attempts==1?60000:attempts==2?120000:300000;
    if(!connected||uint32_t(now-progressedAt)<120000||uint32_t(now-lastAttempt)<backoff)return false;
    lastAttempt=now;if(attempts<3)++attempts;return true;
  }
};
}
