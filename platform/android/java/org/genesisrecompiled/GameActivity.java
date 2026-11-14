package org.genesisrecompiled;

import android.os.Bundle;
import android.system.Os;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import java.io.File;
import java.util.ArrayList;
import java.util.HashSet;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private final HashSet<Integer> held=new HashSet<>();
    private FrameLayout touchOverlay;
    private volatile boolean touchControlsAvailable;
    /** Called by the native runner when gameplay owns input, rather than a menu. */
    public void setTouchControlsAvailable(boolean available) {
        touchControlsAvailable=available;
        runOnUiThread(() -> {
            if(touchOverlay==null)return;
            if(!available)release();
            touchOverlay.setVisibility(available?View.VISIBLE:View.GONE);
        });
    }
    @Override protected String[] getLibraries() { return new String[]{"c++_shared","SDL2",BuildConfig.NATIVE_LIBRARY}; }
    @Override protected String[] getArguments() {
        ArrayList<String> args=new ArrayList<>();
        if(BuildConfig.DEBUG) {
            String[] qa=getIntent().getStringArrayExtra("qaArgs");
            if(qa!=null) {
                args.add(new File(getFilesDir(),"game.bin").getAbsolutePath());
                for(String arg:qa)args.add(arg);
            }
        }
        return args.toArray(new String[0]);
    }
    @Override protected void onCreate(Bundle saved) {
        try {
            Os.setenv("GENESIS_USER_DIR",getFilesDir().getAbsolutePath(),true);
            if(BuildConfig.DEBUG && getIntent().hasExtra("qaMute")) {
                if(getIntent().getBooleanExtra("qaMute",false))Os.setenv("SDL_AUDIODRIVER","dummy",true);
                else Os.unsetenv("SDL_AUDIODRIVER");
            }
            if(BuildConfig.DEBUG) {
                String probe=getIntent().getStringExtra("qaRbProbe");
                if(probe!=null && probe.matches("[0-9]+:[0-9]+"))Os.setenv("GENESIS_RB_PROBE",probe,true);
            }
        } catch(Exception failure) { throw new IllegalStateException(failure); }
        super.onCreate(saved);
        FrameLayout overlay=new FrameLayout(this);touchOverlay=overlay;
        overlay.setVisibility(touchControlsAvailable?View.VISIBLE:View.GONE);
        FrameLayout pad=new FrameLayout(this);pad.setVisibility(View.GONE);
        overlay.addView(pad,new FrameLayout.LayoutParams(-1,-1));
        Button toggle=new Button(this);toggle.setText("Touch controls");toggle.setAlpha(.65f);
        overlay.addView(toggle,new FrameLayout.LayoutParams(-2,dp(44),Gravity.TOP|Gravity.RIGHT));
        toggle.setOnClickListener(v -> { release();pad.setVisibility(pad.getVisibility()==View.VISIBLE?View.GONE:View.VISIBLE); });
        key(pad,"Up",KeyEvent.KEYCODE_DPAD_UP,Gravity.BOTTOM|Gravity.LEFT,60,110);
        key(pad,"Down",KeyEvent.KEYCODE_DPAD_DOWN,Gravity.BOTTOM|Gravity.LEFT,60,0);
        key(pad,"Left",KeyEvent.KEYCODE_DPAD_LEFT,Gravity.BOTTOM|Gravity.LEFT,0,55);
        key(pad,"Right",KeyEvent.KEYCODE_DPAD_RIGHT,Gravity.BOTTOM|Gravity.LEFT,120,55);
        key(pad,"A",KeyEvent.KEYCODE_Z,Gravity.BOTTOM|Gravity.RIGHT,140,20);
        key(pad,"B",KeyEvent.KEYCODE_X,Gravity.BOTTOM|Gravity.RIGHT,70,55);
        key(pad,"C",KeyEvent.KEYCODE_C,Gravity.BOTTOM|Gravity.RIGHT,0,90);
        key(pad,"Start",KeyEvent.KEYCODE_ENTER,Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL,0,0);
        key(pad,"Menu",KeyEvent.KEYCODE_ESCAPE,Gravity.TOP|Gravity.LEFT,0,0);
        addContentView(overlay,new FrameLayout.LayoutParams(-1,-1));
    }
    private int dp(int value) { return (int)(value*getResources().getDisplayMetrics().density+.5f); }
    private void key(FrameLayout pad,String text,int code,int gravity,int side,int bottom) {
        Button button=new Button(this);button.setText(text);button.setAlpha(.5f);button.setFocusable(false);
        FrameLayout.LayoutParams pos=new FrameLayout.LayoutParams(dp(64),dp(52),gravity);
        if((gravity&Gravity.HORIZONTAL_GRAVITY_MASK)==Gravity.RIGHT)pos.rightMargin=dp(side);
        else pos.leftMargin=dp(side);
        pos.bottomMargin=dp(bottom);pad.addView(button,pos);
        button.setOnTouchListener((v,event) -> {
            if(event.getActionMasked()==MotionEvent.ACTION_DOWN) { if(held.add(code))onNativeKeyDown(code);v.setPressed(true); }
            if(event.getActionMasked()==MotionEvent.ACTION_UP || event.getActionMasked()==MotionEvent.ACTION_CANCEL) {
                if(held.remove(code))onNativeKeyUp(code);v.setPressed(false);
            }
            return true;
        });
    }
    private void release() { for(int code:held)onNativeKeyUp(code);held.clear(); }
    @Override protected void onPause() { release();super.onPause(); }
}
