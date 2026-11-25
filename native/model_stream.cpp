// Shared probability state and span checksum helpers.
#include "model_kernel.cpp"

#include <cmath>
#include <vector>
#include <array>
#include <string.h>
#include <memory>

static const std::array<uint32_t,256>& crc_table(){
 static const auto table=[](){std::array<uint32_t,256>a{};for(uint32_t i=0;i<256;++i){uint32_t v=i;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xedb88320U:0);a[i]=v;}return a;}();return table;
}
static uint32_t crc_add(uint32_t v,const uint8_t *p,int64_t n){const auto &a=crc_table();for(int64_t i=0;i<n;++i)v=a[(v^p[i])&255]^(v>>8);return v;}
static void put_le(uint8_t *p,uint64_t v,int n){for(int j=0;j<n;++j)p[j]=uint8_t(v>>(8*j));}
static uint32_t get_le4(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}

template<class GateCount, class HistoryValue=int64_t>
struct ProbabilityState{
 int c;
 int rank_rows;
 std::vector<uint64_t> models;
 std::vector<GateCount> g0,g1;
 std::vector<HistoryValue> prior,runs,adjacent,ranks;
 ProbabilityState(int channels,int rows=256,int adjacent_count=32):
  c(channels),rank_rows(rows),models(rows,model_initial),g0(32,1),g1(32,1),prior(c,0),
  runs(c,0),adjacent(adjacent_count,0),ranks(c+1,0){}
};
