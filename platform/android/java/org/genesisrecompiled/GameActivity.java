package org.genesisrecompiled;

import android.os.Bundle;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.widget.Toast;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.atomic.AtomicReference;
import android.os.Build;
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
    private Runnable unregisterBack;
    private static final int OPEN_DOCUMENT = 7319;
    private final AtomicReference<DocumentRequest> document = new AtomicReference<>();

    private static final class DocumentRequest {
        final String[] patterns;
        final CompletableFuture<byte[]> result = new CompletableFuture<>();

        DocumentRequest(String[] patterns) {
            this.patterns = patterns;
        }
    }

    /** Called on SDL's native thread. Android owns the document UI and reports
     * the result to the main thread, so a launcher request can wait here. */
    public byte[] openDocument(String title, String[] patterns) {
        DocumentRequest request = new DocumentRequest(patterns);
        if (!document.compareAndSet(null, request)) return null;
        runOnUiThread(() -> {
            if (document.get() != request) return;
            if (isFinishing()) {
                finishDocument(request, null);
                return;
            }
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*")
                .putExtra(Intent.EXTRA_TITLE, title);
            try {
                startActivityForResult(intent, OPEN_DOCUMENT);
            } catch (RuntimeException error) {
                documentError(request, "The system file picker is unavailable.");
            }
        });
        try {
            return request.result.get();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            finishDocument(request, null);
        } catch (ExecutionException error) {
            finishDocument(request, null);
        }
        return null;
    }

    @Override protected void onActivityResult(int code, int result, Intent intent) {
        super.onActivityResult(code, result, intent);
        if (code != OPEN_DOCUMENT) return;
        DocumentRequest request = document.get();
        if (request == null) return;
        Uri uri = result == RESULT_OK && intent != null ? intent.getData() : null;
        if (uri == null) {
            finishDocument(request, null);
            return;
        }
        new Thread(() -> {
            try {
                String name = documentName(uri);
                if (!matches(name, request.patterns)) throw new IOException("Choose a supported file.");
                File file = copyDocument(uri, name);
                finishDocument(request, file.getAbsolutePath().getBytes(StandardCharsets.UTF_8));
            } catch (Exception error) {
                documentError(request, error.getMessage() != null ? error.getMessage() : "The file could not be imported.");
            }
        }, "genesis-document-import").start();
    }

    private void finishDocument(DocumentRequest request, byte[] path) {
        if (document.compareAndSet(request, null)) request.result.complete(path);
    }

    private void documentError(DocumentRequest request, String message) {
        runOnUiThread(() -> {
            if (document.get() != request) return;
            Toast.makeText(this, message, Toast.LENGTH_LONG).show();
            finishDocument(request, null);
        });
    }

    private String documentName(Uri uri) throws IOException {
        String name = null;
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        }
        if (name == null || name.isEmpty() || !new File(name).getName().equals(name) ||
                ".".equals(name) || "..".equals(name) || name.chars().anyMatch(Character::isISOControl)) {
            throw new IOException("The document has no usable file name.");
        }
        return name;
    }

    private static boolean matches(String name, String[] patterns) {
        if (patterns == null || patterns.length == 0) return true;
        String lower = name.toLowerCase(Locale.ROOT);
        int dot = lower.lastIndexOf('.');
        for (String pattern : patterns) {
            if (pattern == null) continue;
            if ("*".equals(pattern) || "*.*".equals(pattern)) return true;
            if (dot >= 0 && pattern.startsWith("*.") &&
                    lower.substring(dot).equals(pattern.substring(1).toLowerCase(Locale.ROOT))) return true;
        }
        return false;
    }

    private File copyDocument(Uri uri, String name) throws Exception {
        File imports = new File(getFilesDir(), "imported-documents");
        if (!imports.isDirectory() && !imports.mkdir())
            throw new IOException("Cannot open app storage for imports.");
        File staged = File.createTempFile("document-", ".pending", imports);
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(staged)) {
                if (input == null) throw new IOException("Cannot read the selected document.");
                byte[] buffer = new byte[64 * 1024];
                for (int size; (size = input.read(buffer)) != -1;) {
                    output.write(buffer, 0, size);
                    digest.update(buffer, 0, size);
                }
            }
            StringBuilder identity = new StringBuilder(64);
            for (byte value : digest.digest()) {
                identity.append(Character.forDigit((value >> 4) & 15, 16));
                identity.append(Character.forDigit(value & 15, 16));
            }
            File content = new File(imports, identity.toString());
            if (!content.isDirectory() && !content.mkdir()) throw new IOException("Cannot prepare the imported file.");
            File output = new File(content, name);
            if (!output.isFile() && !staged.renameTo(output))
                throw new IOException("Cannot finish importing the selected file.");
            return output;
        } finally {
            staged.delete();
        }
    }

    /* Both system Back routes use the same native Escape/menu semantics as
     * the gameplay Menu control. Release held touch keys before transferring
     * ownership; this never finishes the Activity behind an open native menu. */
    private void nativeBack() {
        release();
        onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
        onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
    }
    @Override public void onBackPressed() { nativeBack(); }

    /* Isolate API33 classes so API24-32 only load the legacy path. No AndroidX
     * dependency is needed for this SDL Activity. */
    @android.annotation.TargetApi(33)
    private static final class BackApi33 {
        static Runnable register(GameActivity activity) {
            android.window.OnBackInvokedDispatcher dispatcher=activity.getOnBackInvokedDispatcher();
            android.window.OnBackInvokedCallback callback=activity::nativeBack;
            dispatcher.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,callback);
            return () -> dispatcher.unregisterOnBackInvokedCallback(callback);
        }
    }
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
        if(Build.VERSION.SDK_INT>=33)unregisterBack=BackApi33.register(this);
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
    @Override protected void onDestroy() {
        DocumentRequest pending = document.getAndSet(null);
        if (pending != null) pending.result.complete(null);
        if(unregisterBack!=null) { unregisterBack.run();unregisterBack=null; }
        super.onDestroy();
    }
}
