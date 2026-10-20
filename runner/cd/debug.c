#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "debug.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET DSocket;
#define D_INVALID INVALID_SOCKET
#define d_close closesocket
static int d_would_block(void){int e=WSAGetLastError();return e==WSAEWOULDBLOCK;}
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int DSocket;
#define D_INVALID (-1)
#define d_close close
static int d_would_block(void){return errno==EWOULDBLOCK||errno==EAGAIN;}
#endif

#define D_LINE 2048
#define D_OUT 32768
#define D_READ_MAX 1024
#ifdef _WIN32
#define d_strtok strtok_s
#else
#define d_strtok strtok_r
#endif
struct SegaCDDebugServer {
    DSocket listener,client;
    uint16_t port;
    SegaCDMachine *machine;
    SegaCDDebugWriteLog log_write;
    SegaCDDebugCapture capture;
    void *user;
    char input[D_LINE];size_t input_size;
    char output[D_OUT];size_t output_size,output_sent;
    uint16_t pad[2];
    uint64_t step_credits;
    int paused,quit,discard_line;
#ifdef _WIN32
    int winsock;
#endif
};

static void error_text(char *out,size_t n,const char *fmt,...)
{if(!out||!n)return;va_list ap;va_start(ap,fmt);vsnprintf(out,n,fmt,ap);va_end(ap);out[n-1]=0;}
static int nonblocking(DSocket s)
{
#ifdef _WIN32
    u_long on=1;return ioctlsocket(s,FIONBIO,&on)==0;
#else
    int f=fcntl(s,F_GETFL,0);return f>=0&&fcntl(s,F_SETFL,f|O_NONBLOCK)==0;
#endif
}
static void close_client(SegaCDDebugServer*s)
{if(s->client!=D_INVALID)d_close(s->client);s->client=D_INVALID;s->input_size=0;s->output_size=s->output_sent=0;s->discard_line=0;}
static int queue(SegaCDDebugServer*s,const char *fmt,...)
{
    if(s->client==D_INVALID)return 0;
    if(s->output_sent==s->output_size)s->output_sent=s->output_size=0;
    if(s->output_sent){memmove(s->output,s->output+s->output_sent,s->output_size-s->output_sent);s->output_size-=s->output_sent;s->output_sent=0;}
    size_t left=sizeof(s->output)-s->output_size;if(left<2){close_client(s);return 0;}
    va_list ap;va_start(ap,fmt);int n=vsnprintf(s->output+s->output_size,left,fmt,ap);va_end(ap);
    if(n<0||(size_t)n>=left){close_client(s);return 0;}s->output_size+=(size_t)n;return 1;
}
static int number(const char*t,uint64_t *v)
{if(!t||!*t||*t=='-'||*t=='+')return 0;errno=0;char*e;unsigned long long x=strtoull(t,&e,0);if(errno||*e)return 0;*v=(uint64_t)x;return 1;}
static int no_more(char **save)
{return d_strtok(NULL," \t\r",save)==NULL;}
static int memory(SegaCDDebugServer*s,const char*space,uint8_t**base,size_t*size)
{
    if(!strcmp(space,"ram")){extern uint8_t g_ram[0x10000];*base=g_ram;*size=0x10000;}
    else if(!strcmp(space,"prg")){*base=s->machine->devices.prg;*size=SEGACD_PRG_BYTES;}
    else if(!strcmp(space,"word")){*base=s->machine->devices.word;*size=SEGACD_WORD_BYTES;}
    else if(!strcmp(space,"z80")){*base=s->machine->bus.z80_ram;*size=sizeof s->machine->bus.z80_ram;}
    else return 0;
    return 1;
}
static const char*result_name(M68kStepResult r)
{
    switch(r){case M68K_STEP_OK:return "ok";case M68K_STEP_MISS:return "miss";case M68K_STEP_BUS_ERROR:return "bus_error";case M68K_STEP_ADDRESS_ERROR:return "address_error";case M68K_STEP_STOPPED:return "stopped";case M68K_STEP_REENTERED:return "reentered";default:return "error";}
}
static void command(SegaCDDebugServer*s,char*line)
{
    char *save=NULL,*op=d_strtok(line," \t\r",&save);
    if(!op)return;
    if(!strcmp(op,"status")){
        if(!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"status takes no arguments\"}\n");return;}
        SegaCDMachine*m=s->machine;
        queue(s,"{\"ok\":true,\"frames\":%llu,\"main_pc\":%u,\"sub_pc\":%u,\"z80_pc\":%u,\"result\":\"%s\",\"paused\":%s}\n",
            (unsigned long long)m->frames,m->coordinator.cpu[0].state.regs.PC,
            m->coordinator.cpu[1].state.regs.PC,m->z80.state.pc,result_name(m->result),s->paused?"true":"false");return;
    }
    if(!strcmp(op,"read")){
        char *space=d_strtok(NULL," \t\r",&save),*a=d_strtok(NULL," \t\r",&save),*n=d_strtok(NULL," \t\r",&save);uint64_t off,count;uint8_t*b;size_t size;
        if(!space||!a||!n||!number(a,&off)||!number(n,&count)||!memory(s,space,&b,&size)||off>size||count>size-off||count>D_READ_MAX||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"bad space, range, or read length\"}\n");return;}
        queue(s,"{\"ok\":true,\"data\":\"");for(size_t i=0;i<(size_t)count&&s->client!=D_INVALID;++i)queue(s,"%02x",b[off+i]);queue(s,"\"}\n");return;
    }
    if(!strcmp(op,"write")){
        char *space=d_strtok(NULL," \t\r",&save),*a=d_strtok(NULL," \t\r",&save),*v=d_strtok(NULL," \t\r",&save);uint64_t off,val;uint8_t*b;size_t size;
        if(!s->log_write){queue(s,"{\"ok\":false,\"error\":\"write logger unavailable\"}\n");return;}
        if(!space||!a||!v||!number(a,&off)||!number(v,&val)||val>255||!memory(s,space,&b,&size)||off>=size||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"bad space, offset, or byte value\"}\n");return;}
        b[off]=(uint8_t)val;if(s->log_write)s->log_write(s->user,space,(uint32_t)off,b+off,1);queue(s,"{\"ok\":true}\n");return;
    }
    if(!strcmp(op,"pad")){
        char *p=d_strtok(NULL," \t\r",&save),*v=d_strtok(NULL," \t\r",&save);uint64_t port,mask;
        if(!p||!v||!number(p,&port)||!number(v,&mask)||port>1||mask>0xfff||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"pad expects port 0..1 and mask 0..0xfff\"}\n");return;}
        s->pad[port]=(uint16_t)mask;segacd_machine_pad(s->machine,(unsigned)port,s->pad[port]);queue(s,"{\"ok\":true}\n");return;
    }
    if(!strcmp(op,"pause")){char*v=d_strtok(NULL," \t\r",&save);uint64_t x;if(!v||!number(v,&x)||x>1||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"pause expects 0 or 1\"}\n");return;}s->paused=(int)x;if(!s->paused)s->step_credits=0;queue(s,"{\"ok\":true,\"paused\":%s}\n",s->paused?"true":"false");return;}
    if(!strcmp(op,"step")){char*v=d_strtok(NULL," \t\r",&save);uint64_t n=1;if((v&&(!number(v,&n)||!n||n>100000))||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"step count must be 1..100000\"}\n");return;}if(n>100000u-s->step_credits){queue(s,"{\"ok\":false,\"error\":\"step queue full (limit 100000)\"}\n");return;}s->paused=1;s->step_credits+=n;queue(s,"{\"ok\":true,\"queued\":%llu}\n",(unsigned long long)n);return;}
    if(!strcmp(op,"quit")){if(!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"quit takes no arguments\"}\n");return;}s->quit=1;queue(s,"{\"ok\":true}\n");return;}
    if(!strcmp(op,"capture")){char*path=d_strtok(NULL," \t\r",&save);if(!path||!s->capture||!no_more(&save)){queue(s,"{\"ok\":false,\"error\":\"capture callback unavailable or path missing\"}\n");return;}int ok=s->capture(s->user,path,s->machine);queue(s,"{\"ok\":%s}\n",ok?"true":"false");return;}
    queue(s,"{\"ok\":false,\"error\":\"unknown command\"}\n");
}

SegaCDDebugServer *segacd_debug_start(SegaCDMachine*m,uint16_t port,SegaCDDebugWriteLog log_write,SegaCDDebugCapture capture,void*user,char*error,size_t error_size)
{
    if(!m){error_text(error,error_size,"machine is null");return NULL;}
    SegaCDDebugServer*s=(SegaCDDebugServer*)calloc(1,sizeof *s);if(!s){error_text(error,error_size,"allocation failed");return NULL;}s->listener=s->client=D_INVALID;s->machine=m;s->log_write=log_write;s->capture=capture;s->user=user;
#ifdef _WIN32
    WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)){error_text(error,error_size,"WSAStartup failed");free(s);return NULL;}s->winsock=1;
#endif
    s->listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s->listener==D_INVALID){error_text(error,error_size,"socket failed");segacd_debug_stop(s);return NULL;}
    int yes=1;setsockopt(s->listener,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof yes);
    struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);
    if(bind(s->listener,(struct sockaddr*)&a,sizeof a)||listen(s->listener,1)||!nonblocking(s->listener)){error_text(error,error_size,"loopback bind/listen failed");segacd_debug_stop(s);return NULL;}
#ifdef _WIN32
    int alen=(int)sizeof a;
#else
    socklen_t alen=(socklen_t)sizeof a;
#endif
    if(getsockname(s->listener,(struct sockaddr*)&a,&alen)){error_text(error,error_size,"getsockname failed");segacd_debug_stop(s);return NULL;}s->port=ntohs(a.sin_port);return s;
}
void segacd_debug_stop(SegaCDDebugServer*s)
{if(!s)return;close_client(s);if(s->listener!=D_INVALID)d_close(s->listener);
#ifdef _WIN32
 if(s->winsock)WSACleanup();
#endif
 free(s);}
uint16_t segacd_debug_port(const SegaCDDebugServer*s){return s?s->port:0;}
void segacd_debug_poll(SegaCDDebugServer*s)
{
    if(!s)return;
    if(s->client==D_INVALID){struct sockaddr_in a;
#ifdef _WIN32
        int n=(int)sizeof a;
#else
        socklen_t n=(socklen_t)sizeof a;
#endif
        DSocket c=accept(s->listener,(struct sockaddr*)&a,&n);if(c!=D_INVALID){if(ntohl(a.sin_addr.s_addr)!=INADDR_LOOPBACK||!nonblocking(c))d_close(c);else{
#if defined(SO_NOSIGPIPE)
            int no_sigpipe=1;(void)setsockopt(c,SOL_SOCKET,SO_NOSIGPIPE,(const char*)&no_sigpipe,sizeof no_sigpipe);
#endif
            s->client=c;s->input_size=s->output_size=s->output_sent=0;}}
    }
    if(s->client==D_INVALID)return;
    /* A continuous client must yield back to simulation and window events. */
    char chunk[1024];for(unsigned budget=0;budget<16;++budget){
#ifdef _WIN32
        int n=recv(s->client,chunk,sizeof chunk,0);
#else
        ssize_t n=recv(s->client,chunk,sizeof chunk,0);
#endif
        if(n==0){close_client(s);return;}if(n<0){if(!d_would_block())close_client(s);break;}
        for(int i=0;i<n&&s->client!=D_INVALID;++i){
            char c=chunk[i];
            if(c=='\n'){
                if(s->discard_line)s->discard_line=0;
                else {s->input[s->input_size]=0;command(s,s->input);}
                s->input_size=0;
            } else if(s->discard_line) {
                continue;
            } else if(c=='\0') {
                queue(s,"{\"ok\":false,\"error\":\"NUL byte in command line\"}\n");
                if(s->client!=D_INVALID)s->discard_line=1;
                s->input_size=0;
            } else if(s->input_size+1>=sizeof s->input) {
                queue(s,"{\"ok\":false,\"error\":\"command line too long\"}\n");
                if(s->client!=D_INVALID)s->discard_line=1;
                s->input_size=0;
            } else s->input[s->input_size++]=c;
        }
        if(n<(int)sizeof chunk)break;
    }
    while(s->client!=D_INVALID&&s->output_sent<s->output_size){
#ifdef _WIN32
        int n=send(s->client,s->output+s->output_sent,(int)(s->output_size-s->output_sent),0);
#else
#ifdef MSG_NOSIGNAL
        ssize_t n=send(s->client,s->output+s->output_sent,s->output_size-s->output_sent,MSG_NOSIGNAL);
#else
        ssize_t n=send(s->client,s->output+s->output_sent,s->output_size-s->output_sent,0);
#endif
#endif
        if(n<0){if(!d_would_block())close_client(s);break;}if(!n)break;s->output_sent+=(size_t)n;
    }
}
int segacd_debug_should_run_frame(SegaCDDebugServer*s)
{if(!s)return 1;if(!s->paused)return 1;if(s->step_credits){--s->step_credits;return 1;}return 0;}
int segacd_debug_quit_requested(const SegaCDDebugServer*s){return s?s->quit:0;}
uint16_t segacd_debug_pad(const SegaCDDebugServer*s,unsigned port)
{return s&&port<2?s->pad[port]:0;}
void segacd_debug_pause(SegaCDDebugServer*s,int paused)
{if(s){s->paused=paused!=0;if(!s->paused)s->step_credits=0;}}
int segacd_debug_is_paused(const SegaCDDebugServer*s)
{return s?s->paused:0;}
