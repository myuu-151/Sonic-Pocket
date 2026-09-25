// license:BSD-3-Clause
// copyright-holders:Wilbert Pol, Felipe Sanches
// TMP95C061 on-chip peripherals needed by the Neo Geo Pocket Color, adapted
// from MAME's src/devices/cpu/tlcs900/tmp95c061.{h,cpp}. Only the pieces the
// NGPC uses are kept: 8-bit timers 0-3, the interrupt controller, port A
// (timer flip-flop 3 drives the Z80 interrupt), A/D (battery level) and the
// watchdog/serial registers as plain storage.
#pragma once

#include "cpu/tlcs900/tlcs900.h"

#include <functional>

class ngpc_cpu : public tlcs900h_device
{
public:
	explicit ngpc_cpu(tlcs900_bus &bus);

	void device_reset() override;
	void execute_set_input(int inputnum, int state) override;

	// Internal register file at 0x000000-0x00007f.
	uint8_t internal_r(offs_t offset);
	void internal_w(offs_t offset, uint8_t data);

	// Advances the peripherals by `cycles` CPU cycles without executing
	// instructions. Recompiled code uses this in place of step().
	void advance_peripherals(int cycles);

	// Called when timer flip-flop 3 changes (NGPC: rising edge -> Z80 IRQ).
	std::function<void(int to3)> on_to3;

	// Receives the cycle count of every completed step (interpreted or
	// recompiled); the machine uses it to advance video and sound.
	std::function<void(int cycles)> on_step_finished;
	void step_finished(int cycles) override { if (on_step_finished) on_step_finished(cycles); }

	// High-level BIOS entry points; see tlcs900_device::trap_hook.
	std::function<bool(offs_t pc)> on_trap;
	bool trap_hook(offs_t pc) override { return on_trap && on_trap(pc); }

	void tlcs900_check_hdma() override {}
	void tlcs900_check_irqs() override;
	void tlcs900_handle_ad() override;
	void tlcs900_handle_timers() override;

	enum
	{
		INTE0AD,
		INTE45,
		INTE67,
		INTET10,
		INTET32,
		INTET54,
		INTET76,
		INTES0,
		INTES1,
		INTETC10,
		INTETC32
	};

	void change_tff(int which, int change);

	uint8_t m_raw[0x80] = {};     // plain storage for registers without behaviour
	uint8_t m_trun = 0;
	uint8_t m_t8_reg[4] = {};
	uint8_t m_t8_mode[2] = {};
	uint8_t m_t8_invert = 0;
	uint8_t m_trdc = 0;
	uint8_t m_to1 = 0;
	uint8_t m_to3 = 0;
	uint8_t m_ad_mode = 0;
	uint16_t m_ad_result[4] = {};
	uint8_t m_int_reg[0x0b] = {};
	uint8_t m_iimc = 0;
	uint8_t m_dma_vector[4] = {};
	uint8_t m_watchdog_mode = 0;
};
