/* Small opt-in control path for trace-free interactive review builds.
 * It deliberately owns no observation/history ring or legacy fast stepping. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET ReviewSocket;
#define REVIEW_INVALID INVALID_SOCKET
#define review_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
typedef int ReviewSocket;
#define REVIEW_INVALID (-1)
#define review_close close
#endif
#include <stdio.h>
#include <stdlib.h>
#include "owner_review.h"
extern uint8_t g_ram[65536];
int runner_save_state_file(const char *path);
int runner_load_state_file(const char *path);
int runner_write_screenshot_file(const char *path);
static ReviewSocket review_listen=REVIEW_INVALID,review_client=REVIEW_INVALID;
static GenesisOwnerReview review;
static char review_recv[8192];
static int review_length,review_step_id=-1;
static uint32_t review_frame;
static int review_online(void)
{ return g_cmd_server_online && g_cmd_server_online(); }
static void review_nonblocking(ReviewSocket socket)
{
#ifdef _WIN32
    u_long on=1;ioctlsocket(socket,FIONBIO,&on);
#else
    fcntl(socket,F_SETFL,fcntl(socket,F_GETFL,0)|O_NONBLOCK);
#endif
}
void cmd_send_response(const char *json)
{
    if(review_client==REVIEW_INVALID)return;
    send(review_client,json,(int)strlen(json),0);send(review_client,"\n",1,0);
}
void cmd_send_err(int id,const char *message)
{
    char response[512];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":false,\"error\":\"%s\"}",id,message);
    cmd_send_response(response);
}
static void review_reply(int id,unsigned stepped)
{
    char response[224];snprintf(response,sizeof response,
        "{\"id\":%d,\"ok\":true,\"owner_review\":true,\"paused\":%s,\"frames_run\":%u,\"current_frame\":%u}",
        id,review.paused?"true":"false",stepped,review_frame);cmd_send_response(response);
}
static const char *review_value(const char *json,const char *key)
{
    char pattern[64];snprintf(pattern,sizeof pattern,"\"%s\"",key);
    const char *p=strstr(json,pattern);if(!p)return NULL;p+=strlen(pattern);
    while(*p==' ' || *p=='\t' || *p==':')++p;
    return p;
}
static int review_string(const char *json,const char *key,char *out,size_t capacity)
{
    const char *p=review_value(json,key);if(!p || *p++!='\"')return 0;
    size_t n=0;while(*p && *p!='\"' && n+1<capacity)out[n++]=*p++;
    out[n]=0;return *p=='\"';
}
static int review_integer(const char *json,const char *key,int fallback)
{ const char *p=review_value(json,key);return p?atoi(p):fallback; }
static CmdResult review_dispatch(const char *json)
{
    CmdResult cr={0};char cmd[32]={0};int id=review_integer(json,"id",0);
    if(!review.enabled){cmd_send_err(id,"owner review requires GENESIS_OWNER_REVIEW=1");return cr;}
    if(!review_string(json,"cmd",cmd,sizeof cmd)){cmd_send_err(id,"missing cmd");return cr;}
    if(review_online() && strcmp(cmd,"ping") && strcmp(cmd,"read_ram") &&
       strcmp(cmd,"screenshot") && strcmp(cmd,"quit")) {
        cmd_send_err(id,"refused: a netplay session owns execution (read-only queries only)");return cr;
    }
    if(!strcmp(cmd,"owner_review")) {
        char action[16]={0};review_string(json,"action",action,sizeof action);
        int result=genesis_owner_review_request(&review,action,genesis_owner_review_frames(review_value(json,"frames")),review_online());
        if(result==OWNER_REVIEW_BUSY)cmd_send_err(id,"owner review step already pending");
        else if(result!=OWNER_REVIEW_OK)cmd_send_err(id,"owner review action must be pause/play/step; step frames must be 1-8 offline");
        else if(!strcmp(action,"step"))review_step_id=id;
        else review_reply(id,0);
    } else if(!strcmp(cmd,"set_input")) {
        char keys[16]={0};review_string(json,"keys",keys,sizeof keys);
        if(strcmp(keys,"off") && strcmp(keys,"release"))cmd_send_err(id,"only input release is available in owner review");
        else {cr.input_release=true;review_reply(id,0);}
    } else if(!strcmp(cmd,"load_state") || !strcmp(cmd,"save_state") || !strcmp(cmd,"screenshot")) {
        char path[512];if(!review_string(json,"path",path,sizeof path)){cmd_send_err(id,"missing path");return cr;}
        int ok=!strcmp(cmd,"load_state")?runner_load_state_file(path):
            !strcmp(cmd,"save_state")?runner_save_state_file(path):runner_write_screenshot_file(path);
        if(ok){char response[96];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true}",id);cmd_send_response(response);}
        else cmd_send_err(id,"file action failed");
    } else if(!strcmp(cmd,"read_ram")) {
        char address[32];int size=review_integer(json,"size",16);
        if(!review_string(json,"addr",address,sizeof address) || size<1 || size>4096) {cmd_send_err(id,"addr and size1-4096 required");return cr;}
        unsigned offset=(unsigned)strtoul(address,NULL,16)&65535u;char response[8448];
        int n=snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true,\"addr\":\"0x%04X\",\"size\":%d,\"hex\":\"",id,offset,size);
        for(int i=0;i<size;++i)n+=snprintf(response+n,sizeof response-(size_t)n,"%02X",g_ram[(offset+(unsigned)i)&65535u]);
        snprintf(response+n,sizeof response-(size_t)n,"\"}");cmd_send_response(response);
    } else if(!strcmp(cmd,"ping") || !strcmp(cmd,"quit")) {
        char response[128];snprintf(response,sizeof response,"{\"id\":%d,\"ok\":true,\"frame\":%u}",id,review_frame);cmd_send_response(response);
        cr.should_quit=!strcmp(cmd,"quit");
    } else cmd_send_err(id,"command unavailable in trace-free owner review");
    return cr;
}
void cmd_server_owner_review_init(const char *value)
{ genesis_owner_review_init(&review,value);review_step_id=-1;review_frame=0; }
bool cmd_server_is_paused(void)
{ return review.enabled && review.paused && !review_online(); }
bool cmd_server_owner_review_can_advance(void)
{ return !review.enabled || !review.paused || review.remaining || review_online(); }
void cmd_server_owner_review_frame_done(uint32_t completed_frames)
{
    if(!review.enabled)return;
    review_frame=completed_frames;
    unsigned frames=genesis_owner_review_frame_done(&review);
    if(frames && review_step_id>=0){review_reply(review_step_id,frames);review_step_id=-1;}
}
void cmd_server_init(int port)
{
    if(!review.enabled)return;
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return;
#endif
    review_listen=socket(AF_INET,SOCK_STREAM,0);if(review_listen==REVIEW_INVALID)return;
    int yes=1;setsockopt(review_listen,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof yes);
    struct sockaddr_in address;memset(&address,0,sizeof address);address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((uint16_t)port);
    if(bind(review_listen,(struct sockaddr*)&address,sizeof address) || listen(review_listen,1)) {
        review_close(review_listen);review_listen=REVIEW_INVALID;return;
    }
    review_nonblocking(review_listen);fprintf(stderr,"[owner_review] Listening on 127.0.0.1:%d; paused before frame 0\n",port);
}
CmdResult cmd_server_poll(void)
{
    CmdResult cr={0};if(review_listen==REVIEW_INVALID)return cr;
    if(review_client==REVIEW_INVALID) {
        review_client=accept(review_listen,NULL,NULL);
        if(review_client!=REVIEW_INVALID){review_nonblocking(review_client);review_length=0;}
    }
    if(review_client==REVIEW_INVALID)return cr;
    int space=(int)sizeof review_recv-review_length-1;
    if(!space){cmd_send_err(0,"request too long");review_close(review_client);review_client=REVIEW_INVALID;return cr;}
    int count=recv(review_client,review_recv+review_length,space,0);
    if(!count){review_close(review_client);review_client=REVIEW_INVALID;review_length=0;return cr;}
    if(count>0){review_length+=count;review_recv[review_length]=0;}
    char *newline;
    while((newline=strchr(review_recv,'\n'))!=NULL) {
        *newline=0;CmdResult next=review_dispatch(review_recv);if(next.should_quit)cr.should_quit=true;
        if(next.input_release)cr.input_release=true;
        int used=(int)(newline-review_recv)+1;review_length-=used;memmove(review_recv,newline+1,(size_t)review_length+1);
    }
    return cr;
}
void cmd_server_shutdown(void)
{
    if(review_client!=REVIEW_INVALID)review_close(review_client);
    if(review_listen!=REVIEW_INVALID)review_close(review_listen);
    review_client=review_listen=REVIEW_INVALID;review_length=0;
#ifdef _WIN32
    if(review.enabled)WSACleanup();
#endif
}
