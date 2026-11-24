; Driver software and empty asset tables only. The game's ROM converter fills
; music, instruments, envelopes and PCM before this program may run.
MSUMode = 0
ZeroOffsetOptimization = 1
Snd_driver_RAM = @RAM_ADDRESS@
Snd_driver_RAM_end = @RAM_END@
Graphics_flags = $A10001
    cpu 68000
    include "sonic3k.macrosetup.asm"
disableIntsSave macro
    move.w sr,-(sp)
    ori.w #$700,sr
    endm
enableIntsSave macro
    move.w (sp)+,sr
    endm
    include "Sound/Definitions.asm"
    org @LOAD_ADDRESS@
ProgramStart:
    dc.b "S3CR"
    dc.l ProgramEnd-ProgramStart
    dc.l SMPS_LoadDACDriver,SMPS_UpdateDriver
    dc.l SMPS_QueueSound1,SMPS_QueueSound2,SMPS_QueueSound3
    dc.l Clone_GuardedUpdate,Clone_PCMIdle
    dc.l Clone_Driver_RAM,SMPS_RAM.len
    dc.l SMPS_RAM.variables.queue,SMPS_RAM.f_pause
    dc.l SMPS_RAM.variables.v_main_tempo
    dc.l MegaPCM_DAC_Table,MusicIndex,SoundIndex,PSG_Index
    dc.l ModEnvPointers,UniVoiceBank,StopAllSound
    dc.l Clone_NativeDACMapS3,Clone_NativeDACMapSK,dacE0.id
    dc.l (Clone_DACTableEnd-MegaPCM_DAC_Table-2)/10
    dc.l MegaPCM_LoadDriver,MegaPCM_LoadSampleTable
Clone_GuardedUpdate:
    SMPS_UpdateSoundDriver
    rts
Clone_PCMIdle:
    disableIntsSave
    MPCM_stopZ80
    moveq #0,d0
    move.b (MPCM_Z80_RAM+Z_MPCM_LoopId).l,d0
    MPCM_startZ80
    cmpi.b #Z_MPCM_LOOP_IDLE,d0
    seq d0
    andi.w #1,d0
    enableIntsSave
    rts
    include "Sound/Engine/Functions.asm"
    include "Sound/Engine/MegaPCM.asm"
    include "Sound/SamplePlaceholders.asm"
    include "Sound/MegaPCM - DAC Table.asm"
Clone_DACTableEnd:
    include "Sound/Engine/Sonic 2 Clone Driver v2.asm"
    include "NativeDACMaps.asm"
    even
ProgramEnd:
