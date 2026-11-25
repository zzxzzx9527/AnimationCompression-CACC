static constexpr int low_memory_max_channels = 93;
// The public adapter supplies at most 256 bytes, draining complete frames
// before the next chunk. At an input stall at most seven bytes remain.
static constexpr int low_memory_max_pending_bytes = 256 + 7;
static constexpr int low_memory_output_frames = 3;

struct DeadlineState : ProbabilityState<DeadlineGateCount, DeadlineHistory> {
    DeadlineState(int channels, int bits)
        : ProbabilityState(channels, deadline_rank_rows(bits), 1) {}

};

// The range model rescales before keeping a total of 2048. The four stored
// cumulative counts therefore occupy at most 44 bits between symbols.
struct PackedRankRow {
    explicit PackedRankRow(uint64_t row) { *this = row; }

    PackedRankRow& operator=(uint64_t row) {
        const uint32_t low = uint32_t(row);
        const uint16_t high = uint16_t(row >> 32);
        memcpy(bytes.data(), &low, sizeof(low));
        memcpy(bytes.data() + sizeof(low), &high, sizeof(high));
        return *this;
    }

    operator uint64_t() const {
        uint32_t low;
        uint16_t high;
        memcpy(&low, bytes.data(), sizeof(low));
        memcpy(&high, bytes.data() + sizeof(low), sizeof(high));
        return uint64_t(low) | (uint64_t(high) << 32);
    }

private:
    std::array<uint8_t, 6> bytes;
};

// Each activity count is positive and the stored pair sums to at most 2047.
struct PackedActivityCounts {
    PackedActivityCounts() { set(1, 1); }

    void set(uint16_t zero, uint16_t one) {
        const uint16_t low = zero | (one << 11);
        memcpy(bytes.data(), &low, sizeof(low));
        bytes[2] = uint8_t(one >> 5);
    }

    std::array<uint16_t, 2> unpack() const {
        uint16_t low;
        memcpy(&low, bytes.data(), sizeof(low));
        return {uint16_t(low & 2047), uint16_t((low >> 11) | (uint16_t(bytes[2]) << 5))};
    }

private:
    std::array<uint8_t, 3> bytes;
};

static_assert(sizeof(PackedRankRow) == 6);
static_assert(sizeof(PackedActivityCounts) == 3);

struct LowMemoryDecoderState {
    int c;
    int rank_rows;
    std::vector<PackedRankRow> models;
    std::vector<PackedActivityCounts> gates;
    std::vector<uint16_t> prior, ranks;
    std::vector<uint8_t> runs;
    std::array<uint16_t, 1> adjacent{};

    LowMemoryDecoderState(int channels, int bits)
        : c(channels), rank_rows(deadline_rank_rows(bits)),
          models(rank_rows, PackedRankRow(model_initial)), gates(32),
          prior(c, 0), ranks(c + 1, 0), runs(c, 0) {}

};
