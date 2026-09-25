// Minimal stand-ins for the MAME types used by the vendored TLCS-900 core.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using offs_t = uint32_t;

// Little-endian register pair, laid out like MAME's PAIR on LSB-first hosts.
union PAIR
{
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	struct { s8 l, h, h2, h3; } sb;
	struct { s16 l, h; } sw;
	u32 d;
	s32 sd;
};

static_assert(sizeof(PAIR) == 4, "PAIR must be 32 bits");

constexpr int CLEAR_LINE = 0;
constexpr int ASSERT_LINE = 1;

template <typename... Args>
inline void logerror(const char *, Args...) {}
