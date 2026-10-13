#!/usr/bin/env python3
"""Compile-time guards must reject profiles that could accidentally send SMS."""
import pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[1]
cases=[('default',[],True),('bench SMS',['-DRS_ALLOW_SMS=1','-DRS_ENABLE_LTE=1'],False),('missing recipient',['-DRS_BENCH=0','-DRS_ALLOW_SMS=1','-DRS_ENABLE_LTE=1'],False)]
lines=[]
for name,flags,expected in cases:
    r=subprocess.run(['g++','-std=c++17','-fsyntax-only','-x','c++','-I'+str(root/'include'),*flags,'-'],input='#include "board_config.h"\n',text=True,capture_output=True)
    if (r.returncode==0)!=expected: raise SystemExit(name+' unexpected result\n'+r.stderr)
    lines.append('PASS '+name+(' accepted' if expected else ' rejected'))
text='\n'.join(lines)+'\n';print(text,end='');(root/'build').mkdir(exist_ok=True);(root/'build/board-guards.txt').write_text(text)
