#define SDL_MAIN_HANDLED
#include <stdio.h>
#include <stdlib.h>
#include "../../runner/rb_digest.h"
#include "../../runner/replay_writer.c"
#define CHECK(c) do { if(!(c)){fprintf(stderr,"replay hash line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static void reset_queue(GenesisReplayWriter *w,unsigned at)
{
    w->head=at;
    SDL_AtomicSet(&w->read_pos,(int)at);
    SDL_AtomicSet(&w->write_pos,(int)at);
    SDL_AtomicSet(&w->stop,0);
    SDL_AtomicSet(&w->error,OK);
}
static void expected_record(char *out,size_t cap,unsigned frame,
                            const void *const *parts,const size_t *sizes)
{
    uint32_t hashes[4];
    /* The independent contiguous vendor API checks the production Frame
     * call's coverage, order and 32-bit fold, without using its wrapper. */
    for(unsigned i=0;i<4;++i) {
        uint64_t full=XXH64(parts[i],sizes[i],0);
        hashes[i]=(uint32_t)full^(uint32_t)(full>>32);
    }
    snprintf(out,cap,"H %u %08x %08x %08x %08x\n",frame,
             hashes[0],hashes[1],hashes[2],hashes[3]);
}
static int queued_record(GenesisReplayWriter *w,unsigned at,const char *expected)
{
    Command c;
    get(w,at,&c,sizeof c);
    CHECK(c.op==LINE && c.size==strlen(expected) && c.size<64);
    char row[64];
    get(w,(at+sizeof c)%QUEUE_BYTES,row,c.size);
    row[c.size]=0;
    CHECK(!strcmp(row,expected));
    CHECK(w->head==(at+sizeof c+c.size)%QUEUE_BYTES);
    CHECK((unsigned)SDL_AtomicGet(&w->write_pos)==w->head);
    return 0;
}
int main(void)
{
    GenesisReplayWriter *writer=calloc(1,sizeof *writer);
    unsigned char bytes[65537],coop[33],views[7],vram[257];
    CHECK(writer);
    for(size_t i=0;i<sizeof bytes;++i)bytes[i]=(unsigned char)(i*17+(i>>8));
    memset(coop,0x3C,sizeof coop);memset(views,0xA5,sizeof views);memset(vram,0x59,sizeof vram);
    const size_t lengths[]={0,1,31,32,33,255,256,4096,65537};
    const unsigned positions[]={0,QUEUE_BYTES-1,QUEUE_BYTES-7,QUEUE_BYTES-31};
    for(size_t i=0;i<sizeof lengths/sizeof *lengths;++i) {
        size_t sizes[]={lengths[i],lengths[i]?sizeof coop:0,lengths[i]?sizeof views:0,lengths[i]?sizeof vram:0};
        unsigned char *data[]={bytes,coop,views,vram};
        const void *parts[]={lengths[i]?bytes:NULL,lengths[i]?coop:NULL,lengths[i]?views:NULL,lengths[i]?vram:NULL};
        uint64_t full=XXH64(parts[0],sizes[0],0);
        CHECK(genesis_replay_hash(parts[0],sizes[0])==((uint32_t)full^(uint32_t)(full>>32)));
        GenesisReplayHash streamed;genesis_replay_hash_init(&streamed);
        size_t split=sizes[0]/3;
        CHECK(genesis_replay_hash_update(&streamed,bytes,split));
        CHECK(genesis_replay_hash_update(&streamed,NULL,0));
        CHECK(genesis_replay_hash_update(&streamed,bytes+split,sizes[0]-split));
        CHECK(genesis_replay_hash_finish(&streamed)==((uint32_t)full^(uint32_t)(full>>32)));
        for(size_t j=0;j<sizeof positions/sizeof *positions;++j) {
            unsigned at=positions[j];char expected[64];
            expected_record(expected,sizeof expected,UINT32_MAX,parts,sizes);
            reset_queue(writer,at);
            CHECK(GenesisReplayWriterFrame(writer,UINT32_MAX,parts[0],sizes[0],parts[1],sizes[1],parts[2],sizes[2],parts[3],sizes[3]));
            CHECK(!queued_record(writer,at,expected));
            uint32_t hashes[4];
            for(unsigned p=0;p<4;++p) {
                uint64_t digest=XXH64(parts[p],sizes[p],0);
                hashes[p]=(uint32_t)digest^(uint32_t)(digest>>32);
            }
            reset_queue(writer,at);
            CHECK(GenesisReplayWriterHashes(writer,UINT32_MAX,hashes));
            hashes[0]^=1;
            CHECK(!queued_record(writer,at,expected));
            /* Every source may change immediately after Frame returns. The
             * queue owns only the prior formatted hashes, never that memory. */
            for(unsigned part=0;part<4;++part)if(sizes[part]) {
                data[part][sizes[part]-1]^=1;
                CHECK(!queued_record(writer,at,expected));
                char changed[64];expected_record(changed,sizeof changed,UINT32_MAX,parts,sizes);
                CHECK(strcmp(changed,expected));
                reset_queue(writer,at);
                CHECK(GenesisReplayWriterFrame(writer,UINT32_MAX,parts[0],sizes[0],parts[1],sizes[1],parts[2],sizes[2],parts[3],sizes[3]));
                CHECK(!queued_record(writer,at,changed));
                data[part][sizes[part]-1]^=1;
                reset_queue(writer,at);
                CHECK(GenesisReplayWriterFrame(writer,UINT32_MAX,parts[0],sizes[0],parts[1],sizes[1],parts[2],sizes[2],parts[3],sizes[3]));
            }
        }
        /* Attachment checksums remain FNV32, including physical queue wrap. */
        uint32_t state_hash=2166136261u;
        for(size_t k=0;k<sizes[0];++k)state_hash=(state_hash^bytes[k])*16777619u;
        for(size_t j=0;j<sizeof positions/sizeof *positions;++j) {
            put(writer,positions[j],bytes,sizes[0]);
            CHECK(snapshot_digest(writer,positions[j],sizes[0])==state_hash);
        }
    }
    CHECK(!GenesisReplayWriterFrame(NULL,0,NULL,0,NULL,0,NULL,0,NULL,0));
    reset_queue(writer,0);
    CHECK(!GenesisReplayWriterHashes(writer,0,NULL));
    CHECK(SDL_AtomicGet(&writer->error)==INVALID && !writer->head);
    for(unsigned part=0;part<4;++part) {
        const void *parts[]={bytes,coop,views,vram};size_t sizes[]={1,1,1,1};parts[part]=NULL;
        reset_queue(writer,QUEUE_BYTES-1);
        CHECK(!GenesisReplayWriterFrame(writer,0,parts[0],sizes[0],parts[1],sizes[1],parts[2],sizes[2],parts[3],sizes[3]));
        CHECK(SDL_AtomicGet(&writer->error)==INVALID && writer->head==QUEUE_BYTES-1);
        CHECK((unsigned)SDL_AtomicGet(&writer->write_pos)==writer->head);
    }
    reset_queue(writer,0);
    CHECK(!GenesisReplayWriterFrame(writer,0,bytes,QUEUE_BYTES,NULL,0,NULL,0,NULL,0));
    CHECK(SDL_AtomicGet(&writer->error)==INVALID && !writer->head);
    reset_queue(writer,0);
    CHECK(!GenesisReplayWriterFrame(writer,0,bytes,FRAME_BYTES_LIMIT,NULL,0,NULL,0,NULL,0));
    CHECK(SDL_AtomicGet(&writer->error)==INVALID && !writer->head);
    reset_queue(writer,0);SDL_AtomicSet(&writer->read_pos,1);
    CHECK(!GenesisReplayWriterFrame(writer,0,bytes,sizeof bytes,coop,sizeof coop,views,sizeof views,vram,sizeof vram));
    CHECK(SDL_AtomicGet(&writer->error)==FULL && !writer->head);
    reset_queue(writer,0);SDL_AtomicSet(&writer->stop,1);
    CHECK(!GenesisReplayWriterFrame(writer,0,NULL,0,NULL,0,NULL,0,NULL,0));
    CHECK(!SDL_AtomicGet(&writer->error) && !writer->head);
    free(writer);
    puts("Replay checksum: full byte coverage, compact records, queue wrap/empty/invalid/full checks and corruption rejection PASS");
    return 0;
}
