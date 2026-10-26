#include <cassert>
#include <cstdio>
#ifndef SENSOR_SOURCE
#define SENSOR_SOURCE "../firmware/nrf52840/bme688_node/bme688_node.ino"
#endif
#include SENSOR_SOURCE
#include "../firmware/esp32s3/gateway/Integrity.h"

int main(){
 setup();
 sensorReady=true;recoveryPending=false;sensorMode=MODE_ENV;
 measurementGuard.acceptEnvironment(25,50,100000,millis());
 testTimedParallel=true;testBleWriteDelayMs=100;
 assert(startGasScan());transmitted.clear();delay(10);
 const uint32_t start=millis();
 while(sensorMode==MODE_GAS && millis()-start<70000)loop();
 fprintf(stderr,"model frames=%lu complete=%lu overwritten=%lu first_fetch=%lu max_gap=%lu ms\n",
  (unsigned long)validSequence,(unsigned long)scanTracker.complete,
  (unsigned long)testOverwritten,(unsigned long)firstGasFetchMs,(unsigned long)maxGasPollGapMs);
#ifdef TEST_UNBUFFERED
 unsigned firstScanFrames=0;
 for(size_t pos=0;(pos=transmitted.find(",SCAN=1,",pos))!=std::string::npos;++pos)++firstScanFrames;
 assert(firstScanFrames>0&&firstScanFrames<10&&scanTracker.complete==0&&testOverwritten>0&&maxGasPollGapMs>420);
 fprintf(stderr,"baseline first scan valid frames=%u/10\n",firstScanFrames);
 puts("PASS baseline reproduces missing GI with synchronous BLE and three sensor fields");
#else
 assert(validSequence==10&&scanTracker.complete==1&&testOverwritten==0);
 assert(firstGasFetchMs==10&&maxGasPollGapMs<420);
 assert(transmitted.find("V=3,")==std::string::npos);
 const uint8_t mi[10]={4,6,16,46,51,56,61,66,71,76};
 std::string expected;
 for(unsigned i=0;i<10;++i){
  char key[80];snprintf(key,sizeof(key),"SEQ=%u,SCAN=1,STEP=%u,GI=%u,MI=%u,",i+1,i,i,mi[i]);
  assert(std::string(gasFrames.frame[i]).find(key)!=std::string::npos);
  integrity::Frame parsed;assert(integrity::parseFrame(gasFrames.frame[i],parsed));
  assert(parsed.seq==i+1&&parsed.step==i&&parsed.measIndex==mi[i]&&parsed.status==0xb0);
  expected+="\n"+std::string(gasFrames.frame[i])+"\n";
 }
 transmitted.clear();
 // Failed writes retain the exact queued frame and sequence; no new scan overwrites it.
 const std::string first=gasFrames.frame[0];testBleWriteFails=true;
 assert(!serviceGasFrames()&&gasFrames.sent==0&&first==gasFrames.frame[0]);
 assert(!startGasScan());testBleWriteFails=false;
 while(gasFrames.pending()){
  assert(gasHeaterOff&&testMode==BME68X_SLEEP_MODE);
  testMillis=nextGasTxAttempt;assert(serviceGasFrames());
 }
 assert(transmitted==expected); // byte-exact original MS/SEQ/GI order, no replay duplicates
 assert(gasFrames.reset());
 testTimedParallel=false;testBleWriteDelayMs=0;
 auto beginScan=[](){
  sensorReady=true;recoveryPending=false;sensorMode=MODE_ENV;
  measurementGuard.acceptEnvironment(25,50,100000,millis());
  assert(startGasScan());
 };
 auto feed=[](uint8_t gi,uint8_t mi,uint8_t status){
  bme68xData d;d.gas_index=gi;d.meas_index=mi;d.status=status;d.temperature=25+mi*0.01f;
  testFields.push_back(d);testMillis=nextMeasurement;lastHealth=testMillis;loop();
 };
 auto drain=[](){while(gasFrames.pending()){testMillis=nextGasTxAttempt;assert(serviceGasFrames());}};
 beginScan();feed(0,4,0xb0);feed(9,76,0xb0);
 assert(gasFrames.count==2&&scanTracker.incomplete>0);drain();
 beginScan();feed(0,4,0xb0);const std::string timeoutFrame=gasFrames.frame[0];
 testMillis=scanStartedAt+GAS_SCAN_TIMEOUT_MS;loop();
 assert(sensorMode!=MODE_GAS&&gasFrames.pending()&&timeoutFrame==gasFrames.frame[0]);drain();
 beginScan();feed(0,4,0xb0);const std::string recoveryFrame=gasFrames.frame[0];
 testFetchError=true;for(int i=0;i<3;++i){testMillis=nextMeasurement;loop();}testFetchError=false;
 assert(sensorMode!=MODE_GAS&&gasFrames.pending()&&recoveryFrame==gasFrames.frame[0]);drain();
 beginScan();
 for(uint8_t i=0;i<10;++i)feed(0,i,0xb0); // repeated cycles without GI 9
 assert(sensorMode!=MODE_GAS&&gasFrames.count==10&&gasScanDiagnostics.bufferFull);
 assert(scanTracker.complete==1);drain();
 assert(gasFrames.reset());
 // Fixed capacity and reset refusal protect a pending partial scan.
 gasFrames.count=gas_scan::FrameBuffer::CAPACITY;gasFrames.sent=1;
 strcpy(gasFrames.frame[1],"preserved");assert(!gasFrames.reset());
 assert(std::string(gasFrames.frame[1])=="preserved");
 puts("PASS buffered scan, byte-exact FIFO, failed-send retention and overwrite protection");
#endif
}

