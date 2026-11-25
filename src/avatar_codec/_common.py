"""Format declarations and input validation; no implicit quantization."""
import struct
import numpy as np

MAGIC = b'CACC'
FORMAT_REVISION = 5
MAX_CHANNELS = 93
HEADER = struct.Struct('<4sBBHHdII')


def validate(target, names, fps, bits):
    x = np.ascontiguousarray(target, np.float32)
    if (x.ndim != 2 or not 0 < len(x) < 2**27 or not 0 < x.shape[1] <= MAX_CHANNELS
        or len(names) != x.shape[1] or not all(isinstance(n, str) for n in names)
        or not isinstance(bits, (int, np.integer)) or not 2 <= bits <= 14
        or not np.isfinite(fps) or fps <= 0):
        raise ValueError('Settings')
    return x
