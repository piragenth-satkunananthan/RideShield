#!/usr/bin/env python3
"""Test the exact portable libraries used by hardware, plus rider inputs."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
core = root / 'lib/RideShieldCore/src'
inputs = root / 'lib/RideShieldInputs/src'
for name, flags, accepted in (
    ('default board', [], True),
    ('full pin map', ['-DRS_ENABLE_'+f+'=1' for f in ('OUTPUTS', 'GPS', 'SD', 'LTE', 'STRAP', 'MQ')], True),
    ('SMS without recipient', ['-DRS_ALLOW_SMS=1', '-DRS_ENABLE_LTE=1'], False),
    ('SMS in bench mode', ['-DRS_BENCH=1', '-DRS_ALLOW_SMS=1', '-DRS_ENABLE_LTE=1', '-DRS_SMS_NUMBER="+10000000000"'], False),
):
    result = subprocess.run(['g++', '-std=c++17', '-fsyntax-only', '-x', 'c++',
                             '-I'+str(root/'include'), *flags, '-'],
                            input='#include "board_config.h"\n', text=True, capture_output=True)
    if (result.returncode == 0) != accepted:
        raise RuntimeError(name+': unexpected compiler result\n'+result.stderr)
    print('PASS '+name+' '+('accepted' if accepted else 'rejected'), flush=True)
with tempfile.TemporaryDirectory(prefix='rideshield-tests-') as tmp:
    for name, sources in (
        ('core', [*core.glob('*.cpp'), root.parents[1] / 'dev_version/rideshield-core/tests/test_core.cpp']),
        ('inputs', [*core.glob('*.cpp'), *inputs.glob('*.cpp'), root / 'tests/test_inputs.cpp']),
    ):
        binary = str(Path(tmp) / name)
        subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I'+str(core), '-I'+str(inputs), *map(str, sources), '-o', binary], check=True)
        subprocess.run([binary], check=True)
