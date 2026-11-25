// CACC activity contexts and adaptive binary counts.
#include "range_kernel.cpp"

static inline int bucket(int64_t run){
    if(run<4)return int(run);
    if(run<8)return 4;if(run<16)return 5;if(run<32)return 6;return 7;
}
static inline int event_context(int variant,int64_t run,int64_t prior,int64_t scale,int64_t adjacent){
    if(variant==0)return 0;
    int ctx=bucket(run)*2+int(adjacent!=0);
    if(variant==2)ctx+=16*int(prior==0||prior==scale);
    return ctx;
}

#include "reciprocal.inc"
static inline uint32_t exact_empirical(int64_t zero,int64_t one){
    uint32_t total=uint32_t(zero+one),numerator=uint32_t(zero)*65536U;
    uint32_t q=uint32_t((uint64_t(numerator)*RECIPROCAL[total])>>32);
    if(q*total>numerator)--q;
    return q?q:1;
}
static inline uint32_t empirical(int64_t zero,int64_t one){return exact_empirical(zero,one);}

template<class Count>
static inline void observe(Count *zero,Count *one,int ctx,int bit){
    if(bit)++one[ctx];else ++zero[ctx];
    if(zero[ctx]+one[ctx]>=2048){zero[ctx]=(zero[ctx]+1)/2;one[ctx]=(one[ctx]+1)/2;}
}
