#include <cassert>
#include "../firmware/esp32s3/gateway/Spool.h"
using namespace integrity;
int main(){
 Sample s[2]={};memset(s[0].boot,'a',32);s[0].rx=7;seal(s[0]);s[1]=s[0];s[1].rx=8;seal(s[1]);
 OutageSpool spool;assert(spool.begin());String path;assert(spool.save(s,2,path));assert(spool.count==2);
 OutageSpool reboot;assert(reboot.begin()&&reboot.count==2);Sample restored[BATCH_SIZE];uint32_t n=0;String oldest;
 assert(reboot.oldest(restored,n,oldest)&&n==2&&!memcmp(s,restored,sizeof(s)));
 assert(reboot.acknowledge(oldest,n)&&reboot.count==0&&disk.empty());
 assert(reboot.save(s,2,path));String temp=path.substring(0,path.length()-4)+".tmp";
 disk[temp]=disk[path];disk.erase(path);OutageSpool interruptedRename;assert(interruptedRename.begin()&&SPIFFS.exists(path));
 disk[path][sizeof(OutageSpool::Header)+4]^=1;OutageSpool corrupt;assert(!corrupt.begin()&&corrupt.fault);assert(!corrupt.oldest(restored,n,oldest));assert(SPIFFS.exists(path));
 disk.clear();OutageSpool shortWrite;assert(shortWrite.begin());writeLimit=3;assert(!shortWrite.save(s,2,path)&&shortWrite.fault);writeLimit=SIZE_MAX;
 OutageSpool tornBoot;assert(!tornBoot.begin()&&tornBoot.fault&&!disk.empty()); // preserve torn evidence; never format
 disk.clear();OutageSpool full;assert(full.begin());for(unsigned i=0;i<OutageSpool::MAX_FILES;++i)assert(full.save(s,1,path));
 assert(!full.save(s,1,path)&&full.full&&full.count==128);assert(disk.size()==128);
 mountOK=false;OutageSpool mountFailure;assert(!mountFailure.begin()&&mountFailure.fault);assert(!formatAttempted);
 puts("PASS real spool: CRC/FIFO, reboot UID preservation, ACK removal, interrupted rename, corruption/torn write fail-closed, capacity/no overwrite/no format");
}
