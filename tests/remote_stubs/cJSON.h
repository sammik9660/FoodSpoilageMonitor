#pragma once
#include <map>
#include <string>
struct cJSON {int type=0;double valuedouble=0;std::string storage;char* valuestring=nullptr;std::map<std::string,cJSON*> children;};
inline cJSON* cJSON_CreateObject(){auto n=new cJSON;n->type=1;return n;}
inline cJSON* cJSON_AddObjectToObject(cJSON* r,const char* k){return r->children[k]=cJSON_CreateObject();}
inline void cJSON_AddStringToObject(cJSON* r,const char* k,const char* s){auto n=new cJSON;n->type=4;n->storage=s;n->valuestring=n->storage.data();r->children[k]=n;}
inline void cJSON_AddNumberToObject(cJSON* r,const char* k,double v){auto n=new cJSON;n->type=5;n->valuedouble=v;r->children[k]=n;}
inline void cJSON_AddBoolToObject(cJSON* r,const char* k,bool v){auto n=new cJSON;n->type=v?2:3;r->children[k]=n;}
inline const cJSON* cJSON_GetObjectItemCaseSensitive(const cJSON* r,const char* k){if(!r)return nullptr;auto it=r->children.find(k);return it==r->children.end()?nullptr:it->second;}
inline bool cJSON_IsObject(const cJSON* r){return r&&r->type==1;}
inline bool cJSON_IsBool(const cJSON* r){return r&&(r->type==2||r->type==3);}
inline bool cJSON_IsTrue(const cJSON* r){return r&&r->type==2;}
inline bool cJSON_IsString(const cJSON* r){return r&&r->type==4;}
inline bool cJSON_IsNumber(const cJSON* r){return r&&r->type==5;}
inline void cJSON_Delete(cJSON* r){if(!r)return;for(auto& pair:r->children)cJSON_Delete(pair.second);delete r;}
