#include "gf256.h"
#include <stdexcept>

namespace fec {

uint8_t gf_exp_table[512] = {0};
uint8_t gf_log_table[256] = {0};
bool tables_initialized = false;

void init_gf_tables() {
    if (tables_initialized) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp_table[i] = x;
        gf_log_table[x] = i;
        x <<= 1;
        if (x & 0x100) {
            x ^= GF_POLY;
        }
    }
    for (int i = 255; i < 512; i++) {
        gf_exp_table[i] = gf_exp_table[i - 255];
    }
    tables_initialized = true;
}

uint8_t gf_mul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return gf_exp_table[gf_log_table[a] + gf_log_table[b]];
}

uint8_t gf_div(uint8_t a, uint8_t b) {
    if (a == 0) return 0;
    if (b == 0) return 0; // Better than throw in embedded
    return gf_exp_table[(gf_log_table[a] + 255 - gf_log_table[b]) % 255];
}

uint8_t gf_invert(uint8_t a) {
    if (a == 0) return 0; // Avoid throw
    return gf_exp_table[255 - gf_log_table[a]];
}

std::vector<std::vector<uint8_t>> build_vandermonde_matrix(int rows, int cols) {
    init_gf_tables();
    std::vector<std::vector<uint8_t>> matrix(rows, std::vector<uint8_t>(cols, 0));
    
    for (int i = 0; i < rows; i++) {
        if (i < cols) {
            matrix[i][i] = 1;
        } else {
            for (int j = 0; j < cols; j++) {
                int x = i - cols;
                int y = j + (rows - cols);
                matrix[i][j] = gf_invert(x ^ y);
            }
        }
    }
    return matrix;
}

} // namespace fec
