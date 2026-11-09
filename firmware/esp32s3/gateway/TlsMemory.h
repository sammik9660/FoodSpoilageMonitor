#pragma once
#include <stdint.h>
#include <stddef.h>

// The bundled core uses internal-only mbedTLS allocation. Prefer PSRAM for
// large TLS buffers, with an internal fallback. Keep small allocations internal.
inline void* gatewayTlsCalloc(size_t count,size_t size) {
  if(size&&count>SIZE_MAX/size)return nullptr;
  const size_t bytes=count*size;
  const uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const uint32_t external=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
  void* p=nullptr;
  if(bytes>=4096&&ESP.getPsramSize())p=heap_caps_calloc(count,size,external);
  if(!p)p=heap_caps_calloc(count,size,internal);
  return p;
}
inline void gatewayTlsFree(void* p){heap_caps_free(p);}
