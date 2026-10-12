#pragma once
#include <stdint.h>
#include <vector>
#include "Wire.h"
#define BME68X_ERROR -1
#define BME68X_OS_8X 8
#define BME68X_OS_4X 4
#define BME68X_OS_2X 2
#define BME68X_FILTER_SIZE_3 3
#define BME68X_PARALLEL_MODE 2
#define BME68X_FORCED_MODE 1
#define BME68X_SLEEP_MODE 0
#define BME68X_REG_CTRL_GAS_0 0x70
#define BME68X_REG_CTRL_GAS_1 0x71
#define BME68X_HCTRL_MSK 0x08
#define BME68X_RUN_GAS_MSK 0x30
#define BME68X_NEW_DATA_MSK 0x80
#define BME68X_GASM_VALID_MSK 0x20
#define BME68X_HEAT_STAB_MSK 0x10
struct bme68xData {
 uint8_t status=0,gas_index=0,meas_index=0;
 float temperature=25,pressure=100000,humidity=50,gas_resistance=70000;
};
inline bool testBeginOK=true,testFetchError=false;
inline uint32_t testGetDataAdvanceMs=0;
inline unsigned beginCount=0,profileCount=0;
inline uint16_t testSharedMs=0;
inline uint8_t testGas0=0,testGas1=0,testMode=BME68X_SLEEP_MODE;
inline std::vector<bme68xData> testFields;
class Bme68x {
 int status=0; bme68xData current[3]{}; uint8_t n=0,at=0;
 public:
 void begin(uint8_t addr,TwoWire&){++beginCount;status=(testBeginOK&&addr==0x76)?0:-1;testGas0=0;testGas1=0;testMode=BME68X_SLEEP_MODE;}
 int checkStatus(){return status;}
 void setTPH(uint8_t t,uint8_t p,uint8_t h){status=(t==8&&p==4&&h==2)?0:-1;}
 void setFilter(uint8_t n){status=n==3?0:-1;}
 uint32_t getMeasDur(uint8_t mode){status=(mode==BME68X_PARALLEL_MODE||mode==BME68X_FORCED_MODE)?0:-1;return 20000;}
 void setHeaterProf(uint16_t* t,uint16_t* m,uint16_t shared,uint8_t nSteps){
  ++profileCount;testSharedMs=shared;testGas0&=~BME68X_HCTRL_MSK;testGas1|=BME68X_RUN_GAS_MSK;
  status=nSteps==10&&t[0]==320&&t[1]==100&&m[3]==30&&shared==120?0:-1;
 }
 void setOpMode(uint8_t mode){testMode=mode;status=(mode==BME68X_PARALLEL_MODE||mode==BME68X_FORCED_MODE||mode==BME68X_SLEEP_MODE)?0:-1;}
 uint8_t readReg(uint8_t addr){status=0;return addr==BME68X_REG_CTRL_GAS_0?testGas0:testGas1;}
 void writeReg(uint8_t addr,uint8_t value){status=0;if(addr==BME68X_REG_CTRL_GAS_0)testGas0=value;else testGas1=value;}
 uint8_t fetchData(){if(testFetchError){status=-1;return 0;}status=0;n=0;at=0;
  while(n<3&&!testFields.empty()){current[n++]=testFields.front();testFields.erase(testFields.begin());}return n;}
 uint8_t getData(bme68xData& d){testMillis+=testGetDataAdvanceMs;d=current[at++];return n-at;}
};
