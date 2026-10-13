#!/usr/bin/env python3
import pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[1];build=root/'build';build.mkdir(exist_ok=True)
src=root/'lib/RideShieldCore/src';binary=build/'demo'
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(src),str(src/'core.cpp'),str(src/'phone.cpp'),str(src/'codec.cpp'),str(root/'tests/demo.cpp'),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True);(build/'demo.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
