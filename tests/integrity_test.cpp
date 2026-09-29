#include <cassert>
#include <cstdio>
#include "../firmware/esp32s3/gateway/Integrity.h"
using namespace integrity;
int main(){
 const char* valid="BID=1234ABCD,SEQ=1,MS=2000,T=25.91,H=50.93,P=1004.04,G=71.63";
 Frame f;assert(parseFrame(valid,f));assert(f.seq==1&&f.boot==0x1234abcd);
 const char* bad[]={"SEQ=1,MS=2,T=25,H=50,P=1000,G=80",
 "BID=1234ABCD,SEQ=1,MS=2000,T=nan,H=50,P=1000,G=70",
 "BID=1234ABCD,SEQ=1,MS=2000,T=25,H=50,P=1000,G=70junk",
 "BID=1234ABCD,SEQ=4294967296,MS=2,T=25,H=50,P=1000,G=70",
 "BID=1234ABCD,SEQ=1,MS=2,T=25,H=101,P=1000,G=70"};
 for(auto s:bad)assert(!parseFrame(s,f));
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
 Sample s={};s.rx=1;s.t=25;seal(s);assert(intact(s));s.rx=2;assert(!intact(s));
 PendingBatch batch;batch.count=1;batch.samples[0]=s;
 assert(!batch.release(false)&&batch.count==1&&batch.samples[0].rx==s.rx);
 assert(batch.release(true)&&batch.count==0);
 puts("PASS strict parser, fragmented/overflow/disconnect framing, sequence gaps/reboots, health, persistent CRC/schema, ACK-gated batch release/identity");
}
