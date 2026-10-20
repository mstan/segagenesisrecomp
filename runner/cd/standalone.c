/* Small original-program consumer for the reusable Sega CD machine.
 * Window/audio delivery never drives emulated clocks. */
#include "machine.h"
#include "audio_native.h"
#include "backup.h"
#include "debug.h"
#include "disc_image.h"
#include "../audio.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const SegaCDNativeProgram *segacd_standalone_program(void);
extern int segacd_standalone_accept_bios(const uint8_t *,size_t);
extern int segacd_standalone_accept_disc(const uint8_t *);
extern uint8_t g_ram[0x10000];
static SegaCDMachine machine;
typedef struct {uint64_t first,duration;uint16_t mask;} PadEvent;
static PadEvent pad_events[128];static size_t pad_count;
static int parse_number(const char *text,uint64_t *out)
{
    if(!text||!*text||*text=='-'||*text=='+')return 0;
    char*end;errno=0;unsigned long long v=strtoull(text,&end,0);
    if(errno||*end)return 0;
    *out=(uint64_t)v;return 1;
}
static int parse_pad(const char *text)
{
    char*end;uint64_t frame,duration,mask;
    if(!text||pad_count==128||*text=='-'||*text=='+')return 0;
    errno=0;frame=strtoull(text,&end,10);if(errno||end==text||*end++!=':')return 0;
    const char*next=end;if(*next=='-'||*next=='+')return 0;
    duration=strtoull(next,&end,10);if(errno||end==next||*end++!=':')return 0;
    next=end;if(*next=='-'||*next=='+')return 0;
    mask=strtoull(next,&end,0);
    if(errno||end==next||*end||!duration||mask>0xfffu||frame>UINT64_MAX-duration)return 0;
    pad_events[pad_count++]=(PadEvent){frame,duration,(uint16_t)mask};return 1;
}
static uint16_t scripted_pad(uint64_t frame)
{
    uint16_t buttons=0;
    for(size_t i=0;i<pad_count;++i)if(frame>=pad_events[i].first&&frame-pad_events[i].first<pad_events[i].duration)buttons|=pad_events[i].mask;
    return buttons;
}
static int read_bios(const char *path,uint8_t *bytes)
{
    SDL_RWops*f=SDL_RWFromFile(path,"rb");if(!f)return 0;
    int ok=SDL_RWsize(f)==SEGACD_BIOS_BYTES&&SDL_RWread(f,bytes,SEGACD_BIOS_BYTES,1)==1;
    if(SDL_RWclose(f))ok=0;
    return ok;
}
static int media_read(void*image,int32_t lba,uint8_t raw[2352])
{return segacd_image_read(image,lba,raw);}
static uint16_t keyboard_pad(void)
{
    const Uint8*k=SDL_GetKeyboardState(NULL);uint16_t mask=0;
    if(k[SDL_SCANCODE_UP])mask|=GPAD_UP;
    if(k[SDL_SCANCODE_DOWN])mask|=GPAD_DOWN;
    if(k[SDL_SCANCODE_LEFT])mask|=GPAD_LEFT;
    if(k[SDL_SCANCODE_RIGHT])mask|=GPAD_RIGHT;
    if(k[SDL_SCANCODE_Z])mask|=GPAD_A;
    if(k[SDL_SCANCODE_X])mask|=GPAD_B;
    if(k[SDL_SCANCODE_C])mask|=GPAD_C;
    if(k[SDL_SCANCODE_RETURN])mask|=GPAD_START;
    return mask;
}
static uint16_t controller_pad(SDL_GameController *pad)
{
    if(!pad)return 0;
    uint16_t mask=0;
#define BUTTON(sdl,gen) if(SDL_GameControllerGetButton(pad,sdl))mask|=gen
    BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP,GPAD_UP);BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN,GPAD_DOWN);
    BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT,GPAD_LEFT);BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,GPAD_RIGHT);
    BUTTON(SDL_CONTROLLER_BUTTON_X,GPAD_A);BUTTON(SDL_CONTROLLER_BUTTON_A,GPAD_B);
    BUTTON(SDL_CONTROLLER_BUTTON_B,GPAD_C);BUTTON(SDL_CONTROLLER_BUTTON_START,GPAD_START);
#undef BUTTON
    Sint16 x=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),y=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY);
    if(x<-16000)mask|=GPAD_LEFT;
    if(x>16000)mask|=GPAD_RIGHT;
    if(y<-16000)mask|=GPAD_UP;
    if(y>16000)mask|=GPAD_DOWN;
    return mask;
}
static int dump_file(const char *directory,const char *name,const void *data,size_t bytes)
{
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s",directory,name);
    if(n<0||(size_t)n>=sizeof path)return 0;
    FILE*f=fopen(path,"wb");if(!f)return 0;
    int ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=0;return ok;
}
static int dump_state(const char *directory)
{
    int ok=dump_file(directory,"main-ram.bin",g_ram,65536);
    ok&=dump_file(directory,"sub-prg.bin",machine.devices.prg,sizeof machine.devices.prg);
    ok&=dump_file(directory,"word-ram.bin",machine.devices.word,sizeof machine.devices.word);
    ok&=dump_file(directory,"backup-ram.bin",machine.devices.backup,sizeof machine.devices.backup);
    ok&=dump_file(directory,"z80-ram.bin",machine.bus.z80_ram,sizeof machine.bus.z80_ram);
    ok&=dump_file(directory,"vram.bin",machine.vdp.vram,sizeof machine.vdp.vram);
    ok&=dump_file(directory,"frame.argb",machine.pixels,sizeof machine.pixels);
    return ok;
}
static void debug_write_log(void *user,const char *space,uint32_t offset,const uint8_t *bytes,size_t size)
{
    const SegaCDMachine*m=user;
    printf("QA write frame=%llu space=%s offset=%X bytes=",(unsigned long long)m->frames,space,offset);
    for(size_t i=0;i<size;++i)printf("%02X",bytes[i]);
    putchar('\n');fflush(stdout);
}
static int debug_capture(void *user,const char *path,const SegaCDMachine *m)
{
    (void)user;
    FILE*f=fopen(path,"wb");if(!f)return 0;
    int ok=fprintf(f,"P6\n%u %u\n255\n",m->width,m->height)>0;
    for(unsigned y=0;ok&&y<m->height;++y)for(unsigned x=0;x<m->width;++x) {
        uint32_t p=m->pixels[y][x];
        uint8_t rgb[3]={(uint8_t)(p>>16),(uint8_t)(p>>8),(uint8_t)p};
        if(fwrite(rgb,1,3,f)!=3){ok=0;break;}
    }
    if(fclose(f))ok=0;
    return ok;
}
int main(int argc,char **argv)
{
    const char *bios_path=NULL,*disc_path=NULL,*backup_path=NULL,*dump_dir=NULL,*wav_path=NULL;
    uint64_t frame_limit=0,debug_port=0;int headless=0,mute=0,debug_requested=0,start_paused=0;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--headless"))headless=1;
        else if(!strcmp(argv[i],"--mute"))mute=1;
        else if(!strcmp(argv[i],"--start-paused"))start_paused=1;
        else if(i+1<argc&&!strcmp(argv[i],"--debug-port")){if(!parse_number(argv[++i],&debug_port)||debug_port>65535)return 2;debug_requested=1;}
        else if(i+1<argc&&!strcmp(argv[i],"--bios"))bios_path=argv[++i];
        else if(i+1<argc&&!strcmp(argv[i],"--disc"))disc_path=argv[++i];
        else if(i+1<argc&&!strcmp(argv[i],"--backup"))backup_path=argv[++i];
        else if(i+1<argc&&!strcmp(argv[i],"--dump"))dump_dir=argv[++i];
        else if(i+1<argc&&!strcmp(argv[i],"--wav"))wav_path=argv[++i];
        else if(i+1<argc&&!strcmp(argv[i],"--frames")){if(!parse_number(argv[++i],&frame_limit))return 2;}
        else if(i+1<argc&&!strcmp(argv[i],"--pad")){if(!parse_pad(argv[++i]))return 2;}
        else {fprintf(stderr,"Unknown/incomplete option: %s\n",argv[i]);return 2;}
    }
    if(!bios_path||!disc_path||(headless&&!frame_limit)||(start_paused&&!debug_requested)) {
        fprintf(stderr,"Sonic CD --bios <owned BIOS> --disc <verified SGCD> [--backup file] [--headless --frames N --mute] [--pad frame:duration:mask] [--dump directory] [--wav file] [--debug-port N [--start-paused]]\n");return 2;
    }
    if(headless){SDL_setenv("SDL_AUDIODRIVER","dummy",1);mute=1;}
    if(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER|(headless?0:SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER))){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 2;}
    char default_backup[4096];
    if(!backup_path) {
        /* Portable saves stay beside the executable. In particular, an
         * explicit --backup must never create an unused AppData directory. */
        char *base=SDL_GetBasePath();
        if(!base){SDL_Quit();return 2;}
        int n=snprintf(default_backup,sizeof default_backup,"%ssonic-cd.brm",base);
        SDL_free(base);
        if(n<0||(size_t)n>=sizeof default_backup){SDL_Quit();return 2;}
        backup_path=default_backup;
    }
    static uint8_t bios[SEGACD_BIOS_BYTES];char error[512];
    if(!read_bios(bios_path,bios)){fprintf(stderr,"BIOS must be exactly %u readable bytes\n",SEGACD_BIOS_BYTES);SDL_Quit();return 2;}
    if(!segacd_standalone_accept_bios(bios,sizeof bios)){fprintf(stderr,"This build requires the qualified US Model 1 v1.10 BIOS (CRC32 C6D10268).\n");SDL_Quit();return 2;}
    SegaCDImage*image=segacd_image_open(disc_path,error,sizeof error);
    if(!image){fprintf(stderr,"Disc: %s\n",error);SDL_Quit();return 2;}
    if(!segacd_standalone_accept_disc(segacd_image_sha256(image))){fprintf(stderr,"This build requires the qualified original Sonic CD US disc image.\n");segacd_image_close(image);SDL_Quit();return 2;}
    if(!segacd_machine_init(&machine,bios,sizeof bios,segacd_standalone_program())){fprintf(stderr,"Native program initialization failed\n");segacd_image_close(image);SDL_Quit();return 2;}
    SegaCDBackupResult loaded=segacd_backup_load(backup_path,machine.devices.backup);
    if(loaded==SEGACD_BACKUP_ERROR){fprintf(stderr,"Backup RAM cannot be read as exactly %u bytes; existing file retained\n",SEGACD_BACKUP_BYTES);segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
    uint8_t persisted[SEGACD_BACKUP_BYTES];memcpy(persisted,machine.devices.backup,sizeof persisted);
    if(!segacd_cdd_mount(&machine.devices.drive,segacd_image_toc(image))){segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
    segacd_coordinator_media(&machine.coordinator,media_read,image,NULL,NULL);
    SegaCDNativeAudio*audio=segacd_native_audio_create(1);
    if(!audio){fprintf(stderr,"Audio initialization failed\n");segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
    segacd_native_audio_attach_io(audio,&machine.coordinator.io);
    machine.audio_write=segacd_native_audio_write;machine.audio_user=audio;
    audio_set_master_volume(mute?0:100);
    if(wav_path&&audio_wav_start(wav_path)!=0){fprintf(stderr,"Cannot create WAV capture\n");segacd_native_audio_destroy(audio);segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
    SegaCDDebugServer*debug=NULL;
    if(debug_requested) {
        debug=segacd_debug_start(&machine,(uint16_t)debug_port,debug_write_log,debug_capture,&machine,error,sizeof error);
        if(!debug){fprintf(stderr,"QA control: %s\n",error);audio_wav_stop();segacd_native_audio_destroy(audio);segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
        segacd_debug_pause(debug,start_paused);
        printf("QA control listening on 127.0.0.1:%u\n",segacd_debug_port(debug));fflush(stdout);
    }
    SDL_Window*window=NULL;SDL_Renderer*renderer=NULL;SDL_Texture*texture=NULL;SDL_GameController*pad=NULL;
    if(!headless) {
        window=SDL_CreateWindow("Sonic CD",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,672,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        const char *render_driver=SDL_GetHint(SDL_HINT_RENDER_DRIVER);
        Uint32 renderer_flags=render_driver&&!SDL_strcasecmp(render_driver,"software")?SDL_RENDERER_SOFTWARE:SDL_RENDERER_ACCELERATED;
        if(window)renderer=SDL_CreateRenderer(window,-1,renderer_flags);
        if(window&&!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(renderer)texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,GVDP_MAX_WIDTH,GVDP_MAX_HEIGHT);
        if(!texture){fprintf(stderr,"Window: %s\n",SDL_GetError());if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);segacd_debug_stop(debug);audio_wav_stop();segacd_native_audio_destroy(audio);segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return 2;}
        for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);break;}
    }
    int quit=0,paused=0,exit_code=0;uint64_t freq=SDL_GetPerformanceFrequency(),target=SDL_GetPerformanceCounter(),phase=0;
    while(!quit&&(!frame_limit||machine.frames<frame_limit)) {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT)quit=1;
            else if(event.type==SDL_KEYDOWN&&!event.key.repeat&&event.key.keysym.scancode==SDL_SCANCODE_ESCAPE)quit=1;
            else if(event.type==SDL_KEYDOWN&&!event.key.repeat&&event.key.keysym.scancode==SDL_SCANCODE_P){if(debug)segacd_debug_pause(debug,!segacd_debug_is_paused(debug));else paused=!paused;}
            else if(event.type==SDL_CONTROLLERDEVICEADDED&&!pad)pad=SDL_GameControllerOpen(event.cdevice.which);
            else if(event.type==SDL_CONTROLLERDEVICEREMOVED&&pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==event.cdevice.which){SDL_GameControllerClose(pad);pad=NULL;}
        }
        segacd_debug_poll(debug);
        if(quit||segacd_debug_quit_requested(debug))break;
        int run=debug?segacd_debug_should_run_frame(debug):!paused;
        audio_set_playback_enabled(run);
        if(!run){target=SDL_GetPerformanceCounter();phase=0;SDL_Delay(5);continue;}
        segacd_machine_pad(&machine,0,scripted_pad(machine.frames)|segacd_debug_pad(debug,0)|(headless?0:keyboard_pad()|controller_pad(pad)));
        segacd_machine_pad(&machine,1,segacd_debug_pad(debug,1));
        M68kStepResult result=segacd_machine_run_frame(&machine);
        if(result!=M68K_STEP_OK){fprintf(stderr,"Native execution stopped result=%u cpu=%u main=%06X sub=%06X Z80=%04X miss=%04X at Subclock=%llu\n",result,machine.coordinator.last_cpu,machine.coordinator.cpu[0].state.regs.PC,machine.coordinator.cpu[1].state.regs.PC,machine.z80.state.pc,machine.z80.miss_pc,(unsigned long long)machine.coordinator.device_time);exit_code=3;break;}
        SegaCDNativeAudioResult ar=segacd_native_audio_finish_frame(audio,machine.frames*896040u,(uint32_t)machine.frames,!headless);
        if(ar!=SEGACD_NATIVE_AUDIO_OK){fprintf(stderr,"Audio stopped result=%u at frame=%llu\n",ar,(unsigned long long)machine.frames);exit_code=3;break;}
        if(machine.frames%60==0&&memcmp(persisted,machine.devices.backup,sizeof persisted)) {
            if(segacd_backup_store(backup_path,machine.devices.backup)!=SEGACD_BACKUP_OK){fprintf(stderr,"Backup write failed; previous save retained\n");exit_code=4;break;}
            memcpy(persisted,machine.devices.backup,sizeof persisted);
        }
        if(!headless) {
            (void)SDL_UpdateTexture(texture,NULL,machine.pixels,GVDP_MAX_WIDTH*(int)sizeof(uint32_t));
            SDL_RenderSetLogicalSize(renderer,(int)machine.width,(int)machine.height);
            SDL_Rect rect={0,0,(int)machine.width,(int)machine.height};SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,&rect,NULL);SDL_RenderPresent(renderer);
            phase+=freq*896040u;target+=phase/53693175u;phase%=53693175u;
            uint64_t now=SDL_GetPerformanceCounter();if(target>now){uint64_t ms=(target-now)*1000u/freq;if(ms)SDL_Delay((Uint32)ms);}
        }
    }
    if(!exit_code&&memcmp(persisted,machine.devices.backup,sizeof persisted)&&segacd_backup_store(backup_path,machine.devices.backup)!=SEGACD_BACKUP_OK)exit_code=4;
    if(dump_dir&&!dump_state(dump_dir))exit_code=4;
    SegaCDNativeAudioStats stats;segacd_native_audio_get_stats(audio,&stats);
    printf("Sonic CD frames=%llu Subclock=%llu nativeMain=%llu nativeSub=%llu nativeZ80=%llu floor=%llu/%llu audioFaults=%u audioOutput=%llu\n",
        (unsigned long long)machine.frames,(unsigned long long)machine.coordinator.device_time,
        (unsigned long long)machine.coordinator.native_steps[0],(unsigned long long)machine.coordinator.native_steps[1],
        (unsigned long long)machine.z80.retired,(unsigned long long)machine.coordinator.floor_steps[0],(unsigned long long)machine.coordinator.floor_steps[1],stats.faults,(unsigned long long)stats.output_frames);
    printf("Audio events=%llu applied=%llu deferred=%llu underflows=%llu overflows=%llu backwards=%llu FM=%llu PSG=%llu\n",
        (unsigned long long)stats.queued_events,(unsigned long long)stats.applied_events,
        (unsigned long long)stats.deferred_events,(unsigned long long)stats.mix_underflows,
        (unsigned long long)stats.event_overflows,(unsigned long long)stats.backwards_writes,
        (unsigned long long)stats.rendered_fm_frames,(unsigned long long)stats.rendered_psg_frames);
    if(window&&exit_code)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Sonic CD","The game stopped. See the execution log for details.",window);
    if(pad)SDL_GameControllerClose(pad);
    if(texture)SDL_DestroyTexture(texture);
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
    /* Delivery has one emulated frame of lookahead latency. Stopping does
     * not run another guest frame to manufacture future interpolation input.
     * The capture contains the explicitly counted flushed output intervals. */
    printf("Audio intervals delivered=%llu; final lookahead interval ends at stop.\n",(unsigned long long)stats.flushed_frames);
    segacd_debug_stop(debug);audio_wav_stop();segacd_native_audio_destroy(audio);segacd_machine_close(&machine);segacd_image_close(image);SDL_Quit();return exit_code;
}
