#!/usr/bin/env python3
"""Host smoke test of actual Wokwi harness using fake GPIO, time and serial.
This does not run Wokwi or cross-compile for ESP32.
"""
import pathlib,subprocess,tempfile
r=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 exe=pathlib.Path(tmp)/'smoke'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-DRS_WOKWI=1','-I'+str(r/'tests/wokwi_host'),'-I'+str(r/'lib/RideShieldCore/src'),str(r/'src/wokwi_main.cpp'),str(r/'lib/RideShieldCore/src/core.cpp'),str(r/'tests/wokwi_host/smoke.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
