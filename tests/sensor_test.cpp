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
 assert(envConversionPending&&(sensorMode==MODE_ENV||sensorMode==MODE_REC));
 bme68xData env=field(0,++envIndex,0,0x80);
 env.temperature=25.0f+envIndex*0.01f;
 testFields.push_back(env);
 testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(!envConversionPending);
 testBleConnected=true;
}
static void gasStep(const bme68xData& d){
 testFields.push_back(d);testMillis=nextMeasurement;lastHealth=testMillis;loop();
}
static void dueScan(){
 testMillis=nextGasScan;lastHealth=testMillis;loop();
  assert(envConversionPending&&sensorMode==MODE_ENV); // fresh T/H/P immediately before gas
  bme68xData env=field(0,201,0,0x80);env.temperature=27.01f;
  testFields.push_back(env);
  testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(sensorMode==MODE_GAS && (testGas1&BME68X_RUN_GAS_MSK));
 assert(testMillis-scanStartedAt==ACTIVE_LOOP_IDLE_MS);
 assert(!gasFetchSeen);
}
static void drainGas(){
 while(gasFrames.pending()){testMillis=nextGasTxAttempt;assert(serviceGasFrames());}
}
static void endCooling(){
 drainGas();
 testMillis=coolingStartedAt+COOLING_MS;lastHealth=testMillis;loop();
 assert((sensorMode==MODE_ENV||sensorMode==MODE_REC) && !(testGas1&BME68X_RUN_GAS_MSK));
}
int main(){
 assert(ENV_INTERVAL_MS==120000);
 assert(GAS_SCAN_INTERVAL_MS==600000);
 assert(ENV_FAILURE_RETRY_MS<ENV_INTERVAL_MS);
 assert(!loopLivenessExpired(119999,0)&&loopLivenessExpired(120000,0));
 assert(loopLivenessExpired(100,UINT32_MAX-119899));
 assert(isMeasurementValid(true,25,50,100000,70000));
 assert(!isMeasurementValid(true,25,50,100000,0));
 assert(!isMeasurementValid(true,NAN,50,100000,70000));
 assert(!isMeasurementValid(false,25,50,100000,70000));
 assert(!isMeasurementValid(true,25,50,120000,70000));
 measurement_guard::Guard guard;
 guard.acceptEnvironment(3,99,100772,1000);
 assert(guard.environmentValid(3,100,100770,2000)); // cold and saturated can be real
 assert(guard.environmentValid(31,100,100770,3000)); // transient heater warming
 assert(!guard.environmentValid(31.61f,100,73547,4000));
 assert(!guard.gasValid(31.61f,100,73547,200000,4000));
 for(int i=0;i<3;++i)assert(guard.gasValid(5,80,100770,100000+i,5000+i));
 assert(!guard.gasValid(5,80,100770,100003,5008)); // gas changes; T/H/P frozen
 guard.acceptEnvironment(5,80,100770,6000);
 assert(guard.gasValid(5,100,100770,100004,6100)); // RH=100 alone is valid
 for(int i=0;i<3;++i){
   assert(guard.environmentValid(5,80,100770,120000*(i+1)));
   guard.acceptEnvironment(5,80,100770,120000*(i+1));
 }
 assert(!guard.environmentValid(5,80,100770,480000));
 setup();
 assert(Bluefruit.Periph.minMs==60&&Bluefruit.Periph.maxMs==80);
 assert(Bluefruit.Periph.latency==1&&Bluefruit.Periph.timeoutMs==4000);
 assert(LOOP_IDLE_MS==1000&&ACTIVE_LOOP_IDLE_MS==10&&OLED_REFRESH_INTERVAL_MS==5000);
 assert(BATTERY_INTERVAL_MS==60000&&vddhMillivoltsFromRaw(3723)>=4085&&vddhMillivoltsFromRaw(3723)<=4095);
 testUsbPowerPresent=false;testVddhRaw=3723;measureBatteryVoltage();
 assert(batteryMeasurementValid&&batteryMillivolts>=4085&&batteryMillivolts<=4095&&!usbPowerPresent);
 testUsbPowerPresent=true;measureBatteryVoltage();
 assert(!batteryMeasurementValid&&batteryMillivolts==0&&usbPowerPresent);
 const uint32_t lastBatteryAt=batteryMeasuredAt;
 testUsbPowerPresent=false;measureBatteryVoltage();sendHealth();
 assert(batteryMeasuredAt>=lastBatteryAt);
 assert(transmitted.find(",BV=")!=std::string::npos&&transmitted.find(",BOK=1,USB=0,BP=")!=std::string::npos);
 assert(transmitted.find(",BLP=")!=std::string::npos);
 char oledLine[22];
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),false,true,89);
 assert(std::string(oledLine)=="P:----.- B:89%");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),true,false,89);
 assert(std::string(oledLine)=="P:----.- U:89%");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),true,false,255);
 assert(std::string(oledLine)=="P:----.- B:USB");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),false,false,0);
 assert(std::string(oledLine)=="P:----.- B:--%");
 formatUnavailableSensorOledLine(oledLine,sizeof(oledLine),false,true,0);
 assert(std::string(oledLine)=="P:----.- LOW:0%");
 assert(Wire.sda==21&&Wire.scl==20&&profileCount==0&&sensorMode==MODE_REC);
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
 // A failed ambient conversion retries quickly instead of waiting 120 seconds.
 nextEnvMeasurement=testMillis;testFetchError=true;lastHealth=testMillis;loop();
 testMillis=nextMeasurement;lastHealth=testMillis;loop();testFetchError=false;
 assert(!envConversionPending&&nextEnvMeasurement-testMillis==ENV_FAILURE_RETRY_MS-LOOP_IDLE_MS);
 envCycle(nextEnvMeasurement);
 nextGasScan=testMillis+ENV_INTERVAL_MS+30000;
 envCycle(testMillis+ENV_INTERVAL_MS,false); // BLE disconnect does not halt sensing
 assert(envSequence==3&&failures==beforeIdleFailures+1);
 const uint32_t scheduledScan=nextGasScan;
 dueScan();assert(profileCount==1&&testSharedMs==120);
 transmitted.clear();
 lastHealth=testMillis-10000; // A periodic STAT would be due during the scan.
 testMillis=nextMeasurement+ACTIVE_LOOP_IDLE_MS;loop();
 assert(gasFetchSeen&&firstGasFetchMs==ACTIVE_LOOP_IDLE_MS);
 assert(transmitted.find("STAT,")==std::string::npos);
 testMillis=nextMeasurement;loop();
 assert(maxGasPollGapMs==gas_scan::POLL_MS);
 assert((uint32_t)(scanStartedAt-scheduledScan)<ENV_CONVERSION_TIMEOUT_MS);
 assert(nextGasScan==scanStartedAt+GAS_SCAN_INTERVAL_MS);
 const uint8_t mi[10]={250,252,6,36,41,46,51,56,61,66};
 gasStep(field(5,249,0,0x80));assert(validSequence==0&&failures==beforeIdleFailures+1);
 for(uint8_t i=0;i<10;++i)gasStep(field(i,mi[i]));
 assert(validSequence==10&&scanTracker.complete==1&&sensorMode==MODE_COOL);
 assert(!gasScanDiagnostics.active&&gasScanDiagnostics.step[0].accepted==1);
 assert(gasScanDiagnostics.step[0].seen==1&&gasScanDiagnostics.step[9].accepted==1);
 assert((testGas0&BME68X_HCTRL_MSK)&&!(testGas1&BME68X_RUN_GAS_MSK));
 drainGas();
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
 // GI 0 can be fetched as a dummy; later real fields must remain ungrouped.
 nextGasScan=testMillis+1000;dueScan();const uint32_t beforeMissingZero=validSequence;
 gasStep(field(0,90,0,0x80));
 for(uint8_t i=1;i<10;++i)gasStep(field(i,i+90));
 assert(sensorMode==MODE_COOL&&validSequence==beforeMissingZero);
 assert(!gasScanDiagnostics.active&&gasScanDiagnostics.step[0].seen==1);
 assert(gasScanDiagnostics.step[0].newData==1&&gasScanDiagnostics.step[0].gasValid==0);
 assert(gasScanDiagnostics.step[0].skipped==1&&gasScanDiagnostics.step[0].firstMeasurement==90);
 assert(gasScanDiagnostics.step[1].accepted==1&&gasScanDiagnostics.step[9].accepted==1);
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
 // Millis wrap across an ambient forced conversion must preserve the 120 s schedule.
 testMillis=UINT32_MAX-20;nextGasScan=1000000;nextEnvMeasurement=testMillis;
 lastHealth=testMillis;loop();assert(envConversionPending&&nextEnvMeasurement<ENV_INTERVAL_MS);
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
 testGetDataAdvanceMs=0;finishGasScan();drainGas();
 gas_scan::Tracker tracker;assert(tracker.observe(0,254,true));
 assert(tracker.observe(2,255,true));assert(tracker.observe(2,0,true));
 tracker.observe(9,1,true);assert(tracker.complete==0&&tracker.incomplete==1);
 // A fresh index must accompany each real step. No stale gas step is sent.
 sensorMode=MODE_ENV;sensorReady=true;recoveryPending=false;
 measurementGuard.acceptEnvironment(25,50,100000,testMillis);
 assert(startGasScan());scanTracker.resetForRecovery();
 const uint32_t beforeIndexSeq=validSequence,beforeIndexErrors=duplicateGasIndexes;
 gasStep(field(0,10));gasStep(field(1,10));
 assert(validSequence==beforeIndexSeq+1&&duplicateGasIndexes==beforeIndexErrors+1);
 // Recovery first resets the BME688, then reinitializes the I2C bus if the
 // next attempt has still not produced a valid measurement.
 finishGasScan();drainGas();
 sensorMode=MODE_ENV;recoveringGas=false;
 recoveryAttempts=1;recoveryStage=0;recoveryAttemptsSinceGood=0;
 testBeginOK=true;recoverSensor();assert(sensorReady&&sensorMode==MODE_REC&&recoveryPending&&recoveryStage==1);
 transmitted.clear();sendHealth();
 assert(transmitted.find(",OK=0,MODE=REC,EOK=0")!=std::string::npos);
 testBeginOK=false;recoverSensor();assert(!sensorReady&&recoveryStage==2);
 testBeginOK=true;recoverSensor();assert(sensorReady&&sensorMode==MODE_REC&&recoveryStage==2);
 measurementGuard.acceptEnvironment(25,50,100000,testMillis);
 nextGasScan=testMillis+ENV_INTERVAL_MS;envCycle(testMillis+10);
 assert(sensorMode==MODE_ENV&&recoveryStage==0&&recoveryAttemptsSinceGood==0);
 transmitted.clear();sendHealth();
 assert(transmitted.find(",OK=1,MODE=ENV,EOK=1")!=std::string::npos);
 const uint32_t envBeforeInvalid=envSequence;
 testMillis=nextEnvMeasurement;lastHealth=testMillis;loop();assert(envConversionPending);
 bme68xData pressureJump=field(0,42,0,0x80);pressureJump.pressure=73547;
 testFields.push_back(pressureJump);testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(!envReadOK&&envSequence==envBeforeInvalid&&envConsecutiveFailures==1);
 testMillis=nextEnvMeasurement;lastHealth=testMillis;loop();assert(envConversionPending);
 testFields.push_back(field(0,43,0,0));testMillis=nextMeasurement;lastHealth=testMillis;loop();
 assert(!envReadOK&&envSequence==envBeforeInvalid&&envConsecutiveFailures==2);
 sensorReady=true;sensorMode=MODE_ENV;recoveryPending=false;
 measurementGuard.acceptEnvironment(25,50,100000,testMillis);
 assert(startGasScan());scanTracker.resetForRecovery();
 const uint32_t gasBeforeJump=validSequence;
 for(uint8_t step=0;step<3;++step){
   bme68xData jump=field(step,step+50,70000+step);
   jump.pressure=73547;
   gasStep(jump);
 }
 assert(validSequence==gasBeforeJump&&sensorMode!=MODE_GAS);
 testBeginOK=false;recoveryStage=1;recoveryAttemptsSinceGood=5;recoveryDelay=RECOVERY_MAX_MS;
 recoverSensor();
 // sendHealth() advances the host clock by 80 ms after nextRecovery is set.
 assert(!sensorReady&&nextRecovery-testMillis==RECOVERY_MAX_MS-80);
 const uint32_t attemptsAtBudget=recoveryAttempts;
 testMillis+=10000;lastHealth=testMillis;loop();
 assert(recoveryAttempts==attemptsAtBudget); // no high-speed failed-init loop
 testMillis=nextRecovery;lastHealth=testMillis;loop();
 assert(recoveryAttempts==attemptsAtBudget+1); // bounded retries continue after 30 s
 puts("PASS nRF real source: ENV/GAS/COOL, heater-off, 120s/10m schedules, incomplete scan, recovery, liveness and wrap");
}
