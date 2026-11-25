// Bounded residual rank and its inverse.
#include "exact_kernel.cpp"

static inline int64_t rank_delta(int64_t d,int64_t old,int64_t maximum){
    int64_t m=min64(old,maximum-old),a=llabs(d);
    if(a<=m)return d<0?2*a-2:2*a-1;
    return 2*m+a-m-1;
}
static inline int64_t inverse_rank(int64_t r,int64_t old,int64_t maximum){
    int64_t m=min64(old,maximum-old);
    if(r<2*m)return r&1?(r+1)/2:-(r/2)-1;
    int64_t a=r-m+1;return old>maximum-old?-a:a;
}
