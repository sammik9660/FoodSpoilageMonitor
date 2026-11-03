#pragma once
#include "Arduino.h"
#include <map>
#include <algorithm>
inline std::map<std::string,std::vector<uint8_t>> testNvs;
inline bool testNvsWriteFails=false;
class Preferences {
 public:
 bool begin(const char*,bool){return true;}bool isKey(const char* key){return testNvs.count(key)!=0;}
 size_t getBytesLength(const char* key){return testNvs[key].size();}
 size_t getBytes(const char* key,void* out,size_t n){const auto& v=testNvs[key];if(n<v.size())return 0;memcpy(out,v.data(),v.size());return v.size();}
 size_t putBytes(const char* key,const void* p,size_t n){if(testNvsWriteFails)return 0;auto b=(const uint8_t*)p;testNvs[key]=std::vector<uint8_t>(b,b+n);return n;}
 String getString(const char* key,const char* fallback){if(!isKey(key))return String(fallback);const auto& v=testNvs[key];return String(std::string(v.begin(),v.end()));}
 size_t putString(const char* key,const String& s){return putBytes(key,s.c_str(),s.length());}
};
