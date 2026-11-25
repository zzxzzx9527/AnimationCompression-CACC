"""CACC codec. Input is already quantized float32 in [0, 1].

Frames returned by StreamDecoder may precede their arithmetic-span CRC.
Batch decode validates the full stream before returning its reconstruction.
"""
from ._batch import encode, decode
from ._stream import StreamEncoder, StreamDecoder
__version__ = '0.1.0'
__all__ = ['encode', 'decode', 'StreamEncoder', 'StreamDecoder']
