/* Mathematical vectors, independent of private firmware bytes. The golden
 * tables were constructed by repeated multiplication by x modulo 0x43/0x11d,
 * not by replaying the service's recurrence or sorting implementation. */
#include "bios_tables.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int digest(const void *bytes,size_t count,const char *expected)
{
    Sha256 sha;uint8_t sum[32];char hex[65];static const char digits[]="0123456789abcdef";
    sha_init(&sha);
    /* Deliberately split the SHA stream across its 64-byte block boundary. */
    for(size_t offset=0;offset<count;){size_t n=count-offset;if(n>17)n=17;sha_update(&sha,(const uint8_t *)bytes+offset,n);offset+=n;}
    sha_final(&sha,sum);
    for(unsigned i=0;i<32;++i){hex[i*2]=digits[sum[i]>>4];hex[i*2+1]=digits[sum[i]&15];}
    hex[64]=0;return !strcmp(hex,expected);
}
int main(void)
{
    CHECK(digest("",0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(digest("abc",3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    static const char long_vector[]="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(digest(long_vector,sizeof long_vector-1,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    uint8_t data[768],before[768];
    const unsigned bits[2]={6,8};const uint8_t polynomial[2]={0x30,0xb8};
    const char *expected[2]={"ae1e062a575594bc1e5a2cea689f0fcb45e6f0bdaeca43e8dbd30746f81f2d96","46480e4738bf4d70576e6d2e07a2d5b3efcfe93fad9a885bc216334d151f3793"};
    for(unsigned profile=0;profile<2;++profile){
        unsigned count=1u<<bits[profile];memset(data,0xa5,sizeof data);
        CHECK(segacd_bios_protection_tables(bits[profile],polynomial[profile],data,data+count,data+2*count));
        CHECK(digest(data,3*count,expected[profile]));
        for(unsigned i=0;i<count;++i)CHECK(data[i]==i && data[2*count+data[count+i]]==i);
        for(unsigned i=3*count;i<sizeof data;++i)CHECK(data[i]==0xa5);
        memset(data,0x69,sizeof data);memcpy(before,data,sizeof data);
        CHECK(!segacd_bios_protection_tables(bits[profile],0,data,data+count,data+2*count));
        CHECK(!memcmp(data,before,sizeof data));
    }
    memset(data,0x69,sizeof data);memcpy(before,data,sizeof data);
    CHECK(!segacd_bios_protection_tables(7,0xb8,data,data+256,data+512));
    CHECK(!segacd_bios_protection_tables(8,0xb8,NULL,data+256,data+512));
    CHECK(!memcmp(data,before,sizeof data));
    puts("firmware table mathematical vectors, invalid-input atomicity and SHA vectors PASS");return 0;
}
