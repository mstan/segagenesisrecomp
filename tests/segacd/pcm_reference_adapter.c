/* QA-only ABI for an externally supplied independent chip/owned-driver oracle.
 * No reference emulator, original program or media is linked into this DLL. */
#include "rf5c164.h"
#if defined(_WIN32)
#define API __declspec(dllexport)
#elif defined(__GNUC__)
#define API __attribute__((visibility("default")))
#else
#define API
#endif
static Rf5c164 chip;
API void ref_reset(void){rf5c164_init(&chip);}
API void ref_write(unsigned address,unsigned char value){rf5c164_write(&chip,address,value);}
API unsigned char ref_read(unsigned address){return rf5c164_read(&chip,address);}
API void ref_render(unsigned count,int16_t *output)
{for(unsigned n=0;n<count;++n)rf5c164_sample(&chip,&output[2*n],&output[2*n+1]);}
