/* Exercise the actual production command bridge without a desktop/game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
static int WSAAPI capture_send(SOCKET socket,const char *data,int bytes,int flags);
#else
#include <sys/socket.h>
static ssize_t capture_send(int socket,const void *data,size_t bytes,int flags);
#endif
#define send capture_send
#include "../../runner/cmd_server_stub.c"
#undef send
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;}}while(0)
uint8_t g_ram[65536];
static char reply[16384];static size_t reply_length;
static int loads,saves,shots,online;
int runner_load_state_file(const char *path){++loads;return !strcmp(path,"current.state");}
int runner_save_state_file(const char *path){++saves;return !strcmp(path,"current.state");}
int runner_write_screenshot_file(const char *path){++shots;return !strcmp(path,"current.png");}
static int is_online(void){return online;}
#ifdef _WIN32
static int WSAAPI capture_send(SOCKET socket,const char *data,int bytes,int flags)
#else
static ssize_t capture_send(int socket,const void *data,size_t bytes,int flags)
#endif
{
    (void)socket;(void)flags;
    if((size_t)bytes>=sizeof reply-reply_length)return -1;
    memcpy(reply+reply_length,data,(size_t)bytes);reply_length+=(size_t)bytes;reply[reply_length]=0;
    return bytes;
}
static CmdResult command(const char *json)
{ reply_length=0;reply[0]=0;return review_dispatch(json); }
int main(void)
{
    const char *off[]={NULL,"0","true","01","1 "};
    for(unsigned n=0;n<sizeof off/sizeof *off;++n) {
        cmd_server_owner_review_init(off[n]);cmd_server_init(0);
        CHECK(review_listen==REVIEW_INVALID && !cmd_server_is_paused() && cmd_server_owner_review_can_advance());
    }
    review_client=(ReviewSocket)1;
    command("{\"cmd\":\"owner_review\",\"action\":\"step\"}");CHECK(strstr(reply,"GENESIS_OWNER_REVIEW=1"));
    cmd_server_owner_review_init("1");CHECK(cmd_server_is_paused() && !cmd_server_owner_review_can_advance());
    command("{\"id\":7,\"cmd\":\"owner_review\",\"action\":\"step\",\"frames\":2}");
    CHECK(!reply_length && review.remaining==2 && cmd_server_owner_review_can_advance());
    cmd_server_owner_review_frame_done(1);CHECK(!reply_length && review.remaining==1);
    cmd_server_owner_review_frame_done(2);
    CHECK(strstr(reply,"\"frames_run\":2") && strstr(reply,"\"paused\":true") && strstr(reply,"\"id\":7"));
    CHECK(!cmd_server_owner_review_can_advance());
    cmd_server_owner_review_frame_done(3);CHECK(review.remaining==0);
    command("{\"cmd\":\"owner_review\",\"action\":\"step\"}");CHECK(review.remaining==1);
    command("{\"cmd\":\"owner_review\",\"action\":\"step\"}");CHECK(strstr(reply,"already pending"));
    cmd_server_owner_review_frame_done(4);
    const char *invalid[]={"0","-1","9","2.5","\"2\"","999999999999999999999999"};
    for(unsigned n=0;n<sizeof invalid/sizeof *invalid;++n) {
        char request[192];snprintf(request,sizeof request,"{\"cmd\":\"owner_review\",\"action\":\"step\",\"frames\":%s}",invalid[n]);
        command(request);CHECK(strstr(reply,"1-8") && !review.remaining);
    }
    command("{\"cmd\":\"owner_review\",\"action\":\"play\"}");CHECK(!cmd_server_is_paused() && strstr(reply,"\"paused\":false"));
    command("{\"cmd\":\"owner_review\",\"action\":\"pause\"}");CHECK(cmd_server_is_paused());
    command("{\"cmd\":\"load_state\",\"path\":\"current.state\"}");CHECK(loads==1 && cmd_server_is_paused());
    command("{\"cmd\":\"screenshot\",\"path\":\"current.png\"}");CHECK(shots==1);
    command("{\"cmd\":\"save_state\",\"path\":\"current.state\"}");CHECK(saves==1);
    g_ram[65535]=0xAB;g_ram[0]=0xCD;
    CHECK(command("{\"cmd\":\"set_input\",\"keys\":\"off\"}").input_release);
    CHECK(!command("{\"cmd\":\"set_input\",\"keys\":\"FF\"}").input_override);
    command("{\"cmd\":\"read_ram\",\"addr\":\"FFFF\",\"size\":2}");CHECK(strstr(reply,"\"hex\":\"ABCD\""));
    g_cmd_server_online=is_online;online=1;
    command("{\"cmd\":\"owner_review\",\"action\":\"step\"}");CHECK(strstr(reply,"netplay session") && !review.remaining);
    command("{\"cmd\":\"load_state\",\"path\":\"current.state\"}");CHECK(loads==1 && strstr(reply,"netplay session"));
    CHECK(!cmd_server_is_paused() && cmd_server_owner_review_can_advance());online=0;
    command("{\"cmd\":\"pause\"}");CHECK(strstr(reply,"unavailable"));
    CHECK(command("{\"cmd\":\"quit\"}").should_quit);
    puts("Owner review opt-in, full-frame budget, command responses, state actions and online guard PASS");return 0;
}
