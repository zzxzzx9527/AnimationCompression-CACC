// CACC streaming decoder may emit frames before validating the span CRC.
#include "deadline.cpp"
#include "advance_levels.inc"
struct OnlineEncoder{
 DeadlineState state;int bits;int64_t seen=0;uint32_t seq=0;bool failed=false,ended=false;
 Deadline deadline;std::vector<uint8_t> buffer;int64_t sent=0;RangeWriter writer;
 std::vector<int64_t> levels;
 OnlineEncoder(int c,int b):state(c,b),bits(b),buffer(18*c*(b+2)*2+64),writer{buffer.data(),int64_t(buffer.size()),0,0,1,0xffffffffU,0}{
 levels.resize(c);
 }
};
static int64_t advance_online(OnlineEncoder&e){
 auto&s=e.state;
#define CASE(B) case B:return advance_levels<B>(e.levels.data(),s.c,s.c,s.models.data(),s.prior.data(),int64_t(1)<<B,s.adjacent.data(),e.writer,s.g0.data(),s.g1.data(),s.runs.data(),s.ranks.data());
 switch(e.bits){CASE(2) CASE(3) CASE(4) CASE(5) CASE(6) CASE(7) CASE(8) CASE(9) CASE(10) CASE(11) CASE(12) CASE(13) CASE(14)}
 return -1;
#undef CASE
}
static int64_t output_online(OnlineEncoder&e,bool flush,uint8_t*out,int64_t capacity){
 if(flush){for(int i=0;i<5;++i)if(!e.writer.shift())return -300;}
 int64_t bytes=e.writer.pos-e.sent;
 if(bytes+int(flush)*4>capacity)return -301;
 memcpy(out,e.buffer.data()+e.sent,bytes);
 if(flush){
  put_le(out+bytes,packet_crc(e.buffer.data(),e.writer.pos,e.seq,e.deadline.count),4);bytes+=4;
  ++e.seq;e.deadline=Deadline{};e.sent=0;e.writer=RangeWriter{e.buffer.data(),int64_t(e.buffer.size()),0,0,1,0xffffffffU,0};
 }else e.sent=e.writer.pos;
 return bytes;
}
extern "C" __declspec(dllexport) void* cacc_encoder_create(int c,int bits){
 if(c<=0||c>1024||bits<2||bits>14)return nullptr;
 if(c>low_memory_max_channels)return nullptr;
 try{return new OnlineEncoder(c,bits);}catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) void cacc_encoder_destroy(void*p){delete static_cast<OnlineEncoder*>(p);}
extern "C" __declspec(dllexport) int64_t cacc_encoder_push(void*p,const float*x,uint8_t*out,int64_t capacity){
 auto*e=static_cast<OnlineEncoder*>(p);if(!e||e->failed||e->ended||!x||!out)return -302;
 try{
  double maximum=double(int64_t(1)<<e->bits);
  for(int j=0;j<e->state.c;++j){float v=x[j];if(!std::isfinite(v)||v<0||v>1||(v==0&&std::signbit(v)))return -321;int64_t level=int64_t(double(v)*maximum);if(float(double(level)/maximum)!=v)return -321;
   e->levels[j]=level;
  }
  if(!e->writer.bit(0,continue_probability))throw 1;
  while(e->writer.range<0x01000000U){e->writer.range<<=8;if(!e->writer.shift())throw 1;}
  if(advance_online(*e)<0)throw 1;
  ++e->seen;bool flush=e->deadline.choose(e->seen,e->writer.pos);
  if(!e->writer.bit(flush,no_close_probability))throw 1;
  while(e->writer.range<0x01000000U){e->writer.range<<=8;if(!e->writer.shift())throw 1;}
  e->deadline.record(e->writer.pos+int64_t(e->writer.cache_size)+4);
  int64_t result=output_online(*e,flush,out,capacity);if(result<0)throw 1;return result;
 }catch(...){e->failed=true;return -303;}
}
extern "C" __declspec(dllexport) int64_t cacc_encoder_end(void*p,uint8_t*out,int64_t capacity){
 auto*e=static_cast<OnlineEncoder*>(p);if(!e||e->failed||e->ended||!e->seen)return -304;
 try{
  if(!e->writer.bit(1,continue_probability))throw 1;
  while(e->writer.range<0x01000000U){e->writer.range<<=8;if(!e->writer.shift())throw 1;}
  int64_t result=output_online(*e,true,out,capacity-8);if(result<0)throw 1;
  put_le(out+result,e->seen,8);e->ended=true;return result+8;
 }catch(...){e->failed=true;return -305;}
}

struct OnlineDecoder{
 LowMemoryDecoderState state;
 int bits,planes,phase=0,j=0,plane=0;int64_t seen=0,cursor=0,begin=0;uint32_t seq=0,range=0xffffffffU,code=0;
 int span_frames=0;bool terminating=false,failed=false,ended=false;int64_t prefix=0,temporal=0,neighbor=0,previous=0,previous_rank=0;
 std::vector<uint8_t> input;
 int64_t fast_channels=0;
 std::vector<uint16_t> ready;
 OnlineDecoder(int c,int b):state(c,b),bits(b),planes((b+1)/2){
  input.reserve(low_memory_max_pending_bytes);
  ready.reserve(c * low_memory_output_frames);
 }
 uint32_t span_crc = 0xffffffffU;

 void discard_consumed() {
  if (phase >= 1 && phase <= 5) span_crc = crc_add(span_crc,input.data(),cursor);
  input.erase(input.begin(),input.begin()+cursor);
  cursor=0;
 }
 // Commit a primitive only after all its normalized bytes are available.
 bool interval(uint32_t co,uint32_t r){
  int64_t at=cursor;
  while(r<0x01000000U){if(at>=int64_t(input.size()))return false;r<<=8;co=(co<<8)|input[at++];}
  range=r;code=co;cursor=at;return true;
 }
 bool bit(uint32_t probability,int&value){
  uint32_t cut=(range>>16)*probability;value=code>=cut;
  return interval(value?code-cut:code,value?range-cut:cut);
 }
 bool read_activity(int context,int& active){
  auto& gate=state.gates[context];
  auto counts=gate.unpack();
  if(!bit(empirical(counts[0],counts[1]),active))return false;
  observe(counts.data(),counts.data()+1,0,active);
  gate.set(counts[0],counts[1]);
  return true;
 }
 void commit(int64_t rank){
  int64_t old=state.prior[j],maximum=int64_t(1)<<bits,delta=0;
  if(rank>=0){if(rank>=maximum)throw 1;delta=inverse_rank(rank,old,maximum);}
  int64_t word=delta>=0?2*delta:-2*delta-1;
  int64_t current=old+delta;if(current<0||current>maximum)throw 1;
  state.prior[j]=current;
  state.ranks[j+1]=rank<0?0:rank;previous=word;previous_rank=rank<0?0:rank;
  state.runs[j]=word?0:min64(32,state.runs[j]+1);
  if(++j==state.c){state.adjacent[0]=previous;state.ranks[0]=previous_rank;j=0;phase=4;}else phase=2;
 }

#include "direct_parse.inc"
};
extern "C" __declspec(dllexport) void* cacc_decoder_create(int c,int bits){
 if(c<=0||c>1024||bits<2||bits>14)return nullptr;
 if(c>low_memory_max_channels)return nullptr;
 try{return new OnlineDecoder(c,bits);}catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) void cacc_decoder_destroy(void*p){delete static_cast<OnlineDecoder*>(p);}
extern "C" __declspec(dllexport) int64_t cacc_decoder_feed(void*p,const uint8_t*data,int64_t n,void**result){
 auto*d=static_cast<OnlineDecoder*>(p);if(!d||d->failed||d->ended||n<0)return -306;
 try{
  if(n>low_memory_max_pending_bytes-int64_t(d->input.size()))return -309;
  d->ready.clear();if(n)d->input.insert(d->input.end(),data,data+n);d->parse();
  d->discard_consumed();
  *result=d->ready.data();return d->ready.size()/d->state.c;
 }
 catch(...){d->failed=true;return -307;}
}
extern "C" __declspec(dllexport) int cacc_decoder_ended(void*p){auto*d=static_cast<OnlineDecoder*>(p);return d&&d->ended;}

extern "C" __declspec(dllexport) int64_t cacc_low_memory_feed_levels(void* handle, const uint8_t* data,
                                                                  int64_t size, void** result) {
 const auto count=cacc_decoder_feed(handle,data,size,result);
 return count<0?count:(count*2+cacc_decoder_ended(handle));
}

extern "C" __declspec(dllexport) uint64_t cacc_decoder_bytes(const void* handle) {
 const auto& decoder=*static_cast<const OnlineDecoder*>(handle);
 const auto& state=decoder.state;
 return sizeof(decoder) + decoder.input.capacity() + decoder.ready.capacity()*sizeof(uint16_t)
     + state.models.capacity()*sizeof(PackedRankRow) + state.gates.capacity()*sizeof(PackedActivityCounts)
     + (state.prior.capacity()+state.ranks.capacity())*sizeof(uint16_t) + state.runs.capacity()*sizeof(uint8_t);
}

extern "C" __declspec(dllexport) int64_t cacc_decoder_fast_count(void*p){auto*d=static_cast<OnlineDecoder*>(p);return d?d->fast_channels:-1;}
