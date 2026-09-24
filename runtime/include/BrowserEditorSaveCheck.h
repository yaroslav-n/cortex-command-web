#pragma once
#include "PresetMan.h"
#include <emscripten.h>
namespace RTE {
inline void BrowserEditorSaveReport(const char* message) {
 MAIN_THREAD_EM_ASM({let p=document.getElementById('editor-save-result');if(!p){p=document.createElement('pre');p.id='editor-save-result';p.style='position:fixed;top:0;left:0;z-index:9999;background:#111;color:white;padding:10px';document.body.appendChild(p);}p.textContent=UTF8ToString($0);},message);
}
inline void BrowserEditorPersistCheck(const std::string& name,bool saved) {
 if(!saved){BrowserEditorSaveReport("FAIL editor scene serializer");return;}
 BrowserEditorSaveReport("Editor scene serialized; checking files and committing IndexedDB");
 MAIN_THREAD_EM_ASM({
  const name=UTF8ToString($0);const key='cortex-editor-save-contract';
  const report=s=>{document.getElementById('editor-save-result').textContent=s;console.log(s);};
  const digest=async bytes=>Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))).map(b=>b.toString(16).padStart(2,'0')).join('');
  (async()=>{
   try {
    const files=[];
    for(const suffix of ['.ini','.preview.png']){
     const path='/Userdata/UserScenes.rte/'+name+suffix;const bytes=FS.readFile(path);
     if(!bytes.length)throw Error('empty '+suffix);
     if(suffix==='.ini'&&!new TextDecoder().decode(bytes).includes('AddScene'))throw Error('missing AddScene');
     if(suffix==='.preview.png'&&![137,80,78,71,13,10,26,10].every((v,i)=>bytes[i]===v))throw Error('invalid preview signature');
     files.push({path,size:bytes.length,sha256:await digest(bytes)});
    }
    await new Promise((resolve,reject)=>FS.syncfs(false,e=>e?reject(e):resolve()));
    localStorage.setItem(key,JSON.stringify({name,files}));
    report('PASS editor serialization and IndexedDB commit: '+name+'; open ?editor-save-restore-check to verify a fresh runtime');
   }catch(e){report('FAIL editor persistence: '+e);}
  })();
 },name.c_str());
}
inline void BrowserEditorRestoreCheck() {
 static const bool enabled=MAIN_THREAD_EM_ASM_INT({return new URLSearchParams(location.search).has('editor-save-restore-check');});
 static bool ran=false;if(!enabled||ran)return;ran=true;
 char name[128]{};
 const bool manifest=MAIN_THREAD_EM_ASM_INT({try{const m=JSON.parse(localStorage.getItem('cortex-editor-save-contract'));if(!m||!/^BrowserEditorCheck[0-9]+$/.test(m.name)||m.name.length>=128)return 0;stringToUTF8(m.name,$0,128);return 1;}catch(e){return 0;}},name);
 if(!manifest){BrowserEditorSaveReport("FAIL missing editor save manifest");return;}
 if(!g_PresetMan.GetEntityPreset("Scene",name,g_PresetMan.GetModuleID("UserScenes.rte"))){BrowserEditorSaveReport("FAIL saved scene absent from restored preset registry");return;}
 BrowserEditorSaveReport("Saved scene registered; checking restored files");
 MAIN_THREAD_EM_ASM({
  const report=s=>{document.getElementById('editor-save-result').textContent=s;console.log(s);};
  (async()=>{try{
   const m=JSON.parse(localStorage.getItem('cortex-editor-save-contract'));
   if(!Array.isArray(m.files)||m.files.length!==2)throw Error('invalid manifest');
   for(const f of m.files){
    if(!f.path.startsWith('/Userdata/UserScenes.rte/'+m.name+'.'))throw Error('invalid path');
    const b=FS.readFile(f.path);const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',b))).map(v=>v.toString(16).padStart(2,'0')).join('');
    if(b.length!==f.size||hash!==f.sha256)throw Error('restored bytes differ: '+f.path);
   }
   report('PASS fresh runtime: scene preset registered; scene INI and PNG preview restored byte-for-byte: '+m.name);
  }catch(e){report('FAIL editor restore: '+e);}})();
 });
}
}
