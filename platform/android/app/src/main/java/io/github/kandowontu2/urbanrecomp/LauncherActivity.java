package io.github.kandowontu2.urbanrecomp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.graphics.Color;
import android.view.Gravity;
import android.widget.*;
import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

public final class LauncherActivity extends Activity {
    private TextView status;
    private Button play, choose;
    private boolean ready;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);box.setPadding(32,16,32,16);box.setBackgroundColor(Color.rgb(45,33,8));
        TextView title=new TextView(this);title.setText("UrbanRecomp Enhanced");title.setTextSize(28);title.setTextColor(Color.rgb(255,238,175));box.addView(title);
        status=new TextView(this);status.setText("Preparing bundled soundtrack and assets…");status.setTextColor(Color.WHITE);status.setGravity(Gravity.CENTER);box.addView(status);
        choose=new Button(this);choose.setText("Select SimCity ROM");choose.setEnabled(false);box.addView(choose);
        play=new Button(this);play.setText("Play");play.setEnabled(false);box.addView(play);
        Button credits=new Button(this);credits.setText("Credits & controls");box.addView(credits);
        credits.setOnClickListener(v-> {try {
            String text=new String(Files.readAllBytes(new File(getFilesDir(),"MOBILE.md").toPath()),java.nio.charset.StandardCharsets.UTF_8);
            text+="\n\n"+new String(Files.readAllBytes(new File(getFilesDir(),"CREDITS.md").toPath()),java.nio.charset.StandardCharsets.UTF_8);
            TextView view=new TextView(this);view.setText(text);view.setPadding(24,16,24,16);ScrollView scroll=new ScrollView(this);scroll.addView(view);
            new android.app.AlertDialog.Builder(this).setTitle("Credits & controls").setView(scroll).setPositiveButton("Close",null).show();
        }catch(IOException e) {status.setText(e.getMessage());} });
        setContentView(box);
        choose.setOnClickListener(v-> {Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("*/*");startActivityForResult(intent,1);});
        play.setOnClickListener(v->startActivity(new Intent(this,GameActivity.class)));
        new Thread(()-> {try {
            File marker=new File(getFilesDir(),"assets-1.0.1.ready");
            if(!marker.exists()) {copyAssets("",getFilesDir());Files.write(marker.toPath(),new byte[]{1});}
            ready=true;runOnUiThread(()->refresh(""));
        }catch(IOException e) {runOnUiThread(()->status.setText("Assets could not be prepared: "+e.getMessage()));} },"UrbanRecomp assets").start();
    }
    private void refresh(String message) {
        choose.setEnabled(ready);boolean rom=new File(getFilesDir(),"simcity-us.sfc").isFile();play.setEnabled(ready);
        status.setText(message.isEmpty()?(rom?"ROM ready. Saves remain in app storage.":"Select your own clean US SimCity SNES ROM. No ROM is included."):message);
    }
    private void copyAssets(String path,File target) throws IOException {
        String[] children=getAssets().list(path);
        if(children.length>0) {if(!target.isDirectory()&&!target.mkdirs())throw new IOException("Cannot create asset folder");for(String name:children)copyAssets(path.isEmpty()?name:path+"/"+name,new File(target,name));}
        else {File tmp=new File(target.getPath()+".tmp");try(InputStream in=getAssets().open(path);OutputStream out=new FileOutputStream(tmp)) {byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}Files.move(tmp.toPath(),target.toPath(),StandardCopyOption.REPLACE_EXISTING);}
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);if(request!=1||result!=RESULT_OK||data==null)return;
        choose.setEnabled(false);play.setEnabled(false);status.setText("Checking ROM…");
        new Thread(()-> {String message;try(InputStream in=getContentResolver().openInputStream(data.getData())) {
            ByteArrayOutputStream buffer=new ByteArrayOutputStream();byte[] block=new byte[16384];int n;
            while((n=in.read(block))!=-1) {if(buffer.size()+n>0x80200)throw new IOException("ROM has an unexpected size");buffer.write(block,0,n);}
            byte[] bytes=buffer.toByteArray();if(bytes.length!=0x80000&&bytes.length!=0x80200)throw new IOException("Expected a 512 KB US SimCity ROM");
            int offset=bytes.length-0x80000,hash=0x811c9dc5;for(int i=offset;i<bytes.length;i++)hash=(hash^(bytes[i]&255))*0x01000193;
            if(hash!=0xec01686a)throw new IOException("ROM not recognized. Select a clean US SimCity SNES ROM");
            File tmp=new File(getFilesDir(),"simcity-us.sfc.tmp"),rom=new File(getFilesDir(),"simcity-us.sfc");
            try(OutputStream out=new FileOutputStream(tmp)) {out.write(bytes,offset,0x80000);}
            Files.move(tmp.toPath(),rom.toPath(),StandardCopyOption.REPLACE_EXISTING);message="ROM imported. Ready to play.";
        }catch(IOException|SecurityException e) {message=e.getMessage();}
            final String done=message;runOnUiThread(()->refresh(done));
        },"UrbanRecomp ROM import").start();
    }
}
