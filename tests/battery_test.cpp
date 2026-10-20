#include <cassert>
#include <cstdio>
#include "../firmware/nrf52840/bme688_node/Battery.h"

int main(){
 using namespace battery;
 assert(percent(3500)==0&&percent(4200)==100&&percent(4000)==75);
 assert(percent(4090)==88&&validVoltage(2500)&&!validVoltage(2499));

 Tracker t;uint32_t now=0;
 for(int i=0;i<5;++i){now+=60000;assert(t.observe(4090,now,LOAD_ENV));}
 const uint8_t before=t.estimatedPercent();
 now+=60000;assert(t.observe(3300,now,LOAD_GAS)); // transient heater/load sag
 assert(t.estimatedPercent()==before);assert(t.minMv==3300);
 assert(!t.observe(6000,now+60000,LOAD_ENV));

 Tracker learned;now=0;
 for(unsigned i=0;i<=700;++i){
   now+=60000;const uint32_t mv=4200-(700*i/700);
   assert(learned.observe(mv,now,LOAD_ENV));
 }
 assert(learned.state==CALIBRATED&&learned.occupiedBands()>=5);
 const uint8_t estimate=learned.estimatedPercent();
 const uint8_t baseline=percent(learned.filteredMv);
 assert(estimate<=100&&estimate+10>=baseline&&estimate<=baseline+10);

 PersistedState saved=learned.snapshot();assert(intact(saved));
 Tracker restored;assert(restored.restore(saved));
 assert(restored.state==CALIBRATED&&restored.minMv==learned.minMv&&restored.maxMv==learned.maxMv);
 assert(!restored.shouldPersist(now));
 saved.bandSeconds[0]++;Tracker corrupt;assert(!corrupt.restore(saved)&&corrupt.state==INVALID);

 Tracker narrow;now=0;
 for(unsigned i=0;i<24*60;++i){now+=60000;narrow.observe(3900-(i%3),now,LOAD_ENV);}
 assert(narrow.state==INSUFFICIENT_DATA);
 puts("PASS battery curve, filtering, load sag, bounded calibration, persistence CRC, insufficient data");
}
