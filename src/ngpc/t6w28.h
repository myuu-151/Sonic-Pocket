// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
// T6W28 sound chip (Neo Geo Pocket), adapted from MAME's
// src/devices/sound/t6w28.{h,cpp}. The chip is two partial SN76489A cores:
// tone generators are shared, while each stereo side has its own volumes.
#pragma once

#include <cstdint>

class t6w28
{
public:
	t6w28();

	void write(int offset, uint8_t data);
	void set_enable(bool enable) { m_enabled = enable; }

	// Advances the chip by one output sample at clock / 16 and returns the
	// left and right levels in the range 0..0x7fff.
	void generate(int &left, int &right);

private:
	void set_gain(int gain);

	int m_vol_table[16];
	int32_t m_register[16];
	int32_t m_last_register[2];
	int32_t m_volume[8];
	uint32_t m_rng[2];
	int32_t m_noise_mode[2];
	int32_t m_feedback_mask;
	int32_t m_whitenoise_taps;
	int32_t m_whitenoise_invert;
	int32_t m_period[8];
	int32_t m_count[8];
	int32_t m_output[8];
	bool m_enabled = false;
};
