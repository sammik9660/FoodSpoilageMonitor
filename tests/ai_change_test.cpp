#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include "../firmware/esp32s3/gateway/AiChange.h"

int main(int argc,char** argv) {
  static_assert(sizeof(ai_change::Model)<=128,"shadow model unexpectedly enlarged");
  assert(argc==2);
  std::ifstream file(argv[1]);assert(file);
  ai_change::Model model;
  int group,lastGroup=-1,rows=0,valid=0;
  while(file>>group) {
    float gas[10];double expected;
    for(float& g:gas)assert(bool(file>>g));
    assert(bool(file>>expected));
    if(group!=lastGroup){model.reset();lastGroup=group;}
    const auto result=model.observe(gas);
    if(expected<0)assert(!result.valid);
    else {assert(result.valid&&std::fabs(result.change-expected)<0.00001);++valid;}
    ++rows;
  }
  float flat[10];for(float& x:flat)x=5.68f;
  assert(!model.observe(flat).valid);
  std::cout<<"PASS Python/C++ causal change parity rows="<<rows<<" scored="<<valid
           <<" model_bytes="<<sizeof(model)<<"\n";
}
