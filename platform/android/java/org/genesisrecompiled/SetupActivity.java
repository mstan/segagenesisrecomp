package org.genesisrecompiled;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.util.AtomicFile;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.*;
import java.util.Locale;
import java.util.zip.CRC32;

/** Imports a supported, user-owned cartridge into private app storage. */
public final class SetupActivity extends Activity {
    private TextView status;
    private Button play;
    private String error="";
    private File rom() { return new File(getFilesDir(),"game.bin"); }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout layout=new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);layout.setPadding(24,24,24,24);
        TextView title=new TextView(this);title.setText(BuildConfig.GAME_TITLE);title.setTextSize(24);layout.addView(title);
        TextView help=new TextView(this);
        help.setText("Import your own supported cartridge once. No game ROM is included.\n"+
            "Use the launcher's Mods page for widescreen and game options. Touch controls and external controllers are supported.");
        layout.addView(help);
        Button pick=new Button(this);pick.setText("Import game ROM");layout.addView(pick);
        pick.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .setType("*/*").addCategory(Intent.CATEGORY_OPENABLE),1));
        status=new TextView(this);layout.addView(status);
        play=new Button(this);play.setText("Open launcher");layout.addView(play);play.setOnClickListener(v -> launch());
        setContentView(layout);refresh();
        // The isolated debug package can be exercised without a document picker.
        if(BuildConfig.DEBUG && getIntent().getBooleanExtra("qaLaunch",false) && validImport())launch();
    }
    private static byte[] read(InputStream input,int maximum) throws IOException {
        try(InputStream in=input;ByteArrayOutputStream out=new ByteArrayOutputStream()) {
            byte[] chunk=new byte[8192];int n;
            while((n=in.read(chunk))!=-1) {
                if(out.size()+n>maximum)throw new IOException("Selected file is too large");
                out.write(chunk,0,n);
            }
            return out.toByteArray();
        }
    }
    private static boolean valid(byte[] image) {
        CRC32 crc=new CRC32();crc.update(image);
        return image.length==BuildConfig.ROM_SIZE &&
            String.format(Locale.ROOT,"%08x",crc.getValue()).equalsIgnoreCase(BuildConfig.ROM_CRC);
    }
    private boolean validImport() {
        try { return valid(read(new FileInputStream(rom()),BuildConfig.ROM_SIZE)); }
        catch(IOException failure) { return false; }
    }
    private static void write(File file,byte[] data) throws IOException {
        AtomicFile target=new AtomicFile(file);FileOutputStream stream=null;
        try { stream=target.startWrite();stream.write(data);target.finishWrite(stream); }
        catch(IOException failure) { if(stream!=null)target.failWrite(stream);throw failure; }
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request!=1 || result!=RESULT_OK || data==null || data.getData()==null)return;
        try {
            byte[] image=read(getContentResolver().openInputStream(data.getData()),BuildConfig.ROM_SIZE);
            if(!valid(image))throw new IOException("ROM size/CRC does not match this build ("+BuildConfig.ROM_CRC+").");
            write(rom(),image);error="";
        } catch(Exception failure) { error=failure.getMessage(); }
        refresh();
    }
    private void refresh() {
        boolean ready=validImport();
        status.setText("ROM: "+(ready?"ready":"missing or unsupported")+"\n"+error);play.setEnabled(ready);
    }
    private void copyAsset(String name,File destination) throws IOException {
        String[] children=getAssets().list(name);
        if(children!=null && children.length>0) {
            if(!destination.isDirectory() && !destination.mkdirs())throw new IOException("Cannot create asset directory");
            for(String child:children)copyAsset(name+"/"+child,new File(destination,child));
        } else write(destination,read(getAssets().open(name),32*1024*1024));
    }
    private void launch() {
        try {
            if(!validImport())throw new IOException("Import a supported ROM first.");
            copyAsset("payload",getFilesDir());
            write(new File(getFilesDir(),BuildConfig.ROM_CONFIG),(rom().getAbsolutePath()+"\n").getBytes("UTF-8"));
            Intent game=new Intent(this,GameActivity.class);
            if(BuildConfig.DEBUG && getIntent().getBooleanExtra("qaLaunch",false)) {
                game.putExtra("qaArgs",getIntent().getStringArrayExtra("qaArgs"));
                game.putExtra("qaMute",getIntent().getBooleanExtra("qaMute",true));
                game.putExtra("qaRbProbe",getIntent().getStringExtra("qaRbProbe"));
            }
            startActivity(game);
        } catch(Exception failure) {
            new AlertDialog.Builder(this).setMessage(failure.getMessage()).setPositiveButton("OK",null).show();
        }
    }
}
