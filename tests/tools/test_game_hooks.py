#!/usr/bin/env python3
"""Execute sparse instruction hooks in real generated C; no game ROM needed."""
import argparse
from pathlib import Path
import tempfile
from test_stack_skip import CONFIG, HARNESS, run

HOOK_HARNESS = HARNESS.split("int main(")[0] + r'''
static int hook_mode, hook_calls;
int genesis_game_instruction_hook(uint32_t pc) {
    if (pc!=0x242 && pc!=0x282) abort();
    if (g_cpu.D[1]!=1) abort(); /* before the configured ADDQ, after MOVEQ */
    ++hook_calls;
    if (!hook_mode) return 0;
    g_cpu.D[1]=40;
    return hook_mode==2;
}
int main(int argc,char **argv) {
    (void)argc; hook_mode=atoi(argv[1]);
    g_cpu.A[7]=0xFFFE00;g_cpu.SR=0x2700;
    recomp_call_addr(0x200);
    unsigned expected=hook_mode==0?2:hook_mode==1?41:40;
    if (g_cpu.D[0]!=42 || g_cpu.D[1]!=expected || hook_calls!=1 ||
        g_cpu.A[7]!=0xFFFE00 || pending) {
        fprintf(stderr,"hook=%d calls=%d D0=%u D1=%u SP=%x pending=%d\n",
                hook_mode,hook_calls,g_cpu.D[0],g_cpu.D[1],g_cpu.A[7],pending);
        return 1;
    }
    return 0;
}
'''

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--recompiler",type=Path,required=True)
    ap.add_argument("--cc",type=Path,required=True)
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="genesis-game-hooks-") as temp:
        td=Path(temp)
        (td/"harness.c").write_text(HOOK_HARNESS)
        for split in (False,True):
            site=0x282 if split else 0x242
            config=CONFIG.replace("0x2C0, 0x2E0, 0x2F0", "0x284")
            config+=f'\n[[widescreen_site]]\naddr = {site}\nkind = "game_hook"\n'
            (td/"game.toml").write_text(config)
            rom=bytearray(b"\xff"*0x400)
            rom[:8]=bytes.fromhex("00fffe00 00000200")
            rom[0x100:0x200]=b" "*256;rom[0x100:0x104]=b"SEGA"
            rom[0x200:0x20A]=bytes.fromhex("4eb9 00000240 702a 4e75")
            rom[0x280:0x286]=bytes.fromhex("7201 5241 4e75")
            rom[0x240:0x246]=bytes.fromhex("4ef9 00000280" if split else "7201 5241 4e75")
            (td/"test.bin").write_bytes(rom)
            run([args.recompiler.resolve(),"test.bin","--game","game.toml","--output-dir","generated"],td)
            sources=sorted((td/"generated").glob("skip_part*.c"))+[td/"generated/skip_dispatch.c",td/"harness.c"]
            # Exactly one call is emitted: no blanket per-instruction callback.
            count=sum(p.read_text().count("genesis_game_instruction_hook(") for p in sources[:-1])
            if count!=1:raise RuntimeError(f"expected one sparse hook, found {count}")
            exe=td/"test.exe"
            run([args.cc.resolve(),"-std=c11","-O1","-I",root/"runner/include","-I",root/"runner",*sources,"-o",exe],td)
            for mode in (0,1,2):
                run([exe,str(mode)],td)
                print(f"PASS game hook mode={mode} split={split}")

if __name__=="__main__":main()
