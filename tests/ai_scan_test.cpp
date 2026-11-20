#include <cassert>
#include <cmath>
#include <iostream>
#include "../firmware/esp32s3/gateway/AiScan.h"
#include "../firmware/esp32s3/gateway/AiChange.h"
#include "../firmware/esp32s3/gateway/AiState.h"
#include "../firmware/esp32s3/gateway/AiAnomaly.h"
int main(){
  ai_scan::Assembler scan;ai_scan::Complete done;
  auto step=[](int i,uint32_t scanId=7){return ai_scan::Step{42,scanId,uint32_t(i+1),uint32_t(100+i*140),uint8_t(i),uint8_t(i),uint8_t(4+i*5),0xb0,float(20+i*40),float(20+i),50,1010};};
  for(int i=0;i<10;++i)assert(scan.observe(step(i),done)==(i==9));
  assert(done.boot==42&&done.scan==7&&done.firstMs==100&&done.lastMs==1360);
  assert(done.t==24.5f&&done.h==50&&done.p==1010);
  for(int i=0;i<10;++i)assert(done.gas[i]==20+i*40);
  assert(!scan.observe(step(0,8),done));assert(!scan.observe(step(2,8),done));
  assert(!scan.observe(step(0,11),done));assert(!scan.observe(step(1,11),done));
  assert(!scan.observe(step(0,12),done));assert(scan.partialScans==1&&scan.lastPartialMask==3);
  for(int i=0;i<10;++i)assert(scan.observe(step(i,9),done)==(i==9));
  auto bad=step(0,10);bad.status=0x90;assert(!scan.observe(bad,done));
  for(int i=1;i<10;++i)assert(!scan.observe(step(i,10),done));
  ai_change::Model model;for(int i=0;i<3;++i)assert(!model.observe(done.gas).valid);
  auto saved=model.snapshot();ai_change::Model restarted;assert(restarted.restore(saved));
  ai_state::Blob nvs={};std::strcpy(nvs.storage,"STO-00000000-0000-0000-0000-000000000001");
  std::strcpy(nvs.sensor,"aaaabbbbccccdddd");nvs.started=1790000000000LL;nvs.model=saved;
  ai_state::seal(nvs);
  assert(ai_state::validFor(nvs,nvs.storage,nvs.sensor,nvs.started));
  assert(!ai_state::validFor(nvs,"STO-other",nvs.sensor,nvs.started));
  assert(!ai_state::validFor(nvs,nvs.storage,"different-sensor",nvs.started));
  assert(!ai_state::validFor(nvs,nvs.storage,nvs.sensor,nvs.started+1));
  auto corrupted=nvs;corrupted.model.mean+=1;assert(!ai_state::validFor(corrupted,nvs.storage,nvs.sensor,nvs.started));
  ai_anomaly::Model am;float pattern[10];for(int i=0;i<10;++i)pattern[i]=done.gas[i];
  for(int i=0;i<6;++i)assert(!am.observe(pattern,1790000000000LL+i*600000).valid);
  ai_anomaly::Blob ab={};std::strcpy(ab.storage,nvs.storage);std::strcpy(ab.sensor,nvs.sensor);
  ab.started=nvs.started;ab.state=am.state;ai_anomaly::seal(ab);
  assert(ai_anomaly::validFor(ab,nvs.storage,nvs.sensor,nvs.started));
  ai_anomaly::Model resumed;assert(resumed.restore(ab.state));
  assert(resumed.observe(pattern,1790003600000LL).valid);
  float changed[10];for(int i=0;i<10;++i)changed[i]=done.gas[i]*(i<5?1.3f:0.8f);
  assert(std::fabs(model.observe(changed).change-restarted.observe(changed).change)<1e-12);
  saved.count=4;assert(!restarted.restore(saved));
  std::cout<<"PASS strict 0..9 assembler, invalid step, matching NVS session restore and CRC\n";
}
