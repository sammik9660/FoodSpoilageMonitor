#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <iostream>

constexpr uint32_t MALLOC_CAP_INTERNAL=1,MALLOC_CAP_SPIRAM=2,MALLOC_CAP_8BIT=4;
struct {size_t psram=8388608;size_t getPsramSize()const{return psram;}} ESP;
std::vector<uint32_t> calls;
bool failInternal=false,failExternal=false;
void* heap_caps_calloc(size_t count,size_t size,uint32_t caps) {
  calls.push_back(caps);
  if((caps&MALLOC_CAP_SPIRAM)?failExternal:failInternal)return nullptr;
  return std::calloc(count,size);
}
void heap_caps_free(void* p){std::free(p);}
#include "../firmware/esp32s3/gateway/TlsMemory.h"

int main() {
  void* p=gatewayTlsCalloc(1,16384);assert(p&&calls==std::vector<uint32_t>({MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT}));gatewayTlsFree(p);
  calls.clear();p=gatewayTlsCalloc(1,256);assert(p&&calls==std::vector<uint32_t>({MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT}));gatewayTlsFree(p);
  calls.clear();failExternal=true;p=gatewayTlsCalloc(1,16384);assert(p&&calls.size()==2&&calls[0]&MALLOC_CAP_SPIRAM&&calls[1]&MALLOC_CAP_INTERNAL);gatewayTlsFree(p);failExternal=false;
  calls.clear();failInternal=true;p=gatewayTlsCalloc(1,256);assert(!p&&calls.size()==1&&calls[0]&MALLOC_CAP_INTERNAL);failInternal=false;
  calls.clear();ESP.psram=0;p=gatewayTlsCalloc(1,16384);assert(p&&calls.size()==1&&calls[0]&MALLOC_CAP_INTERNAL);gatewayTlsFree(p);
  calls.clear();assert(!gatewayTlsCalloc(SIZE_MAX,2)&&calls.empty());
  std::cout<<"PASS TLS allocator: large PSRAM, small internal-only, large fallback and overflow\n";
}
