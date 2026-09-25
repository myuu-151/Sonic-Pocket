// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
/*******************************************************************

Toshiba TLCS-900 emulation

Standalone adaptation of MAME's src/devices/cpu/tlcs900/tlcs900.cpp.
The instruction tables and handlers (900tbl.hxx, 900htbl.hxx) are
included unmodified from upstream.

*******************************************************************/

#include "tlcs900.h"

static constexpr u8 FLAG_CF = 0x01;
static constexpr u8 FLAG_NF = 0x02;
static constexpr u8 FLAG_VF = 0x04;
static constexpr u8 FLAG_HF = 0x10;
static constexpr u8 FLAG_ZF = 0x40;
static constexpr u8 FLAG_SF = 0x80;

tlcs900h_device::tlcs900h_device(tlcs900_bus &bus) :
	tlcs900_device(bus)
{
	m_mnemonic_80 = s_mnemonic_80;
	m_mnemonic_88 = s_mnemonic_88;
	m_mnemonic_90 = s_mnemonic_90;
	m_mnemonic_98 = s_mnemonic_98;
	m_mnemonic_a0 = s_mnemonic_a0;
	m_mnemonic_b0 = s_mnemonic_b0;
	m_mnemonic_b8 = s_mnemonic_b8;
	m_mnemonic_c0 = s_mnemonic_c0;
	m_mnemonic_c8 = s_mnemonic_c8;
	m_mnemonic_d0 = s_mnemonic_d0;
	m_mnemonic_d8 = s_mnemonic_d8;
	m_mnemonic_e0 = s_mnemonic_e0;
	m_mnemonic_e8 = s_mnemonic_e8;
	m_mnemonic_f0 = s_mnemonic_f0;
	m_mnemonic = s_mnemonic;
}


inline uint8_t tlcs900_device::RDOP()
{
	uint8_t data;

	if ( m_prefetch_clear )
	{
		for ( int i = 0; i < 4; i++ )
		{
			m_prefetch[ i ] = RDMEM( m_pc.d + i );
		}
		m_prefetch_index = 0;
		m_prefetch_clear = false;
	}
	else
	{
		m_prefetch[ m_prefetch_index ] = RDMEM( m_pc.d + 3 );
		m_prefetch_index = ( m_prefetch_index + 1 ) & 0x03;
	}
	data = m_prefetch[ m_prefetch_index ];

	m_pc.d++;
	return data;
}


void tlcs900_device::device_start()
{
	m_pc.d = 0;
	memset(m_xwa, 0x00, sizeof(m_xwa));
	memset(m_xbc, 0x00, sizeof(m_xbc));
	memset(m_xde, 0x00, sizeof(m_xde));
	memset(m_xhl, 0x00, sizeof(m_xhl));
	m_xix.d = 0;
	m_xiy.d = 0;
	m_xiz.d = 0;
	m_xnsp.d = 0;
	m_xssp.d = 0;
	m_sr.d = 0;
	m_f2.d = 0;
	memset(m_dmas, 0x00, sizeof(m_dmas));
	memset(m_dmad, 0x00, sizeof(m_dmad));
	memset(m_dmac, 0x00, sizeof(m_dmac));
	memset(m_dmam, 0x00, sizeof(m_dmam));
	m_intnest = 0;
	memset(m_level, 0x00, sizeof(m_level));
	m_nmi_state = CLEAR_LINE;
	m_icount = 0;
}


void tlcs900_device::device_reset()
{
	m_pc.d = 0x00008000;
	/* system mode, iff set to 111, min mode, register bank 0 */
	m_sr.d = 0xf000;
	m_regbank = 0;
	m_xssp.d = 0x0100;
	m_intnest = 0;
	m_halted = 0;
	m_check_irqs = 0;
	m_prefetch_clear = true;
	m_irq_inhibit = false;
}


void tlcs900h_device::device_reset()
{
	m_pc.b.l = RDMEM( 0xffff00 );
	m_pc.b.h = RDMEM( 0xffff01 );
	m_pc.b.h2 = RDMEM( 0xffff02 );
	m_pc.b.h3 = 0;
	/* system mode, iff set to 111, max mode, register bank 0 */
	m_sr.d = 0xf800;
	m_regbank = 0;
	m_xssp.d = 0x0100;
	m_intnest = 0;
	m_halted = 0;
	m_check_irqs = 0;
	m_prefetch_clear = true;
	m_irq_inhibit = false;
}


#include "900tbl.hxx"
#include "900htbl.hxx"


int tlcs900_device::step()
{
	const tlcs900inst *inst;

	m_cycles = 0;

	if ( m_check_irqs )
	{
		if ( m_irq_inhibit )
		{
			/* Interrupt shadow after EI/RETI: acceptance is deferred until
			   after the instruction following EI or RETI. */
			m_irq_inhibit = false;
		}
		else
		{
			tlcs900_check_irqs();
			m_check_irqs = 0;
		}
	}
	else
	{
		m_irq_inhibit = false;
	}

	if ( m_halted )
	{
		debugger_wait_hook();
		m_cycles += 8;
	}
	else if ( trap_hook( m_pc.d ) )
	{
		/* High-level emulated code (BIOS) ran in place of instructions at PC. */
		m_prefetch_clear = true;
		m_cycles += 8;
	}
	else
	{
		debugger_instruction_hook( m_pc.d );

		m_op = RDOP();
		inst = &m_mnemonic[m_op];
		prepare_operands( inst );

		/* Execute the instruction */
		(this->*inst->opfunc)();
		m_cycles += inst->cycles;
	}

	tlcs900_handle_ad();

	tlcs900_handle_timers();

	tlcs900_check_hdma();

	return m_cycles;
}


int tlcs900_device::execute(int cycles)
{
	m_icount = cycles;
	do
	{
		m_icount -= step();
	} while ( m_icount > 0 );
	return cycles - m_icount;
}
