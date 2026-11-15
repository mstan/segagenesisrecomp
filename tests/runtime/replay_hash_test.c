#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include "../../runner/rb_digest.h"
#include "../../runner/replay_writer.c"
#define CHECK(c) do { if(!(c)){fprintf(stderr,"replay hash line %d: %s\n",__LINE__,#c);return 1;} } while(0)
int main(void)
{
    GenesisReplayWriter *writer=calloc(1,sizeof *writer);
    unsigned char bytes[65537];
    CHECK(writer);
    for(size_t i=0;i<sizeof bytes;++i)bytes[i]=(unsigned char)(i*17+(i>>8));
    const size_t sizes[]={0,1,31,32,33,255,256,4096,65537};
    const unsigned positions[]={0,QUEUE_BYTES-1,QUEUE_BYTES-31,QUEUE_BYTES-4096};
    for(size_t i=0;i<sizeof sizes/sizeof *sizes;++i) {
        size_t size=sizes[i];
        /* Independent contiguous API is the oracle for the production
         * writer's split streaming updates at physical ring boundaries. */
        uint64_t full=XXH64(bytes,size,0);
        uint32_t expected=(uint32_t)full^(uint32_t)(full>>32);
        CHECK(genesis_replay_hash(bytes,size)==expected);
        for(size_t j=0;j<sizeof positions/sizeof *positions;++j) {
            put(writer,positions[j],bytes,size);
            CHECK(digest(writer,positions[j],size)==expected);
            if(size) {
                bytes[size-1]^=1;put(writer,positions[j],bytes,size);
                CHECK(digest(writer,positions[j],size)!=expected);
                bytes[size-1]^=1;
            }
        }
    }
    free(writer);
    puts("Replay checksum: full byte coverage, contiguous/split ring equality and corruption rejection PASS");
    return 0;
}
