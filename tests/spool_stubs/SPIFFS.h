#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using std::min;
class String:public std::string{
 public:using std::string::string;String(const std::string& s):std::string(s){}
 bool startsWith(const char* p)const{return rfind(p,0)==0;}
 bool endsWith(const char* p)const{size_t n=strlen(p);return size()>=n&&compare(size()-n,n,p)==0;}
 bool isEmpty()const{return empty();}String substring(size_t a,size_t b)const{return substr(a,b-a);}
};
inline std::map<std::string,std::vector<uint8_t>> disk;
inline bool mountOK=true,renameOK=true,removeOK=true,formatAttempted=false;
inline size_t writeLimit=SIZE_MAX;
class File{
 std::string name;size_t pos=0,index=0;bool valid=false,root=false;std::vector<std::string> names;
 public:File(){}File(const std::string& n):name(n),valid(true),root(n=="/"){if(root)for(auto& x:disk)names.push_back(x.first);}
 operator bool()const{return valid;}
 File openNextFile(){return index<names.size()?File(names[index++]):File();}
 String path()const{return name;}
 void seek(size_t n){pos=n;}
 size_t size()const{return disk[name].size();}
 size_t read(uint8_t* p,size_t n){auto& d=disk[name];n=std::min(n,d.size()-std::min(pos,d.size()));memcpy(p,d.data()+pos,n);pos+=n;return n;}
 size_t write(const uint8_t* p,size_t n){auto& d=disk[name];n=std::min(n,writeLimit);if(d.size()<pos+n)d.resize(pos+n);memcpy(d.data()+pos,p,n);pos+=n;return n;}
 void flush(){}void close(){valid=false;}
};
#define FILE_WRITE "w"
#define FILE_READ "r"
struct SpiffsStub{
 bool begin(bool format){formatAttempted|=format;return mountOK;}
 size_t totalBytes(){return 896*1024;}size_t usedBytes(){size_t n=0;for(auto& f:disk)n+=f.second.size()+256;return n;}
 bool exists(const String& p){return disk.count(p);}
 File open(const String& p,const char* mode=FILE_READ){if(p=="/")return File(p);if(*mode=='w'){disk[p]={};return File(p);}return exists(p)?File(p):File();}
 bool rename(const String& a,const String& b){if(!renameOK||!exists(a)||exists(b))return false;disk[b]=disk[a];disk.erase(a);return true;}
 bool remove(const String& p){return removeOK&&disk.erase(p);}
};
inline SpiffsStub SPIFFS;
