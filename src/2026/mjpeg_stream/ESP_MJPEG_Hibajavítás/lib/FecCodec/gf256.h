#pragma once

#include <stdint.h>
#include <vector>

namespace fec {

// Galois Field (GF256) Math Constants
constexpr uint16_t GF_POLY = 0x11D;

// Expose tables if needed, but functions are preferred
extern uint8_t gf_exp_table[512];
extern uint8_t gf_log_table[256];

// Initialize lookup tables
void init_gf_tables();

// Basic GF operations
inline uint8_t gf_add(uint8_t a, uint8_t b) { return a ^ b; }
inline uint8_t gf_sub(uint8_t a, uint8_t b) { return a ^ b; }

uint8_t gf_mul(uint8_t a, uint8_t b);
uint8_t gf_div(uint8_t a, uint8_t b);
uint8_t gf_invert(uint8_t a);

// Matrix Operations
std::vector<std::vector<uint8_t>> build_vandermonde_matrix(int rows, int cols);

} // namespace fec
