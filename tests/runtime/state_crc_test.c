/* Independent old one-shot CRC oracle, real chunked production helper. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GENESIS_TEST_CRC_HEADER
#define GENESIS_TEST_CRC_HEADER "../../runner/state_crc.h"
#endif
#include GENESIS_TEST_CRC_HEADER
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(2);}++checks;}while(0)
static unsigned checks,scenarios;
static uint32_t old_update(uint32_t crc,const uint8_t*data,size_t n){for(size_t i=0;i<n;++i){crc^=data[i];for(unsigned bit=0;bit<8;++bit)crc=crc&1u?(crc>>1)^UINT32_C(0xedb88320):crc>>1;}return crc;}
static uint32_t old_crc(const uint8_t*data,size_t n){return ~old_update(UINT32_MAX,data,n);}
int main(void){
 static uint8_t data[1048607];for(size_t i=0;i<sizeof data;++i)data[i]=(uint8_t)((i*37u)^(i>>8));
 CHECK(old_crc((const uint8_t*)"123456789",9)==UINT32_C(0xcbf43926));CHECK(~genesis_state_crc32_update(UINT32_MAX,(const uint8_t*)"123456789",9)==UINT32_C(0xcbf43926));++scenarios;
 const size_t lengths[]={0,1,31,65535,65536,65537,131071,131072,131073,sizeof data};
 for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];++i){size_t n=lengths[i];FILE*f=tmpfile();CHECK(f);CHECK(fwrite("PREFIX!",1,7,f)==7&&fwrite(data,1,n,f)==n);CHECK(!fseek(f,7,SEEK_SET));uint32_t crc=UINT32_C(0xdeadbeef);CHECK(genesis_state_crc32_file(f,n,&crc));CHECK(crc==old_crc(data,n));CHECK(ftell(f)==(long)(n+7));CHECK(!fseek(f,7,SEEK_SET));crc=UINT32_C(0xdeadbeef);CHECK(!genesis_state_crc32_file(f,n+1,&crc));CHECK(crc==UINT32_C(0xdeadbeef));CHECK(!fclose(f));++scenarios;}
 FILE*f=tmpfile();CHECK(f);uint32_t expected=UINT32_MAX;size_t remaining=32u*1024u*1024u;while(remaining){size_t n=remaining<65536?remaining:65536;CHECK(fwrite(data,1,n,f)==n);expected=old_update(expected,data,n);remaining-=n;}CHECK(!fseek(f,0,SEEK_SET));uint32_t crc=0;CHECK(genesis_state_crc32_file(f,32u*1024u*1024u,&crc)&&crc==~expected);CHECK(!fclose(f));++scenarios;
 f=fopen("crc-read-error.tmp","wb");CHECK(f);crc=UINT32_C(0x12345678);CHECK(!genesis_state_crc32_file(f,1,&crc));CHECK(crc==UINT32_C(0x12345678));CHECK(!fclose(f));CHECK(!remove("crc-read-error.tmp"));++scenarios;
 printf("CRC PASS scenarios=%u checks=%u max_file_bytes=%u fixed_chunk_bytes=65536\n",scenarios,checks,32u*1024u*1024u);return 0;
}
