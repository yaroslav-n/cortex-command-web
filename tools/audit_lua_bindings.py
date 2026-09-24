#!/usr/bin/env python3
"""Compile temporary typed versions of explicit Lua member casts; never edits engine sources. Run with emsdk_env.sh loaded."""
import json,shlex,subprocess,re,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
temporary=tempfile.TemporaryDirectory(prefix='cortex-member-audit-')
tmp=Path(temporary.name)
pattern=re.compile(r'\(([^()\n]+?\([^()\n]*::\*\)\([^()\n]*\)(?:\s*const)?)\)\s*&\s*([\w:]+)')
commands=json.loads((root/'build/wasm/compile_commands.json').read_text())
failed=False
for entry in commands:
    if '/Source/Lua/LuaBindings' not in entry['file'] or not entry['file'].endswith('.cpp'):continue
    original=Path(entry['file']);source,count=pattern.subn(r'static_cast<\1>(&\2)',original.read_text())
    if not count:continue
    target=tmp/original.name;target.write_text(source)
    print(f'{original.name}: validating {count} member casts',flush=True)
    args=shlex.split(entry['command']);oi=args.index('-o');del args[oi:oi+2];args.remove('-c');args=[str(target) if a==entry['file'] else a for a in args]
    args.extend(['-fsyntax-only','-Wno-deprecated-comma-subscript'])
    result=subprocess.run(args,cwd=entry['directory'],capture_output=True,text=True)
    if result.returncode:print(result.stdout+result.stderr,flush=True);failed=True
raise SystemExit(1 if failed else 0)
