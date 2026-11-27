"""Assemble the shared software-only Clone Driver for a Genesis consumer."""
from pathlib import Path
import argparse, hashlib, json, platform, re, stat, struct, subprocess, urllib.parse, urllib.request

SOURCE_COMMIT = '13ca55d1f9bf2268f440616404cc65a3b08a6ad6'
FIELDS = ('payload_size initialise update queue_music queue_sfx queue_extra '
          'guarded_update pcm_idle ram_address ram_size queue_offset pause_offset '
          'tempo_offset sample_table music_index sound_index psg_index modenv_index '
          'voice_bank stop_all dac_map_s3 dac_map_sk sega_id sample_count '
          'megapcm_load_driver megapcm_load_table').split()
HEADER = struct.Struct('>4s' + 'I' * len(FIELDS))

def digest(data):
    return hashlib.sha256(data).hexdigest()

def fetch(cache, name, spec, repository, commit, offline):
    p = cache / name
    if p.is_file() and p.stat().st_size == spec['size'] and digest(p.read_bytes()) == spec['sha256']:
        return p
    if offline:
        raise RuntimeError('Missing or damaged pinned input: ' + name)
    url = 'https://raw.githubusercontent.com/' + repository + '/' + commit + '/' + urllib.parse.quote(name, safe='/')
    with urllib.request.urlopen(url, timeout=60) as stream:
        data = stream.read(spec['size'] + 1)
    if len(data) != spec['size'] or digest(data) != spec['sha256']:
        raise RuntimeError('Pinned input identity mismatch: ' + name)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p

def write(p, text):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')

def assemble(source, assembler, p2bin, out, profile):
    load_address = profile["load_address"]
    ram_address = profile["ram_address"]
    ram_size = profile["ram_capacity"]
    legacy_tempo = profile["tempo_policy"] == "song_format"
    legacy_psg_period = profile["legacy_psg_period"]
    host_queue = not legacy_tempo
    legacy_gloop = profile["legacy_gloop"]
    out.mkdir(parents=True, exist_ok=False)
    inputs = out / 'inputs'
    relocation_count = 0
    for name in source:
        text = source[name].read_text(encoding='utf-8')
        # The original port assumes a 16-bit absolute work-RAM address. This
        # isolated cartridge RAM uses long addresses; instruction sizes remain
        # unchanged except for the deliberately relocated address operands.
        rows = []
        for line in text.splitlines():
            if 'Clone_Driver_RAM' in line or 'Graphics_flags' in line:
                line, n = re.subn(r'\)\.w\b', ').l', line)
                relocation_count += n
            rows.append(line)
        text = '\n'.join(rows) + '\n'
        if name == 'Sound/Engine/RAM.asm':
            # Keep the S3 extra-update clock in the normal song backup, so a
            # 1-UP restores its predecessor's speed and countdown together.
            marker = 'SMPS_RAM_Variables ENDSTRUCT'
            assert text.count(marker) == 1, 'Upstream song variables changed'
            text = text.replace(marker, '\tv_native_speedup: ds.b 1\n\tv_native_speedup_timeout: ds.b 1\n\tv_foreign_s3: ds.b 1\n\n' + marker, 1)
        if name == 'Sound/Engine/Sonic 2 Clone Driver v2.asm':
            # Imported S3 music keeps advancing while an SFX borrows FM.
            # Returning that channel must restore its current pitch/key,
            # unless the sequence is resting or holding a note without an
            # attack. The song header selects the S3 FM return contract.
            marker = ("\tbclr\t#2,SMPS_Track.PlaybackControl(a5)\t; Clear 'SFX overriding' bit\n"
                      "\tbset\t#1,SMPS_Track.PlaybackControl(a5)\t; Set 'track at rest' bit\n"
                      '\tbsr.w\tSetVoice\n')
            assert text.count(marker) == 1, 'Upstream FM SFX return changed'
            text = text.replace(marker, '''\tbclr #2,SMPS_Track.PlaybackControl(a5)
\ttst.b SMPS_RAM.variables.v_foreign_s3(a6)
\tbeq.s .legacyFMReturn
\tbsr.w SetVoice
\tbtst #1,SMPS_Track.PlaybackControl(a5)
\tbne.s .novoiceupd
\tbtst #4,SMPS_Track.PlaybackControl(a5)
\tbne.s .novoiceupd
\tbsr.w FMPrepareNote
\tbsr.w FMNoteOn
\tbra.s .novoiceupd
.legacyFMReturn:
\tbset #1,SMPS_Track.PlaybackControl(a5)
\tbsr.w SetVoice
''', 1)
            # S3 accelerates music with extra music updates, preserving its
            # ordinary tempo accumulator. An alternate tempo of 1 is much
            # too fast for slower songs. The original countdown runs after
            # both music and SFX, including each extra music update; SFX
            # themselves still advance only once per frame.
            start = text.index('SpeedUpMusic:\n')
            if legacy_tempo:
                legacy_end=text.index('; sub_7267C:',start)
                legacy_speed=text[start:legacy_end].replace('SpeedUpMusic','S2_LegacySpeedUpMusic').replace('SlowDownMusic','S2_LegacySlowDownMusic')
            end = text.index('; sub_7267C:', start)
            text = text[:start] + '''SpeedUpMusic:
\tbtst #f_1up_playing,SMPS_RAM.variables.bitfield2(a6)
\tbne.s SpeedUpMusic_1up
\tmove.b #8,SMPS_RAM.variables.v_native_speedup(a6)
\tbset #f_speedup,SMPS_RAM.variables.bitfield2(a6)
\trts
SpeedUpMusic_1up:
\tmove.b #8,SMPS_RAM.variables_backup.v_native_speedup(a6)
\tbset #f_speedup,SMPS_RAM.variables_backup.bitfield2(a6)
\trts
SlowDownMusic:
\tbtst #f_1up_playing,SMPS_RAM.variables.bitfield2(a6)
\tbne.s SlowDownMusic_1up
\tclr.b SMPS_RAM.variables.v_native_speedup(a6)
\tbclr #f_speedup,SMPS_RAM.variables.bitfield2(a6)
\trts
SlowDownMusic_1up:
\tclr.b SMPS_RAM.variables_backup.v_native_speedup(a6)
\tbclr #f_speedup,SMPS_RAM.variables_backup.bitfield2(a6)
\trts

Clone_NativeSpeedupTick:
\ttst.b SMPS_RAM.variables.v_native_speedup(a6)
\tbeq.s .done
\ttst.b SMPS_RAM.variables.v_native_speedup_timeout(a6)
\tbne.s .countdown
\tmove.b SMPS_RAM.variables.v_native_speedup(a6),SMPS_RAM.variables.v_native_speedup_timeout(a6)
\tbra.w UpdateMusic
.countdown:
\tsubq.b #1,SMPS_RAM.variables.v_native_speedup_timeout(a6)
.done:
\trts

''' + ('' if legacy_tempo else '''Clone_SetNativeTempo:
\tlea SMPS_RAM.variables(a6),a5
\tbtst #f_1up_playing,SMPS_RAM.variables.bitfield2(a6)
\tbeq.s .selected
\tlea SMPS_RAM.variables_backup(a6),a5
.selected:
\tmove.b d0,SMPS_RAM_Variables.v_native_speedup(a5)
\tbclr #f_speedup,SMPS_RAM_Variables.bitfield2(a5)
\ttst.b d0
\tbeq.s .done
\tbset #f_speedup,SMPS_RAM_Variables.bitfield2(a5)
.done:
\trts

''') + text[end:]
            if host_queue:
                # Native callers supply the whole extra-update period, not a
                # boolean shoes switch. Reserve $100..$1FF in the existing word
                # queue, retaining normal command IDs and the 1-UP backup rule.
                marker = '\tclr.w\t(a1)+\t\t\t; Clear entry\n'
                assert text.count(marker) == 1, 'Upstream queue reader changed'
                text = text.replace(marker, marker + '''\tcmpi.w #$100,d0
\tblo.s .nativeTempoReady
\tcmpi.w #$200,d0
\tbhs.s .nativeTempoReady
\tbsr.w Clone_SetNativeTempo
\tbra.w .next_queue
.nativeTempoReady:
''', 1)
            text = text.replace('\tbra.w\tPSGUpdateTrack\n.locret:\n    endif\n\trts\n; End of function SMPS_UpdateDriver', '\tbsr.w\tPSGUpdateTrack\n.locret:\n    endif\n\trts\n; End of function SMPS_UpdateDriver', 1)
            marker = '\trts\n; End of function SMPS_UpdateDriver'
            assert text.count(marker) == 1, 'Upstream SFX update changed'
            text = text.replace(marker, '\tbsr.w Clone_NativeSpeedupTick\n\trts\n; End of function SMPS_UpdateDriver', 1)
            marker = '\tbra.w\tTempoWait\n; End of function UpdateMusic'
            assert text.count(marker) == 1, 'Upstream music update changed'
            text = text.replace(marker, '\tbsr.w TempoWait\n\tbra.w Clone_NativeSpeedupTick\n; End of function UpdateMusic', 1)
            # Extra music reached after SFX needs the same call depth as the
            # ordinary music pass. cfFadeInToPrevious deliberately unwinds
            # that stack when its 1-UP sequence finishes. Music-to-music
            # extra updates use tail jumps and retain that one call frame.
            # This point is after taking that sequence's song backup.
            marker = '.bgm_loadMusic:\n'
            assert text.count(marker) == 1, 'Upstream music loader changed'
            # InitMusicPlayback clears the continuous SFX ID/channel count.
            # Clear its global latch as well; leaving it set makes an active
            # turbine decrement zero to 255 and keep looping in the next song.
            text = text.replace(marker, marker + '\tbclr #f_speedup,SMPS_RAM.variables.bitfield2(a6)\n\tbclr #f_continuous_sfx,SMPS_RAM.bitfield1(a6)\n', 1)
            if legacy_tempo:
                # The shared sequencer supports both tempo policies. The song's
                # immutable header selects one; native 1-UP backups retain it.
                for label,legacy in [('SpeedUpMusic','S2_LegacySpeedUpMusic'),('SlowDownMusic','S2_LegacySlowDownMusic')]:
                    guard=(label+':\n\tbtst #f_1up_playing,SMPS_RAM.variables.bitfield2(a6)\n'
                           '\tbne.s .source_backup\n\ttst.b SMPS_RAM.variables.v_foreign_s3(a6)\n'
                           '\tbeq.w '+legacy+'\n\tbra.s .source_ready\n.source_backup:\n'
                           '\ttst.b SMPS_RAM.variables_backup.v_foreign_s3(a6)\n\tbeq.w '+legacy+'\n.source_ready:\n')
                    text=text.replace(label+':\n',guard,1)
                text += '\n'+legacy_speed
                text=text.replace('\tbclr #f_speedup,SMPS_RAM.variables.bitfield2(a6)\n\tbclr #f_continuous_sfx',
                                  '\tbclr #f_continuous_sfx',1)
            marker='\tmovea.l\td1,a4'
            assert text.count(marker)==1
            line=text.index('\n',text.index(marker))+1
            text=text[:line]+('\tmove.b 5(a4),SMPS_RAM.variables.v_foreign_s3(a6)\n'
                              '\tandi.b #1,SMPS_RAM.variables.v_foreign_s3(a6)\n')+text[line:]
            if legacy_tempo:
                marker='\tmove.b\tSMPS_RAM.variables.v_speeduptempo(a6),d0'
                assert text.count(marker)==1
                text=text.replace(marker,'\ttst.b SMPS_RAM.variables.v_foreign_s3(a6)\n'
                                  '\tbeq.s .source_legacy_tempo\n'
                                  '\tmove.b #8,SMPS_RAM.variables.v_native_speedup(a6)\n'
                                  '\tbra.s .nospeedshoes\n.source_legacy_tempo:\n'+marker,1)
            # S3's Z80 keeps a PSG channel silent when a tied note follows
            # an envelope's note-off. The S1-derived port clears that rest
            # early and audibly retriggers HCZ percussion. Header byte 5
            # marks converted S3 music; PSG's unused FM voice-pointer bytes
            # retain that mode and the preceding rest state per channel.
            marker = '.bgm_psgloadloop:\n'
            assert text.count(marker) == 1, 'Upstream PSG loader changed'
            text = text.replace(marker, marker + '\tmove.b 5(a3),SMPS_Track.VoicePtr+1(a1)\n', 1)
            # S3 initializes its tempo accumulator from the song tempo. Keep
            # that first delay phase without changing S1/S2's zero start.
            marker = '\tmove.b\td1,SMPS_RAM.variables.v_main_tempo_timeout(a6)\t; Clownacy | Cleared to avoid unintended overflow on first frame of playback'
            assert text.count(marker) == 1, 'Upstream tempo initialization changed'
            text = text.replace(marker, marker + '''
\tbtst #0,5(a4)
\tbeq.s .nativeTempoReady
\tmove.b d0,SMPS_RAM.variables.v_main_tempo_timeout(a6)
.nativeTempoReady:
''', 1)
            # The S3 FM rest silences the channel but retains its pitch.
            # A following duration-only event uses that pitch again.
            start = text.index('FMSetFreq:\n')
            end = text.index('; End of function FMSetFreq', start)
            segment = text[start:end]
            marker = '\tbeq.w\tTrackSetRest'
            assert segment.count(marker) == 1, 'Upstream FM rest branch changed'
            segment = segment.replace(marker, '\tbeq.w\tClone_FMSetRest', 1)
            text = text[:start] + segment + text[end:]
            text += '''
Clone_FMSetRest:
\ttst.b SMPS_RAM.variables.v_foreign_s3(a6)
\tbeq.w TrackSetRest
\tmove.l a5,d5
\tsub.l a6,d5
\tcmpi.w #SMPS_RAM.v_music_fm_tracks_end,d5
\tbhs.w TrackSetRest
\tbset #1,SMPS_Track.PlaybackControl(a5)
\trts
'''
            start = text.index('PSGDoNext:\n')
            end = text.index('; End of function PSGDoNext', start)
            segment = text[start:end]
            segment = segment.replace('PSGDoNext:\n', 'PSGDoNext:\n\tmove.b SMPS_Track.PlaybackControl(a5),SMPS_Track.VoicePtr+2(a5)\n', 1)
            marker = '.gotnote:\n'
            assert segment.count(marker) == 1, 'Upstream PSG note decoder changed'
            segment = segment.replace(marker, marker + '''\tbtst #0,SMPS_Track.VoicePtr+1(a5)
\tbeq.s .nativeRestReady
\tbtst #4,SMPS_Track.PlaybackControl(a5)
\tbeq.s .nativeRestReady
\tbtst #1,SMPS_Track.VoicePtr+2(a5)
\tbeq.s .nativeRestReady
\tbset #1,SMPS_Track.PlaybackControl(a5)
.nativeRestReady:
''', 1)
            text = text[:start] + segment + text[end:]
            marker = 'SetPSGVolume:\n'
            assert text.count(marker) == 1, 'Upstream PSG volume decoder changed'
            text = text.replace(marker, marker + '''\tbtst #0,SMPS_Track.VoicePtr+1(a5)
\tbeq.s .nativeVolumeReady
\tbtst #4,SMPS_Track.PlaybackControl(a5)
\tbne.s locret_7298A
.nativeVolumeReady:
''', 1)
            start = text.index('PSGSetFreq:\n')
            end = text.index('; End of function PSGSetFreq', start)
            segment = text[start:end]
            if legacy_psg_period:
                # Legacy sequences use zero as the last PSG period; S3 adds
                # period 16 at that index. Apply the header's bit 1 after
                # transpose, leaving bit 0 for S3 rest/tie behavior.
                marker = '\tadd.b\tSMPS_Track.Transpose(a5),d5'
                assert segment.count(marker) == 1, 'Upstream PSG transpose changed'
                at = segment.index('\n', segment.index(marker)) + 1
                segment = segment[:at] + '''\tbtst #1,SMPS_Track.VoicePtr+1(a5)
\tbeq.s .legacyPeriodReady
\tcmpi.b #$51,d5
\tbne.s .legacyPeriodReady
\taddq.b #1,d5
.legacyPeriodReady:
''' + segment[at:]
            # S3 rests silence the channel and retain the last period.
            marker = '\tmove.w\t#-1,SMPS_Track.Freq(a5)'
            assert segment.count(marker) == 1, 'Upstream PSG rest changed'
            segment = segment.replace(marker, '\tbtst #0,SMPS_Track.VoicePtr+1(a5)\n'
                '\tbne.s .nativeRestFrequency\n' + marker, 1)
            marker = '\tbra.w\tPSGNoteOff'
            assert segment.count(marker) == 1, 'Upstream PSG rest return changed'
            segment = segment.replace(marker, '.nativeRestFrequency:\n' + marker, 1)
            text = text[:start] + segment + text[end:]
            text = text.replace('\tbra.s\tPSGDoVolFX_Loop', '\tbra.w\tPSGDoVolFX_Loop')
            # A new song resets its DAC track pan to stereo. MegaPCM keeps a
            # separate pan shadow for sample/SFX restoration; reset that too,
            # or a song without a pan command inherits the preceding song's
            # left/right-only DAC setting.
            marker = '.bgm_loadMusic:\n'
            assert text.count(marker) == 1, 'Upstream music loader changed'
            text = text.replace(marker, marker + '\tMPCM_setPan #$C0\n', 1)
            # The logo sample is not a music track: it bypasses the song
            # loader that restores PCM volume after a demo fade. Restore its
            # own full-volume stereo output before requesting the sample.
            marker = 'PlaySegaSound:\n'
            assert text.count(marker) == 1, 'Upstream logo playback changed'
            text = text.replace(marker, marker + '\tMPCM_setVol #0\n\tMPCM_setPan #$C0\n', 1)
            # Retail SMPS Z80 keeps the full unsigned transposed note. Low
            # bass notes deliberately wrap into octaves beyond seven. Clone's
            # 7-bit mask instead turns them into high audible notes, and its
            # 96-entry table also permits reads into following instructions.
            # Encode the retail first-octave frequencies and octave OR for all
            # 256 byte indices (Clone indexes C as 1 rather than 0).
            start = text.index('FMSetFreq:\n')
            end = text.index('; End of function FMSetFreq', start)
            segment = text[start:end]
            assert segment.count('andi.w\t#$7F,d5') == 1, 'Upstream FM note lookup changed'
            text = text[:start] + segment.replace('andi.w\t#$7F,d5', 'andi.w\t#$FF,d5') + text[end:]
            octave = (0x284, 0x2AB, 0x2D3, 0x2FE, 0x32D, 0x35C,
                      0x38F, 0x3C5, 0x3FF, 0x43C, 0x47C, 0x4C0)
            frequencies = [octave[((i-1)&255)%12] | ((((i-1)&255)//12)<<11) for i in range(256)]
            start = text.index('FMFrequencies:\n') + len('FMFrequencies:\n')
            end = start
            while text[end:].startswith('\tdc.w '):
                end = text.index('\n', end) + 1
            assert end > start, 'Upstream FM frequency table changed'
            text = text[:start] + ''.join('\tdc.w ' + ','.join(f'${n:04X}' for n in frequencies[i:i+12]) + '\n' for i in range(0,256,12)) + text[end:]
            # Two channel maps stored sign-extended 16-bit work-RAM pointers.
            # Widen their entries, then scale the original byte index using
            # LEA rather than arithmetic that would change condition flags.
            text, tables = re.subn(
                r'(SFX_(?:BGM|SFX)ChannelRAM:\n)((?:\s*dc\.w[^\n]*\n)+)',
                lambda m: m[1] + m[2].replace('dc.w', 'dc.l'), text)
            assert tables == 2, 'Upstream channel map layout changed'
            text, loads = re.subn(
                r'^(\s*)movea\.w\s+\((a[0-6]),(d[0-7]\.w)\),(a[0-6])([^\n]*)$',
                lambda m: f'{m[1]}lea ({m[2]},{m[3]}),{m[4]}\n\tmovea.l ({m[4]},{m[3]}),{m[4]}{m[5]}',
                text, flags=re.M)
            assert loads == 8, 'Upstream channel map consumers changed'
            # These eight musical modulation envelopes are game data. Keep
            # only the pointer layout; derive their bytes from the player's
            # ROM alongside PSG envelopes, instruments and sequences.
            table = text.index('ModEnvPointers:\n')
            start = text.index('; These have been converted to SMPS 68k format:')
            text = text[:table] + 'ModEnvPointers:\n' + ''.join(f'\tdc.l ModEnv_{i:02X}\n' for i in range(32)) + text[start:]
            start = text.index('; These have been converted to SMPS 68k format:')
            end = text.index('    endif', start)
            text = text[:start] + ''.join(f'ModEnv_{i:02X}: dc.b [128]0\n' for i in range(32)) + text[end:]
        if host_queue and name == 'Sound/Engine/Functions.asm':
            start = text.index('SMPS_QueueSound3:\n')
            end = text.index('\n\trts', start)
            text = text[:start] + '''SMPS_QueueSound3:
\tmove.w d0,(Clone_Driver_RAM+SMPS_RAM.variables.queue.v_playsnd3).l''' + text[end:]
        if name == 'Sound/MegaPCM - DAC Table.asm':
            marker = '\t\t\tdc.w -1'
            assert text.count(marker) == 1, 'Upstream PCM table terminator changed'
            extra = ''.join(f'proto_dac{i:02X}: dcSample TYPE_PCM, PrototypeSample, 16000\n' for i in range(56))
            text = text.replace(marker, extra + marker)
        if legacy_gloop and name == 'Sound/Settings.asm':
            text=re.sub(r'(?m)^(SMPS_GloopSFXBehaviour\s*=\s*)[0-9]+',r'\g<1>1',text)
            text += '\nSndID_Gloop = $7A\n'
        if legacy_gloop and name == 'Sound/Engine/Sonic 2 Clone Driver v2.asm':
            old='\tbchg\t#v_gloop_toggle,SMPS_RAM.bitfield1(a6)\t; Z80 cpl\n\tbne.s\tSound_PlaySFX.play_sfx'
            if old in text:text=text.replace(old,old.replace('bne.s','beq.s'))
        write(inputs / name, text)
    write(inputs / 'Sound/Music.asm', 'MusicIndex:\n' + ''.join(f'ptr_mus{i:02X}: dc.l 0\n' for i in range(1, 0x34)) + 'ptr_musend:\n')
    sound_ids = source['Sound/Sound IDs.asm'].read_text()
    last_sfx = max(int(n, 16) for n in re.findall(r'ptr_snd([0-9A-F]{2})\b', sound_ids))
    write(inputs / 'Sound/SFX.asm', 'SoundIndex:\n' + ''.join(f'ptr_snd{i:02X}: dc.l 0\n' for i in range(0x40, last_sfx + 1)) + 'ptr_sndend:\n')
    write(inputs / 'Sound/Special SFX.asm', 'SpecSoundIndex: dc.l 0\n')
    if profile.get("sound_ids"):
        write(inputs / "Sound/Sound IDs.asm", profile["sound_ids"])
    write(inputs / 'Sound/FM Universal Voice Bank.asm', '    dc.b [800]0\n' * 4)
    env_equates = source['Sound/SMPS2ASM - PSG Volume Envelope Equates.asm'].read_text()
    envs = list(dict.fromkeys(re.findall(r'SMPS_id\((ptr_(?:s3|sk)psg\w+)\)', env_equates)))
    # Runtime conversion preserves retail PSG envelope IDs. Equate labels are
    # aliases of those slots; their upstream include order is not an asset ID.
    psg = 'PSG_Index:\n'
    for number in range(1, 257):
        psg += ''.join(name + ':\n' for name in envs if int(name[-2:], 16) == number)
        psg += '    dc.l 0\n'
    write(inputs / 'Sound/PSG Volume Envelopes.asm', psg)
    descriptors = source['Sound/MegaPCM - DAC Table.asm'].read_text()
    samples = ['PrototypeSample'] + sorted(set(re.findall(r'dcSample\s+\w+\s*,\s*(\w+)', descriptors)))
    write(inputs / 'Sound/SamplePlaceholders.asm', ''.join(f'{name} = $600000\n{name}_End = {name}+16\n' for name in samples))
    write(inputs / 'NativeDACMaps.asm', Path(__file__).with_name('native_dac_order.asm').read_text())
    wrapper = out / "driver.asm"
    template = Path(__file__).with_name("driver.asm").read_text()
    write(wrapper, template.replace("@LOAD_ADDRESS@", "$" + f"{load_address:X}")
          .replace("@RAM_ADDRESS@", "$" + f"{ram_address:X}")
          .replace("@RAM_END@", "$" + f"{ram_address + ram_size:X}"))
    for command, log in [([str(assembler), '-xx', '-n', '-q', '-A', '-L', '-U', '-i', str(inputs), '-o', str(out/'driver.p'), '-olist', str(out/'driver.lst'), '-E', str(out/'driver-errors.log'), str(wrapper)], 'assembler.log'), ([str(p2bin), '-p=FF', str(out/'driver.p'), str(out/'image.bin')], 'p2bin.log')]:
        with (out/log).open('w') as stream:
            subprocess.run(command, cwd=out, stdout=stream, stderr=subprocess.STDOUT, check=True)
    raw = (out/'image.bin').read_bytes()
    assert load_address + HEADER.size <= len(raw) < load_address + 0x20000
    assert raw[:load_address] == b'\xff' * load_address
    payload = raw[load_address:]
    magic, *values = HEADER.unpack_from(payload)
    info = dict(zip(FIELDS, values))
    assert magic == b'S3CR' and info['payload_size'] == len(payload)
    assert info['ram_address'] == ram_address and 0 < info['ram_size'] <= ram_size
    end = info['sample_table'] + info['sample_count'] * 10
    assert raw[end:end+2] == b'\xff\xff', 'PCM descriptors must precede their terminator'
    for key in ('initialise', 'update', 'queue_music', 'queue_sfx', 'queue_extra', 'guarded_update', 'pcm_idle', 'stop_all'):
        assert load_address + HEADER.size <= info[key] < len(raw) and not info[key] & 1, key
    (out/'clone-driver.bin').write_bytes(payload)
    manifest = {'source': SOURCE_COMMIT, 'asset_mode': 'rom', 'load_address': load_address, 'sha256': digest(payload), 'relocated_absolute_operands': relocation_count, **info}
    if legacy_psg_period:
        manifest['legacy_psg_period_policy'] = 1
    write(out/'program.json', json.dumps(manifest, indent=2) + '\n')
    return manifest

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', type=Path, required=True)
    ap.add_argument('--cache', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--assembler', type=Path)
    ap.add_argument('--p2bin', type=Path)
    ap.add_argument('--offline', action='store_true')
    args = ap.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    if profile.get("tempo_policy") not in ("song_format", "host_queue"):
        ap.error("Unknown tempo policy")
    if profile.get("sound_ids"):
        profile["sound_ids"] = (args.profile.parent / profile["sound_ids"]).read_text(encoding="utf-8")
    lock = json.loads(Path(__file__).with_name('sources.lock.json').read_text())
    assert lock['commit'] == SOURCE_COMMIT
    cache = args.cache.resolve()
    source = {name: fetch(cache/'source', name, spec, lock['repository'], lock['commit'], args.offline) for name, spec in lock['sources'].items()}
    if bool(args.assembler) != bool(args.p2bin):
        ap.error('Supply --assembler and --p2bin together')
    if args.assembler:
        assembler, p2bin = args.assembler.resolve(strict=True), args.p2bin.resolve(strict=True)
    else:
        host = platform.system() + '-' + platform.machine().lower()
        host = {'Windows-amd64': 'Windows-x86_64', 'Linux-x86_64': 'Linux-x86_64'}.get(host)
        if not host:
            ap.error('Supply native AS/p2bin on this build host')
        names = ('asw.exe', 'p2bin.exe') if host.startswith('Windows') else ('asl', 'p2bin')
        tools = [fetch(cache/'tools', 'build_tools/'+host+'/'+name, lock['tools']['files']['build_tools/'+host+'/'+name], lock['tools']['repository'], lock['tools']['commit'], args.offline) for name in names]
        for tool in tools:
            if not host.startswith('Windows'):
                tool.chmod(tool.stat().st_mode | stat.S_IXUSR)
        assembler, p2bin = tools
    print(json.dumps(assemble(source, assembler, p2bin, args.output.resolve(), profile), indent=2))

if __name__ == '__main__':
    main()
