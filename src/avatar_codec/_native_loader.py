"""Load package-local CPython extensions without historical path imports."""
from pathlib import Path
import importlib.util
import sys
import sysconfig

BIN = Path(__file__).resolve().parent/'_bin'


def extension(name):
    qualified = __package__+'.'+name
    if qualified in sys.modules:
        return sys.modules[qualified]
    path = BIN/(name+sysconfig.get_config_var('EXT_SUFFIX'))
    if not path.exists():
        raise ImportError(f'Missing {path.name}. Run scripts/build_native.py with this Python.')
    spec = importlib.util.spec_from_file_location(qualified, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[qualified] = module
    spec.loader.exec_module(module)
    return module
