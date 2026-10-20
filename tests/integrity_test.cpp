#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include "../firmware/esp32s3/gateway/Integrity.h"
using namespace integrity;
int main(){
 const char* valid="BID=1234ABCD,SEQ=1,MS=2000,T=25.91,H=50.93,P=1004.04,G=71.63";
 Frame f;assert(parseFrame(valid,f));assert(f.seq==1&&f.boot==0x1234abcd);
 Frame env;assert(parseFrame("ENV,BID=1234ABCD,SEQ=1,MS=2000,T=25.91,H=50.93,P=1004.04",env));
 assert(env.version==4&&env.seq==1&&env.g==0);
 assert(!parseFrame("ENV,BID=1234ABCD,SEQ=2,MS=4000,T=25,H=50,P=1000,G=0",env));
 assert(!parseFrame("ENV,BID=1234ABCD,SEQ=2,MS=4000,T=nan,H=50,P=1000",env));
 EnvironmentState live;bool save=false;
 assert(parseFrame("ENV,BID=1234ABCD,SEQ=1,MS=2000,T=25,H=50,P=1000",env));
 assert(live.accept(env,1000,save)&&save&&live.latest.t==25);
 env.seq=2;env.t=26;assert(live.accept(env,300999,save)&&!save&&live.latest.t==26);
 env.seq=3;assert(live.accept(env,301000,save)&&save);
 assert(!live.accept(env,301001,save));
 live.historyRejected();env.seq=4;assert(live.accept(env,301002,save)&&save);
 assert(retryWait(150000,-11,30000,150000)==30000);
 assert(retryNext(150000,-11,30000,150000)==30000);
 assert(retryWait(5000,429,30000,150000)==150000);
 assert(retryWait(5000,503,30000,150000)==150000);
 assert(retryNext(5000,429,30000,150000)==10000);
 std::ifstream vectorFile("tests/v3_vector.txt");std::string scan;assert(std::getline(vectorFile,scan));
 assert(parseFrame(scan.c_str(),f)&&f.version==3&&f.seq==11&&f.scan==2&&f.step==3&&f.measIndex==44);
 Framer scanFramer;
 for(size_t i=0;i<scan.size();i+=20)for(char ch:scan.substr(i,20))assert(scanFramer.feed(ch)==0);
 assert(scanFramer.feed('\n')==1&&parseFrame(scanFramer.line,f));
 Sample vectorSample={};vectorSample.schema=f.version;vectorSample.scan=f.scan;
 vectorSample.step=f.step;vectorSample.gasIndex=f.gasIndex;vectorSample.measIndex=f.measIndex;
 vectorSample.sensorStatus=f.status;vectorSample.profile=354;vectorSample.t=f.t;
 vectorSample.h=f.h;vectorSample.p=f.p;vectorSample.g=f.g;seal(vectorSample);assert(intact(vectorSample));
 char worst[180];int length=snprintf(worst,sizeof(worst),
  "V=3,BID=FFFFFFFF,SEQ=4294967295,SCAN=4294967295,STEP=9,GI=9,MI=255,MS=4294967295,ST=176,PID=354,T=-40.00,H=100.00,P=1100.00,G=1000000000.00");
 assert(length>0&&length<180&&length<=MAX_FRAME&&parseFrame(worst,f));
 assert(!parseFrame("V=3,BID=1234ABCD,SEQ=1,SCAN=1,STEP=1,GI=2,MI=1,MS=1,ST=176,PID=354,T=25.00,H=50.00,P=1000.00,G=70.00",f));
 assert(!parseFrame("V=3,BID=1234ABCD,SEQ=1,SCAN=1,STEP=1,GI=1,MI=1,MS=1,ST=128,PID=354,T=25.00,H=50.00,P=1000.00,G=70.00",f));
 assert(!parseFrame("V=3,BID=1234ABCD,SEQ=1,SCAN=1,STEP=1,GI=1,MI=1,MS=1,ST=176,PID=354,T=25.00,H=50.00,P=1000.00,G=0.00",f));
 const char* bad[]={"SEQ=1,MS=2,T=25,H=50,P=1000,G=80",
 "BID=1234ABCD,SEQ=1,MS=2000,T=nan,H=50,P=1000,G=70",
 "BID=1234ABCD,SEQ=1,MS=2000,T=25,H=50,P=1000,G=70junk",
 "BID=1234ABCD,SEQ=4294967296,MS=2,T=25,H=50,P=1000,G=70",
 "BID=1234ABCD,SEQ=1,MS=2,T=25,H=101,P=1000,G=70"};
 for(auto s:bad)assert(!parseFrame(s,f));
 assert(parseFrame(valid,f)&&f.version==2);
 Framer framer;int complete=0;
 // Single-byte delivery is maximal fragmentation; chunk boundaries cannot alter it.
 for(const char* p=valid;*p;++p)assert(framer.feed(*p)==0);
 assert(framer.feed('\r')==0);assert(framer.feed('\n')==1);assert(parseFrame(framer.line,f));
 for(unsigned i=0;i<MAX_FRAME+10;++i)framer.feed('x');
 for(const char* p=valid;*p;++p)assert(framer.feed(*p)==0);
 assert(framer.feed('\n')==0);assert(framer.overflows==1);
 framer.feed('T');framer.reset();for(const char* p=valid;*p;++p)framer.feed(*p);
 assert(framer.feed('\n')==1&&parseFrame(framer.line,f));
 Continuity c;assert(c.accept(f));f.seq=4;assert(c.accept(f)&&c.gaps==2);assert(!c.accept(f));
 f.boot++;f.seq=1;assert(c.accept(f)&&c.restarts==1);
 Health h;assert(parseHealth("STAT,BID=1234ABCD,MS=1,FAIL=3,REC=1,TRY=1,TXERR=0,OK=0",h));assert(h.failures==3);
 assert(parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0",h));
 assert(h.mode==1&&h.envOK==1&&h.gasOK==0);
 assert(parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0,BV=4090,BOK=1,USB=0",h));
 assert(h.batteryKnown==1&&h.batteryMillivolts==4090&&h.usbPower==0);
 assert(parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0,BV=4090,BOK=1,USB=0,BP=89,BS=1,MN=3600,MX=4150",h));
 assert(h.batteryExtended&&h.batteryPercent==89&&h.batteryState==1&&h.batteryMinMv==3600&&h.batteryMaxMv==4150);
 assert(parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0,BV=0,BOK=0,USB=1",h));
 assert(!h.batteryKnown&&h.usbPower);
 assert(!parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0,BV=4090,BOK=1,USB=1",h));
 assert(!parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=ENV,EOK=1,GOK=0,BV=4090,BOK=1,USB=0,BP=101,BS=1,MN=3600,MX=4150",h));
 assert(!parseHealth("STAT,BID=1234ABCD,MS=2,FAIL=3,REC=1,TRY=1,TXERR=0,OK=1,MODE=NOPE,EOK=1,GOK=0",h));
 Sample s={};s.rx=1;s.t=25;seal(s);assert(intact(s));s.rx=2;assert(!intact(s));
 PendingBatch batch;batch.count=1;batch.samples[0]=s;
 assert(!batch.release(false)&&batch.count==1&&batch.samples[0].rx==s.rx);
 assert(batch.release(true)&&batch.count==0);
 puts("PASS v2/v3/ENV strict parser, environment state and 5m history, fragmented framing, health, CRC/schema, ACK-gated release");
}
