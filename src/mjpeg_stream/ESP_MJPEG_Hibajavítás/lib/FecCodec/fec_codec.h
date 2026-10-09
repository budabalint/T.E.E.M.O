#pragma once

#include "gf256.h"
#include <vector>

namespace fec {

class FecCodec {
public:
    FecCodec(int data_count, int parity_count);

    // Encode data to generate flattened parity packets
    std::vector<uint8_t> encode(const uint8_t* data, size_t data_len, int packet_len);

private:
    int K;
    int M;
    int N;
    std::vector<std::vector<uint8_t>> G;
};

} // namespace fec
