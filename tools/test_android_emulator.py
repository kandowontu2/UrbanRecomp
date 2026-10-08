#!/usr/bin/env python3
import os, subprocess, time, xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'platform-deps/android-validation';out.mkdir(parents=True,exist_ok=True)
temp=Path(os.environ['RUNNER_TEMP']);key=temp/'validation-only.p12';apk=temp/'validation-only.apk'
subprocess.run(['keytool','-genkeypair','-keystore',str(key),'-alias','validation','-storepass','validation-only','-keypass','validation-only','-keyalg','RSA','-keysize','2048','-validity','2','-dname','CN=Temporary simulator validation'],check=True)
signer=Path(os.environ['ANDROID_HOME'])/'build-tools/35.0.0/apksigner'
source=root/'platform/android/app/build/outputs/apk/release/app-release-unsigned.apk'
subprocess.run([str(signer),'sign','--ks',str(key),'--ks-pass','pass:validation-only','--out',str(apk),str(source)],check=True)
def adb(*args):return subprocess.check_output(['adb',*args],text=True)
adb('install',str(apk));adb('logcat','-c')
adb('shell','wm','size','1280x720')
adb('shell','input','keyevent','82');adb('shell','wm','dismiss-keyguard')
print(adb('shell','am','start','-W','-n','io.github.kandowontu2.urbanrecomp/.LauncherActivity'))
play=None
for attempt in range(12):
    time.sleep(5)
    adb('shell','uiautomator','dump','/sdcard/window.xml');xml=adb('shell','cat','/sdcard/window.xml');(out/'launcher.xml').write_text(xml)
    logs=adb('logcat','-d');(out/'logcat.txt').write_text(logs)
    play=next((e for e in ET.fromstring(xml).iter('node') if e.attrib.get('text','').casefold()=='play'),None)
    nodes=list(ET.fromstring(xml).iter('node'))
    if any('Quickstep isn' in e.attrib.get('text','') for e in nodes):
        close=next(e for e in nodes if e.attrib.get('resource-id')=='android:id/aerr_close')
        import re
        x1,y1,x2,y2=map(int,re.findall(r'\d+',close.attrib['bounds']))
        adb('shell','input','tap',str((x1+x2)//2),str((y1+y2)//2))
    if play is not None and play.attrib.get('enabled')=='true':break
print(xml)
adb('shell','screencap','-p','/sdcard/launcher.png');adb('pull','/sdcard/launcher.png',str(out/'launcher.png'))
assert play is not None and play.attrib.get('enabled')=='true','Bundled assets/launcher failed to initialize'
import re
x1,y1,x2,y2=map(int,re.findall(r'\d+',play.attrib['bounds']));adb('shell','input','tap',str((x1+x2)//2),str((y1+y2)//2))
time.sleep(15)
logs=adb('logcat','-d');(out/'logcat.txt').write_text(logs)
assert 'Running main function SDL_main' in logs,'SDL native entry did not start'
assert 'Fatal signal' not in logs and 'FATAL EXCEPTION' not in logs,'Native/Java startup crashed'
adb('shell','screencap','-p','/sdcard/rom-picker.png');adb('pull','/sdcard/rom-picker.png',str(out/'rom-picker.png'))
adb('shell','uiautomator','dump','/sdcard/window.xml');picker=adb('shell','cat','/sdcard/window.xml');(out/'rom-picker.xml').write_text(picker)
assert 'com.android.documentsui' in picker,'Native ROM picker was not presented'
pid=adb('shell','pidof','io.github.kandowontu2.urbanrecomp').strip()
assert pid,'App exited while opening the ROM picker'
adb('shell','input','keyevent','4');time.sleep(2)
adb('shell','uiautomator','dump','/sdcard/window.xml');returned=adb('shell','cat','/sdcard/window.xml')
ack=next((e for e in ET.fromstring(returned).iter('node') if e.attrib.get('text','').casefold()=='got it'),None)
if ack is not None:
    x1,y1,x2,y2=map(int,re.findall(r'\d+',ack.attrib['bounds']))
    adb('shell','input','tap',str((x1+x2)//2),str((y1+y2)//2));time.sleep(2)
adb('shell','screencap','-p','/sdcard/native-launcher.png');adb('pull','/sdcard/native-launcher.png',str(out/'native-launcher.png'))
assert adb('shell','pidof','io.github.kandowontu2.urbanrecomp').strip()==pid,'App exited on ROM-picker cancellation'
adb('shell','input','tap','640','360');time.sleep(3)
adb('shell','uiautomator','dump','/sdcard/window.xml');reopened=adb('shell','cat','/sdcard/window.xml')
(out/'reopened.xml').write_text(reopened);(out/'logcat.txt').write_text(adb('logcat','-d'))
assert 'com.android.documentsui' in reopened,'ROM picker did not reopen after cancellation'
key.unlink();apk.unlink()
print('Android emulator: assets, launcher, native SDL entry and ROM picker passed; production key never used')
