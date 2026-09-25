// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
// Adapted from MAME's src/devices/sound/t6w28.cpp (based on sn76496.c).
// The stream update is reduced to producing one sample per call.

#include "t6w28.h"

namespace {

constexpr int MAX_OUTPUT = 0x7fff;
constexpr int STEP = 0x10000;

} // namespace


t6w28::t6w28()
{
	for (int i = 0; i < 8; i++)
		m_volume[i] = 0;

	m_last_register[0] = 0;
	m_last_register[1] = 0;
	for (int i = 0; i < 16; i += 2)
	{
		m_register[i] = 0;
		m_register[i + 1] = 0x0f;   /* volume = 0 */
	}

	for (int i = 0; i < 8; i++)
	{
		m_output[i] = 0;
		m_period[i] = m_count[i] = STEP;
	}

	m_noise_mode[0] = m_noise_mode[1] = 0;

	/* Default is SN76489 non-A */
	m_feedback_mask = 0x4000;
	m_whitenoise_taps = 0x03;
	m_whitenoise_invert = 1;
	m_rng[0] = m_feedback_mask;
	m_rng[1] = m_feedback_mask;
	m_output[3] = m_rng[0] & 1;

	set_gain(0);

	/* values from sn76489a */
	m_feedback_mask = 0x8000;
	m_whitenoise_taps = 0x06;
	m_whitenoise_invert = 0;
}


void t6w28::write(int offset, uint8_t data)
{
	int n, r, c;

	offset &= 1;

	if (data & 0x80)
	{
		r = (data & 0x70) >> 4;
		m_last_register[offset] = r;
		m_register[offset * 8 + r] = (m_register[offset * 8 + r] & 0x3f0) | (data & 0x0f);
	}
	else
	{
		r = m_last_register[offset];
	}
	c = r / 2;
	switch (r)
	{
	case 0: /* tone 0 : frequency */
	case 2: /* tone 1 : frequency */
	case 4: /* tone 2 : frequency */
		if ((data & 0x80) == 0) m_register[offset * 8 + r] = (m_register[offset * 8 + r] & 0x0f) | ((data & 0x3f) << 4);
		m_period[offset * 4 + c] = STEP * m_register[offset * 8 + r];
		if (m_period[offset * 4 + c] == 0) m_period[offset * 4 + c] = STEP;
		if (r == 4)
		{
			/* update noise shift frequency */
			if ((m_register[offset * 8 + 6] & 0x03) == 0x03)
				m_period[offset * 4 + 3] = 2 * m_period[offset * 4 + 2];
		}
		break;
	case 1: /* tone 0 : volume */
	case 3: /* tone 1 : volume */
	case 5: /* tone 2 : volume */
	case 7: /* noise  : volume */
		m_volume[offset * 4 + c] = m_vol_table[data & 0x0f];
		if ((data & 0x80) == 0) m_register[offset * 8 + r] = (m_register[offset * 8 + r] & 0x3f0) | (data & 0x0f);
		break;
	case 6: /* noise  : frequency, mode */
		{
			if ((data & 0x80) == 0) m_register[offset * 8 + r] = (m_register[offset * 8 + r] & 0x3f0) | (data & 0x0f);
			n = m_register[offset * 8 + 6];
			m_noise_mode[offset] = (n & 4) ? 1 : 0;
			/* N/512,N/1024,N/2048,Tone #3 output */
			m_period[offset * 4 + 3] = ((n & 3) == 3) ? 2 * m_period[offset * 4 + 2] : (STEP << (5 + (n & 3)));
			/* Reset noise shifter */
			m_rng[offset] = m_feedback_mask;
			m_output[offset * 4 + 3] = m_rng[offset] & 1;
		}
		break;
	}
}


void t6w28::generate(int &left, int &right)
{
	/* If the volume is 0, increase the counter */
	for (int i = 0; i < 8; i++)
	{
		if (m_volume[i] == 0)
		{
			if (m_count[i] <= STEP) m_count[i] += STEP;
		}
	}

	int vol[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	for (int i = 2; i < 3; i++)
	{
		if (m_output[i]) vol[i] += m_count[i];
		m_count[i] -= STEP;
		while (m_count[i] <= 0)
		{
			m_count[i] += m_period[i];
			if (m_count[i] > 0)
			{
				m_output[i] ^= 1;
				if (m_output[i]) vol[i] += m_period[i];
				break;
			}
			m_count[i] += m_period[i];
			vol[i] += m_period[i];
		}
		if (m_output[i]) vol[i] -= m_count[i];
	}

	for (int i = 4; i < 7; i++)
	{
		if (m_output[i]) vol[i] += m_count[i];
		m_count[i] -= STEP;
		while (m_count[i] <= 0)
		{
			m_count[i] += m_period[i];
			if (m_count[i] > 0)
			{
				m_output[i] ^= 1;
				if (m_output[i]) vol[i] += m_period[i];
				break;
			}
			m_count[i] += m_period[i];
			vol[i] += m_period[i];
		}
		if (m_output[i]) vol[i] -= m_count[i];
	}

	int remaining = STEP;
	do
	{
		int nextevent;

		if (m_count[3] < remaining) nextevent = m_count[3];
		else nextevent = remaining;

		if (m_output[3]) vol[3] += m_count[3];
		m_count[3] -= nextevent;
		if (m_count[3] <= 0)
		{
			if (m_noise_mode[0] == 1) /* White Noise Mode */
			{
				if (((m_rng[0] & m_whitenoise_taps) != static_cast<uint32_t>(m_whitenoise_taps)) && ((m_rng[0] & m_whitenoise_taps) != 0))
				{
					m_rng[0] >>= 1;
					m_rng[0] |= m_feedback_mask;
				}
				else
				{
					m_rng[0] >>= 1;
				}
				m_output[3] = m_whitenoise_invert ? !(m_rng[0] & 1) : m_rng[0] & 1;
			}
			else /* Periodic noise mode */
			{
				if (m_rng[0] & 1)
				{
					m_rng[0] >>= 1;
					m_rng[0] |= m_feedback_mask;
				}
				else
				{
					m_rng[0] >>= 1;
				}
				m_output[3] = m_rng[0] & 1;
			}
			m_count[3] += m_period[3];
			if (m_output[3]) vol[3] += m_period[3];
		}
		if (m_output[3]) vol[3] -= m_count[3];

		remaining -= nextevent;
	} while (remaining > 0);

	unsigned int out0 = 0, out1 = 0;
	if (m_enabled)
	{
		out0 = vol[4] * m_volume[4] + vol[5] * m_volume[5] +
				vol[6] * m_volume[6] + vol[3] * m_volume[7];
		out1 = vol[4] * m_volume[0] + vol[5] * m_volume[1] +
				vol[6] * m_volume[2] + vol[3] * m_volume[3];
	}

	if (out0 > static_cast<unsigned>(MAX_OUTPUT) * STEP) out0 = static_cast<unsigned>(MAX_OUTPUT) * STEP;
	if (out1 > static_cast<unsigned>(MAX_OUTPUT) * STEP) out1 = static_cast<unsigned>(MAX_OUTPUT) * STEP;

	// Offset 0 carries the left side's volumes, offset 1 the right's (MAME
	// routes stream 0 to the left speaker).
	left = static_cast<int>(out0 / STEP);
	right = static_cast<int>(out1 / STEP);
}


void t6w28::set_gain(int gain)
{
	double out;

	gain &= 0xff;

	/* increase max output basing on gain (0.2 dB per step) */
	out = MAX_OUTPUT / 3;
	while (gain-- > 0)
		out *= 1.023292992; /* = (10 ^ (0.2/20)) */

	/* build volume table (2dB per step) */
	for (int i = 0; i < 15; i++)
	{
		/* limit volume to avoid clipping */
		if (out > MAX_OUTPUT / 3) m_vol_table[i] = MAX_OUTPUT / 3;
		else m_vol_table[i] = static_cast<int>(out);

		out /= 1.258925412; /* = 10 ^ (2/20) = 2dB */
	}
	m_vol_table[15] = 0;
}
