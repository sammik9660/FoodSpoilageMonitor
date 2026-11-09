#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace remote_update {
constexpr uint32_t POLL_MS=30000, REPORT_MS=15000, RUN_SECONDS=1200, CHUNK_BYTES=131072;
struct Command {
  char device[13]={},id[33]={},nonce[33]={},kind[6]={},sensor[17]={},version[24]={},build[21]={};
  uint32_t seq=0,issued=0,expires=0,bytes=0,manifestBytes=0;
  char url[385]={},sha[65]={},manifestUrl[385]={},manifestSha[65]={},mac[65]={};
  bool allowDowngrade=false;
};
inline bool hex(const char* s,size_t n){if(!s||strlen(s)!=n)return false;for(size_t i=0;i<n;++i)if(!isxdigit((unsigned char)s[i]))return false;return true;}
inline bool version(const char* s){unsigned a,b,c;char tail;if(!s||strlen(s)>=24)return false;for(const char* p=s;*p;++p)if(!isdigit((unsigned char)*p)&&*p!='.')return false;return sscanf(s,"%u.%u.%u%c",&a,&b,&c,&tail)==3;}
inline bool buildNumber(const char* s){if(strlen(s)!=10)return false;for(const char* p=s;*p;++p)if(!isdigit((unsigned char)*p))return false;return true;}
inline bool allowedUrl(const char* url,const char* hosts) {
  if(!url||!hosts||strlen(url)>384||strncmp(url,"https://",8))return false;
  const char* end=strchr(url+8,'/');if(!end||end==url+8||end-url>260)return false;
  for(const char* p=url;*p;++p)if((unsigned char)*p<=32||(unsigned char)*p>126||*p=='?'||*p=='#'||*p=='%'||*p=='\\')return false;
  if(strstr(url,".."))return false;
  for(const char* p=url+8;p<end;++p)if(!((*p>='a'&&*p<='z')||isdigit((unsigned char)*p)||*p=='.'||*p=='-'))return false;
  const size_t n=end-(url+8);
  for(const char* p=hosts;*p;){const char* comma=strchr(p,',');const size_t len=comma?(size_t)(comma-p):strlen(p);if(len==n&&!memcmp(p,url+8,n))return true;if(!comma)break;p=comma+1;}
  return false;
}
inline bool githubReleaseUrl(const char* url) {
  const char* prefix="https://github.com/";
  return url&&strncmp(url,prefix,strlen(prefix))==0&&strstr(url+strlen(prefix),"/releases/download/")!=nullptr;
}
inline bool githubAssetRedirect(const char* url) {
  const char* prefix="https://release-assets.githubusercontent.com/github-production-release-asset/";
  if(!url||strncmp(url,prefix,strlen(prefix))!=0)return false;
  const size_t length=strlen(url);if(length>2048)return false;
  const char* query=strchr(url+strlen(prefix),'?');if(!query||query==url+strlen(prefix)||!query[1])return false;
  for(const char* p=url;*p;++p)if((unsigned char)*p<=32||(unsigned char)*p>126||*p=='#'||*p=='\\')return false;
  return true;
}
inline bool validCommand(const Command& c,const char* device,uint32_t now,uint32_t previous,const char* hosts) {
  if(strcmp(c.device,device)||!hex(c.id,32)||!hex(c.nonce,32)||!hex(c.sha,64)||!version(c.version)||
     c.seq<=previous||c.issued>now+60||now>c.expires||c.expires<c.issued||c.expires-c.issued>600||!c.bytes||!allowedUrl(c.url,hosts))return false;
  if(!strcmp(c.kind,"esp32"))return buildNumber(c.build)&&c.bytes<=0x300000&&c.manifestBytes>0&&c.manifestBytes<512&&hex(c.manifestSha,64)&&allowedUrl(c.manifestUrl,hosts);
  return !strcmp(c.kind,"nrf")&&hex(c.sensor,16)&&c.bytes<=831488&&c.manifestBytes==0&&!c.manifestUrl[0]&&!c.manifestSha[0];
}
inline int canonical(const Command& c,char* out,size_t capacity) {
  return snprintf(out,capacity,"FSGWREMOTE1\n%s\n%s\n%lu\n%s\n%lu\n%lu\n%s\n%s\n%s\n%s\n%s\n%s\n%lu\n%s\n%s\n%lu\n%u\n",
    c.device,c.id,(unsigned long)c.seq,c.nonce,(unsigned long)c.issued,(unsigned long)c.expires,c.kind,c.sensor,c.version,c.build,c.url,c.sha,
    (unsigned long)c.bytes,c.manifestUrl,c.manifestSha,(unsigned long)c.manifestBytes,c.allowDowngrade?1:0);
}
inline bool rangeMatches(const char* value,uint32_t start,uint32_t end,uint32_t total) {
  unsigned long a,b,c;char tail;return value&&sscanf(value,"bytes %lu-%lu/%lu%c",&a,&b,&c,&tail)==3&&a==start&&b==end&&c==total;
}
inline bool terminal(const char* s){return !strcmp(s,"SUCCESS")||!strcmp(s,"FAILED")||!strcmp(s,"IDLE");}
inline uint32_t crc(const void* data,size_t size){uint32_t c=0xffffffff;const uint8_t* p=(const uint8_t*)data;while(size--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
}
