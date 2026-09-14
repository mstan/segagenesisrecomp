#!/usr/bin/env python3
"""Independent REV00 Kosinski/Enigma/layout decode versus captured stage RAM.

Formats follow canonical s1disasm KosDec, EniDec and LevelLayoutLoad2, not
generated C or the custom renderer. Takes the owner's ROM; ships no game data.
"""
import argparse
from pathlib import Path

def kosinski(src):
    pos=2;desc=int.from_bytes(src[:2],"little");bits=16;out=bytearray()
    def bit():
        nonlocal pos,desc,bits
        value=desc&1;desc>>=1;bits-=1
        if not bits:desc=int.from_bytes(src[pos:pos+2],"little");pos+=2;bits=16
        return value
    def byte():
        nonlocal pos
        value=src[pos];pos+=1;return value
    while True:
        if bit():out.append(byte());continue
        if bit():
            lo=byte();hi=byte();offset=((hi&248)<<5|lo)-8192;count=hi&7
            if not count:
                count=byte()
                if not count:return bytes(out)
                if count==1:continue
                count+=1
            else:count+=2
        else:count=(bit()<<1|bit())+2;offset=byte()-256
        assert -len(out)<=offset<0,(pos,offset,len(out))
        for _ in range(count):out.append(out[offset])

def enigma(src):
    inline=src[0];flags=src[1];inc=int.from_bytes(src[2:4],"big");common=int.from_bytes(src[4:6],"big");pos=48;out=[]
    def take(n):
        nonlocal pos
        value=0
        for _ in range(n):value=value*2+((src[pos//8]>>(7-pos%8))&1);pos+=1
        return value
    def literal():
        attr=0
        for b in range(4,-1,-1):
            if flags&(1<<b):attr|=take(1)<<(11+b)
        return attr|take(inline)
    while True:
        if not take(1):
            kind=take(1);count=take(4)+1
            for _ in range(count):
                out.append(common if kind else inc)
                if not kind:inc=(inc+1)&65535
        else:
            kind=take(2);count=take(4)+1
            if kind==3 and count==16:break
            value=literal()
            for i in range(count):
                out.append(value)
                if kind==3 and i+1<count:value=literal()
                elif kind==1:value=(value+1)&65535
                elif kind==2:value=(value-1)&65535
    return b"".join(v.to_bytes(2,"big") for v in out)

def expected_stage(rom,zone,act):
    head=0x1D5A2+zone*16
    def ptr(a):return int.from_bytes(rom[a:a+4],"big")&0xFFFFFF
    chunks=kosinski(rom[ptr(head+8):]);blocks=enigma(rom[ptr(head+4):])
    layout=bytearray(1024)
    for plane in range(2):
        idx=0x68BD6+zone*24+act*6+plane*2
        p=0x68BD6+int.from_bytes(rom[idx:idx+2],"big",signed=True)
        w,h=rom[p]+1,rom[p+1]+1;p+=2
        for y in range(h):layout[y*128+plane*64:y*128+plane*64+w]=rom[p:p+w];p+=w
    return (("chunks",0,chunks),("blocks",0xB000,blocks),("layout",0xA400,bytes(layout)))

def main():
    ap=argparse.ArgumentParser();ap.add_argument("--rom",type=Path,required=True);ap.add_argument("captures",nargs="+",type=Path);a=ap.parse_args()
    rom=a.rom.read_bytes();failed=False
    for path in a.captures:
        ram=path.read_bytes();zone,act=ram[0xFE10:0xFE12]
        print(path,zone,act)
        for name,base,data in expected_stage(rom,zone,act):
            differences=[i for i,v in enumerate(data) if v!=ram[base+i]]
            print(name,len(data),"mismatches",len(differences),"first",[(hex(base+i),hex(data[i]),hex(ram[base+i])) for i in differences[:12]])
            failed|=bool(differences)
    raise SystemExit(int(failed))

if __name__=="__main__":main()
