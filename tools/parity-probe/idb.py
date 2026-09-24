#!/usr/bin/env python3
"""Emit JavaScript that edits the game's IndexedDB stores (IDBFS layout: files are
{timestamp, mode: 33206, contents}, directories {timestamp, mode: 16895})."""
import json, pathlib, sys
OPEN = "const open=n=>new Promise((res,rej)=>{const r=indexedDB.open(n);r.onsuccess=()=>res(r.result);r.onerror=()=>rej(r.error);});"
def write(store, puts, deletes=()):
    return (f"(async()=>{{{OPEN}const db=await open('{store}');const t=db.transaction('FILE_DATA','readwrite');const st=t.objectStore('FILE_DATA');const enc=new TextEncoder();"
            + ''.join(f"st.delete({json.dumps(k)});" for k in deletes)
            + ''.join(f"st.put({{timestamp:new Date(),mode:{mode}{',contents:enc.encode(' + json.dumps(text) + ')' if text is not None else ''}}},{json.dumps(k)});" for k, mode, text in puts)
            + "await new Promise((res,rej)=>{t.oncomplete=res;t.onerror=()=>rej(t.error);});return 'ok';})()")
# Every file a probe script can leave behind. Both install and restore delete them,
# so a run can never pick up the previous run's output.
OUTPUTS = ['/Userdata/ParityProbe.bin', '/Userdata/ParityTimeline.txt', '/Userdata/ParityEndgame.txt', '/Userdata/ParityScreen.txt']
MOD = pathlib.Path(__file__).parent / 'ParityProbe.rte'
MOD_FILES = [f'/Mods/ParityProbe.rte/{f.name}' for f in sorted(MOD.iterdir())]
cmd = sys.argv[1]
if cmd == 'install':
    mod, settings = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]).read_text()
    puts = [('/Mods/ParityProbe.rte', 16895, None)] + [(f'/Mods/ParityProbe.rte/{f.name}', 33206, f.read_text()) for f in sorted(mod.iterdir())]
    print("(async()=>{await " + write('/Mods', puts) + ";return await "
          + write('/Userdata', [('/Userdata/Settings.ini', 33206, settings)], OUTPUTS) + ";})()")
elif cmd == 'restore':
    settings = pathlib.Path(sys.argv[2]).read_text()
    print("(async()=>{await " + write('/Mods', [], MOD_FILES + ['/Mods/ParityProbe.rte']) + ";return await "
          + write('/Userdata', [('/Userdata/Settings.ini', 33206, settings)], OUTPUTS) + ";})()")
elif cmd == 'exists':
    print(f"(async()=>{{{OPEN}const db=await open('/Userdata');return await new Promise(res=>{{const r=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA').getKey({json.dumps(sys.argv[2])});r.onsuccess=()=>res(r.result?1:0);}});}})()")
elif cmd == 'shots':
    # The newest of the probe's screenshots in the /ScreenShots store ('' if none).
    print(f"(async()=>{{{OPEN}const db=await open('/ScreenShots');return await new Promise(res=>{{const r=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA').getAllKeys();r.onsuccess=()=>res(r.result.filter(k=>k.startsWith('/ScreenShots/ParityScreen_')).sort().pop()||'');}});}})()")
elif cmd == 'clearshots':
    # Optional prefix (default ParityScreen_): which screenshots to delete.
    prefix = '/ScreenShots/' + (sys.argv[2] if len(sys.argv) > 2 else 'ParityScreen_')
    print(f"(async()=>{{{OPEN}const db=await open('/ScreenShots');const t=db.transaction('FILE_DATA','readwrite');const st=t.objectStore('FILE_DATA');const keys=await new Promise(res=>{{const r=st.getAllKeys();r.onsuccess=()=>res(r.result);}});for(const k of keys)if(k.startsWith({json.dumps(prefix)}))st.delete(k);await new Promise((res,rej)=>{{t.oncomplete=res;t.onerror=()=>rej(t.error);}});return 'ok';}})()")
elif cmd == 'listshots':
    # Every screenshot whose name starts with the prefix, sorted, one per line.
    prefix = '/ScreenShots/' + sys.argv[2]
    print(f"(async()=>{{{OPEN}const db=await open('/ScreenShots');return await new Promise(res=>{{const r=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA').getAllKeys();r.onsuccess=()=>res(r.result.filter(k=>k.startsWith({json.dumps(prefix)})).sort().join(' '));}});}})()")
