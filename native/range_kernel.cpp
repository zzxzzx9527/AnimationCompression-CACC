// Byte-normalized range coding, a standard coding backend, NOT a novelty claim.
#include "kernel.cpp"

struct RangeWriter{
    uint8_t *out;int64_t capacity,pos;uint64_t low,cache_size;uint32_t range;uint8_t cache;
    inline bool shift(){
        uint32_t lo=uint32_t(low),carry=uint32_t(low>>32);
        if(lo<0xff000000U||carry){
            uint8_t temp=cache;
            do{
                if(pos>=capacity)return false;
                out[pos++]=uint8_t(temp+carry);temp=0xff;
            }while(--cache_size);
            cache=uint8_t(lo>>24);
        }
        ++cache_size;low=uint32_t(lo<<8);return true;
    }
    inline bool bit(int bit,uint32_t p){
        uint32_t bound=(range>>16)*p;
        if(!bit)range=bound;else{low+=bound;range-=bound;}
        if(range<0x01000000U){range<<=8;return shift();}
        return true;
    }
};
