// Integer helpers used by the CACC range coder.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

static inline int64_t min64(int64_t a,int64_t b){return a<b?a:b;}
static inline int64_t max64(int64_t a,int64_t b){return a>b?a:b;}
