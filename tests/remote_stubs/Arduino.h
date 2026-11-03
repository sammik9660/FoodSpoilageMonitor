#pragma once
#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>
class String {
 std::string value;
 public:
 String(const char* s=""):value(s){} String(const std::string& s):value(s){}
 const char* c_str()const{return value.c_str();}size_t length()const{return value.size();}bool isEmpty()const{return value.empty();}
 char operator[](size_t i)const{return value[i];}bool operator!=(const String& b)const{return value!=b.value;}
 bool operator==(const String& b)const{return value==b.value;}bool startsWith(const char* s)const{return value.rfind(s,0)==0;}
 friend String operator+(const String& a,const String& b){return String(a.value+b.value);}
 void toCharArray(char* out,size_t n)const{if(n){strncpy(out,value.c_str(),n-1);out[n-1]=0;}}
};
struct TestQueue{std::vector<uint8_t> bytes;bool full=false;explicit TestQueue(size_t n):bytes(n){}};
using QueueHandle_t=TestQueue*;
constexpr int pdTRUE=1;
inline QueueHandle_t xQueueCreate(unsigned,size_t n){return new TestQueue(n);}
inline int xQueueSend(QueueHandle_t q,const void* data,unsigned){if(q->full)return 0;memcpy(q->bytes.data(),data,q->bytes.size());q->full=true;return 1;}
inline int xQueueReceive(QueueHandle_t q,void* data,unsigned){if(!q->full)return 0;memcpy(data,q->bytes.data(),q->bytes.size());q->full=false;return 1;}
inline void esp_fill_random(void* p,size_t n){static uint8_t value=0;memset(p,++value,n);}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q){return q&&q->full?1:0;}
