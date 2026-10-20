/* Read-only progress/CPU/I/O observer. Optional stack snapshots are for a
 * tool-owned child only: briefly copy its context/stack, resume immediately,
 * then unwind the copy. Never suspend an owner's interactive session.
 * This development executable is independent of the game and its simulation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dbghelp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t wall, thread_cpu, cycles, process_cpu, read_bytes, write_bytes;
    uint64_t sys_idle, sys_total, available, working_set;
    DWORD frame, faults;
    unsigned char tag;
} Sample;
typedef struct {
    Sample begin, end;
    uint64_t pause_us, pc[32];
    unsigned depth;
    char symbols[32][192];
    int startup;
    int resumed;
} Stall;

static uint64_t ft(FILETIME f) { ULARGE_INTEGER n; n.LowPart=f.dwLowDateTime; n.HighPart=f.dwHighDateTime; return n.QuadPart; }
static uint64_t qpc(void) { LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
static void sample_wait(HANDLE timer)
{
    LARGE_INTEGER due; due.QuadPart=-50000; /* 5 ms, relative */
    if(timer && SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) WaitForSingleObject(timer,INFINITE);
    else Sleep(5);
}
static DWORD main_thread(DWORD pid)
{
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 e; e.dwSize=sizeof e;
    uint64_t earliest=UINT64_MAX; DWORD tid=0;
    if(Thread32First(snap,&e)) do {
        if(e.th32OwnerProcessID!=pid) continue;
        HANDLE h=OpenThread(THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
        FILETIME c,x,k,u;
        if(h && GetThreadTimes(h,&c,&x,&k,&u) && ft(c)<earliest) { earliest=ft(c); tid=e.th32ThreadID; }
        if(h) CloseHandle(h);
    } while(Thread32Next(snap,&e));
    CloseHandle(snap); return tid;
}

#if defined(_M_X64)
static unsigned char stack_copy[65536];
static DWORD64 stack_base;
static DWORD64 stack_limit;
static SIZE_T stack_size;
static BOOL CALLBACK snapshot_read(HANDLE p,DWORD64 at,PVOID out,DWORD size,LPDWORD read)
{
    if(at>=stack_base && at-stack_base<=stack_size && size<=stack_size-(SIZE_T)(at-stack_base)) {
        memcpy(out,stack_copy+(SIZE_T)(at-stack_base),size); *read=size; return TRUE;
    }
    if(at>=stack_base && at<stack_limit) { *read=0; return FALSE; } /* uncopied stack is not a stable snapshot */
    SIZE_T n=0; BOOL ok=ReadProcessMemory(p,(LPCVOID)(uintptr_t)at,out,size,&n); *read=(DWORD)n; return ok;
}
static void capture_stack(HANDLE process,HANDLE thread,Stall *s,uint64_t freq)
{
    CONTEXT ctx; memset(&ctx,0,sizeof ctx); ctx.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
    uint64_t begin=qpc();
    if(SuspendThread(thread)==(DWORD)-1) return;
    BOOL ok=GetThreadContext(thread,&ctx);
    stack_base=ctx.Rsp; stack_limit=ctx.Rsp; stack_size=0;
    if(ok) {
        MEMORY_BASIC_INFORMATION mem;
        if(VirtualQueryEx(process,(LPCVOID)(uintptr_t)ctx.Rsp,&mem,sizeof mem)) {
            stack_limit=(uintptr_t)mem.BaseAddress+mem.RegionSize;
            SIZE_T size=(SIZE_T)(stack_limit-ctx.Rsp);
            if(size>sizeof stack_copy) size=sizeof stack_copy;
            /* Do not fault cold stack pages into the child while it is paused.
             * A truncated resident snapshot is preferable to manufacturing a
             * long scheduling/paging stall with the diagnostic itself. */
            SYSTEM_INFO system; GetSystemInfo(&system);
            PSAPI_WORKING_SET_EX_INFORMATION pages[32];
            uintptr_t first=(uintptr_t)ctx.Rsp & ~((uintptr_t)system.dwPageSize-1);
            unsigned n=(unsigned)(((uintptr_t)ctx.Rsp+size-first+system.dwPageSize-1)/system.dwPageSize);
            if(n>32) n=32;
            memset(pages,0,sizeof pages);
            for(unsigned i=0;i<n;++i) pages[i].VirtualAddress=(PVOID)(first+(uintptr_t)i*system.dwPageSize);
            if(QueryWorkingSetEx(process,pages,n*sizeof pages[0])) {
                unsigned resident=0;
                while(resident<n && pages[resident].VirtualAttributes.Valid) ++resident;
                uintptr_t end=first+(uintptr_t)resident*system.dwPageSize;
                if(end<ctx.Rsp) size=0;
                else if(size>end-ctx.Rsp) size=(SIZE_T)(end-ctx.Rsp);
            } else size=0;
            ReadProcessMemory(process,(LPCVOID)(uintptr_t)ctx.Rsp,stack_copy,size,&stack_size);
        }
    }
    /* All symbol work and unwinding happen AFTER resuming the child. */
    ResumeThread(thread);
    s->pause_us=(qpc()-begin)*1000000/freq;
    if(!ok) return;
    s->pc[s->depth++]=ctx.Rip;
    if(!SymInitialize(process,"",TRUE)) return;
    STACKFRAME64 frame; memset(&frame,0,sizeof frame);
    frame.AddrPC.Offset=ctx.Rip; frame.AddrPC.Mode=AddrModeFlat;
    frame.AddrStack.Offset=ctx.Rsp; frame.AddrStack.Mode=AddrModeFlat;
    frame.AddrFrame.Offset=ctx.Rbp; frame.AddrFrame.Mode=AddrModeFlat;
    for(unsigned steps=0;steps<64 && s->depth<32 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&ctx,
                snapshot_read,SymFunctionTableAccess64,SymGetModuleBase64,NULL);++steps) {
        if(!frame.AddrPC.Offset) break;
        if(frame.AddrPC.Offset<0x10000 || frame.AddrPC.Offset>=UINT64_C(0x0000800000000000)) break;
        if(frame.AddrPC.Offset==s->pc[s->depth-1]) continue; /* first walk returns the captured PC */
        s->pc[s->depth++]=frame.AddrPC.Offset;
    }
    for(unsigned i=0;i<s->depth;++i) {
        union { ULONG64 align; char data[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]; } storage;
        SYMBOL_INFO *symbol=(SYMBOL_INFO *)storage.data;
        memset(&storage,0,sizeof storage); symbol->SizeOfStruct=sizeof *symbol; symbol->MaxNameLen=MAX_SYM_NAME;
        IMAGEHLP_MODULE64 module; memset(&module,0,sizeof module); module.SizeOfStruct=sizeof module;
        DWORD64 displacement=0;
        if(SymFromAddr(process,s->pc[i],&displacement,symbol)) {
            SymGetModuleInfo64(process,s->pc[i],&module);
            snprintf(s->symbols[i],sizeof s->symbols[i],"%s!%s+0x%llx",module.ModuleName,symbol->Name,displacement);
        }
    }
    SymCleanup(process);
}
#else
static void capture_stack(HANDLE p,HANDLE t,Stall *s,uint64_t f) { (void)p;(void)t;(void)s;(void)f; }
#endif

static int sample(HANDLE p,HANDLE t,uintptr_t address,uintptr_t tag_address,Sample *s)
{
    SIZE_T n; FILETIME c,x,k,u,idle,sysk,sysu; IO_COUNTERS io;
    PROCESS_MEMORY_COUNTERS mem; MEMORYSTATUSEX sysmem;
    memset(s,0,sizeof *s); s->wall=qpc();
    if(!ReadProcessMemory(p,(LPCVOID)address,&s->frame,sizeof s->frame,&n)) return 0;
    s->tag=255;
    if(tag_address) ReadProcessMemory(p,(LPCVOID)tag_address,&s->tag,1,&n);
    if(GetThreadTimes(t,&c,&x,&k,&u)) s->thread_cpu=ft(k)+ft(u);
    QueryThreadCycleTime(t,&s->cycles);
    if(GetProcessTimes(p,&c,&x,&k,&u)) s->process_cpu=ft(k)+ft(u);
    if(GetProcessIoCounters(p,&io)) { s->read_bytes=io.ReadTransferCount; s->write_bytes=io.WriteTransferCount; }
    mem.cb=sizeof mem;
    if(GetProcessMemoryInfo(p,&mem,sizeof mem)) { s->faults=mem.PageFaultCount; s->working_set=mem.WorkingSetSize; }
    if(GetSystemTimes(&idle,&sysk,&sysu)) { s->sys_idle=ft(idle); s->sys_total=ft(sysk)+ft(sysu); }
    sysmem.dwLength=sizeof sysmem;
    if(GlobalMemoryStatusEx(&sysmem)) s->available=sysmem.ullAvailPhys;
    return 1;
}

int main(int argc,char **argv)
{
    if(argc<5) { fprintf(stderr,"usage: observer PID FRAME_RVA SECONDS PREFIX [owned-child-stacks|read-only] [TAG_RVA]\n"); return 2; }
    DWORD pid=(DWORD)strtoul(argv[1],NULL,10); uintptr_t rva=(uintptr_t)strtoull(argv[2],NULL,0);
    unsigned seconds=(unsigned)strtoul(argv[3],NULL,10); int stacks=argc>5 && !strcmp(argv[5],"owned-child-stacks");
    uintptr_t tag_rva=argc>6?(uintptr_t)strtoull(argv[6],NULL,0):0;
    if(!seconds || seconds>600) return 2;
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid);
    DWORD tid=main_thread(pid);
    HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION|(stacks?(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME):0),FALSE,tid);
    if(!process || !thread) { fprintf(stderr,"open failed: %lu\n",GetLastError()); return 3; }
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
    MODULEENTRY32W module; module.dwSize=sizeof module;
    if(!Module32FirstW(modules,&module)) return 4;
    uintptr_t base=(uintptr_t)module.modBaseAddr; CloseHandle(modules);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); uint64_t freq=f.QuadPart;
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    if(!timer) timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    size_t cap=(size_t)seconds*200+1000,count=0;
    Sample *history=(Sample *)calloc(cap,sizeof *history);
    Stall *events=(Stall *)calloc(256,sizeof *events); unsigned event_count=0; int in_stall=0,progressed=0;
    Sample last={0},last_progress={0}; uint64_t start=qpc(),max_gap=0;
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    while(qpc()-start<(uint64_t)seconds*freq && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        Sample now;
        if(!sample(process,thread,base+rva,tag_rva?base+tag_rva:0,&now)) { sample_wait(timer); continue; }
        if(count) {
            uint64_t gap=now.wall-last.wall; if(gap>max_gap) max_gap=gap;
            if(now.frame!=last.frame) {
                progressed=1;
                if(in_stall) { events[event_count-1].end=now; events[event_count-1].resumed=1; in_stall=0; }
                last_progress=now;
            } else if(!in_stall && event_count<256 && now.wall-last_progress.wall>=freq/20) {
                Stall *s=&events[event_count++]; memset(s,0,sizeof *s);
                s->begin=last_progress; s->end=now; s->startup=!progressed; in_stall=1;
                if(stacks) capture_stack(process,thread,s,freq);
            } else if(in_stall) events[event_count-1].end=now;
        } else last_progress=now;
        if(count<cap) history[count++]=now;
        last=now; sample_wait(timer);
    }
    char path[1024]; snprintf(path,sizeof path,"%s-samples.csv",argv[4]);
    FILE *csv=fopen(path,"w"); if(!csv) return 5;
    fprintf(csv,"seconds,frame,thread_cpu_ms,thread_cycles,process_cpu_ms,page_faults,read_bytes,write_bytes,system_idle,system_total,available_bytes,working_set,tag\n");
    for(size_t i=0;i<count;++i) {
        Sample *s=&history[i];
        fprintf(csv,"%.6f,%lu,%.4f,%llu,%.4f,%lu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n",
                (double)(s->wall-start)/freq,s->frame,s->thread_cpu/10000.,s->cycles,s->process_cpu/10000.,s->faults,
                s->read_bytes,s->write_bytes,s->sys_idle,s->sys_total,s->available,s->working_set,s->tag);
    }
    fclose(csv); snprintf(path,sizeof path,"%s-events.json",argv[4]); FILE *out=fopen(path,"w"); if(!out) return 5;
    fprintf(out,"{\"pid\":%lu,\"thread\":%lu,\"base\":%llu,\"frame_rva\":%llu,\"samples\":%zu,\"max_observer_gap_ms\":%.3f,\"stacks_suspend_owned_child\":%s,\"events\":[",
            pid,tid,(unsigned long long)base,(unsigned long long)rva,count,max_gap*1000./freq,stacks?"true":"false");
    for(unsigned i=0;i<event_count;++i) {
        Stall *s=&events[i]; Sample *a=&s->begin,*b=&s->end;
        double system_busy=b->sys_total>a->sys_total?100.*(1.-(double)(b->sys_idle-a->sys_idle)/(b->sys_total-a->sys_total)):0;
        fprintf(out,"%s{\"seconds\":%.6f,\"frame\":%lu,\"tag\":%u,\"startup\":%s,\"duration_ms\":%.3f,\"main_cpu_ms\":%.4f,\"main_cycles\":%llu,\"process_cpu_ms\":%.4f,\"page_faults\":%lu,\"read_bytes\":%llu,\"write_bytes\":%llu,\"system_busy_percent\":%.2f,\"available_bytes\":%llu,\"probe_pause_us\":%llu,\"pcs\":[",
                i?",":"",(a->wall-start)/(double)freq,a->frame,a->tag,s->startup?"true":"false",(b->wall-a->wall)*1000./freq,
                (b->thread_cpu-a->thread_cpu)/10000.,b->cycles-a->cycles,(b->process_cpu-a->process_cpu)/10000.,
                b->faults-a->faults,b->read_bytes-a->read_bytes,b->write_bytes-a->write_bytes,system_busy,b->available,s->pause_us);
        for(unsigned j=0;j<s->depth;++j) fprintf(out,"%s%llu",j?",":"",s->pc[j]);
        fprintf(out,"],\"symbols\":[");
        for(unsigned j=0;j<s->depth;++j) {
            fprintf(out,"%s\"",j?",":"");
            for(const unsigned char *p=(unsigned char *)s->symbols[j];*p;++p) {
                if(*p=='\\' || *p=='\"') fputc('\\',out);
                if(*p>=32) fputc(*p,out);
            }
            fputc('"',out);
        }
        fprintf(out,"],\"progress_resumed\":%s}",s->resumed?"true":"false");
    }
    fprintf(out,"]}\n"); fclose(out);
    printf("observed pid=%lu samples=%zu stalls=%u max_observer_gap_ms=%.3f stacks=%d\n",pid,count,event_count,max_gap*1000./freq,stacks);
    free(events); free(history); if(timer) CloseHandle(timer); CloseHandle(thread); CloseHandle(process); return 0;
}
