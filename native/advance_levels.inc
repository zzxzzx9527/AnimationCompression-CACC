template<int Bits> __attribute__((always_inline)) static inline int64_t advance_levels(
 const int64_t *words,int64_t n,int c,uint64_t *models,
 DeadlineHistory *prior,int64_t maximum,DeadlineHistory *adjacent,
 RangeWriter &writer,DeadlineGateCount *gate0,DeadlineGateCount *gate1,DeadlineHistory *runs,DeadlineHistory *rankstate){
 constexpr int maxbits=Bits;
 // A rank word spans the full M-symbol legal set. Fixed pairs anchored at bit0.
 int planes=(maxbits+1)/2;
 int64_t previous_word=adjacent[0];
 int64_t previous_rank=rankstate[0];int j=0;
 for(int64_t i=0;i<n;++i){
  int64_t word=0,delta=0;
  delta=words[i]-prior[j];word=delta>=0?2*delta:-2*delta-1;
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
