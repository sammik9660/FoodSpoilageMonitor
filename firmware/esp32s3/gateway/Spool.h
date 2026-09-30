#pragma once
#include <SPIFFS.h>
#include "Integrity.h"

// Immutable, CRC-checked chunks. Only ACKed chunks are deleted. No auto-format,
// overwrite, or compaction of unacknowledged data; reclaim whole ACKed chunks.
class OutageSpool {
 public:
  static constexpr uint32_t MAX_FILES=128;
  struct Header { uint32_t magic, version, count, checksum; };
  bool mounted=false, fault=false, full=false;
  uint32_t count=0, files=0, nextId=1;
  size_t budget=0, bytes=0;
  bool begin() {
    mounted=SPIFFS.begin(true);
    if(!mounted) { fault=true; return false; }
    budget=SPIFFS.totalBytes()*60/100; // leave GC/metadata and other files headroom
    File root=SPIFFS.open("/");
    for(File f=root.openNextFile(); f; f=root.openNextFile()) {
      String path=f.path();
      if(path.startsWith("/q") && (path.endsWith(".bin") || path.endsWith(".tmp"))) {
        uint32_t id=(uint32_t)strtoul(path.c_str()+2,nullptr,10);
        if(id>=nextId) nextId=id+1;
        Header h={};
        if(!validate(f,h)) { fault=true; f.close(); continue; }
        ++files; count+=h.count; bytes+=f.size();
        f.close();
        // A complete temporary chunk survived a crash before rename.
        if(path.endsWith(".tmp")) {
          String dest=path.substring(0,path.length()-4)+".bin";
          if(SPIFFS.exists(dest) || !SPIFFS.rename(path,dest)) fault=true;
        }
      }
    }
    return !fault;
  }
  bool validate(File& f, Header& h) {
    f.seek(0);
    if(f.read((uint8_t*)&h,sizeof(h))!=sizeof(h) || h.magic!=0x46534d32 || h.version!=2 ||
       !h.count || h.count>integrity::BATCH_SIZE ||
       h.checksum!=integrity::crc(&h,12) || f.size()!=sizeof(h)+h.count*sizeof(integrity::Sample)) return false;
    integrity::Sample s;
    for(uint32_t i=0;i<h.count;++i)
      if(f.read((uint8_t*)&s,sizeof(s))!=sizeof(s)||!integrity::intact(s)) return false;
    return true;
  }
  bool save(const integrity::Sample* samples, uint32_t n, String& path) {
    if(!mounted || fault || !n || n>integrity::BATCH_SIZE) return false;
    size_t need=sizeof(Header)+n*sizeof(integrity::Sample);
    if(files>=MAX_FILES || bytes+need>budget || SPIFFS.usedBytes()+need>SPIFFS.totalBytes()*75/100) {
      full=true; return false;
    }
    char name[32]; snprintf(name,sizeof(name),"/q%010lu",(unsigned long)nextId++);
    String temp=String(name)+".tmp"; path=String(name)+".bin";
    if(SPIFFS.exists(temp)||SPIFFS.exists(path)) { fault=true; return false; }
    Header h={0x46534d32,2,n,0}; h.checksum=integrity::crc(&h,12);
    File f=SPIFFS.open(temp,FILE_WRITE);
    bool ok=f && f.write((uint8_t*)&h,sizeof(h))==sizeof(h) &&
      f.write((const uint8_t*)samples,n*sizeof(*samples))==n*sizeof(*samples);
    f.flush(); f.close();
    File check=SPIFFS.open(temp,FILE_READ); Header verified={};
    ok=ok && check && validate(check,verified); check.close();
    if(!ok || !SPIFFS.rename(temp,path)) { fault=true; return false; }
    ++files; count+=n; bytes+=need; full=false; return true;
  }
  bool oldest(integrity::Sample* samples, uint32_t& n, String& path) {
    if(!mounted || fault || !files) return false;
    path=""; File root=SPIFFS.open("/");
    for(File f=root.openNextFile(); f; f=root.openNextFile()) {
      String p=f.path();
      if(p.startsWith("/q")&&p.endsWith(".bin")&&(path.isEmpty()||p<path)) path=p;
    }
    File f=SPIFFS.open(path,FILE_READ); Header h={};
    if(!f||!validate(f,h)) { fault=true; return false; }
    f.seek(sizeof(h)); n=h.count;
    if(f.read((uint8_t*)samples,n*sizeof(*samples))!=n*sizeof(*samples)) { fault=true; return false; }
    return true;
  }
  bool acknowledge(const String& path, uint32_t n) {
    // DATA INTEGRITY: call only after exact application ACK; crashes before
    // deletion intentionally replay the same UIDs for cloud deduplication.
    if(!SPIFFS.remove(path)) { fault=true; return false; }
    --files; count-=n; bytes-=sizeof(Header)+n*sizeof(integrity::Sample); full=false; return true;
  }
};
