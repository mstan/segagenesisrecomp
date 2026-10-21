#include "../../runner/cd/debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
typedef SOCKET TestSocket;
#define TEST_INVALID INVALID_SOCKET
#define test_close closesocket
#define set_receive_timeout(s) do { DWORD timeout_ms=2000; if(setsockopt((s),SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout_ms,sizeof timeout_ms))fail("receive timeout"); } while(0)
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int TestSocket;
#define TEST_INVALID (-1)
#define test_close close
#define set_receive_timeout(s) do { struct timeval timeout={2,0}; if(setsockopt((s),SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout))fail("receive timeout"); } while(0)
#endif

uint8_t g_ram[0x10000];

/* debug.c's pad command is intentionally outside this read-only test. */
void segacd_machine_pad(SegaCDMachine *machine,unsigned port,uint16_t buttons)
{(void)machine;(void)port;(void)buttons;}

static void fail(const char *message)
{fprintf(stderr,"debug test failed: %s\n",message);exit(1);}

int main(void)
{
    static SegaCDMachine machine;memset(&machine,0,sizeof machine);
    machine.frames=91;machine.bus_clock=0x12345678u;
    machine.coordinator.cpu[SEGACD_MAIN].state.regs.PC=0x123456u;
    machine.coordinator.cpu[SEGACD_MAIN].state.regs.SR=0x2700u;
    machine.coordinator.cpu[SEGACD_MAIN].state.regs.D[0]=0x89ABCDEFu;
    machine.coordinator.cpu[SEGACD_MAIN].state.regs.A[7]=0x00FF1234u;
    machine.coordinator.cpu[SEGACD_MAIN].state.irq_pins=4;
    machine.coordinator.cpu[SEGACD_SUB].state.regs.PC=0x05E8u;
    machine.coordinator.cpu[SEGACD_SUB].state.regs.D[3]=0xFEDCBA98u;
    machine.coordinator.cpu[SEGACD_SUB].state.stopped=1;
    machine.coordinator.interrupts[SEGACD_SUB][2]=3;
    machine.coordinator.last_interrupt_level[SEGACD_SUB]=2;
    machine.coordinator.last_interrupt_vector[SEGACD_SUB]=26;
    machine.coordinator.last_interrupt_pc[SEGACD_SUB]=0x000005E8u;
    machine.coordinator.last_interrupt_target[SEGACD_SUB]=0x00005F2u;
    machine.coordinator.main_irq=6;machine.irq4=1;machine.irq6=1;
    machine.vdp.reg[0]=0x12;machine.vdp.reg[1]=0x34;
    machine.vdp.scanline=0x123;machine.vdp.vint_pending=1;
    machine.coordinator.busy[SEGACD_SUB]=1;machine.coordinator.waits=3;
    machine.coordinator.wait_address=0x0A12003u;machine.coordinator.wait_cpu=SEGACD_MAIN;
    machine.coordinator.wait_result=M68K_STEP_NO_WAIT_HANDLER;
    machine.devices.sub_cycles=0x23456789u;
    machine.devices.memory_mode=5u;
    machine.devices.main_control=1u;machine.devices.prg_bank=2u;
    machine.devices.command[0]=0x1234u;machine.devices.status[7]=0xABCDu;
    machine.devices.cdc.transfer_active=1;machine.devices.cdc.remaining=17;
    machine.devices.cdc.fifo_count=2;machine.devices.cdc.ifstat=0x78;
    machine.devices.drive.command_pending=1;machine.devices.drive.seek_target=0x4567;
    machine.devices.dma.active=1;machine.devices.stamp.busy=1;
    machine.z80.state.pc=0xC123u;machine.z80.state.halted=1;
    machine.z80.state.iff1=1;machine.z80.state.im=2;machine.z80.irq_pending=1;
    machine.coordinator.cdd_submission_total=70;
    for(unsigned i=0;i<SEGACD_CDD_TRACE_CAPACITY;++i) {
        machine.coordinator.cdd_submissions[i].device_clock=UINT64_MAX;
        machine.coordinator.cdd_submissions[i].command_deadline=UINT64_MAX;
        machine.coordinator.cdd_submissions[i].instruction_pc=0xA00000u+i;
        memset(machine.coordinator.cdd_submissions[i].packet,15,10);
    }

    char error[128];SegaCDDebugServer*server=segacd_debug_start(&machine,0,NULL,NULL,NULL,error,sizeof error);
    if(!server){fprintf(stderr,"debug server start failed: %s\n",error);return 1;}
#ifdef _WIN32
    TestSocket client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
#else
    TestSocket client=socket(AF_INET,SOCK_STREAM,0);
#endif
    if(client==TEST_INVALID)fail("client socket");
    set_receive_timeout(client);
    struct sockaddr_in address;memset(&address,0,sizeof address);
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(segacd_debug_port(server));
    if(connect(client,(struct sockaddr*)&address,sizeof address))fail("connect");
    const char request[]="inspect unexpected\ninspect\n";
#ifdef _WIN32
    if(send(client,request,(int)sizeof request-1,0)!=(int)sizeof request-1)fail("send");
#else
    if(send(client,request,sizeof request-1,0)!=(ssize_t)(sizeof request-1))fail("send");
#endif
    SegaCDMachine *machine_before=(SegaCDMachine*)malloc(sizeof machine);
    uint8_t *ram_before=(uint8_t*)malloc(sizeof g_ram);
    if(!machine_before||!ram_before)fail("snapshot allocation");
    memcpy(machine_before,&machine,sizeof machine);memcpy(ram_before,g_ram,sizeof g_ram);
    segacd_debug_poll(server);
    char response[32768];size_t response_size=0;unsigned lines=0;
    while(lines<2&&response_size+1<sizeof response) {
#ifdef _WIN32
        int received=recv(client,response+response_size,(int)(sizeof response-response_size-1),0);
#else
        ssize_t received=recv(client,response+response_size,sizeof response-response_size-1,0);
#endif
        if(received<=0)fail("response");
        for(int i=0;i<received;++i)if(response[response_size+(size_t)i]=='\n')++lines;
        response_size+=(size_t)received;
    }
    response[response_size]=0;
    if(lines!=2)fail("two command responses");
    if(!strstr(response,"\"error\":\"inspect takes no arguments\""))fail("extra arguments accepted");
    if(!strstr(response,"\"pc\":\"123456\",\"sr\":\"2700\""))fail("Main PC/SR snapshot");
    if(!strstr(response,"\"89ABCDEF\"")||!strstr(response,"\"00FF1234\""))fail("Main D/A registers");
    if(!strstr(response,"\"pc\":\"0005E8\"")||!strstr(response,"\"FEDCBA98\"")||!strstr(response,"\"stopped\":true"))fail("Sub snapshot");
    if(!strstr(response,"\"waits\":3,\"wait_address\":\"A12003\"")||!strstr(response,"\"wait_result\":\"no_wait_handler\""))fail("scheduler wait state");
    if(!strstr(response,"\"accepted\":[[0,0,0,0,0,0,0,0],[0,0,3,0,0,0,0,0]]"))fail("accepted interrupt counters");
    if(!strstr(response,"\"level\":2,\"vector\":26,\"entry_pc\":\"0005E8\",\"target_pc\":\"0005F2\"}"))fail("last interrupt entry/target");
    if(!strstr(response,"\"host\":{\"irq4\":1,\"irq6\":1,\"main_level\":6},\"vdp\":{\"reg0\":18,\"reg1\":52,\"scanline\":291,\"vint_pending\":1}"))fail("host/VPD interrupt state");
    if(!strstr(response,"\"memory_mode\":5,\"word_ram_1m\":true,\"word_ram_ret\":true")||!strstr(response,"\"word_ram_owner\":\"split_1m\""))fail("Gate Array state");
    if(!strstr(response,"\"active\":true,\"end\":false,\"fifo_count\":2,\"remaining\":17"))fail("CDC transfer state");
    if(!strstr(response,"\"seek_target\":17767")||!strstr(response,"\"dma\":{\"active\":true")||!strstr(response,"\"stamp\":{\"busy\":true"))fail("device busy state");
    if(!strstr(response,"\"z80\":{\"pc\":\"C123\",\"halted\":true,\"iff1\":true"))fail("Z80 interrupt state");
    if(!strstr(response,"\"cdd_submissions\":{\"total\":70")||
       !strstr(response,"\"pc\":\"A00006\"")||!strstr(response,"\"packet\":\"FFFFFFFFFF\""))fail("full CDD history snapshot");
    if(memcmp(machine_before,&machine,sizeof machine)||memcmp(ram_before,g_ram,sizeof g_ram))fail("inspect mutated machine state");
    free(machine_before);free(ram_before);test_close(client);segacd_debug_stop(server);
    printf("segacd debug snapshot PASS (SegaCDMachine=%zu bytes)\n",sizeof machine);return 0;
}
