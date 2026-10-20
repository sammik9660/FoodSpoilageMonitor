#include <cassert>
#include <cmath>
#include "../firmware/nrf52840/bme688_node/bme688_node.ino"

static bme68xData field(uint8_t step,uint8_t mi,float gas=70000,uint8_t status=0xb0){
 bme68xData d;d.status=status;d.gas_index=step;d.meas_index=mi;d.gas_resistance=gas;return d;
}
static void pollAt(uint32_t at){testMillis=at;nextMeasurement=at;lastHealth=at;loop();}
static void envCycle(uint32_t at,bool connected=true){
 static uint8_t envIndex=0;
 testBleConnected=connected;testMillis=at;nextEnvMeasurement=at;lastHealth=at;loop();
 assert(envConversionPending&&sensorMode==MODE_ENV);
 testFields.push_back(field(0,++envIndex,0,0x80));
 testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(!envConversionPending);
 testBleConnected=true;
}
static void gasStep(const bme68xData& d){
 testFields.push_back(d);testMillis=nextMeasurement;lastHealth=testMillis;loop();
}
static void dueScan(){
 testMillis=nextGasScan;lastHealth=testMillis;loop();
 assert(sensorMode==MODE_GAS && (testGas1&BME68X_RUN_GAS_MSK));
}
static void endCooling(){
 testMillis=coolingStartedAt+COOLING_MS;lastHealth=testMillis;loop();
 assert(sensorMode==MODE_ENV && !(testGas1&BME68X_RUN_GAS_MSK));
}
int main(){
 assert(GAS_SCAN_INTERVAL_MS==600000);
 assert(isMeasurementValid(true,25,50,100000,70000));
 assert(!isMeasurementValid(true,25,50,100000,0));
 assert(!isMeasurementValid(true,NAN,50,100000,70000));
 assert(!isMeasurementValid(false,25,50,100000,70000));
 setup();
 assert(BATTERY_INTERVAL_MS==60000&&vddhMillivoltsFromRaw(3723)>=4085&&vddhMillivoltsFromRaw(3723)<=4095);
 testUsbPowerPresent=false;testVddhRaw=3723;measureBatteryVoltage();
 assert(batteryMeasurementValid&&batteryMillivolts>=4085&&batteryMillivolts<=4095&&!usbPowerPresent);
 testUsbPowerPresent=true;measureBatteryVoltage();
 assert(!batteryMeasurementValid&&batteryMillivolts==0&&usbPowerPresent);
 testUsbPowerPresent=false;measureBatteryVoltage();sendHealth();
 assert(transmitted.find(",BV=")!=std::string::npos&&transmitted.find(",BOK=1,USB=0,BP=")!=std::string::npos);
 char oledLine[22];
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),false,true,89);
 assert(std::string(oledLine)=="P:----.- B:89%");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),true,false,0);
 assert(std::string(oledLine)=="P:----.- B:USB");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),false,false,0);
 assert(std::string(oledLine)=="P:----.- B:--%");
 assert(Wire.sda==21&&Wire.scl==20&&profileCount==0&&sensorMode==MODE_ENV);
 assert((testGas0&BME68X_HCTRL_MSK)&&!(testGas1&BME68X_RUN_GAS_MSK));
 const uint32_t bootAt=testMillis;
 envCycle(bootAt+10);
 assert(nextEnvMeasurement==bootAt+10+ENV_INTERVAL_MS);
 assert(envSequence==1&&validSequence==0&&recoveries==1);
 assert(transmitted.find("ENV,BID=")!=std::string::npos);
 assert(transmitted.find("ENV,BID=")==std::string::npos || transmitted.find(",P=1000.00")!=std::string::npos);
 // No gas watchdog while the heater is deliberately off, even after 20 s.
 const uint32_t beforeIdleFailures=failures;
 nextGasScan=testMillis+30000; // Keep this idle-only check before the first scheduled scan.
 nextEnvMeasurement=testMillis+30000;testMillis+=20000;lastHealth=testMillis;loop();
 assert(sensorMode==MODE_ENV&&failures==beforeIdleFailures);
 envCycle(testMillis+2000,false); // BLE disconnect does not halt sensing
 assert(envSequence==2&&failures==beforeIdleFailures);
 const uint32_t scheduledScan=nextGasScan;
 dueScan();assert(profileCount==1&&testSharedMs==120);
 assert(nextGasScan==scheduledScan+GAS_SCAN_INTERVAL_MS);
 const uint8_t mi[10]={250,252,6,36,41,46,51,56,61,66};
 gasStep(field(5,249,0,0x80));assert(validSequence==0&&failures==beforeIdleFailures);
 for(uint8_t i=0;i<10;++i)gasStep(field(i,mi[i]));
 assert(validSequence==10&&scanTracker.complete==1&&sensorMode==MODE_COOL);
 assert((testGas0&BME68X_HCTRL_MSK)&&!(testGas1&BME68X_RUN_GAS_MSK));
 assert(transmitted.find("V=3,BID=")!=std::string::npos);
 assert(transmitted.find("SCAN=1,STEP=9,GI=9,MI=66")!=std::string::npos);
 // Cooling neither creates ambient samples nor runs the gas watchdog.
 const uint32_t beforeCoolingFailures=failures,beforeEnv=envSequence;
 testMillis=coolingStartedAt+COOLING_MS-1;lastHealth=testMillis;loop();
 assert(sensorMode==MODE_COOL&&failures==beforeCoolingFailures&&envSequence==beforeEnv);
 endCooling();envCycle(testMillis+10);assert(envSequence==beforeEnv+1);
 // A missing heater step remains an incomplete fingerprint, with raw steps intact.
 const uint32_t incompleteBefore=scanTracker.incomplete,seqBefore=validSequence;
 nextGasScan=testMillis+1000;dueScan();
 for(uint8_t i=0;i<10;++i)if(i!=3)gasStep(field(i,i+80));
 assert(sensorMode==MODE_COOL&&scanTracker.incomplete==incompleteBefore+1);
 assert(scanTracker.complete==1&&validSequence==seqBefore+9);
 endCooling();envCycle(testMillis+10);
 // Real G=0 fields still enter the existing threshold/recovery path.
 nextGasScan=testMillis+1000;dueScan();const uint32_t gasSeqBefore=validSequence;
 for(uint8_t i=0;i<3;++i)gasStep(field(i,i+100,0));
 assert(validSequence==gasSeqBefore&&sensorMode!=MODE_GAS);
 assert(!(testGas1&BME68X_RUN_GAS_MSK));
 if(!sensorReady){testMillis=nextRecovery;lastHealth=testMillis;loop();}
 assert(sensorReady&&sensorMode==MODE_COOL);
 endCooling();envCycle(testMillis+10);assert(recoveries>=2);
 // A bounded scan with no real gas result resets the sensor instead of idling
 // with the heater on until the next 10-minute slot.
 nextGasScan=testMillis+1000;dueScan();const uint32_t attemptsBeforeTimeout=recoveryAttempts;
 testMillis=scanStartedAt+GAS_SCAN_TIMEOUT_MS;lastHealth=testMillis;loop();
 assert(recoveryAttempts>attemptsBeforeTimeout&&sensorMode==MODE_COOL);
 assert(!(testGas1&BME68X_RUN_GAS_MSK));
 endCooling();envCycle(testMillis+10);
 // Millis wrap across an ambient forced conversion must preserve the 2 s schedule.
 testMillis=UINT32_MAX-20;nextGasScan=1000000;nextEnvMeasurement=testMillis;
 lastHealth=testMillis;loop();assert(envConversionPending&&nextEnvMeasurement<2000);
 testFields.push_back(field(0,200,0,0x80));testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(envReadOK&&envSequence>=4);
 const uint32_t beforeDuplicateSeq=envSequence,beforeDuplicateFailures=failures;
 testMillis=nextEnvMeasurement;lastHealth=testMillis;loop();assert(envConversionPending);
 testFields.push_back(field(0,200,0,0x80));testMillis=nextMeasurement;lastHealth=testMillis;loop();
 // Forced conversions may reuse the 8-bit measurement index; current firmware
 // accepts the fresh NEW_DATA result rather than treating that index as loss.
 assert(envSequence==beforeDuplicateSeq+1&&failures==beforeDuplicateFailures&&envReadOK);
 // Existing gas watchdog boundary and uint32 rollover remain covered.
 sensorMode=MODE_GAS;scanStartedAt=UINT32_MAX-10000;
 lastSensorField=scanStartedAt;consecutiveFailures=0;
 const uint32_t watchdogFailures=failures;
 pollAt(9998);assert(failures==watchdogFailures);
 pollAt(9999);assert(failures==watchdogFailures+1&&lastSensorField==9999);
 // Real field produced after loop-entry time must not underflow the watchdog.
 scanStartedAt=testMillis;consecutiveFailures=0;testGetDataAdvanceMs=1;
 const uint32_t beforeFieldFailures=failures;
 gasStep(field(0,140));assert(failures==beforeFieldFailures);
 testGetDataAdvanceMs=0;
 gas_scan::Tracker tracker;assert(tracker.observe(0,254,true));
 assert(tracker.observe(2,255,true));assert(tracker.observe(2,0,true));
 tracker.observe(9,1,true);assert(tracker.complete==0&&tracker.incomplete==1);
 puts("PASS nRF real source: ENV/GAS/COOL, heater-off, 2s/10m schedules, incomplete scan, recovery, watchdog and wrap");
}
