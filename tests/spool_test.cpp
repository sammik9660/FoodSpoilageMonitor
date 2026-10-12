#include <cassert>
#include <fstream>
#include "../firmware/esp32s3/gateway/Spool.h"
using namespace integrity;
int main(){
 Sample s[2]={};memset(s[0].boot,'a',32);s[0].rx=7;s[0].schema=3;seal(s[0]);s[1]=s[0];s[1].rx=8;seal(s[1]);
 OutageSpool spool;assert(spool.begin());String path;assert(spool.save(s,2,path));assert(spool.count==2);
 OutageSpool reboot;assert(reboot.begin()&&reboot.count==2);Sample restored[BATCH_SIZE];uint32_t n=0;String oldest;
 assert(reboot.oldest(restored,n,oldest)&&n==2&&!memcmp(s,restored,sizeof(s)));
 assert(reboot.acknowledge(oldest,n)&&reboot.count==0&&disk.empty());
 assert(reboot.save(s,2,path));String temp=path.substring(0,path.length()-4)+".tmp";
 disk[temp]=disk[path];disk.erase(path);OutageSpool interruptedRename;assert(interruptedRename.begin()&&SPIFFS.exists(path));
 disk[path][sizeof(OutageSpool::Header)+4]^=1;OutageSpool corrupt;assert(!corrupt.begin()&&corrupt.fault);assert(!corrupt.oldest(restored,n,oldest));assert(SPIFFS.exists(path));
 // A deployed v2 file is never reinterpreted as the wider v3 record. Replay
 // keeps its gateway boot/rx UID and capture fields, then exact ACK removes it.
 disk.clear();LegacySample old={};memset(old.boot,'b',32);old.rx=99;old.seq=17;old.g=0;
 old.checksum=crc(&old,offsetof(LegacySample,checksum));
 OutageSpool::Header h={0x46534d32,2,1,0};h.checksum=crc(&h,12);
 std::vector<uint8_t> legacy(sizeof(h)+sizeof(old));memcpy(legacy.data(),&h,sizeof(h));
 memcpy(legacy.data()+sizeof(h),&old,sizeof(old));disk["/q0000000001.bin"]=legacy;
 OutageSpool migrate;assert(migrate.begin()&&migrate.count==1&&!migrate.fault);
 assert(migrate.oldest(restored,n,oldest)&&n==1&&restored[0].schema==2&&restored[0].rx==99);
 assert(restored[0].g==0&&intact(restored[0]));assert(disk["/q0000000001.bin"]==legacy);
 assert(migrate.acknowledge(oldest,n)&&disk.empty());
 std::ifstream vectorFile("tests/v3_vector.txt");std::string wire;assert(std::getline(vectorFile,wire));
 Frame frame;assert(parseFrame(wire.c_str(),frame));
 Sample vectorSample={};memset(vectorSample.boot,'c',32);vectorSample.rx=11;
 vectorSample.nrfBoot=frame.boot;vectorSample.seq=frame.seq;vectorSample.ms=frame.ms;
 vectorSample.t=frame.t;vectorSample.h=frame.h;vectorSample.p=frame.p;vectorSample.g=frame.g;
 vectorSample.schema=3;vectorSample.scan=frame.scan;vectorSample.step=frame.step;
 vectorSample.gasIndex=frame.gasIndex;vectorSample.measIndex=frame.measIndex;
 vectorSample.sensorStatus=frame.status;vectorSample.profile=354;seal(vectorSample);
 OutageSpool vectorSpool;assert(vectorSpool.begin()&&vectorSpool.save(&vectorSample,1,path));
 OutageSpool vectorReboot;assert(vectorReboot.begin()&&vectorReboot.oldest(restored,n,oldest));
 assert(n==1&&!memcmp(restored,&vectorSample,sizeof(vectorSample)));
 assert(vectorReboot.acknowledge(oldest,n)&&disk.empty());
 Sample ambient={};memset(ambient.boot,'d',32);ambient.rx=12;ambient.seq=7;
 ambient.schema=4;ambient.t=24;ambient.h=51;ambient.p=1002;seal(ambient);
 OutageSpool ambientSpool;assert(ambientSpool.begin()&&ambientSpool.save(&ambient,1,path));
 OutageSpool ambientReboot;assert(ambientReboot.begin()&&ambientReboot.oldest(restored,n,oldest));
 assert(n==1&&restored[0].schema==4&&intact(restored[0]));
 assert(ambientReboot.acknowledge(oldest,n)&&disk.empty());
 disk.clear();OutageSpool shortWrite;assert(shortWrite.begin());writeLimit=3;assert(!shortWrite.save(s,2,path)&&shortWrite.fault);writeLimit=SIZE_MAX;
 OutageSpool tornBoot;assert(!tornBoot.begin()&&tornBoot.fault&&!disk.empty()); // preserve torn evidence; never format
 disk.clear();OutageSpool full;assert(full.begin());for(unsigned i=0;i<OutageSpool::MAX_FILES;++i)assert(full.save(s,1,path));
 assert(!full.save(s,1,path)&&full.full&&full.count==128);assert(disk.size()==128);
 mountOK=false;OutageSpool mountFailure;assert(!mountFailure.begin()&&mountFailure.fault&&mountFailure.mountFailedAtBoot());
 assert(!formatAttempted&&disk.size()==128); // unreadable files remain untouched
 assert(!corrupt.mountFailedAtBoot()&&!tornBoot.mountFailedAtBoot()); // mounted faults stay fail-closed
 puts("PASS real spool: v2/v3/environment CRC/FIFO, reboot UID preservation, ACK removal, corruption/torn write fail-closed, capacity/no overwrite/no format");
}
