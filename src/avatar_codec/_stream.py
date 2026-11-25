"""Incremental CACC encoding and bounded-memory decoding."""

import ctypes as ct
from functools import lru_cache
import json
import struct
import zlib

import numpy as np

from ._common import HEADER, MAGIC, FORMAT_REVISION, MAX_CHANNELS
from ._native_loader import BIN, extension


@lru_cache(maxsize=1)
def _library():
    lib = ct.CDLL(str(BIN / 'low_memory.dll'))
    p, i, l = ct.c_void_p, ct.c_int, ct.c_int64
    for name, args, result in (
        ('cacc_encoder_create', [i, i], p), ('cacc_encoder_destroy', [p], None),
        ('cacc_encoder_push', [p, p, p, l], l), ('cacc_encoder_end', [p, p, l], l),
        ('cacc_decoder_create', [i, i], p), ('cacc_decoder_destroy', [p], None),
        ('cacc_decoder_bytes', [p], ct.c_uint64),
    ):
        fn = getattr(lib, name)
        fn.argtypes, fn.restype = args, result
    return lib


@lru_cache(maxsize=1)
def _feed_frame_batch():
    bridge = extension('_cacc_buffer_bridge')
    bridge.bind_low_memory(str(BIN / 'low_memory.dll'))
    return bridge.feed_low_memory


class State:
    native=None
    def close(self):
        if getattr(self,'handle',None):
            getattr(self.native,'cacc_'+self.role+'_destroy')(self.handle);self.handle=None
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
    def __del__(self):self.close()


class StreamEncoder(State):
    role='encoder'
    def __init__(self,names,fps,*,bits=12):
        self.native = _library()
        if not isinstance(names,(tuple,list)) or not 0<len(names)<=MAX_CHANNELS or not all(isinstance(n,str) for n in names) or not isinstance(bits,(int,np.integer)) or not 2<=bits<=14 or not np.isfinite(fps) or fps<=0:raise ValueError('Settings')
        meta=json.dumps(list(names),ensure_ascii=False,separators=(',',':')).encode()
        if len(meta)>1000000:raise ValueError('Names too long')
        h=HEADER.pack(MAGIC,FORMAT_REVISION,bits,len(names),2,float(fps),len(meta),0)
        self.header=h[:-4]+struct.pack('<I',zlib.crc32(h[:-4]+meta))+meta
        self.c=len(names);self.handle=self.native.cacc_encoder_create(self.c,bits)
        if not self.handle:raise MemoryError('Native state')
        self.out=np.empty(18*self.c*(bits+2)*2+128,np.uint8);self.pointer=self.out.ctypes.data
        self.seen=0;self.ended=False
    def push(self,frame):
        if not self.handle or self.ended:raise ValueError('Closed')
        x=np.ascontiguousarray(frame,np.float32)
        if x.shape!=(self.c,):raise ValueError('Expected one frame')
        n=self.native.cacc_encoder_push(self.handle,x.ctypes.data,self.pointer,len(self.out))
        if n==-321:raise ValueError('Expected exact quantized frame')
        if n<0:self.close();raise ValueError(('Push',n))
        self.seen+=1;return self.out[:n].tobytes()
    def finish(self):
        if not self.handle or self.ended:raise ValueError('Closed')
        n=self.native.cacc_encoder_end(self.handle,self.pointer,len(self.out))
        if n<=0:self.close();raise ValueError(('End',n))
        b=self.out[:n].tobytes();self.ended=True;self.close();return b


class StreamDecoder(State):
    role = 'decoder'

    def __init__(self, header):
        if len(header) < HEADER.size:
            raise ValueError('Short CACC header')
        tag, revision, bits, channels, deadline, fps, length, crc = HEADER.unpack_from(header)
        if (tag != MAGIC or revision != FORMAT_REVISION or deadline != 2
                or not 2 <= bits <= 14 or not 0 < channels <= MAX_CHANNELS
                or not np.isfinite(fps) or fps <= 0
                or length > 1000000 or len(header) != HEADER.size + length):
            raise ValueError('CACC header')
        metadata = header[HEADER.size:]
        if zlib.crc32(header[:HEADER.size - 4] + metadata) != crc:
            raise ValueError('CACC header CRC')
        names = json.loads(metadata)
        if not isinstance(names, list) or len(names) != channels or not all(isinstance(name, str) for name in names):
            raise ValueError('CACC names')

        self.native = _library()
        self._feed_batch = _feed_frame_batch()
        self._bits = bits
        self.c, self.names, self.fps = channels, names, fps
        self.seen, self.ended = 0, False
        self.handle = self.native.cacc_decoder_create(channels, bits)
        if not self.handle:
            raise MemoryError('Native state')

    @property
    def working_bytes(self):
        """Bytes reserved by the native decoder and its buffers."""
        return self.native.cacc_decoder_bytes(self.handle) if self.handle else 0

    def feed(self, chunk):
        """Iterate over decoded frames. Consume this iterator before sending the next chunk."""
        if not self.handle or self.ended:
            raise ValueError('Closed')
        data = memoryview(chunk).cast('B')
        for offset in range(0, len(data), 256):
            payload = bytes(data[offset:offset + 256])
            while True:
                try:
                    decoded, ended = self._feed_batch(self.handle, payload, self.c, self._bits)
                except ValueError:
                    self.close()
                    raise
                count = len(decoded)
                if count:
                    self.seen += count
                    yield from decoded
                if ended:
                    self.ended = True
                    self.close()
                    if offset + 256 < len(data):
                        raise ValueError('Trailing bytes')
                    return
                if count < 3:
                    break
                payload = b''
