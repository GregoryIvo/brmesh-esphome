#pragma once
#include <algorithm>
#include <cstdint>
namespace esphome {
template<typename T> T clamp(T x,T lo,T hi) { return std::clamp(x,lo,hi); }
inline uint32_t fnv1_hash(const char *) { return 0; }
inline uint32_t random_uint32() { return 0x12345678; }
}
