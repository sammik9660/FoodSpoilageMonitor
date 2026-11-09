#include <cassert>
#include <string>
#include "../firmware/nrf52840/bme688_node/bme688_node.ino"
int main(){
 setup();sensorReady=true;recoveryPending=false;sensorMode=MODE_ENV;gasHeaterOff=true;
 gasFrames.count=1;gasFrames.sent=0;strcpy(gasFrames.frame[0],"preserved-original-frame");
 testNotifyEnabled=false;testBleConnected=true;
 const std::string original=gasFrames.frame[0];
 // Five-hour simulated outage. Loop remains alive but Notify does not progress.
 for(unsigned second=0;second<18000;++second){
   testMillis=second*1000;serviceGasFrames();
   assert(gasFrames.pending()&&original==gasFrames.frame[0]);
   assert(!startGasScan());
   if(!testBleConnected)testBleConnected=true; // central reconnects, still no CCCD
 }
 assert(testDisconnects>0&&testDisconnects<100);
 testNotifyEnabled=true;testBleWriteFails=false;testMillis+=300000;
 assert(serviceGasFrames());assert(!gasFrames.pending());
 assert(transmitted.find(original)!=std::string::npos);
 assert(gasFrames.reset());gasFrames.count=1;strcpy(gasFrames.frame[0],original.c_str());
 testBleWriteFails=true;const auto disconnectsBefore=testDisconnects;
 for(unsigned second=0;second<240;++second){testMillis+=1000;serviceGasFrames();if(!testBleConnected)testBleConnected=true;}
 assert(testDisconnects>disconnectsBefore&&gasFrames.pending()&&original==gasFrames.frame[0]);
 testBleWriteFails=false;testMillis+=1000;assert(serviceGasFrames());
 progress_guard::Pending wrap;
 assert(!wrap.poll(UINT32_MAX-500,true,0,true));
 assert(!wrap.poll(100,true,0,true));assert(wrap.poll(120000,true,0,true));
 assert(!wrap.poll(120001,true,1,true)); // real TX progress resets backoff
 sensorReady=true;recoveryPending=false;sensorMode=MODE_ENV;
 lastEnvironmentProgressAt=millis();lastProgressRecoveryAt=millis();
 nextEnvMeasurement=millis()+1000000;const auto before=recoveryAttempts;
 testMillis+=ENV_INTERVAL_MS+60001;loop();assert(recoveryAttempts>before);
 puts("PASS five-hour Notify stall recovers without clearing the original frame");
}
