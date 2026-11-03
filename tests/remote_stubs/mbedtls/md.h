#pragma once
// Windows host uses real SHA-256 HMAC through BCrypt; not a permissive crypto mock.
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#pragma comment(lib,"bcrypt.lib")
struct mbedtls_md_info_t{};
constexpr int MBEDTLS_MD_SHA256=6;
inline const mbedtls_md_info_t* mbedtls_md_info_from_type(int){static mbedtls_md_info_t info;return &info;}
inline int mbedtls_md_hmac(const mbedtls_md_info_t*,const unsigned char* key,size_t keySize,const unsigned char* data,size_t size,unsigned char* output){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;ULONG objectSize=0,used=0;int result=-1;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0)return -1;
 if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&objectSize,sizeof(objectSize),&used,0)>=0){
  std::vector<unsigned char> object(objectSize);
  if(BCryptCreateHash(algorithm,&hash,object.data(),objectSize,(PUCHAR)key,(ULONG)keySize,0)>=0){
   if(BCryptHashData(hash,(PUCHAR)data,(ULONG)size,0)>=0&&BCryptFinishHash(hash,output,32,0)>=0)result=0;
   BCryptDestroyHash(hash);
  }
 }
 BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
