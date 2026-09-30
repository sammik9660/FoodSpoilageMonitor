#include <cassert>
#include "../firmware/nrf52840/bme688_node/bme688_node.ino"
int main(){
 assert(isMeasurementValid(true,25,50,100000,70000));
 assert(!isMeasurementValid(true,25,50,100000,0));
 assert(!isMeasurementValid(true,25,50,100000,-1));
 assert(!isMeasurementValid(true,25,50,100000,NAN));
 assert(!isMeasurementValid(true,25,50,100000,INFINITY));
 assert(!isMeasurementValid(true,NAN,50,100000,70000));
 assert(!isMeasurementValid(true,25,INFINITY,100000,70000));
 assert(!isMeasurementValid(true,25,50,-INFINITY,70000));
 assert(!isMeasurementValid(false,25,50,100000,70000));

 setup();assert(Wire.sda==21&&Wire.scl==20);
 loop();assert(validSequence==1);assert(transmitted.find("T=25.00")!=std::string::npos);

 // A numeric performReading() with G=0 is still invalid and reaches the
 // existing three-failure soft-reset/reinitialization path without a data frame.
 transmitted.clear();bme.gas_resistance=0;
 for(int i=0;i<3;++i){testMillis+=2000;loop();}
 assert(validSequence==1&&failures==3&&!lastReadOK);
 assert(transmitted.find("T=")==std::string::npos);assert(recoveryAttempts>=2);
 bme.gas_resistance=70000;loop();
 assert(sensorReady&&lastReadOK&&validSequence==2&&recoveries==2);

 // Existing read-failure threshold and bounded recovery backoff remain intact.
 transmitted.clear();testReadOK=false;
 for(int i=0;i<3;++i){testMillis+=2000;loop();}
 assert(validSequence==2&&failures==6);assert(transmitted.find("T=")==std::string::npos);
 assert(recoveryAttempts>=3);unsigned attempts=recoveryAttempts;
 testBeginOK=false;testMillis+=2000;loop();
 for(int i=0;i<100;++i)loop();assert(recoveryAttempts==attempts); // no 2 s reset storm
 testMillis+=60000;loop();assert(!sensorReady);assert(validSequence==2);
 testBeginOK=true;testReadOK=true;testMillis+=60000;loop();loop();
 assert(sensorReady&&lastReadOK&&validSequence==3&&recoveries==3);
 puts("PASS real nRF source: G<=0/non-finite/read failure rejection, no stale TX/SEQ, threshold, backoff, recovery");
}
