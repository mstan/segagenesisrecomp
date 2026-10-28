#include "segacd.h"
#include "sector.h"
#include <string.h>

enum { RET = 1, DMNA = 2, MODE_1M = 4, PRIORITY = 24 };

void segacd_reset(SegaCD *s)
{
    rf5c164_reset(&s->pcm);
    segacd_cdd_reset(&s->drive);
    segacd_cdda_reset(&s->cdda,s->drive.cycles,0x4000);
    s->cdda_error=0;
    segacd_cdc_reset(&s->cdc);s->cdc_destination=0;s->cdc_dma_address=0;
    segacd_stamp_reset(&s->stamp);
    segacd_dma_init(&s->dma);
    s->media_error=0;s->media_error_lba=0;s->media_sectors=s->cdda_sectors=0;
    ++s->sub_reset_generation;
    s->main_control = s->write_protect = s->prg_bank = 0;
    s->memory_mode = RET; /* Power-on Word RAM belongs to the main CPU. */
    s->irq_mask = s->irq_pending = s->timer_reload = 0;
    s->flags[0] = s->flags[1] = 0;
    memset(s->command, 0, sizeof s->command);
    memset(s->status, 0, sizeof s->status);
    s->hint_vector = 0xFFFF;
    s->led = s->font_bits = s->font_color = 0;
    s->vdp_word_latch=0;
    s->stopwatch_origin = s->timer_origin = s->sub_cycles;
    s->peripheral_ready = s->sub_cycles + SEGACD_SUB_CLOCK / 10;
}

int segacd_init(SegaCD *s, const void *bios, size_t bytes)
{
    if (!s || !bios || bytes != SEGACD_BIOS_BYTES) return 0;
    memset(s, 0, sizeof *s);
    memcpy(s->bios, bios, bytes);
    memset(s->backup, 0xFF, sizeof s->backup);
    segacd_reset(s);
    return 1;
}

int segacd_sub_runnable(const SegaCD *s) { return (s->main_control & 3) == 1; }

typedef struct {SegaCD *cd;const SegaCDIO *io;} SectorDelivery;
static void deliver_sector(void *user,int32_t lba)
{
    SectorDelivery *delivery=user;SegaCD *s=delivery->cd;const SegaCDIO *io=delivery->io;
    uint8_t raw[2352];
    if(s->media_error)return;
    if(!io || !io->read_sector || !io->read_sector(io->media_user,lba,raw)) {
        s->media_error=1;s->media_error_lba=lba;return;
    }
    ++s->media_sectors;
    unsigned track=0;
    while(track+1<s->drive.toc.count && lba>=s->drive.toc.track[track+1].index00)++track;
    if(s->drive.toc.track[track].data) {
        /* lba is the sector actually delivered, whereas drive.lba becomes
         * the NEXT sector after delivery. Do not discard the last audio
         * sector early merely because that next sector belongs to data. */
        segacd_cdda_clear_for_data(&s->cdda);
        SegaCDSectorResult status=segacd_sector_validate(raw);
        /* Data pregap headers participate in the decoder's pre-roll. Stored
         * audio/mode-zero pregaps do not contain supported MODE1 blocks;
         * valid MODE1 pregaps follow the same real EDC path as program data. */
        if(lba<s->drive.toc.track[track].start &&
           (status==SEGACD_SECTOR_BAD_SYNC || status==SEGACD_SECTOR_UNSUPPORTED_MODE))return;
        if(status==SEGACD_SECTOR_UNSUPPORTED_MODE) {
            s->media_error=2;s->media_error_lba=lba;return;
        }
        segacd_cdc_sector(&s->cdc,raw,status==SEGACD_SECTOR_MODE1_OK);
    } else {
        ++s->cdda_sectors;
        segacd_cdda_play(&s->cdda);
        SegaCDCddaResult result=segacd_cdda_push_raw_sector(&s->cdda,s->drive.cycles,lba,raw);
        if(result!=SEGACD_CDDA_OK && !s->cdda_error)s->cdda_error=(uint8_t)result;
        if(io && io->cdda_sector)io->cdda_sector(io->cdda_user,s->drive.cycles,lba,raw);
    }
}
void segacd_advance_io(SegaCD *s,uint32_t cycles,const SegaCDIO *io)
{
    uint32_t remaining=cycles;SectorDelivery delivery={s,io};
    while(remaining) {
        /* A command can stop/seek/pause the source between sector events.
         * Do not expose queued audio past that emulated command boundary. */
        if(s->drive.status!=1)segacd_cdda_stop(&s->cdda);
        uint32_t delta=remaining;
        uint64_t event=segacd_cdd_next_event(&s->drive);
        if(event>=s->drive.cycles && event-s->drive.cycles<delta)delta=(uint32_t)(event-s->drive.cycles);
        if(s->dma.active && 6u-s->dma.phase<delta)delta=6u-s->dma.phase;
        if(s->stamp.busy && !(s->memory_mode&(MODE_1M|RET)) && s->stamp.remaining_clocks<delta)
            delta=s->stamp.remaining_clocks;
        /* Age the preceding decoder pulse before delivering a new sector at
         * its actual CDD event boundary, even during a long stopped interval. */
        if(io && io->pcm_timed_sink)
            rf5c164_advance_timed(&s->pcm,s->drive.cycles,delta,io->pcm_timed_sink,io->pcm_timed_user);
        else
            rf5c164_advance(&s->pcm,delta,io?io->pcm_sink:NULL,io?io->pcm_user:NULL);
        if(!s->cdda_error) {
            SegaCDCddaResult result=segacd_cdda_advance_to(&s->cdda,s->drive.cycles+delta,
                io?io->cdda_sample:NULL,io?io->cdda_sample_user:NULL,NULL);
            if(result!=SEGACD_CDDA_OK)s->cdda_error=(uint8_t)result;
        }
        segacd_cdc_advance(&s->cdc,delta);
        if(!(s->memory_mode&(MODE_1M|RET)) && segacd_stamp_advance(&s->stamp,s->word,(s->memory_mode>>3)&3u,delta))
            s->irq_pending|=1u<<1;
        (void)segacd_dma_advance(s,&s->dma,delta);
        if(segacd_cdd_advance(&s->drive,delta,deliver_sector,&delivery))s->irq_pending|=1u<<4;
        remaining-=delta;
    }
    s->sub_cycles += cycles;
    if (s->timer_reload) {
        uint32_t period = ((uint32_t)s->timer_reload + 1) * SEGACD_TIMER_DIVISOR;
        uint64_t elapsed = s->sub_cycles - s->timer_origin;
        if (elapsed >= period) {
            s->timer_origin += (elapsed / period) * period;
            s->irq_pending |= 1u << 3;
        }
    }
}
void segacd_advance_audio(SegaCD *s,uint32_t cycles,Rf5c164Sink sink,void *user)
{
    SegaCDIO io={0};io.pcm_sink=sink;io.pcm_user=user;segacd_advance_io(s,cycles,&io);
}
void segacd_advance(SegaCD *s,uint32_t cycles){segacd_advance_audio(s,cycles,NULL,NULL);}

unsigned segacd_irq_level(const SegaCD *s)
{
    unsigned pending = s->irq_mask & (s->irq_pending | (segacd_cdc_irq(&s->cdc)?32u:0u));
    for (unsigned level = 6; level; --level)
        if (pending & (1u << level)) return level;
    return 0;
}
void segacd_irq_ack(SegaCD *s, unsigned level)
{
    if (level && level <= 6) s->irq_pending &= (uint8_t)~(1u << level);
}

static uint16_t stopwatch(const SegaCD *s)
{ return (uint16_t)(((s->sub_cycles - s->stopwatch_origin) / SEGACD_TIMER_DIVISOR) & 4095); }

static uint32_t word_1m(uint32_t offset, unsigned bank)
{
    /* In 2M mode the two physical banks are interleaved by 16-bit word. */
    return ((offset & 0x1FFFEu) << 1) | (bank << 1) | (offset & 1u);
}

static uint32_t cell_to_linear(uint32_t offset)
{
    /* Hardware Manual pp16-17: column-major 8x8 cells, 512-dot bitmap rows.
     * Five independently mapped bands
     * have 32, 16, 8, 4 and 4 vertical cells respectively. */
    uint32_t base; unsigned rows;
    if (offset < 0x10000u) { base = 0; rows = 32; }
    else if (offset < 0x18000u) { base = 0x10000; rows = 16; }
    else if (offset < 0x1C000u) { base = 0x18000; rows = 8; }
    else if (offset < 0x1E000u) { base = 0x1C000; rows = 4; }
    else { base = 0x1E000; rows = 4; }
    uint32_t n = offset - base, tile = n / 32;
    return base + ((tile % rows) * 8 + (n % 32) / 4) * 256
         + (tile / rows) * 4 + (n & 3);
}

static SegaCDResult word_address(const SegaCD *s, SegaCDCpu cpu,
                                 uint32_t a, uint32_t *offset, int *dot)
{
    *dot = 0;
    if (cpu == SEGACD_MAIN) {
        if (a < 0x200000u || a >= 0x240000u) return SEGACD_UNMAPPED;
        if (!(s->memory_mode & MODE_1M)) {
            if (!(s->memory_mode & RET)) return SEGACD_WAIT;
            *offset = a - 0x200000u;
        } else {
            uint32_t o = (a - 0x200000u) & 0x1FFFFu;
            if (a & 0x20000u) o = cell_to_linear(o);
            *offset = word_1m(o, s->memory_mode & RET);
        }
    } else if (!(s->memory_mode & MODE_1M)) {
        if (a < 0x080000u || a >= 0x0C0000u) return SEGACD_UNMAPPED;
        if (s->memory_mode & RET) return SEGACD_WAIT;
        *offset = a - 0x080000u;
    } else {
        uint32_t o;
        if (a >= 0x080000u && a < 0x0C0000u) {
            o = (a - 0x080000u) / 2;
            *dot = (a & 1u) ? 2 : 1;
        } else if (a >= 0x0C0000u && a < 0x0E0000u) o = a - 0x0C0000u;
        else return SEGACD_UNMAPPED;
        *offset = word_1m(o, (s->memory_mode & RET) ^ 1u);
    }
    return SEGACD_OK;
}

static unsigned font_word(const SegaCD *s, unsigned offset)
{
    unsigned out = 0, shift = 12 - ((offset - 0x50) / 2) * 4;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned bit = (s->font_bits >> (shift + 3 - i)) & 1;
        out = (out << 4) | ((s->font_color >> (bit * 4)) & 15);
    }
    return out;
}

static unsigned cdc_mode(const SegaCD*s,SegaCDCpu cpu)
{
    return ((unsigned)s->cdc_destination<<8)|(s->cdc.transfer_end?0x8000u:0)|
        (s->cdc.fifo_count==2?0x4000u:0)|(cpu==SEGACD_SUB && s->cdc.fifo_count==1?0x2000u:0)|
        (cpu==SEGACD_SUB?s->cdc.ar:0);
}
static uint16_t cdc_host_word(SegaCD*s,SegaCDCpu cpu)
{
    uint8_t high,low;
    if(s->cdc_destination!=(cpu==SEGACD_MAIN?2u:3u) || s->cdc.fifo_count<2)return 0xFFFF;
    if(!segacd_cdc_host_read(&s->cdc,&high) || !segacd_cdc_host_read(&s->cdc,&low))return 0xFFFF;
    return (uint16_t)(((unsigned)high<<8)|low);
}
static SegaCDResult reg_read(SegaCD *s, SegaCDCpu cpu, unsigned r, uint8_t *v)
{
    if(cpu==SEGACD_SUB && r>=0x34 && r<0x4C){*v=segacd_cdd_read(&s->drive,r);return SEGACD_OK;}
    if(cpu==SEGACD_SUB && r>=0x58 && r<0x68) {
        uint16_t value=segacd_stamp_read(&s->stamp,(r-0x58)/2);
        *v=(uint8_t)(r&1u?value:value>>8);return SEGACD_OK;
    }
    unsigned word;
    switch (r & ~1u) {
    case 0x04:word=cdc_mode(s,cpu);break;
    case 0x00:
        word = cpu == SEGACD_MAIN ?
            ((s->irq_mask & 4) ? 0x8000u : 0) | ((s->irq_pending & 4) ? 0x100u : 0) | s->main_control :
            s->led | (s->sub_cycles >= s->peripheral_ready);
        break;
    case 0x02:
        word = ((unsigned)s->write_protect << 8) | (s->prg_bank << 6) | s->memory_mode;
        if (cpu == SEGACD_MAIN) word &= 0xFFC7u;
        else word &= 0xFF1Fu;
        break;
    case 0x06:
        if(cpu==SEGACD_SUB){*v=(r&1u)?segacd_cdc_read(&s->cdc):0;return SEGACD_OK;}
        word = s->hint_vector; break;
    case 0x08:
        /* The physical host data port requires a word access. A byte peek
         * does not consume half a sector or duplicate a FIFO transfer. */
        if(s->cdc.fifo_count==2)word=((unsigned)s->cdc.fifo[s->cdc.fifo_head]<<8)|s->cdc.fifo[s->cdc.fifo_head^1u];
        else word=0xFFFF;
        break;
    case 0x0A:
        if(cpu==SEGACD_MAIN)return SEGACD_UNMAPPED;
        word=0;break; /* DMA address is write only. */
    case 0x0C: word = stopwatch(s); break;
    case 0x0E: word = ((unsigned)s->flags[0] << 8) | s->flags[1]; break;
    default:
        if (r >= 0x10 && r < 0x20) word = s->command[(r - 0x10) / 2];
        else if (r >= 0x20 && r < 0x30) word = s->status[(r - 0x20) / 2];
        else if (cpu != SEGACD_SUB) return SEGACD_UNMAPPED;
        else switch (r & ~1u) {
        case 0x30: word = s->timer_reload; break;
        case 0x32: word = s->irq_mask; break;
        case 0x4C: word = s->font_color; break;
        case 0x4E: word = s->font_bits; break;
        case 0x50: case 0x52: case 0x54: case 0x56: word = font_word(s, r & ~1u); break;
        default: return SEGACD_UNMAPPED;
        }
    }
    *v = (uint8_t)((r & 1u) ? word : word >> 8);
    return SEGACD_OK;
}

const uint8_t *segacd_code_span(const SegaCD *s,SegaCDCpu cpu,uint32_t a,size_t bytes)
{
    if(!s||!bytes||(a&1u)||a>0xffffffu||bytes>0x1000000u-a)return NULL;
    if(cpu==SEGACD_MAIN) {
        if(a<SEGACD_BIOS_BYTES && bytes<=SEGACD_BIOS_BYTES-a) {
            if(a<0x74u && a+bytes>0x72u)return NULL;
            return s->bios+a;
        }
        if(a>=0x020000u&&a<0x040000u&&bytes<=0x040000u-a) {
            if(segacd_sub_runnable(s)||s->prg_bank>=4)return NULL;
            return s->prg+(size_t)s->prg_bank*0x20000u+a-0x020000u;
        }
    }else if(cpu==SEGACD_SUB) {
        if(a<SEGACD_PRG_BYTES&&bytes<=SEGACD_PRG_BYTES-a)return s->prg+a;
    }else return NULL;
    if(!(s->memory_mode&MODE_1M)) {
        uint32_t offset;int dot;
        if(word_address(s,cpu,a,&offset,&dot)==SEGACD_OK&&bytes<=SEGACD_WORD_BYTES-offset)
            return s->word+offset;
    }
    return NULL;
}

SegaCDResult segacd_read8(SegaCD *s, SegaCDCpu cpu, uint32_t a, uint8_t *v)
{
    a &= 0xFFFFFFu;
    if (cpu == SEGACD_MAIN) {
        if (a < SEGACD_BIOS_BYTES) {
            *v = a == 0x72 ? (uint8_t)(s->hint_vector >> 8) :
                 a == 0x73 ? (uint8_t)s->hint_vector : s->bios[a];
            return SEGACD_OK;
        }
        if (a >= 0x020000u && a < 0x040000u) {
            if (segacd_sub_runnable(s)) return SEGACD_WAIT;
            *v = s->prg[(s->prg_bank * 0x20000u) + a - 0x020000u];
            return SEGACD_OK;
        }
        if (a >= 0xA12000u && a < 0xA12030u) return reg_read(s, cpu, a - 0xA12000u, v);
    } else {
        if (a < SEGACD_PRG_BYTES) { *v = s->prg[a]; return SEGACD_OK; }
        if (a>=0xFF0000u && a<0xFF4000u) {
            *v=(a&1u)?rf5c164_read(&s->pcm,(a-0xFF0000u)/2):0xFF;
            return SEGACD_OK;
        }
        if (a >= 0xFE0000u && a < 0xFE4000u) {
            *v = (a & 1u) ? s->backup[(a - 0xFE0000u) / 2] : 0xFF;
            return SEGACD_OK;
        }
        if (a >= 0xFF8000u && a < 0xFF8200u) return reg_read(s, cpu, a - 0xFF8000u, v);
    }
    uint32_t offset; int dot;
    SegaCDResult result = word_address(s, cpu, a, &offset, &dot);
    if (result != SEGACD_OK) return result;
    *v = dot ? (s->word[offset] >> (dot == 1 ? 4 : 0)) & 15 : s->word[offset];
    return SEGACD_OK;
}

SegaCDResult segacd_read16(SegaCD *s, SegaCDCpu cpu, uint32_t a, uint16_t *v)
{
    if (a & 1u) return SEGACD_ADDRESS_ERROR;
    a&=0xFFFFFFu;
    if((cpu==SEGACD_MAIN && a==0xA12008u) || (cpu==SEGACD_SUB && a==0xFF8008u)) {
        *v=cdc_host_word(s,cpu);return SEGACD_OK;
    }
    uint8_t hi, lo;
    SegaCDResult result = segacd_read8(s, cpu, a, &hi);
    if (result != SEGACD_OK) return result;
    result = segacd_read8(s, cpu, a + 1, &lo);
    if (result == SEGACD_OK) *v = (uint16_t)(((unsigned)hi << 8) | lo);
    return result;
}

static void mode_write(SegaCD *s, SegaCDCpu cpu, uint8_t v)
{
    if (cpu == SEGACD_MAIN) {
        s->prg_bank = v >> 6;
        if (s->memory_mode & MODE_1M) {
            if (!(v & DMNA)) s->memory_mode |= DMNA;
        } else if (v & DMNA) s->memory_mode = (s->memory_mode & PRIORITY) | DMNA;
    } else {
        unsigned old = s->memory_mode;
        if (v & MODE_1M) s->memory_mode = v & (MODE_1M | PRIORITY | RET);
        else {
            unsigned owner = (v & RET) ? RET : (old & MODE_1M) ? DMNA : old & (RET | DMNA);
            s->memory_mode = (uint8_t)((v & PRIORITY) | owner);
        }
    }
}

static SegaCDResult reg_write(SegaCD *s, SegaCDCpu cpu, unsigned r, uint8_t v)
{
    if(cpu==SEGACD_SUB && r>=0x34 && r<0x4C) {
        segacd_cdd_write(&s->drive,r,v);
        if(r==0x35) {
            SegaCDCddaResult result=segacd_cdda_set_fader_target_raw(&s->cdda,s->drive.fader);
            if(result!=SEGACD_CDDA_OK && !s->cdda_error)s->cdda_error=(uint8_t)result;
        }
        return SEGACD_OK;
    }
    if(cpu==SEGACD_SUB && r>=0x58 && r<0x68) {
        unsigned reg=(r-0x58)/2;
        /* Trace-vector start requires a word access. */
        if(reg<7) {
            uint16_t old=s->stamp.reg[reg];
            segacd_stamp_latch(&s->stamp,reg,(uint16_t)(r&1u?(old&0xFF00u)|v:(old&255u)|((unsigned)v<<8)));
        }
        return SEGACD_OK;
    }
    if(r==4 || r==5) {
        if(cpu==SEGACD_SUB) {
            if(r==4){
                s->cdc_destination=v&7u;s->cdc.transfer_end=0;
                if(!s->cdc_destination){s->cdc_dma_address=0;segacd_dma_address_programmed(s,&s->dma);}
                if(s->cdc_destination!=4 && s->cdc_destination!=5 && s->cdc_destination!=7)s->dma.active=0;
            }
            else segacd_cdc_address(&s->cdc,v);
        }
        return SEGACD_OK; /* Main CDC mode is read only. */
    }
    if(cpu==SEGACD_SUB && (r==6 || r==7)) {
        if(r==7) {
            if(s->cdc.ar==6 && (s->cdc.ifctrl&2u) && s->cdc_destination>=4 && !s->cdc.transfer_active &&
               segacd_dma_begin(s,&s->dma)!=SEGACD_DMA_OK)return SEGACD_UNMAPPED;
            segacd_cdc_write(&s->cdc,v);
        }
        return SEGACD_OK;
    }
    if(r==8 || r==9)return SEGACD_OK; /* Host data is read only. */
    if(cpu==SEGACD_SUB && (r==10 || r==11)) {
        s->cdc_dma_address=(r&1u)?(uint16_t)((s->cdc_dma_address&0xFF00u)|v):
            (uint16_t)((s->cdc_dma_address&255u)|((unsigned)v<<8));
        segacd_dma_address_programmed(s,&s->dma);return SEGACD_OK;
    }
    if (r == 0 || r == 1) {
        if (cpu == SEGACD_MAIN) {
            if (r == 0) { if ((v & 1) && (s->irq_mask & 4)) s->irq_pending |= 4; }
            else {
                /* Preserve a reset pulse even when both edges occur before
                 * the scheduler next gives the sub CPU an execution slice. */
                if ((s->main_control & 1) && !(v & 1)) ++s->sub_reset_generation;
                s->main_control = v & 3;
            }
        } else if (r == 0) s->led = (uint16_t)((v & 3u) << 8);
        else if (!(v & 1)) {
            s->peripheral_ready = s->sub_cycles + SEGACD_SUB_CLOCK / 10;
            rf5c164_reset(&s->pcm);segacd_cdd_reset(&s->drive);segacd_cdc_reset(&s->cdc);
            segacd_cdda_reset(&s->cdda,s->drive.cycles,0x4000);s->cdda_error=0;
            segacd_dma_init(&s->dma);
        }
        return SEGACD_OK;
    }
    if (r == 2) { if (cpu == SEGACD_MAIN) s->write_protect = v; return SEGACD_OK; }
    if (r == 3) { mode_write(s, cpu, v); return SEGACD_OK; }
    if (r == 6 || r == 7) {
        if (cpu != SEGACD_MAIN) return SEGACD_UNMAPPED;
        s->hint_vector = r == 6 ? (uint16_t)((s->hint_vector & 255) | ((unsigned)v << 8)) :
            (uint16_t)((s->hint_vector & 0xFF00) | v);
        return SEGACD_OK;
    }
    if (r == 0xC || r == 0xD) return SEGACD_OK; /* word access only */
    if (r == 0xE || r == 0xF) {
        if (r == 0xE && cpu == SEGACD_MAIN) s->flags[0] = v;
        if (r == 0xF && cpu == SEGACD_SUB) s->flags[1] = v;
        return SEGACD_OK;
    }
    if (r >= 0x10 && r < 0x30) {
        uint16_t *word;
        if (r < 0x20) { if (cpu != SEGACD_MAIN) return SEGACD_OK; word = &s->command[(r - 0x10) / 2]; }
        else { if (cpu != SEGACD_SUB) return SEGACD_OK; word = &s->status[(r - 0x20) / 2]; }
        *word = (r & 1u) ? (uint16_t)((*word & 0xFF00) | v) : (uint16_t)((*word & 255) | ((unsigned)v << 8));
        return SEGACD_OK;
    }
    if (cpu != SEGACD_SUB) return SEGACD_UNMAPPED;
    switch (r) {
    case 0x30: case 0x32: case 0x4C: return SEGACD_OK;
    case 0x31: s->timer_reload = v; s->timer_origin = s->sub_cycles; return SEGACD_OK;
    case 0x33: s->irq_mask = v & 0x7E; return SEGACD_OK;
    case 0x4D: s->font_color = v; return SEGACD_OK;
    case 0x4E: s->font_bits = (uint16_t)((s->font_bits & 255) | ((unsigned)v << 8)); return SEGACD_OK;
    case 0x4F: s->font_bits = (uint16_t)((s->font_bits & 0xFF00) | v); return SEGACD_OK;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: return SEGACD_OK;
    default: return SEGACD_UNMAPPED;
    }
}

static unsigned priority_pixel(unsigned dest, unsigned src, unsigned mode)
{
    if (mode == 1 && dest) return dest; /* underwrite */
    if (mode == 2 && !src) return dest; /* transparent source */
    return src;
}

SegaCDResult segacd_write8(SegaCD *s, SegaCDCpu cpu, uint32_t a, uint8_t v)
{
    a &= 0xFFFFFFu;
    if (cpu == SEGACD_MAIN) {
        if (a < SEGACD_BIOS_BYTES) return SEGACD_OK; /* ROM */
        if (a >= 0x020000u && a < 0x040000u) {
            if (segacd_sub_runnable(s)) return SEGACD_WAIT;
            s->prg[s->prg_bank * 0x20000u + a - 0x020000u] = v;
            return SEGACD_OK;
        }
        if (a >= 0xA12000u && a < 0xA12030u) return reg_write(s, cpu, a - 0xA12000u, v);
    } else {
        if(a>=0xFF0000u && a<0xFF4000u) {
            if(a&1u)rf5c164_write(&s->pcm,(a-0xFF0000u)/2,v);
            return SEGACD_OK;
        }
        if (a < SEGACD_PRG_BYTES) {
            if (a >= (unsigned)s->write_protect * 512u) s->prg[a] = v;
            return SEGACD_OK;
        }
        if (a >= 0xFE0000u && a < 0xFE4000u) {
            if (a & 1u) s->backup[(a - 0xFE0000u) / 2] = v;
            return SEGACD_OK;
        }
        if (a >= 0xFF8000u && a < 0xFF8200u) return reg_write(s, cpu, a - 0xFF8000u, v);
    }
    uint32_t offset; int dot;
    SegaCDResult result = word_address(s, cpu, a, &offset, &dot);
    if (result != SEGACD_OK) return result;
    /* Hardware p54: CPU pixel priority belongs to the unpacked dot aperture.
     * Direct 1M RAM writes remain ordinary RAM accesses. */
    unsigned mode = dot ? (s->memory_mode >> 3) & 3 : 0;
    if (mode == 3) return SEGACD_UNMAPPED; /* prohibited by hardware contract */
    unsigned old = s->word[offset];
    if (dot) {
        unsigned shift = dot == 1 ? 4 : 0;
        unsigned pixel = priority_pixel((old >> shift) & 15, v & 15, mode);
        s->word[offset] = (uint8_t)((old & ~(15u << shift)) | (pixel << shift));
    } else s->word[offset] = (uint8_t)((priority_pixel(old >> 4, v >> 4, mode) << 4) |
        priority_pixel(old & 15, v & 15, mode));
    return SEGACD_OK;
}

SegaCDResult segacd_write16(SegaCD *s, SegaCDCpu cpu, uint32_t a, uint16_t v)
{
    if (a & 1u) return SEGACD_ADDRESS_ERROR;
    a &= 0xFFFFFFu;
    if(cpu==SEGACD_SUB && a==0xFF8066u) {
        if(s->memory_mode&MODE_1M)return SEGACD_UNMAPPED;
        if(s->memory_mode&RET)return SEGACD_WAIT;
        segacd_stamp_start(&s->stamp,v);return SEGACD_OK;
    }
    if ((cpu == SEGACD_MAIN && a == 0xA1200Cu) || (cpu == SEGACD_SUB && a == 0xFF800Cu)) {
        s->stopwatch_origin = s->sub_cycles;
        return SEGACD_OK;
    }
    SegaCDResult result = segacd_write8(s, cpu, a, (uint8_t)(v >> 8));
    return result == SEGACD_OK ? segacd_write8(s, cpu, a + 1, (uint8_t)v) : result;
}
