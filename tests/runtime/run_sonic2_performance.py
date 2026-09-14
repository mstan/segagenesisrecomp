#!/usr/bin/env python3
"""Free-running input-only gameplay cadence probe, using the always-on ring.

Report actual level updates, not just VBlank/output FPS. Never patch RAM,
pause, step, or raise the wall-frame rate as a gameplay fix.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from run_sonic1_custom_video import command
from run_sonic2_custom_video import timeline


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--exe',type=Path,required=True)
    ap.add_argument('--rom',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--zone',default='ehz',choices=['ehz','cpz','arz','cnz','htz','mcz','ooz','mtz','wfz'])
    ap.add_argument('--mode',default='2676:374')
    ap.add_argument('--require-full-rate',action='store_true')
    ap.add_argument('--realtime',action='store_true',help='use real video/audio at NTSC speed, not dummy devices')
    ap.add_argument('--window-size',nargs=2,type=int,metavar=('WIDTH','HEIGHT'))
    args=ap.parse_args()
    # Never attach to an owner's existing game or mix two runtimes' evidence.
    try:
        existing=socket.create_connection(('127.0.0.1',4442),timeout=.3)
    except OSError:
        pass
    else:
        existing.close()
        raise RuntimeError('Port 4442 is in use; close the existing game with owner approval first')
    case=args.out.resolve();case.mkdir(parents=True,exist_ok=True)
    # Same real-input route as visual regression, without capture IO in timings.
    script='\n'.join(line for line in timeline(args.zone).splitlines()
                     if not line.startswith(('SCREENSHOT','DUMP_')))+'\n'
    script_path=case/'input.txt';script_path.write_text(script)
    env=os.environ.copy()
    if not args.realtime:
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    frames=3200 if args.zone=='ehz' else 5200
    argv=[str(args.exe.resolve()),str(args.rom.resolve()),'--no-launcher','--port','4442',
          '--target-fps','59.94' if args.realtime else '1000','--max-frames',str(frames),'--widescreen',args.mode,
          '--input-script',str(script_path)]
    fields=['game_mode','wram[FE05]','wram[F711]','sonic.routine','wram16[F63A]','internal_frame']
    rows={};sock=None;last=-1;video={};audio={};wall_samples=[];configured=False
    start=time.perf_counter()
    with (case/'run.log').open('w') as log:
        proc=subprocess.Popen(argv,cwd=case,env=env,stdout=log,stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        try:
            while proc.poll() is None:
                if time.perf_counter()-start>240:raise RuntimeError('performance route timed out')
                if sock is None:
                    try:sock=socket.create_connection(('127.0.0.1',4442),timeout=1);sock.settimeout(5)
                    except OSError:time.sleep(.03);continue
                try:
                    info=command(sock,{'cmd':'frame_info'})
                    if args.window_size and not configured:
                        w,h=args.window_size
                        reply=command(sock,{'cmd':'video_configure','mode':args.mode,'window_width':w,'window_height':h})
                        assert not reply.get('error'),reply
                        configured=True
                    video=command(sock,{'cmd':'custom_video'})
                    end=info['current_frame']-1
                    begin=max(last+1,info['oldest_frame']+8)
                    if end-begin<160:time.sleep(.05);continue
                    if info['current_frame']>=1000:
                        wall_samples.append((info['current_frame'],time.perf_counter()))
                    audio=command(sock,{'cmd':'audio_stats'})
                    assert last<0 or begin==last+1, ('probe fell behind ring',last,begin)
                    columns=[command(sock,{'cmd':'frame_timeseries','field':f,'from':begin,'to':end})['values'] for f in fields]
                    for i,n in enumerate(range(begin,end+1)):
                        row=[col[i] for col in columns]
                        if all(v is not None for v in row):rows[n]=row
                    last=end
                except (OSError,RuntimeError):
                    # Socket shutdown precedes process exit on Windows.
                    try:proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:raise RuntimeError('debug socket lost while game is still running')
                    break
            proc.wait(timeout=15)
        finally:
            if sock:sock.close()
            if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
    elapsed=time.perf_counter()-start
    log_text=(case/'run.log').read_text()
    assert proc.returncode==0,proc.returncode
    assert '0 unique true-miss addrs, 0 raw miss events' in log_text
    cadence=Counter();vints=Counter()
    def active(row):
        # Level_MainLoop, started, alive, not paused (Game_paused $F63A).
        return row[0]==12 and row[2]==1 and row[3]<6 and row[4]==0
    for frame,row in rows.items():
        prev=rows.get(frame-1)
        if prev and active(prev) and active(row):
            cadence[(row[1]-prev[1])&255]+=1
            vints[(row[5]-prev[5])&0xFFFFFFFF]+=1
    measured=sum(cadence.values())
    result=dict(zone=args.zone,mode=args.mode,measured_gameplay_frames=measured,
        level_updates_per_frame=dict(cadence),vints_per_frame=dict(vints),
        effective_gameplay_fps=59.94*sum(k*v for k,v in cadence.items())/measured if measured else 0,
        vblank_cadence={key:video[key] for key in ('tick_samples','tick_updates','tick_lag','tick_multi','publication_lag') if key in video},
        audio={key:audio[key] for key in ('total_flushes','underrun_flushes','dropped_flushes') if key in audio},
        run_seconds=elapsed,average_host_ms_per_frame=elapsed*1000/frames)
    if len(wall_samples)>1:
        result['measured_host_fps']=(wall_samples[-1][0]-wall_samples[0][0])/(wall_samples[-1][1]-wall_samples[0][1])
    (case/'cadence.json').write_text(json.dumps(result,indent=2))
    (case/'samples.json').write_text(json.dumps(rows))
    print(json.dumps(result),flush=True)
    assert measured>500, 'insufficient active gameplay'
    if args.require_full_rate:
        assert video.get('tick_samples',0)>500, 'missing VBlank-aligned cadence evidence'
        assert video['tick_lag']==0 and video['tick_multi']==0, ('VBlank-aligned gameplay cadence',video)
        assert video['publication_lag']==0, ('VBlank-aligned sprite publication cadence',video)
        assert set(vints)=={1}, ('VBlank rate changed',vints)
        if args.realtime:
            assert 58.5<result['measured_host_fps']<61, ('realtime frame pacing',result)
            assert not audio['underrun_flushes'] and not audio['dropped_flushes'], ('realtime audio delivery',audio)


if __name__=='__main__':main()
