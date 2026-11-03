#pragma once
#include "md.h"
struct mbedtls_sha256_context{std::vector<unsigned char> bytes;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->bytes.clear();}
inline void mbedtls_sha256_free(mbedtls_sha256_context* c){c->bytes.clear();}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){c->bytes.clear();return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const unsigned char* p,size_t n){c->bytes.insert(c->bytes.end(),p,p+n);return 0;}
inline int mbedtls_sha256(const unsigned char* bytes,size_t size,unsigned char* out,int){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;ULONG len=0,used=0;int result=-1;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return -1;
 if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&len,sizeof(len),&used,0)>=0){std::vector<unsigned char> object(len);
  if(BCryptCreateHash(alg,&hash,object.data(),len,nullptr,0,0)>=0){if(BCryptHashData(hash,(PUCHAR)bytes,(ULONG)size,0)>=0&&BCryptFinishHash(hash,out,32,0)>=0)result=0;BCryptDestroyHash(hash);}}
 BCryptCloseAlgorithmProvider(alg,0);return result;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,unsigned char* out){return mbedtls_sha256(c->bytes.data(),c->bytes.size(),out,0);}
