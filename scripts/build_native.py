"""Build the frozen Windows CACC kernels and CPython/NumPy extensions."""
from pathlib import Path
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import sysconfig

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', help='Zig executable; otherwise uses ZIG or PATH')
    args = parser.parse_args()
    if os.name != 'nt' or struct.calcsize('P') != 8:
        parser.error('This saved fast API currently supports Windows x64 only.')
    import numpy as np
    zig = args.zig or os.environ.get('ZIG') or shutil.which('zig')
    if not zig:
        parser.error('Install Zig and set PATH/ZIG, or pass --zig <executable>.')
    if not Path(zig).is_file():
        parser.error('Zig executable does not exist.')
    dest = ROOT/'src/avatar_codec/_bin'
    dest.mkdir(parents=True, exist_ok=True)
    native = ROOT/'native'
    common = [zig, 'c++', '-O3', '-std=c++17', '-shared']
    commands = [
        common+[str(native/'deadline.cpp'), '-o', str(dest/'batch.dll')],
        common+[str(native/'live.cpp'), '-o', str(dest/'low_memory.dll')],
    ]
    libname = f'python{sys.version_info.major}{sys.version_info.minor}.lib'
    python_lib = Path(sys.base_prefix)/'libs'/libname
    if not python_lib.is_file():
        parser.error(f'Missing CPython import library: {python_lib}')
    commands.append(common+['-I'+sysconfig.get_path('include'), '-I'+np.get_include(),
        str(native/'buffer_bridge.cpp'), str(python_lib), '-o',
        str(dest/('_cacc_buffer_bridge'+sysconfig.get_config_var('EXT_SUFFIX')))])
    logdir = ROOT/'build';logdir.mkdir(exist_ok=True)
    for index, cmd in enumerate(commands):
        print(f'Building {index+1}/{len(commands)}: {Path(cmd[-1]).name}', flush=True)
        with (logdir/f'native-{index}.log').open('w', encoding='utf-8') as log:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
    (logdir/'native-build.json').write_text(json.dumps(dict(
        commands=commands, python=sys.version, numpy=np.__version__), indent=2)+'\n', 'utf-8')
    print(f'Built all {len(commands)} native components. Run tests in a new Python process.')


if __name__ == '__main__':
    main()
