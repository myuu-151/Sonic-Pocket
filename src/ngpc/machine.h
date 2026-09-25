// Neo Geo Pocket Color machine used by the Sonic Pocket recomp runtime.
//
// Memory map and I/O behaviour follow MAME's src/mame/snk/ngp.cpp. The BIOS is
// high-level emulated: boot state, interrupt dispatch and system calls match
// the Mednafen/BizHawk NGP core so runs can be compared with its RAM dumps.
#pragma once

#include "ngpc/k2ge.h"
#include "ngpc/ngpc_cpu.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "z80.h"
}

namespace ngpc {

// Controller bits as read from I/O register 0xB0.
enum Button : uint8_t
{
	BUTTON_UP = 0x01,
	BUTTON_DOWN = 0x02,
	BUTTON_LEFT = 0x04,
	BUTTON_RIGHT = 0x08,
	BUTTON_A = 0x10,
	BUTTON_B = 0x20,
	BUTTON_OPTION = 0x40,
};

struct RtcTime
{
	int year = 2000;   // full year; stored as two BCD digits
	int month = 1;
	int day = 1;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int day_of_week = 6;
};

class Machine : public tlcs900_bus
{
public:
	static constexpr uint32_t CART_BASE = 0x200000;
	static constexpr uint32_t BIOS_BASE = 0xff0000;
	static constexpr int CPU_CLOCK = 6144000;

	Machine();
	~Machine() override;

	// Loads a cartridge image. Returns an empty string on success.
	std::string load_cartridge(std::vector<uint8_t> image);

	// Puts the machine in the state the BIOS leaves it in when it starts the
	// cartridge (entry point, system RAM, I/O and video defaults).
	void boot(int language = 1, const RtcTime &rtc = {});

	// Runs one video frame (199 lines, 102485 CPU cycles).
	void run_frame();

	void set_buttons(uint8_t buttons) { m_buttons = buttons; }

	// Prints the next `count` instructions' PC and registers to stderr.
	void trace_instructions(int64_t count) { m_trace_remaining = count; }

	const uint32_t *framebuffer() const { return m_video.framebuffer; }
	uint64_t frame_count() const { return m_frame; }

	// Main work RAM, 0x4000-0x6FFF.
	uint8_t *work_ram() { return m_ram; }

	// tlcs900_bus
	uint8_t read_byte(offs_t addr) override;
	void write_byte(offs_t addr, uint8_t data) override;

	ngpc_cpu &cpu() { return *m_cpu; }
	k2ge &video() { return m_video; }

	// Sound chip writes (T6W28 port 0 = right/tone, 1 = left/noise), exposed
	// for the audio backend.
	std::vector<std::pair<uint8_t, uint8_t>> psg_writes;

private:
	enum FlashState
	{
		F_READ,
		F_PROG1,
		F_PROG2,
		F_COMMAND,
		F_ID_READ,
		F_AUTO_PROGRAM,
		F_AUTO_CHIP_ERASE,
		F_AUTO_BLOCK_ERASE,
		F_BLOCK_PROTECT,
	};

	uint8_t io_read(offs_t offset);
	void io_write(offs_t offset, uint8_t data);
	void flash_write(offs_t offset, uint8_t data);
	void restore_flash_id_bytes();

	bool bios_trap(offs_t pc);
	void bios_system_call(int function);
	void bios_return_from_call();
	void bios_return_from_interrupt();

	void run_cpu_until(int64_t target_cycle);
	void run_z80_until(int64_t target_cpu_cycle);

	static uint8_t z80_read(void *userdata, uint16_t addr);
	static void z80_write(void *userdata, uint16_t addr, uint8_t data);
	static uint8_t z80_in(z80 *z, uint8_t port);
	static void z80_out(z80 *z, uint8_t port, uint8_t data);

	std::unique_ptr<ngpc_cpu> m_cpu;
	k2ge m_video;
	z80 m_z80 = {};
	bool m_z80_running = false;
	uint8_t m_old_to3 = 0;

	std::vector<uint8_t> m_cart;        // flash contents, mutable
	std::vector<uint8_t> m_bios;        // synthetic 64 KiB BIOS image
	uint8_t m_ram[0x3000] = {};
	uint8_t m_sound_ram[0x1000] = {};
	uint8_t m_io[0x40] = {};
	uint8_t m_buttons = 0;

	FlashState m_flash_state = F_READ;
	uint8_t m_flash_command = 0;
	uint8_t m_flash_id_backup[16] = {};

	int64_t m_cpu_cycle = 0;           // total CPU cycles since boot
	int64_t m_z80_cycle_target = 0;    // in CPU cycles
	uint64_t m_frame = 0;
	int m_line_start_cycle = 0;
	int64_t m_trace_remaining = 0;
};

} // namespace ngpc
