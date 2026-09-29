#pragma once
#include "Wire.h"
#define BME680_OS_8X 8
#define BME680_OS_2X 2
#define BME680_OS_4X 4
#define BME680_FILTER_SIZE_3 3
inline bool testReadOK=true,testBeginOK=true;
inline unsigned beginCount=0;
class Adafruit_BME680{
 public:float temperature=25,humidity=50,pressure=100000;uint32_t gas_resistance=70000;
 explicit Adafruit_BME680(TwoWire*){}
 bool begin(uint8_t address,bool initSettings){++beginCount;return testBeginOK&&address==0x76&&!initSettings;}
 bool setTemperatureOversampling(int n){return n==8;}bool setHumidityOversampling(int n){return n==2;}
 bool setPressureOversampling(int n){return n==4;}bool setIIRFilterSize(int n){return n==3;}
 bool setGasHeater(int t,int ms){return t==320&&ms==150;}bool performReading(){return testReadOK;}
};
