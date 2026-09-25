// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
// K2GE (Neo Geo Pocket Color) graphics, adapted from MAME's
// src/mame/snk/k1ge.{h,cpp}. Colour mode only; the monochrome K1GE
// compatibility palette is not implemented because Sonic Pocket
// Adventure is a colour-only title.
#pragma once

#include <cstdint>
#include <functional>

class k2ge
{
public:
	static constexpr int SCREEN_WIDTH = 160;
	static constexpr int SCREEN_HEIGHT = 152;
	static constexpr int LINES_PER_FRAME = 199;
	static constexpr int CYCLES_PER_LINE = 515;
	static constexpr int HBLANK_END_CYCLE = 480;

	void reset();

	uint8_t read(uint32_t offset) const;
	void write(uint32_t offset, uint8_t data);

	// Line timing, driven by the machine. begin_line() runs at the start of
	// every line (0..198) and end_hblank() at cycle 480 of lines that raised
	// the HBlank pin.
	void begin_line(int line);
	void end_hblank();

	// Current beam position, supplied by the machine for RAS.H/RAS.V reads.
	std::function<int()> current_cycle_in_line;
	int current_line = 0;

	std::function<void(int)> vblank_pin_w;
	std::function<void(int)> hblank_pin_w;

	// Finished frame, 0xAARRGGBB.
	uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT] = {};

	uint8_t vram[0x4000] = {};

private:
	struct sprite_t
	{
		uint16_t spr_data;
		uint8_t x;
		uint8_t y;
		uint8_t index;
	};

	void draw(int line);
	void draw_scroll_plane(uint16_t *p, uint16_t base, int line, int scroll_x, int scroll_y, int pal_base);
	void draw_sprite_plane(uint16_t *p, uint16_t priority, int line, int scroll_x, int scroll_y);
	void get_tile_data(int offset_x, uint16_t base, int line, int scroll_y, int pal_base,
			uint16_t &pcode, bool &hflip, uint16_t &tile_addr, uint16_t &tile_data) const;
	uint16_t bg_pen() const;
	uint16_t oow_pen() const;
	uint32_t pen_to_argb(uint16_t pen) const;

	uint8_t m_wba_h = 0;
	uint8_t m_wba_v = 0;
	uint8_t m_wsi_h = 0xff;
	uint8_t m_wsi_v = 0xff;
};
