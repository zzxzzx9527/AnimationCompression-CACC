// Adaptive four-symbol range-coding primitives used by CACC.
#include "rice_kernel.cpp"
#include <cmath>
#include <algorithm>

using Input = float;

static constexpr uint64_t model_one=(uint64_t(1)<<0)|(uint64_t(1)<<11)|(uint64_t(1)<<22)|(uint64_t(1)<<33);
static constexpr uint64_t model_initial=1|(uint64_t(2)<<11)|(uint64_t(3)<<22)|(uint64_t(4)<<33);
static inline uint32_t model_total(uint64_t row){return uint32_t(row>>33);}
static inline uint32_t model_lower(uint64_t row,int symbol){return symbol?uint32_t((row>>(11*(symbol-1)))&2047):0;}
static inline uint32_t model_upper(uint64_t row,int symbol){return symbol==3?model_total(row):uint32_t((row>>(11*symbol))&2047);}
static inline uint32_t model_count(uint64_t row,int symbol){return model_upper(row,symbol)-model_lower(row,symbol);}
static inline uint64_t model_pack(const uint32_t *a){return uint64_t(a[0])|(uint64_t(a[0]+a[1])<<11)|(uint64_t(a[0]+a[1]+a[2])<<22)|(uint64_t(a[0]+a[1]+a[2]+a[3])<<33);}
static inline uint64_t model_update(uint64_t row,int symbol){
 static constexpr uint64_t increments[]={model_one,model_one-1,model_one-1-(uint64_t(1)<<11),uint64_t(1)<<33};
 row+=increments[symbol];
 if(row&(uint64_t(1)<<44)){
  uint32_t a[4];for(int k=0;k<4;++k)a[k]=(model_count(row,k)+1)/2;
  row=model_pack(a);
 }
 return row;
}
static inline bool model_write(RangeWriter &w,uint64_t row,int symbol){
 uint64_t pair=(row<<11)>>(11*symbol);
 uint32_t unit=w.range/model_total(row),cum=uint32_t(pair&2047),freq=uint32_t((pair>>11)&2047)-cum;
 w.low+=uint64_t(cum)*unit;w.range=unit*freq;
 while(w.range<0x01000000U){w.range<<=8;if(!w.shift())return false;}return true;
}
