#!/usr/bin/env python3
"""Compile the portable production modules, then run deterministic fault tests."""
import argparse, os, pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(); p.add_argument('--sanitize', action='store_true'); args = p.parse_args()
build = root / 'build'; build.mkdir(exist_ok=True)
name = 'native-sanitized' if args.sanitize else 'native-tests'
cmd = ['g++', '-std=c++17', '-g', '-O1' if args.sanitize else '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(root/'lib/RideShieldCore/src')]
if args.sanitize: cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
cmd += [str(f) for f in sorted((root/'lib/RideShieldCore/src').glob('*.cpp'))] + [str(root/'tests/test_core.cpp'), '-o', str(build/name)]
subprocess.run(cmd, check=True)
env = os.environ.copy()
if args.sanitize: env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
result = subprocess.run([str(build/name)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
(build/(name+'.txt')).write_text(result.stdout)
print(result.stdout, end=''); raise SystemExit(result.returncode)
