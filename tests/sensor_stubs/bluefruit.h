#pragma once
#include <algorithm>
#include <stdint.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
using std::min;
inline uint32_t testMillis=0;
inline uint32_t millis(){return testMillis;}
inline uint32_t micros(){return testMillis*1000;}
inline void delay(uint32_t n){testMillis+=n;}
struct TestSerial{void begin(int){} template<class T>void print(T){} template<class T>void println(T){}};
inline TestSerial Serial;
struct FICR{uint32_t DEVICEID[2]={1,2};};inline FICR ficr;inline FICR* NRF_FICR=&ficr;
#define NRF_SUCCESS 0
inline int sd_rand_application_vector_get(uint8_t* p,size_t n){memset(p,42,n);return 0;}
inline std::string transmitted;
struct BLEUart{void begin(){}bool notifyEnabled(){return true;}size_t write(const uint8_t* p,size_t n){transmitted.append((char*)p,n);return n;}};
struct AdvertisingStub{void addService(BLEUart&){}void addName(){}void restartOnDisconnect(bool){}void setInterval(int,int){}void setFastTimeout(int){}void start(int){}};
struct BluefruitStub{AdvertisingStub Advertising;void begin(){}void setTxPower(int){}void setName(const char*){}void autoConnLed(bool){}bool connected(){return true;}};
inline BluefruitStub Bluefruit;
