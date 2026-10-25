#pragma once
#include <algorithm>
#include <stdint.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
using std::min;
#define HEX 16
inline uint32_t testMillis=0;
inline uint32_t millis(){return testMillis;}
inline uint32_t micros(){return testMillis*1000;}
inline void delay(uint32_t n){testMillis+=n;}
struct TestSerial{void begin(int){} template<class T>void print(T){} template<class T>void print(T,int){} template<class T>void println(T){} template<class T>void println(T,int){}};
inline TestSerial Serial;
struct FICR{uint32_t DEVICEID[2]={1,2};};inline FICR ficr;inline FICR* NRF_FICR=&ficr;
#define NRF_SUCCESS 0
inline int sd_rand_application_vector_get(uint8_t* p,size_t n){memset(p,42,n);return 0;}
inline std::string transmitted;
inline bool testBleConnected=true;
struct BLEUart{void begin(){}bool notifyEnabled(){return true;}size_t write(const uint8_t* p,size_t n){transmitted.append((char*)p,n);return n;}};
struct AdvertisingStub{void addService(BLEUart&){}void addName(){}void restartOnDisconnect(bool){}void setInterval(int,int){}void setFastTimeout(int){}void start(int){}};
struct PeriphStub{uint16_t minMs=0,maxMs=0,latency=0,timeoutMs=0;
 bool setConnIntervalMS(uint16_t min,uint16_t max){minMs=min;maxMs=max;return true;}
 bool setConnSlaveLatency(uint16_t value){latency=value;return true;}
 bool setConnSupervisionTimeoutMS(uint16_t value){timeoutMs=value;return true;}};
struct BluefruitStub{AdvertisingStub Advertising,ScanResponse;PeriphStub Periph;void begin(){}void setTxPower(int){}void setName(const char*){}void autoConnLed(bool){}bool connected(){return testBleConnected;}};
inline BluefruitStub Bluefruit;
