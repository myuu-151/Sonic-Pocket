// license:BSD-3-Clause
// copyright-holders:Wilbert Pol, Felipe Sanches
// Adapted from MAME's src/devices/cpu/tlcs900/tmp95c061.cpp.

#include "ngpc_cpu.h"

#include <algorithm>

namespace {

enum ff_change
{
	FF_CLEAR,
	FF_SET,
	FF_INVERT
};

// Prescaler taps in CPU cycles. MAME uses the databook's fc/8, fc/32, fc/128
// and fc/2048; the NGPC runs its timers much slower than that. These are the
// Mednafen/BizHawk rates (256, 1024, 4096 and 65536 cycles), which match the
// reference captures. Timer 2 on phiT1 runs at twice the rate, as in Mednafen.
constexpr int PRESCALE_T1   = 8;
constexpr int PRESCALE_T4   = 10;
constexpr int PRESCALE_T16  = 12;
constexpr int PRESCALE_T256 = 16;
constexpr int PRESCALE_T1_TIMER2 = 7;

const struct {
	uint8_t reg;
	uint8_t iff;
	uint8_t vector;
} irq_vector_map[] =
{
	{ ngpc_cpu::INTETC32, 0x80, 0x80 },   // INTTC3
	{ ngpc_cpu::INTETC32, 0x08, 0x7c },   // INTTC2
	{ ngpc_cpu::INTETC10, 0x80, 0x78 },   // INTTC1
	{ ngpc_cpu::INTETC10, 0x08, 0x74 },   // INTTC0
	{ ngpc_cpu::INTE0AD,  0x80, 0x70 },   // INTAD
	{ ngpc_cpu::INTES1,   0x80, 0x6c },   // INTTX1
	{ ngpc_cpu::INTES1,   0x08, 0x68 },   // INTRX1
	{ ngpc_cpu::INTES0,   0x80, 0x64 },   // INTTX0
	{ ngpc_cpu::INTES0,   0x08, 0x60 },   // INTRX0
	{ ngpc_cpu::INTET76,  0x80, 0x5c },   // INTTR7
	{ ngpc_cpu::INTET76,  0x08, 0x58 },   // INTTR6
	{ ngpc_cpu::INTET54,  0x80, 0x54 },   // INTTR5
	{ ngpc_cpu::INTET54,  0x08, 0x50 },   // INTTR4
	{ ngpc_cpu::INTET32,  0x80, 0x4c },   // INTT3
	{ ngpc_cpu::INTET32,  0x08, 0x48 },   // INTT2
	{ ngpc_cpu::INTET10,  0x80, 0x44 },   // INTT1
	{ ngpc_cpu::INTET10,  0x08, 0x40 },   // INTT0
	{ ngpc_cpu::INTE67,   0x80, 0x38 },   // INT7
	{ ngpc_cpu::INTE67,   0x08, 0x34 },   // INT6
	{ ngpc_cpu::INTE45,   0x80, 0x30 },   // INT5
	{ ngpc_cpu::INTE45,   0x08, 0x2c },   // INT4
	{ ngpc_cpu::INTE0AD,  0x08, 0x28 }    // INT0
};
constexpr int NUM_MASKABLE_IRQS = sizeof(irq_vector_map) / sizeof(irq_vector_map[0]);

} // namespace


ngpc_cpu::ngpc_cpu(tlcs900_bus &bus) :
	tlcs900h_device(bus)
{
	m_am8_16 = 1;
}


void ngpc_cpu::device_reset()
{
	tlcs900h_device::device_reset();

	m_to1 = 0;
	m_to3 = 0;
	m_ad_cycles_left = 0;
	m_timer_pre = 0;
	std::fill_n(m_timer_change, 8, 0);
	std::fill_n(m_timer_8, 6, 0);
	m_trun = 0x00;
	std::fill_n(m_t8_reg, 4, 0x00);
	std::fill_n(m_t8_mode, 2, 0x00);
	m_t8_invert = 0xcc;
	m_trdc = 0x00;
	m_watchdog_mode = 0x80;
	m_ad_mode = 0x00;
	std::fill_n(m_int_reg, 0xb, 0x00);
	m_iimc = 0x00;
	std::fill_n(m_dma_vector, 4, 0x00);
	// Main battery power reads as full scale on AN0.
	std::fill_n(m_ad_result, 4, 0x3ff);
}


void ngpc_cpu::tlcs900_check_irqs()
{
	/* Check for NMI */
	if ( m_nmi_state == ASSERT_LINE )
	{
		tlcs900_intnest_accept();

		m_xssp.d -= 4;
		WRMEML( m_xssp.d, m_pc.d );
		m_xssp.d -= 2;
		WRMEMW( m_xssp.d, m_sr.w.l );
		m_pc.d = RDMEML( 0xffff00 + 0x20 );
		m_cycles += 18;
		m_prefetch_clear = true;

		m_halted = 0;

		m_nmi_state = CLEAR_LINE;

		return;
	}

	/* Check regular irqs */
	int irq_vectors[9] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	for( int i = 0; i < NUM_MASKABLE_IRQS; i++ )
	{
		if ( m_int_reg[irq_vector_map[i].reg] & irq_vector_map[i].iff )
		{
			switch( irq_vector_map[i].iff )
			{
			case 0x80:
				irq_vectors[ ( m_int_reg[ irq_vector_map[i].reg ] >> 4 ) & 0x07 ] = i;
				break;
			case 0x08:
				irq_vectors[ m_int_reg[ irq_vector_map[i].reg ] & 0x07 ] = i;
				break;
			}
		}
	}

	/* Check highest allowed priority irq */
	int irq = -1;
	int level = 0;
	for ( int i = std::max( 1, ( ( m_sr.b.h & 0x70 ) >> 4 ) ); i < 7; i++ )
	{
		if ( irq_vectors[i] >= 0 )
		{
			irq = irq_vectors[i];
			level = i + 1;
		}
	}

	/* Take irq */
	if ( irq >= 0 )
	{
		uint8_t vector = irq_vector_map[irq].vector;

		tlcs900_intnest_accept();

		m_xssp.d -= 4;
		WRMEML( m_xssp.d, m_pc.d );
		m_xssp.d -= 2;
		WRMEMW( m_xssp.d, m_sr.w.l );

		/* Mask off any lower priority interrupts  */
		m_sr.b.h = ( m_sr.b.h & 0x8f ) | ( level << 4 );

		m_pc.d = RDMEML( 0xffff00 + vector );
		m_cycles += 18;
		m_prefetch_clear = true;

		m_halted = 0;

		/* Clear taken IRQ */
		m_int_reg[ irq_vector_map[irq].reg ] &= ~ irq_vector_map[irq].iff;
	}
}


void ngpc_cpu::tlcs900_handle_ad()
{
	if ( m_ad_cycles_left > 0 )
	{
		m_ad_cycles_left -= m_cycles;
		if ( m_ad_cycles_left <= 0 )
		{
			/* Battery and unused channels are constant: results already hold them. */
			m_ad_mode &= ~ 0x40;
			m_ad_mode |= 0x80;

			m_int_reg[INTE0AD] |= 0x80;
			m_check_irqs = 1;

			if ( m_ad_mode & 0x20 )
				m_ad_cycles_left = ( m_ad_mode & 0x08 ) ? 320 : 160;
		}
	}
}


void ngpc_cpu::change_tff( int which, int change )
{
	uint8_t &ff = ( which == 1 ) ? m_to1 : m_to3;
	switch( change )
	{
	case FF_CLEAR:
		ff = 0;
		break;
	case FF_SET:
		ff = 1;
		break;
	case FF_INVERT:
		ff ^= 1;
		break;
	}

	if ( which == 3 && on_to3 )
		on_to3( m_to3 );
}


void ngpc_cpu::tlcs900_handle_timers()
{
	uint32_t  old_pre = m_timer_pre;

	/* Is the pre-scaler active */
	if ( m_trun & 0x80 )
		m_timer_pre += m_cycles;

	/* Timer 0 */
	if ( m_trun & 0x01 )
	{
		switch( m_t8_mode[0] & 0x03 )
		{
		case 0x00:  /* TIO */
			break;
		case 0x01:  /* T1 */
			m_timer_change[0] += ( m_timer_pre >> PRESCALE_T1 ) - ( old_pre >> PRESCALE_T1 );
			break;
		case 0x02:  /* T4 */
			m_timer_change[0] += ( m_timer_pre >> PRESCALE_T4 ) - ( old_pre >> PRESCALE_T4 );
			break;
		case 0x03:  /* T16 */
			m_timer_change[0] += ( m_timer_pre >> PRESCALE_T16 ) - ( old_pre >> PRESCALE_T16 );
			break;
		}

		for( ; m_timer_change[0] > 0; m_timer_change[0]-- )
		{
			m_timer_8[0] += 1;
			if ( m_timer_8[0] == m_t8_reg[0] )
			{
				if ( ( m_trun & 0x02 ) && ( m_t8_mode[0] & 0x0c ) == 0x00 )
				{
					m_timer_change[1] += 1;
				}

				/* In 16bit timer mode the timer should not be reset */
				if ( ( m_t8_mode[0] & 0xc0 ) != 0x40 )
				{
					m_timer_8[0] = 0;
					m_int_reg[INTET10] |= 0x08;
					m_check_irqs = 1;
				}
			}
		}
	}

	/* Timer 1 */
	if ( m_trun & 0x02 )
	{
		switch( ( m_t8_mode[0] >> 2 ) & 0x03 )
		{
		case 0x00:  /* TO0TRG */
			break;
		case 0x01:  /* T1 */
			m_timer_change[1] += ( m_timer_pre >> PRESCALE_T1 ) - ( old_pre >> PRESCALE_T1 );
			break;
		case 0x02:  /* T16 */
			m_timer_change[1] += ( m_timer_pre >> PRESCALE_T16 ) - ( old_pre >> PRESCALE_T16 );
			break;
		case 0x03:  /* T256 */
			m_timer_change[1] += ( m_timer_pre >> PRESCALE_T256 ) - ( old_pre >> PRESCALE_T256 );
			break;
		}

		for( ; m_timer_change[1] > 0; m_timer_change[1]-- )
		{
			m_timer_8[1] += 1;
			if ( m_timer_8[1] == m_t8_reg[1] )
			{
				m_timer_8[1] = 0;
				m_int_reg[INTET10] |= 0x80;
				m_check_irqs = 1;

				if ( m_t8_invert & 0x02 )
				{
					change_tff( 1, FF_INVERT );
				}

				/* In 16bit timer mode also reset timer 0 */
				if ( ( m_t8_mode[0] & 0xc0 ) == 0x40 )
				{
					m_timer_8[0] = 0;
				}
			}
		}
	}

	/* Timer 2 */
	if ( m_trun & 0x04 )
	{
		switch( m_t8_mode[1] & 0x03 )
		{
		case 0x00:  /* invalid */
		case 0x01:  /* T1 */
			m_timer_change[2] += ( m_timer_pre >> PRESCALE_T1_TIMER2 ) - ( old_pre >> PRESCALE_T1_TIMER2 );
			break;
		case 0x02:  /* T4 */
			m_timer_change[2] += ( m_timer_pre >> PRESCALE_T4 ) - ( old_pre >> PRESCALE_T4 );
			break;
		case 0x03:  /* T16 */
			m_timer_change[2] += ( m_timer_pre >> PRESCALE_T16 ) - ( old_pre >> PRESCALE_T16 );
			break;
		}

		for( ; m_timer_change[2] > 0; m_timer_change[2]-- )
		{
			m_timer_8[2] += 1;
			if ( m_timer_8[2] == m_t8_reg[2] )
			{
				if ( ( m_trun & 0x08 ) && ( m_t8_mode[1] & 0x0c ) == 0x00 )
				{
					m_timer_change[3] += 1;
				}

				/* In 16bit timer mode the timer should not be reset */
				if ( ( m_t8_mode[1] & 0xc0 ) != 0x40 )
				{
					m_timer_8[2] = 0;
					m_int_reg[INTET32] |= 0x08;
					m_check_irqs = 1;
				}
			}
		}
	}

	/* Timer 3 */
	if ( m_trun & 0x08 )
	{
		switch( ( m_t8_mode[1] >> 2 ) & 0x03 )
		{
		case 0x00:  /* TO2TRG */
			break;
		case 0x01:  /* T1 */
			m_timer_change[3] += ( m_timer_pre >> PRESCALE_T1 ) - ( old_pre >> PRESCALE_T1 );
			break;
		case 0x02:  /* T16 */
			m_timer_change[3] += ( m_timer_pre >> PRESCALE_T16 ) - ( old_pre >> PRESCALE_T16 );
			break;
		case 0x03:  /* T256 */
			m_timer_change[3] += ( m_timer_pre >> PRESCALE_T256 ) - ( old_pre >> PRESCALE_T256 );
			break;
		}

		for( ; m_timer_change[3] > 0; m_timer_change[3]-- )
		{
			m_timer_8[3] += 1;
			if ( m_timer_8[3] == m_t8_reg[3] )
			{
				m_timer_8[3] = 0;
				m_int_reg[INTET32] |= 0x80;
				m_check_irqs = 1;

				if ( m_t8_invert & 0x20 )
				{
					change_tff( 3, FF_INVERT );
				}

				/* In 16bit timer mode also reset timer 2 */
				if ( ( m_t8_mode[1] & 0xc0 ) == 0x40 )
				{
					m_timer_8[2] = 0;
				}
			}
		}
	}

	m_timer_pre &= 0xffffff;
}


void ngpc_cpu::advance_peripherals(int cycles)
{
	m_cycles = cycles;
	tlcs900_handle_ad();
	tlcs900_handle_timers();
}


void ngpc_cpu::execute_set_input(int input, int level)
{
	switch( input )
	{
	case TLCS900_NMI:
		if ( m_level[TLCS900_NMI] == CLEAR_LINE && level == ASSERT_LINE )
		{
			m_nmi_state = level;
		}
		m_level[TLCS900_NMI] = level;
		break;

	case TLCS900_INT0:
		if ( m_iimc & 0x04 )
		{
			if ( m_iimc & 0x02 )
			{
				if ( m_level[TLCS900_INT0] == CLEAR_LINE && level == ASSERT_LINE )
				{
					m_halted = 0;
					m_int_reg[INTE0AD] |= 0x08;
				}
			}
			else
			{
				if ( level == ASSERT_LINE )
					m_int_reg[INTE0AD] |= 0x08;
				else
					m_int_reg[INTE0AD] &= ~ 0x08;
			}
		}
		m_level[TLCS900_INT0] = level;
		break;

	case TLCS900_INT4:
		if ( m_level[TLCS900_INT4] == CLEAR_LINE && level == ASSERT_LINE )
		{
			m_int_reg[INTE45] |= 0x08;
		}
		m_level[TLCS900_INT4] = level;
		break;

	case TLCS900_INT5:
		if ( m_level[TLCS900_INT5] == CLEAR_LINE && level == ASSERT_LINE )
		{
			m_int_reg[INTE45] |= 0x80;
		}
		m_level[TLCS900_INT5] = level;
		break;

	case TLCS900_TIO:   /* External timer input for timer 0 */
		if ( ( m_trun & 0x01 ) && ( m_t8_mode[0] & 0x03 ) == 0x00 )
		{
			if ( m_level[TLCS900_TIO] == CLEAR_LINE && level == ASSERT_LINE )
			{
				m_timer_change[0] += 1;
			}
		}
		m_level[TLCS900_TIO] = level;
		break;
	}
	m_check_irqs = 1;
}


uint8_t ngpc_cpu::internal_r(offs_t offset)
{
	offset &= 0x7f;
	switch ( offset )
	{
	case 0x20: return m_trun;
	case 0x25: return m_t8_invert;
	case 0x29: return m_trdc;
	case 0x60: case 0x62: case 0x64: case 0x66:
		m_ad_mode &= ~0x80;
		m_int_reg[INTE0AD] &= ~0x80;
		return m_ad_result[(offset - 0x60) >> 1] << 6 | 0x3f;
	case 0x61: case 0x63: case 0x65: case 0x67:
		m_ad_mode &= ~0x80;
		return m_ad_result[(offset - 0x60) >> 1] >> 2;
	case 0x6d: return m_ad_mode;
	case 0x6e: return m_watchdog_mode;
	default:
		if ( offset >= 0x70 && offset <= 0x7a )
			return m_int_reg[offset - 0x70];
		return m_raw[offset];
	}
}


void ngpc_cpu::internal_w(offs_t offset, uint8_t data)
{
	offset &= 0x7f;
	switch ( offset )
	{
	case 0x20:
		if ( ! ( data & 0x01 ) ) { m_timer_8[0] = 0; m_timer_change[0] = 0; }
		if ( ! ( data & 0x02 ) ) { m_timer_8[1] = 0; m_timer_change[1] = 0; }
		if ( ! ( data & 0x04 ) ) { m_timer_8[2] = 0; m_timer_change[2] = 0; }
		if ( ! ( data & 0x08 ) ) { m_timer_8[3] = 0; m_timer_change[3] = 0; }
		if ( ! ( data & 0x10 ) ) m_timer_8[4] = 0;
		if ( ! ( data & 0x20 ) ) m_timer_8[5] = 0;
		m_trun = data;
		return;
	case 0x22: case 0x23:
		m_t8_reg[offset - 0x22] = data;
		return;
	case 0x24:
		m_t8_mode[0] = data;
		return;
	case 0x25:
		switch( data & 0x0c )
		{
		case 0x00: change_tff( 1, FF_INVERT ); break;
		case 0x04: change_tff( 1, FF_SET ); break;
		case 0x08: change_tff( 1, FF_CLEAR ); break;
		}
		switch( data & 0xc0 )
		{
		case 0x00: change_tff( 3, FF_INVERT ); break;
		case 0x40: change_tff( 3, FF_SET ); break;
		case 0x80: change_tff( 3, FF_CLEAR ); break;
		}
		m_t8_invert = data | 0xcc;
		return;
	case 0x26: case 0x27:
		m_t8_reg[offset - 0x26 + 2] = data;
		return;
	case 0x28:
		m_t8_mode[1] = data;
		return;
	case 0x29:
		m_trdc = data;
		return;
	case 0x6d:
		data = ( m_ad_mode & 0xc0 ) | ( data & 0x3f );
		if ( data & 0x04 )
		{
			data &= ~0x04;
			data |= 0x40;
			m_ad_cycles_left = ( data & 0x08 ) ? 320 : 160;
		}
		m_ad_mode = data;
		return;
	case 0x6e:
		m_watchdog_mode = data;
		return;
	case 0x7b:
		m_iimc = data;
		m_check_irqs = 1;
		return;
	case 0x7c: case 0x7d: case 0x7e: case 0x7f:
		m_dma_vector[offset - 0x7c] = data;
		return;
	default:
		if ( offset >= 0x70 && offset <= 0x7a )
		{
			const int reg = offset - 0x70;
			if ( data & 0x80 )
				data = ( data & 0x7f ) | ( m_int_reg[reg] & 0x80 );
			if ( data & 0x08 )
				data = ( data & 0xf7 ) | ( m_int_reg[reg] & 0x08 );
			m_int_reg[reg] = data;
			m_check_irqs = 1;
			return;
		}
		m_raw[offset] = data;
		return;
	}
}
