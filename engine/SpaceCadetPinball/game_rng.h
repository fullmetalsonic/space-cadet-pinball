#pragma once
#include <cstdint>

namespace game_rng
{
constexpr int Max = 32767;

inline int DrawFromState(uint32_t& state)
{
    state = state * UINT32_C(214013) + UINT32_C(2531011);
    return static_cast<int>((state >> 16) & UINT32_C(32767));
}

inline int Next()
{
    static uint32_t state = 1;
    return DrawFromState(state);
}
}
