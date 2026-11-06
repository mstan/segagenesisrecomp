/* Included by main.c after its machine state declarations. Opt-in envelope;
 * existing games continue to use their unchanged GROWNS2 path. Raw machine
 * payloads are private to this exact source/compiler/architecture fingerprint. */
enum { HOST_STATE_SECTIONS=6, HOST_STATE_LIMIT=32*1024*1024 };
extern uint8_t g_ram[65536];
extern uint8_t g_rom[0x400000];
static Uint32 host_state_notice_until;
static void host_state_notice(const char *message)
{
#if RECOMP_LAUNCHER
    if (s_runtime_ui.window) {
        char title[256]; snprintf(title,sizeof title,"%s - %s",g_game_spec.display_name,message);
        SDL_SetWindowTitle(s_runtime_ui.window,title); host_state_notice_until=SDL_GetTicks()+5000;
    }
#endif
}
typedef struct HostStateHeader {
    char magic[8], build[65];
    uint32_t rom, bytes, crc, section[HOST_STATE_SECTIONS];
} HostStateHeader;
static int host_machine_write(FILE *f,HostStateHeader *h)
{
    long at=ftell(f),end;
    glue_save_state(f); end=ftell(f); h->section[1]=(uint32_t)(end-at); at=end;
    if (fwrite(g_ram,1,65536,f)!=65536) return 0;
    h->section[2]=65536; at=ftell(f);
    if (!machine_save_state(f)) return 0;
    end=ftell(f); h->section[3]=(uint32_t)(end-at); at=end;
    if (!ym2612_save_state(f)) return 0;
    end=ftell(f); h->section[4]=(uint32_t)(end-at); at=end;
    if (!psg_save_state(f)) return 0;
    end=ftell(f); h->section[5]=(uint32_t)(end-at);
    return end>=0 && !ferror(f);
}
static int host_machine_read(FILE *f)
{
    glue_load_state(f);
    return fread(g_ram,1,65536,f)==65536 && machine_load_state(f) &&
        ym2612_load_state(f) && psg_load_state(f) && !ferror(f);
}
typedef struct HostStateWrite {
    FILE *file;
    HostStateHeader header;
    char temporary[640],path[512];
    Uint64 timing[6];
    SDL_Thread *thread;
    SDL_atomic_t done;
    int ok;
} HostStateWrite;
static HostStateWrite *host_state_writer;

/* The worker owns only an immutable captured file/header, never live guest,
 * renderer or audio data. Flush/checksum/atomic commit may block on storage. */
static int host_state_write_finish(void *context)
{
    HostStateWrite *w=context;
    FILE *f=w->file; HostStateHeader *h=&w->header;
    int ok=!fseek(f,sizeof *h,SEEK_SET) && genesis_state_crc32_file(f,h->bytes,&h->crc);
    ok=ok && !fseek(f,0,SEEK_SET) && fwrite(h,sizeof *h,1,f)==1 && !fflush(f);
    w->timing[3]=SDL_GetPerformanceCounter();
    if(fclose(f))ok=0;
    w->timing[4]=SDL_GetPerformanceCounter();
    if(ok) {
#ifdef _WIN32
        ok=MoveFileExA(w->temporary,w->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=rename(w->temporary,w->path)==0;
#endif
    }
    if(!ok)remove(w->temporary); /* Only this request's unique temporary file. */
    w->timing[5]=SDL_GetPerformanceCounter();w->ok=ok;
    SDL_AtomicSet(&w->done,1);
    return 0;
}
static int host_state_collect(int wait)
{
    HostStateWrite *w=host_state_writer;
    if(!w)return 1;
    if(!wait && !SDL_AtomicGet(&w->done))return 0;
    if(w->thread)SDL_WaitThread(w->thread,NULL);
    int ok=w->ok;
    if(getenv("GENESIS_STATE_TIMING")) {
        double us=1e6/(double)SDL_GetPerformanceFrequency();
        fprintf(stderr,"[SAVE-TIMING] capture_us=%.1f serialize_us=%.1f checksum_us=%.1f close_us=%.1f commit_us=%.1f\n",
            (w->timing[1]-w->timing[0])*us,(w->timing[2]-w->timing[1])*us,
            (w->timing[3]-w->timing[2])*us,(w->timing[4]-w->timing[3])*us,(w->timing[5]-w->timing[4])*us);
    }
    fprintf(stderr,"[SAVE] %s %s\n",ok?"saved":"failed (previous state retained)",w->path);
    /* atexit also drains an explicit script exit; SDL may already be down. */
    if(SDL_WasInit(SDL_INIT_VIDEO))host_state_notice(ok?"State saved":"Save failed: previous state retained");
    free(w);host_state_writer=NULL;return ok;
}
static void host_state_shutdown(void) { (void)host_state_collect(1); }
static int host_state_save_now(const char *path)
{
    Uint64 timing[3]={SDL_GetPerformanceCounter(),0,0};
    HostStateHeader h={0};
    memcpy(h.magic,"GRHOST2",8);
    if (!g_game_spec.state_build_id || strlen(g_game_spec.state_build_id)>=sizeof h.build) return 0;
    strcpy(h.build,g_game_spec.state_build_id);
    h.rom=rom_crc32(g_rom,g_game_spec.expected_rom_size);
    size_t size=g_game_spec.state_size();
    if (!size || size>HOST_STATE_LIMIT) return 0;
    uint8_t *host=malloc(size); if (!host) return 0;
    int ok=g_game_spec.state_save(host,size);
    timing[1]=SDL_GetPerformanceCounter();
    if (!ok) { free(host); host_state_notice("Save failed: wait for supported gameplay"); fprintf(stderr,"[SAVE] The game adapter cannot save in its current mode.\n"); return 0; }
    char tmp[640]; snprintf(tmp,sizeof tmp,"%s.tmp.%llu",path,(unsigned long long)SDL_GetPerformanceCounter());
    FILE *f=fopen(tmp,"w+b");
    if (!f) { free(host); return 0; }
    h.section[0]=(uint32_t)size;
    ok=fwrite(&h,sizeof h,1,f)==1 && fwrite(host,1,size,f)==size && host_machine_write(f,&h);
    timing[2]=SDL_GetPerformanceCounter();
    free(host);
    long end=ftell(f);
    if (end<(long)sizeof h || end-(long)sizeof h>HOST_STATE_LIMIT) ok=0;
    if (ok) h.bytes=(uint32_t)(end-sizeof h);
    HostStateWrite *w=ok?calloc(1,sizeof *w):NULL;
    if(!w) { fclose(f);remove(tmp);host_state_notice("Save failed: previous state retained");return 0; }
    w->file=f;w->header=h;
    memcpy(w->timing,timing,sizeof timing);
    strcpy(w->temporary,tmp);strcpy(w->path,path);
    host_state_writer=w;
    static int shutdown_registered;
    if(!shutdown_registered){atexit(host_state_shutdown);shutdown_registered=1;}
    w->thread=SDL_CreateThread(host_state_write_finish,"state-writer",w);
    if(!w->thread) { host_state_write_finish(w);return host_state_collect(1); }
    host_state_notice("Saving state...");
    return 1;
}
static char host_state_pending[512];
static unsigned host_state_pending_frames;
static int host_state_save(const char *path)
{
    /* Recorded accepted saves must not become busy rejections merely because
     * uncapped playback reaches the next request before storage catches up.
     * Draining here advances no guest tick or mutable simulation state. */
    host_state_collect(genesis_replay_playing());
    if(host_state_writer){host_state_notice("Save already in progress");return 0;}
    if (strlen(path)>=sizeof host_state_pending || *host_state_pending) return 0;
    strcpy(host_state_pending,path); host_state_pending_frames=0;
    glue_state_boundary_request(1);
    fprintf(stderr,"[SAVE] queued for the next completed gameplay tick: %s\n",path);
    host_state_notice("Saving at next gameplay tick...");
    return 1;
}
static void host_state_tick(void)
{
    host_state_collect(0);
#if RECOMP_LAUNCHER
    if (host_state_notice_until && (Sint32)(SDL_GetTicks()-host_state_notice_until)>=0) {
        if (s_runtime_ui.window) SDL_SetWindowTitle(s_runtime_ui.window,g_game_spec.display_name);
        host_state_notice_until=0;
    }
#endif
    if (!*host_state_pending) return;
    if (glue_state_boundary_ready()) {
        host_state_save_now(host_state_pending);
    } else if (++host_state_pending_frames<120) return;
    else { host_state_notice("Save unavailable in the current game mode");
        fprintf(stderr,"[SAVE] no resumable game boundary; previous state retained.\n"); }
    host_state_pending[0]=0; glue_state_boundary_request(0);
}
static int host_state_load(const char *path)
{
    if(*host_state_pending) {
        host_state_notice("Save capture pending: load after confirmation");
        fprintf(stderr,"[LOAD] rejected: save capture still pending; previous slot not loaded.\n");return 0;
    }
    if(!host_state_collect(1)) {
        host_state_notice("Latest save failed: previous state retained");return 0;
    }
    glue_state_boundary_request(0);
    HostStateHeader h={0},expected={0};
    FILE *f=fopen(path,"rb"),*backup=NULL,*staged=NULL;
    uint8_t *body=NULL; int ok=0;
    if (!f) goto done;
    if (fread(&h,sizeof h,1,f)!=1 || memcmp(h.magic,"GRHOST2",8) ||
        !g_game_spec.state_build_id || h.build[64] || strcmp(h.build,g_game_spec.state_build_id) ||
        h.rom!=rom_crc32(g_rom,g_game_spec.expected_rom_size) ||
        !h.bytes || h.bytes>HOST_STATE_LIMIT || h.section[0]!=g_game_spec.state_size()) goto done;
    uint64_t total=0; for (unsigned i=0;i<HOST_STATE_SECTIONS;++i) total+=h.section[i];
    if (total!=h.bytes) goto done;
    body=malloc(h.bytes); if (!body) goto done;
    if (fread(body,1,h.bytes,f)!=h.bytes || fgetc(f)!=EOF || ferror(f) ||
        rom_crc32(body,h.bytes)!=h.crc || !g_game_spec.state_load(body,h.section[0],0)) goto done;
    /* Check every fixed section and the opaque FM length BEFORE mutating RAM,
     * CPU, audio or host simulation. Also retain a rollback for I/O failures. */
    backup=tmpfile(); staged=tmpfile(); if (!backup || !staged) goto done;
    if (!host_machine_write(backup,&expected)) goto done;
    size_t fm_at=0;
    for (unsigned i=1;i<HOST_STATE_SECTIONS;++i) {
        if (h.section[i]!=expected.section[i]) goto done;
        if (i<4) fm_at+=h.section[i];
    }
    uint32_t fm_size=0,old_fm_size=0;
    memcpy(&fm_size,body+h.section[0]+fm_at,sizeof fm_size);
    if (fseek(backup,(long)fm_at,SEEK_SET) || fread(&old_fm_size,sizeof old_fm_size,1,backup)!=1 || fm_size!=old_fm_size) goto done;
    if (fwrite(body+h.section[0],1,h.bytes-h.section[0],staged)!=h.bytes-h.section[0] || fflush(staged)) goto done;
    rewind(staged);
    if (!host_machine_read(staged)) { rewind(backup); host_machine_read(backup); goto done; }
    /* apply cannot fail after successful preflight; game adapters own no disk
     * writes and cannot select a serialized filesystem path. */
    ok=g_game_spec.state_load(body,h.section[0],1);
    if (!ok) { rewind(backup); host_machine_read(backup); goto done; }
    audio_discard_playback(); /* host payload restored deferred chip events */
    uint32_t pc=g_game_spec.resume_main_loop_pc;
    if (g_game_spec.save_resume_pc && g_game_layout.game_mode_addr) {
        uint32_t mapped=g_game_spec.save_resume_pc(glue_peek8(g_game_layout.game_mode_addr));
        if (mapped) pc=mapped;
    }
    if (pc) glue_restart_game_fiber(pc);
done:
    if (f) fclose(f); if (backup) fclose(backup); if (staged) fclose(staged); free(body);
    fprintf(stderr,"[LOAD] %s %s\n",ok?"loaded":"rejected: missing, damaged, incompatible build or configuration",path);
    host_state_notice(ok?"State loaded":"Load rejected: file missing, damaged, or setup/build mismatch");
    return ok;
}
