#include "fec_codec.h"

namespace fec {

FecCodec::FecCodec(int data_count, int parity_count) 
    : K(data_count), M(parity_count), N(data_count + parity_count) {
    G = build_vandermonde_matrix(N, K);
}

std::vector<uint8_t> FecCodec::encode(const uint8_t* data, size_t data_len, int packet_len) {
    std::vector<uint8_t> parity(M * packet_len, 0);
    
    for (int p_idx = 0; p_idx < M; p_idx++) {
        int row_in_G = K + p_idx;
        for (int byte_idx = 0; byte_idx < packet_len; byte_idx++) {
            uint8_t val = 0;
            for (int d_idx = 0; d_idx < K; d_idx++) {
                size_t offset = (size_t)d_idx * packet_len + byte_idx;
                uint8_t d_val = (offset < data_len) ? data[offset] : 0;
                val ^= gf_mul(G[row_in_G][d_idx], d_val);
            }
            parity[p_idx * packet_len + byte_idx] = val;
        }
    }
    return parity;
}

} // namespace fec
