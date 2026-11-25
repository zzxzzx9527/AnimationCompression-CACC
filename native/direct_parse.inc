 // One dispatch per feed/resume. Complete input follows explicit control
 // edges; underflow returns only after saving its next primitive's phase.
 void parse(){
  switch(phase){
   case 0:goto span_start;
   case 1:goto frame_tag;
   case 2:goto activity;
   case 3:goto residual;
   case 4:goto frame_close;
   case 5:goto span_check;
   case 6:goto total_check;
   case 7:goto finished;
   default:throw 1;
  }
 span_start:
  if(cursor+5>int64_t(input.size())){phase=0;return;}
  if(cursor){input.erase(input.begin(),input.begin()+cursor);cursor=0;}
  begin=cursor;range=0xffffffffU;code=0;
  span_crc=0xffffffffU;
  for(int k=0;k<5;++k)code=(code<<8)|input[cursor++];
  span_frames=0;terminating=false;
 frame_tag:
  {
   int end=0;
   if(!bit(continue_probability,end)){phase=1;return;}
   if(end){terminating=true;goto span_check;}
   previous=state.adjacent[0];previous_rank=state.ranks[0];
  }
 activity:
  {
   int ctx=event_context(2,state.runs[j],state.prior[j],int64_t(1)<<bits,previous);
   int active=0;
   if(!read_activity(ctx,active)){phase=2;return;}
   if(!active){
    commit(-1);
    if(j==0)goto frame_close;
    goto activity;
   }
   temporal=state.ranks[j+1];neighbor=previous_rank;prefix=0;plane=planes-1;
  }
 residual:
  {
   if(plane>=1&&prefix){
    int nb=2*plane;uint32_t unit=range>>nb,value=code/unit;
    if(value>=(1U<<nb))throw 1;
    if(!interval(code-unit*value,unit)){phase=3;return;}
    prefix|=int64_t(value)<<2;plane=0;
    goto residual;
   }
   int ctx=rank_context_row(plane,prefix!=0,neighbor,temporal);
   uint64_t row=state.models[ctx];uint32_t total=model_total(row),unit=range/total;
   if(code>=unit*total)throw 1;
   int value=0;
   if(plane==0)value=int(code>=unit*model_upper(row,0))+int(code>=unit*model_upper(row,1))+int(code>=unit*model_upper(row,2));
   else while(value<3&&code>=unit*model_upper(row,value))++value;
   if(!interval(code-unit*model_lower(row,value),unit*model_count(row,value))){phase=3;return;}
   state.models[ctx]=model_update(row,value);prefix|=int64_t(value)<<(2*plane);
   if(--plane>=0)goto residual;
   commit(prefix);
   if(j==0)goto frame_close;
   goto activity;
  }
 frame_close:
  {
   int flush=0;
   if(!bit(no_close_probability,flush)){phase=4;return;}
   ++seen;++span_frames;
   if((seen==1||span_frames==18||seen%576==0)&&!flush)throw 1;
   ready.insert(ready.end(),state.prior.begin(),state.prior.end());
   if (ready.size() >= size_t(state.c * low_memory_output_frames)) {
    phase=flush?5:1;
    return;
   }
   if(flush)goto span_check;
   goto frame_tag;
  }
 span_check:
  if(cursor+4>int64_t(input.size())){phase=5;return;}
  {
   uint8_t trailer[6];put_le(trailer,seq,4);put_le(trailer+4,span_frames,2);
   const uint32_t crc=crc_add(crc_add(span_crc,input.data(),cursor),trailer,6)^0xffffffffU;
   if(get_le4(input.data()+cursor)!=crc)throw 1;
  }
  cursor+=4;++seq;
  if(!terminating)goto span_start;
 total_check:
  {
   if(cursor+8>int64_t(input.size())){phase=6;return;}
   uint64_t n=0;for(int k=0;k<8;++k)n|=uint64_t(input[cursor+k])<<(8*k);
   if(n!=uint64_t(seen)||cursor+8!=int64_t(input.size()))throw 1;
   cursor+=8;ended=true;phase=7;return;
  }
 finished:
  if(cursor!=int64_t(input.size()))throw 1;
  return;
 }
