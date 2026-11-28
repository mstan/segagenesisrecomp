#pragma once
#include "gamepad.h"
#include "recomp_runtime_ui.h"

typedef struct RuntimeUiPad {
    uint32_t previous;
    uint64_t next_repeat;
} RuntimeUiPad;

/* Share the state/rewind overlays' physical input path. Only directions
 * repeat; accepting an action always requires a fresh press. */
static void runtime_ui_gamepad_poll(RecompRuntimeUi *ui, RuntimeUiPad *pad,
                                    uint32_t held, uint64_t now, int nintendo)
{
    const uint32_t directions=GP_OVERLAY_UP|GP_OVERLAY_DOWN|GP_OVERLAY_LEFT|GP_OVERLAY_RIGHT;
    uint32_t pressed=held&~pad->previous;
    uint32_t current_dir=held&directions,old_dir=pad->previous&directions;
    int repeat=0;
    pad->previous=held;
    if(!recomp_runtime_ui_is_open(ui)){pad->next_repeat=0;return;}
    if(current_dir!=old_dir || !pad->next_repeat)pad->next_repeat=now+280;
    else if(current_dir && now>=pad->next_repeat){pressed|=current_dir;pad->next_repeat=now+80;repeat=1;}
    uint32_t accept=nintendo?GP_OVERLAY_B:GP_OVERLAY_A;
    uint32_t back=nintendo?GP_OVERLAY_A:GP_OVERLAY_B;
    if(pressed&back){recomp_runtime_ui_handle_input(ui,RECOMP_RUNTIME_UI_INPUT_BACK,1,0);return;}
    static const uint32_t bits[]={GP_OVERLAY_UP,GP_OVERLAY_DOWN,GP_OVERLAY_LEFT,GP_OVERLAY_RIGHT};
    static const RecompRuntimeUiInput inputs[]={RECOMP_RUNTIME_UI_INPUT_UP,RECOMP_RUNTIME_UI_INPUT_DOWN,
        RECOMP_RUNTIME_UI_INPUT_LEFT,RECOMP_RUNTIME_UI_INPUT_RIGHT};
    for(unsigned i=0;i<4;++i)if(pressed&bits[i]){
        recomp_runtime_ui_handle_input(ui,inputs[i],1,repeat);
        break;
    }
    if(pressed&accept)recomp_runtime_ui_handle_input(ui,RECOMP_RUNTIME_UI_INPUT_ACCEPT,1,0);
}
