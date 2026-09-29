#pragma once
#include <stdint.h>
struct TwoWire{int sda=-1,scl=-1;void end(){}void setPins(int a,int b){sda=a;scl=b;}void begin(){}void beginTransmission(int){}int endTransmission(){return 0;}void write(int){}};
inline TwoWire Wire;
