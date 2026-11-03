#include <cassert>
#include <fstream>
#include <iostream>
#include "../firmware/esp32s3/gateway/RemoteUpdateClient.h"
using remote_update::Command;
static cJSON* reply(const Command& c){
 cJSON* root=cJSON_CreateObject();cJSON* r=cJSON_AddObjectToObject(root,"remote");
 for(auto pair:std::vector<std::pair<const char*,const char*>>{{"device",c.device},{"id",c.id},{"nonce",c.nonce},{"kind",c.kind},{"sensor",c.sensor},{"version",c.version},{"build",c.build},{"url",c.url},{"sha256",c.sha},{"manifest_url",c.manifestUrl},{"manifest_sha256",c.manifestSha},{"mac",c.mac}})cJSON_AddStringToObject(r,pair.first,pair.second);
 cJSON_AddNumberToObject(r,"seq",c.seq);cJSON_AddNumberToObject(r,"issued",c.issued);cJSON_AddNumberToObject(r,"expires",c.expires);cJSON_AddNumberToObject(r,"bytes",c.bytes);cJSON_AddNumberToObject(r,"manifest_bytes",c.manifestBytes);cJSON_AddBoolToObject(r,"allow_downgrade",c.allowDowngrade);return root;
}
int main(){
 std::ifstream file("data/remote-test-vectors/canonical.txt");std::vector<std::string> fields;std::string line,expected;
 while(std::getline(file,line)){fields.push_back(line);expected+=line+'\n';}assert(fields.size()==18);
 Command c;strcpy(c.device,fields[1].c_str());strcpy(c.id,fields[2].c_str());c.seq=std::stoul(fields[3]);strcpy(c.nonce,fields[4].c_str());c.issued=std::stoul(fields[5]);c.expires=std::stoul(fields[6]);strcpy(c.kind,fields[7].c_str());strcpy(c.sensor,fields[8].c_str());strcpy(c.version,fields[9].c_str());strcpy(c.build,fields[10].c_str());strcpy(c.url,fields[11].c_str());strcpy(c.sha,fields[12].c_str());c.bytes=std::stoul(fields[13]);strcpy(c.manifestUrl,fields[14].c_str());strcpy(c.manifestSha,fields[15].c_str());c.manifestBytes=std::stoul(fields[16]);c.allowDowngrade=fields[17]=="1";
 std::ifstream mac("data/remote-test-vectors/mac.txt");std::getline(mac,line);strcpy(c.mac,line.c_str());
 char canonical[1536];int n=remote_update::canonical(c,canonical,sizeof(canonical));assert(n==(int)expected.size()&&expected==canonical);
 const char* hosts="updates.example.invalid";
 assert(remote_update::validCommand(c,c.device,c.issued,0,hosts));
 assert(!remote_update::validCommand(c,"222222222222",c.issued,0,hosts));
 assert(!remote_update::validCommand(c,c.device,c.expires+1,0,hosts));
 assert(!remote_update::validCommand(c,c.device,c.issued,1,hosts));
 assert(remote_update::rangeMatches("bytes 0-1023/1024",0,1023,1024));
 assert(!remote_update::rangeMatches("bytes 1-1023/1024",0,1023,1024));
 for(const char* url:{"http://updates.example.invalid/a","https://evil.invalid/a","https://updates.example.invalid:443/a","https://updates.example.invalid/a?secret=1","https://updates.example.invalid/%2e/a"})assert(!remote_update::allowedUrl(url,hosts));
 RemoteUpdateClient client;client.begin(c.device);assert(!client.enabled());
 assert(client.configure(String(std::string(64,'b')),String(hosts)));assert(client.enabled());
 Command accepted;auto response=reply(c);
 client.receive(response,"bad nonce");assert(!client.take(accepted,c.issued));
 Command tampered=c;tampered.bytes++;auto bad=reply(tampered);client.receive(bad,c.nonce);assert(!client.take(accepted,c.issued));cJSON_Delete(bad);
 strcpy(client.record.state,"RECEIVED"); // A repeated state name must not skip persisting a new identity.
 client.receive(response,c.nonce);assert(client.take(accepted,c.issued));assert(client.record.seq==1&&client.active);
 RemoteUpdateClient immediatelyAfterAccept;immediatelyAfterAccept.begin(c.device);assert(immediatelyAfterAccept.record.seq==1);
 assert(client.state("DOWNLOADING","",50));
 RemoteUpdateClient reboot;reboot.begin(c.device);assert(reboot.record.seq==1);reboot.receive(response,c.nonce);assert(!reboot.take(accepted,c.issued));
 testNvs.clear();RemoteUpdateClient fail;fail.begin(c.device);assert(fail.configure(String(std::string(64,'b')),String(hosts)));
 testNvsWriteFails=true;fail.receive(response,c.nonce);assert(!fail.take(accepted,c.issued)&&!fail.enabled());testNvsWriteFails=false;
 testNvs.clear();RemoteUpdateClient corrupt;corrupt.begin(c.device);assert(corrupt.configure(String(std::string(64,'b')),String(hosts)));
 corrupt.receive(response,c.nonce);assert(corrupt.take(accepted,c.issued));testNvs["job"][12]^=1;
 RemoteUpdateClient afterCorrupt;afterCorrupt.begin(c.device);assert(!afterCorrupt.enabled());
 cJSON_Delete(response);std::cout<<"PASS real remote client: cross-language HMAC, nonce/device/expiry, NVS replay barrier, write failure and corruption\n";
}
