"""Independent Python integer decoder for the CACC bitstream."""
import json,struct,zlib
import numpy as np
from ._common import FORMAT_REVISION, HEADER as H, MAGIC, MAX_CHANNELS


class Arithmetic:
    def __init__(self,data,at):
        self.data=data;self.start=at;self.at=at;self.span=2**32-1;self.code=0
        self.low=0;self.cached=1;self.emitted=0
        if at+5>len(data):raise ValueError('Short initial range')
        for _ in range(5):self.code=(self.code*256+data[self.at])%(2**32);self.at+=1
    def take(self,cumulative,frequency,total,binary=False):
        step=self.span//total;offset=step*cumulative
        self.code-=offset;self.low+=offset
        self.span=self.span-offset if binary and cumulative else step*frequency
        if self.code<0:raise ValueError('Negative code')
        while self.span<2**24:
            if self.at>=len(self.data):raise ValueError('Short range')
            self.span*=256;self.code=(self.code*256+self.data[self.at])%(2**32);self.at+=1
            lo=self.low%(2**32);carry=self.low//(2**32)
            if lo<0xff000000 or carry:self.emitted+=self.cached;self.cached=0
            self.cached+=1;self.low=(lo*256)%(2**32)
    def bit(self,p):
        result=int(self.code>=(self.span//65536)*p)
        self.take(p if result else 0,65536-p if result else p,65536,True)
        return result
    def category(self,row):
        step=self.span//sum(row);cum=0
        for result,freq in enumerate(row):
            if self.code<step*(cum+freq):
                self.take(cum,freq,sum(row));return result
            cum+=freq
        raise ValueError('Illegal category')


def update(row,symbol):
    row[symbol]+=1
    if sum(row)>=2048:
        for j in range(len(row)):row[j]=(row[j]+1)//2


def decode(data):
    tag,v,bits,c,d,fps,n,checksum=H.unpack_from(data)
    header=H.size+n
    if tag!=MAGIC or v!=FORMAT_REVISION or not 2<=bits<=14 or not 0<c<=MAX_CHANNELS or d!=2 or n>1000000 or header>len(data)-17 or not np.isfinite(fps) or fps<=0:raise ValueError('Header')
    if zlib.crc32(data[:H.size-4]+data[H.size:header])!=checksum:raise ValueError('Header CRC')
    names=json.loads(data[H.size:header])
    if not isinstance(names,list) or len(names)!=c or not all(isinstance(v,str) for v in names):raise ValueError('Names')
    expected=struct.unpack_from('<Q',data,len(data)-8)[0]
    raw=data[header:-8];counts=[[1]*4 for _ in range(16 + 20 * ((bits + 1) // 2))];gates=[[1,1] for _ in range(32)]
    old=[0]*c;history=[0]*c;run=[0]*c;previous=neighbor=0
    maximum=2**bits;planes=(bits+1)//2;seen=seq=0;at=0;result=[];stats=[]
    while True:
        coder=Arithmetic(raw,at);span_frames=0
        while True:
            if coder.bit(65280):
                crc=zlib.crc32(raw[at:coder.at]+struct.pack('<IH',seq,span_frames))
                if coder.at+4!=len(raw) or crc!=struct.unpack_from('<I',raw,coder.at)[0] or seen!=expected:raise ValueError('End CRC/count')
                return np.ascontiguousarray(np.array(result)/maximum,np.float32),dict(names=names,fps=fps,spans=seq+1,frames=seen),stats
            if seen>=expected:raise ValueError('Too many frames')
            frame=[]
            for channel in range(c):
                r=run[channel];bucket=r if r<4 else 4 if r<8 else 5 if r<16 else 6 if r<32 else 7
                rowid=bucket*2+int(previous!=0)+16*int(old[channel] in (0,maximum))
                row=gates[rowid];active=coder.bit(max(1,row[0]*65536//sum(row)));update(row,active)
                rank=word=0;delta=0
                if active:
                    temporal=history[channel];plane=planes-1
                    while plane>=0:
                        if rank and plane>=1:
                            total=4**plane;step=coder.span//total;literal=coder.code//step
                            if literal>=total:raise ValueError('Illegal literal')
                            coder.take(literal,1,total);rank+=literal*4;plane=0;continue
                        tid=temporal//(4**plane);nid=(neighbor//(4**plane))%4
                        inner=4*nid+tid%4 if rank else 16+20*plane+4*min(4,tid)+nid
                        row=counts[inner]
                        symbol=coder.category(row);update(row,symbol);rank+=symbol*4**plane;plane-=1
                    if rank>=maximum:raise ValueError('Illegal rank')
                    near=min(old[channel],maximum-old[channel])
                    if rank<2*near:delta=(rank+1)//2 if rank%2 else -(rank//2+1)
                    else:delta=(rank-near+1)*(-1 if old[channel]>maximum/2 else 1)
                    word=delta*2 if delta>=0 else -delta*2-1
                old[channel]+=delta
                if not 0<=old[channel]<=maximum:raise ValueError('Control bounds')
                frame.append(old[channel]);run[channel]=0 if active else min(32,run[channel]+1)
                previous=word;history[channel]=rank;neighbor=rank
            seen+=1;span_frames+=1
            flush=bool(coder.bit(61440))
            if (seen==1 or span_frames==18 or seen%576==0) and not flush:raise ValueError('Span boundary')
            result.append(frame);stats.append((coder.emitted,coder.at-at,int(flush)))
            if flush:
                crc=zlib.crc32(raw[at:coder.at]+struct.pack('<IH',seq,span_frames))
                if coder.at+4>len(raw) or crc!=struct.unpack_from('<I',raw,coder.at)[0]:raise ValueError('Span CRC')
                at=coder.at+4;seq+=1;break
