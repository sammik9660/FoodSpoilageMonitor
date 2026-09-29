#include <cassert>
#include "../firmware/nrf52840/bme688_node/bme688_node.ino"
int main(){
 setup();assert(Wire.sda==21&&Wire.scl==20);loop();assert(validSequence==1);assert(transmitted.find("T=25.00")!=std::string::npos);
 transmitted.clear();testReadOK=false;
 for(int i=0;i<3;++i){testMillis+=2000;loop();}
 assert(validSequence==1&&failures==3);assert(transmitted.find("T=")==std::string::npos);
 assert(recoveryAttempts>=2);unsigned attempts=recoveryAttempts;
 testBeginOK=false;testMillis+=2000;loop();
 for(int i=0;i<100;++i)loop();assert(recoveryAttempts==attempts); // no 2 s reset storm
 testMillis+=60000;loop();assert(!sensorReady);assert(validSequence==1);
 testBeginOK=true;testReadOK=true;testMillis+=60000;loop();loop();
 assert(sensorReady&&lastReadOK&&validSequence==2&&recoveries==2);
 puts("PASS real nRF source: no stale TX/sequence on failure, threshold, backoff, verified pins/settings, successful recovery");
}
