"""Complete-file CACC encoding and checksum-validated decoding."""
from pathlib import Path
import ctypes as ct,struct,json,zlib,sys
import numpy as np
ROOT=Path(__file__).resolve().parent
from ._common import FORMAT_REVISION, HEADER, MAGIC, MAX_CHANNELS, validate
DLL=ct.CDLL(str(ROOT/'_bin/batch.dll'))
P,I,L=ct.c_void_p,ct.c_int,ct.c_int64
DLL.deadline_encode.argtypes=[P,L,I,I,P,L,P];DLL.deadline_encode.restype=L
DLL.deadline_decode.argtypes=[P,L,I,I,P,L,P];DLL.deadline_decode.restype=L


def encode(x,names,fps,*,bits=12,diagnostic=False):
    x=validate(x,names,fps,bits);n,c=x.shape
    meta=json.dumps(list(names),ensure_ascii=False,separators=(',',':')).encode()
    head=HEADER.pack(MAGIC,FORMAT_REVISION,bits,c,2,float(fps),len(meta),0)
    header=head[:-4]+struct.pack('<I',zlib.crc32(head[:-4]+meta))+meta
    out=np.empty(n*c*(bits+2)*2+n*16+64,np.uint8)
    stats=np.empty((n,3),np.int64) if diagnostic else None
    size=DLL.deadline_encode(x.ctypes.data,n,c,bits,out.ctypes.data,len(out),stats.ctypes.data if diagnostic else None)
    if size<=0:raise ValueError(('Deadline encode',size))
    blob=header+out[:size].tobytes()+struct.pack('<Q',n)
    return (blob,stats) if diagnostic else blob


def decode(blob,*,diagnostic=False):
    blob=bytes(blob)
    if len(blob)<HEADER.size+17:raise ValueError('Short CACC')
    tag,variant,bits,c,deadline,fps,nlen,crc=HEADER.unpack_from(blob)
    at=HEADER.size+nlen
    if tag!=MAGIC or variant!=FORMAT_REVISION or deadline!=2 or not 2<=bits<=14 or not 0<c<=MAX_CHANNELS or not np.isfinite(fps) or fps<=0 or nlen>1000000 or at>len(blob)-17:raise ValueError('CACC header')
    if zlib.crc32(blob[:HEADER.size-4]+blob[HEADER.size:at])!=crc:raise ValueError('CACC header CRC')
    names=json.loads(blob[HEADER.size:at])
    if not isinstance(names,list) or len(names)!=c or not all(isinstance(v,str) for v in names):raise ValueError('CACC names')
    n=struct.unpack_from('<Q',blob,len(blob)-8)[0]
    if not 0<n<(1<<27):raise ValueError('CACC frame count')
    payload=np.frombuffer(blob,np.uint8,offset=at,count=len(blob)-at-8)
    out=np.empty((n,c),np.float32);stats=np.empty((n,3),np.int64) if diagnostic else None
    size=DLL.deadline_decode(payload.ctypes.data,len(payload),c,bits,out.ctypes.data,n,stats.ctypes.data if diagnostic else None)
    if size!=n:raise ValueError(('Deadline decode',size,n))
    result=(out,dict(names=names,fps=fps,header_bytes=at,frame_count_footer_bytes=8))
    return result+(stats,) if diagnostic else result
