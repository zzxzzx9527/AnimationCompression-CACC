// CACC range-coding span termination.
#include "model_stream.cpp"
#include "deadline_context.inc"
static constexpr uint32_t continue_probability=65280; // EOS1 has fixed1/256 mass.
static constexpr uint32_t no_close_probability=61440;
struct Deadline{
 int count=0;int64_t previous=0,two_ago=0;
 bool choose(int64_t seen,int64_t emitted){
  return seen==1||count+1==18||seen%576==0||(count>=2&&emitted<two_ago);
 }
 void record(int64_t need){++count;two_ago=previous;previous=need;}
};
static uint32_t packet_crc(const uint8_t*data,int64_t n,uint32_t seq,int nf){
 uint8_t h[6];put_le(h,seq,4);put_le(h+4,nf,2);
 return crc_add(crc_add(0xffffffffU,data,n),h,6)^0xffffffffU;
}
template<int Bits> __attribute__((always_inline)) static inline int64_t advance_bits(
 const Input *words,int64_t n,int c,uint64_t *models,
 DeadlineHistory *prior,int64_t maximum,DeadlineHistory *adjacent,
 RangeWriter &writer,DeadlineGateCount *gate0,DeadlineGateCount *gate1,DeadlineHistory *runs,DeadlineHistory *rankstate){
 constexpr int maxbits=Bits;
 // A rank word spans the full M-symbol legal set. Fixed pairs anchored at bit0.
 int planes=(maxbits+1)/2;
 int64_t previous_word=adjacent[0];
 int64_t previous_rank=rankstate[0];int j=0;
 for(int64_t i=0;i<n;++i){
  int64_t word=0,delta=0;
  {
   float value=words[i];
   if(!std::isfinite(value)||value<0||value>1||(value==0&&std::signbit(value)))return -40;
   int64_t level=int64_t(double(value)*double(maximum));
   if(float(double(level)/double(maximum))!=value)return -41;
   delta=level-prior[j];word=delta>=0?2*delta:-2*delta-1;
  }
  int active=word!=0;
  int gc=event_context(2,runs[j],prior[j],maximum,previous_word);
  if(!writer.bit(active,empirical(gate0[gc],gate1[gc])))return -4;
  while(writer.range<0x01000000U){writer.range<<=8;if(!writer.shift())return -4;}
  observe(gate0,gate1,gc,active);
  int64_t rank=0;
  if(active){
   rank=rank_delta(delta,prior[j],maximum);int64_t prefix=0;
   int64_t temporal=rankstate[j+1];
   int64_t neighbor=previous_rank;
   for(int p=planes-1;p>=0;--p){
    if(p>=1&&prefix){
     int nbits=2*p;uint32_t literal=uint32_t((rank>>2)&((int64_t(1)<<nbits)-1)),unit=writer.range>>nbits;
     writer.low+=uint64_t(unit)*literal;writer.range=unit;
     while(writer.range<0x01000000U){writer.range<<=8;if(!writer.shift())return -4;}
     prefix|=int64_t(literal)<<2;p=1;continue;
    }
    int ctx=rank_context_row(p,prefix!=0,neighbor,temporal);
    int symbol=(rank>>(2*p))&3;
    if(!model_write(writer,models[ctx],symbol))return -4;
    models[ctx]=model_update(models[ctx],symbol);prefix|=int64_t(symbol)<<(2*p);
   }
  }
  int64_t current=prior[j]+delta;if(current<0||current>maximum)return -5;
  prior[j]=current;
  previous_word=word;
  previous_rank=rank;rankstate[j+1]=rank;
  runs[j]=active?0:min64(32,runs[j]+1);if(++j==c)j=0;
 }
 adjacent[0]=previous_word;rankstate[0]=previous_rank;
 return writer.pos*8;
}
template<int Bits> __attribute__((always_inline)) static inline int64_t advance_decode(
 const uint8_t *data,int64_t valid,int nf,int c,uint64_t *models,
 DeadlineHistory *prior,int64_t maximum,DeadlineHistory *adjacent,
 float *out,DeadlineGateCount *gate0,DeadlineGateCount *gate1,DeadlineHistory *runs,DeadlineHistory *rankstate,uint32_t &range,uint32_t &code,int64_t &cursor){
 int64_t size=valid/8;
 auto normalize=[&](){while(range<0x01000000U){if(cursor>=size)return false;range<<=8;code=(code<<8)|data[cursor++];}return true;};
 constexpr int planes=(Bits+1)/2;
 int64_t previous_word=adjacent[0];
 int64_t previous_rank=rankstate[0];int j=0;
 for(int64_t i=0;i<int64_t(nf)*c;++i){
  int gc=event_context(2,runs[j],prior[j],maximum,previous_word);
  uint32_t cut=(range>>16)*empirical(gate0[gc],gate1[gc]);int active=code>=cut;
  if(active){code-=cut;range-=cut;}else range=cut;
  if(!normalize())return -6;observe(gate0,gate1,gc,active);
  int64_t rank=0,delta=0,word=0;
  if(active){
   int64_t temporal=rankstate[j+1];
   int64_t neighbor=previous_rank;
   for(int p=planes-1;p>=0;--p){
    if(p>=1&&rank){
     int nbits=2*p;uint32_t unit=range>>nbits,literal=code/unit;
     if(literal>=(1U<<nbits))return -6;code-=unit*literal;range=unit;
     if(!normalize())return -6;rank|=int64_t(literal)<<2;p=1;continue;
    }
    int ctx=rank_context_row(p,rank!=0,neighbor,temporal);
    uint64_t row=models[ctx];uint32_t total=model_total(row),unit=range/total,cum=0,freq=0;int symbol=0;
    uint32_t c0=uint32_t(row&2047),c1=uint32_t((row>>11)&2047),c2=uint32_t((row>>22)&2047);
    if(code>=unit*total)return -6;
    if(p<=1){
     symbol=int(code>=unit*c0)+int(code>=unit*c1)+int(code>=unit*c2);
     uint64_t pair=(row<<11)>>(11*symbol);cum=uint32_t(pair&2047);freq=uint32_t((pair>>11)&2047)-cum;
    }else{
     if(code<unit*c0){symbol=0;cum=0;freq=c0;}
     else if(code<unit*c1){symbol=1;cum=c0;freq=c1-c0;}
     else if(code<unit*c2){symbol=2;cum=c1;freq=c2-c1;}
     else{symbol=3;cum=c2;freq=total-c2;}
    }
    code-=unit*cum;range=unit*freq;if(!normalize())return -6;
    models[ctx]=model_update(models[ctx],symbol);rank|=int64_t(symbol)<<(2*p);
   }
   if(rank>=maximum)return -5;
   delta=inverse_rank(rank,prior[j],maximum);word=delta>=0?2*delta:-2*delta-1;
  }
  int64_t current=prior[j]+delta;if(current<0||current>maximum)return -5;
  prior[j]=current;
  previous_word=word;
  previous_rank=rank;rankstate[j+1]=rank;
  runs[j]=active?0:min64(32,runs[j]+1);
  out[i]=float(double(prior[j])/double(maximum));if(++j==c)j=0;
 }
 adjacent[0]=previous_word;rankstate[0]=previous_rank;
 return valid;
}
template<int Bits> static int64_t deadline_encode_impl(const float*x,int64_t n,int c,int bits,uint8_t*out,int64_t capacity,int64_t*stat){
 if(n<=0||n>=(int64_t(1)<<27)||c<=0||c>1024||bits<2||bits>14)return -100;
 try{
 DeadlineState st(c,Bits);int64_t at=0;uint32_t seq=0;Deadline dl;
 RangeWriter writer{out,capacity,0,0,1,0xffffffffU,0};
 auto close_span=[&](){
  for(int k=0;k<5;++k)if(!writer.shift())return false;
  int64_t size=writer.pos;if(at+size+4>capacity)return false;
  put_le(out+at+size,packet_crc(out+at,size,seq,dl.count),4);
  at+=size+4;++seq;dl=Deadline{};writer=RangeWriter{out+at,capacity-at,0,0,1,0xffffffffU,0};return true;
 };
 for(int64_t i=0;i<n;++i){
  if(!writer.bit(0,continue_probability))return -101;
  while(writer.range<0x01000000U){writer.range<<=8;if(!writer.shift())return -101;}
  if(advance_bits<Bits>(x+i*c,c,c,st.models.data(),st.prior.data(),int64_t(1)<<Bits,st.adjacent.data(),writer,st.g0.data(),st.g1.data(),st.runs.data(),st.ranks.data())<0)return -102;
  // writer.shift count equals the bytes an original range decoder normalizes.
  // In the carry implementation emitted+cache_size = shift_count+1.
  bool flush=dl.choose(i+1,writer.pos);
  if(!writer.bit(flush,no_close_probability))return -107;
  while(writer.range<0x01000000U){writer.range<<=8;if(!writer.shift())return -107;}
  int64_t shifts=writer.pos+int64_t(writer.cache_size)-1;
  dl.record(shifts+5);
  if(stat){stat[3*i]=writer.pos;stat[3*i+1]=shifts+5;stat[3*i+2]=flush;}
  if(flush&&!close_span())return -103;
 }
 if(!writer.bit(1,continue_probability))return -104;
 while(writer.range<0x01000000U){writer.range<<=8;if(!writer.shift())return -104;}
 if(!close_span())return -105;
 return at;
 }catch(...){return -106;}
}
template<int Bits> static int64_t deadline_decode_impl(const uint8_t*data,int64_t size,int c,int bits,float*out,int64_t frame_capacity,int64_t*stat){
 if(size<9||c<=0||c>1024||bits<2||bits>14)return -200;
 try{
 DeadlineState st(c,Bits);int64_t cursor=0,begin=0,seen=0;uint32_t seq=0,range=0xffffffffU,code=0;Deadline dl;
 auto initialize=[&](){
  range=0xffffffffU;code=0;dl=Deadline{};begin=cursor;
  if(cursor+5>size)return false;
  for(int k=0;k<5;++k)code=(code<<8)|data[cursor++];return true;
 };
 auto normalize=[&](){while(range<0x01000000U){if(cursor>=size)return false;range<<=8;code=(code<<8)|data[cursor++];}return true;};
 auto close_span=[&](){
  if(cursor+4>size||get_le4(data+cursor)!=packet_crc(data+begin,cursor-begin,seq,dl.count))return false;
  cursor+=4;++seq;return true;
 };
 if(!initialize())return -201;
 while(true){
  uint32_t cut=(range>>16)*continue_probability;int end=code>=cut;
  if(end){code-=cut;range-=cut;}else range=cut;
  if(!normalize())return -202;
  if(end){if(!close_span()||cursor!=size)return -203;return seen;}
  if(seen>=frame_capacity)return -204;
  if(advance_decode<Bits>(data,size*8,1,c,st.models.data(),st.prior.data(),int64_t(1)<<Bits,st.adjacent.data(),out+seen*c,st.g0.data(),st.g1.data(),st.runs.data(),st.ranks.data(),range,code,cursor)<0)return -205;
  ++seen;++dl.count;
  uint32_t close_cut=(range>>16)*no_close_probability;bool flush=code>=close_cut;
  if(flush){code-=close_cut;range-=close_cut;}else range=close_cut;
  if(!normalize())return -208;
  if((seen==1||dl.count==18||seen%576==0)&&!flush)return -209;
  if(stat){stat[(seen-1)*3]=0;stat[(seen-1)*3+1]=cursor-begin;stat[(seen-1)*3+2]=flush;}
  if(flush){if(!close_span()||!initialize())return -206;}
 }
 }catch(...){return -207;}
}

extern "C" __declspec(dllexport) int64_t deadline_encode(const float*x,int64_t n,int c,int bits,uint8_t*out,int64_t capacity,int64_t*stat){
#define CASE(B) case B:return deadline_encode_impl<B>(x,n,c,bits,out,capacity,stat);
 switch(bits){CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7) CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14)}
 return -100;
#undef CASE
}
extern "C" __declspec(dllexport) int64_t deadline_decode(const uint8_t*data,int64_t size,int c,int bits,float*out,int64_t frame_capacity,int64_t*stat){
#define CASE(B) case B:return deadline_decode_impl<B>(data,size,c,bits,out,frame_capacity,stat);
 switch(bits){CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7) CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14)}
 return -200;
#undef CASE
}
