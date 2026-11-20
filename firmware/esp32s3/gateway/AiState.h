#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "AiChange.h"
#include "Integrity.h"

namespace ai_state {
struct Blob { uint32_t magic=0;char storage[48]={},sensor[17]={};
  int64_t started=0,firstBaseline=0,readyBaseline=0;
  ai_change::State model={};uint32_t checksum=0; };
constexpr uint32_t MAGIC=0x41495331;
inline void seal(Blob& b){b.magic=MAGIC;b.checksum=integrity::crc(&b,offsetof(Blob,checksum));}
inline bool validFor(const Blob& b,const char* storage,const char* sensor,int64_t started){
  return b.magic==MAGIC&&b.checksum==integrity::crc(&b,offsetof(Blob,checksum))&&
    b.storage[sizeof(b.storage)-1]==0&&b.sensor[sizeof(b.sensor)-1]==0&&
    std::strcmp(b.storage,storage)==0&&std::strcmp(b.sensor,sensor)==0&&
    b.started==started;
}
} // namespace ai_state
