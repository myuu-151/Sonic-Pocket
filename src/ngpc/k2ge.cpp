// license:BSD-3-Clause
// copyright-holders:Wilbert Pol
// Adapted from MAME's src/mame/snk/k1ge.cpp (K2GE colour path).

#include "k2ge.h"

#include <cstring>

namespace {

constexpr bool bit(uint32_t value, int n) { return (value >> n) & 1; }

// Pens 0..191 index palette RAM directly (sprites, plane 1, plane 2);
// 0xf0..0xf7 are background colours and 0xf8..0xff window colours.
inline void get_tile_addr(uint16_t data, bool &hflip, uint16_t &tile_addr)
{
	hflip = bit(data, 15);
	tile_addr = 0x2000 + ((data & 0x1ff) * 16);
}

inline uint16_t get_pixel(bool hflip, uint16_t &tile_data)
{
	uint16_t col;
	if (hflip)
	{
		col = tile_data & 0x0003;
		tile_data >>= 2;
	}
	else
	{
		col = tile_data >> 14;
		tile_data <<= 2;
	}
	return col;
}

} // namespace


void k2ge::reset()
{
	std::memset(vram, 0, sizeof(vram));
	write(0x000, 0x00);   /* Interrupt enable */
	write(0x002, 0x00);   /* WBA.H */
	write(0x003, 0x00);   /* WVA.V */
	write(0x004, 0xff);   /* WSI.H */
	write(0x005, 0xff);   /* WSI.V */
	write(0x007, 0xc6);   /* REF */
	write(0x012, 0x00);   /* 2D control */
	write(0x020, 0x00);   /* PO.H */
	write(0x021, 0x00);   /* PO.V */
	write(0x030, 0x00);   /* PF */
	write(0x032, 0x00);   /* S1SO.H */
	write(0x033, 0x00);   /* S1SO.V */
	write(0x034, 0x00);   /* S2SO.H */
	write(0x035, 0x00);   /* S2SO.V */
	for (uint32_t offset : { 0x101u, 0x102u, 0x103u, 0x105u, 0x106u, 0x107u,
			0x109u, 0x10au, 0x10bu, 0x10du, 0x10eu, 0x10fu,
			0x111u, 0x112u, 0x113u, 0x115u, 0x116u, 0x117u, 0x118u })
		write(offset, 0x07);
	write(0x400, 0xff);   /* LED control */
	write(0x402, 0x80);   /* LEDFREG */
	write(0x7e0, 0x52);   /* RESET */
	write(0x7e2, 0x00);   /* MODE */
	m_wba_h = vram[0x002];
	m_wba_v = vram[0x003];
	m_wsi_h = vram[0x004];
	m_wsi_v = vram[0x005];
}


uint8_t k2ge::read(uint32_t offset) const
{
	offset &= 0x3fff;
	uint8_t data = vram[offset];

	switch (offset)
	{
	case 0x008:     /* RAS.H */
		data = (current_cycle_in_line ? current_cycle_in_line() : 0) >> 2;
		break;
	case 0x009:     /* RAS.V */
		data = current_line;
		break;
	}
	return data;
}


void k2ge::write(uint32_t offset, uint8_t data)
{
	offset &= 0x3fff;

	switch (offset)
	{
	case 0x000:
		if (vblank_pin_w)
			vblank_pin_w(bit(data, 7) ? bit(vram[0x010], 6) : 0);
		break;
	case 0x030:
		data &= 0x80;
		break;
	case 0x100: case 0x101: case 0x102: case 0x103:
	case 0x104: case 0x105: case 0x106: case 0x107:
	case 0x108: case 0x109: case 0x10a: case 0x10b:
	case 0x10c: case 0x10d: case 0x10e: case 0x10f:
	case 0x110: case 0x111: case 0x112: case 0x113:
	case 0x114: case 0x115: case 0x116: case 0x117:
		data &= 0x07;
		break;
	case 0x7e2:
		if (vram[0x7f0] != 0xaa)
			return;
		data &= 0x80;
		break;
	}

	/* Only the lower 4 bits of the palette entry high bytes can be written */
	if (offset >= 0x0200 && offset < 0x0400 && bit(offset, 0))
		data &= 0x0f;

	vram[offset] = data;
}


uint16_t k2ge::bg_pen() const
{
	const uint8_t bg = vram[0x118];
	return 0xf0 + (((bg & 0xc0) == 0x80) ? (bg & 7) : 0);
}


uint16_t k2ge::oow_pen() const
{
	return 0xf8 + (vram[0x012] & 7);
}


uint32_t k2ge::pen_to_argb(uint16_t pen) const
{
	const uint16_t palette = vram[0x200 + pen * 2] | (vram[0x201 + pen * 2] << 8);
	const uint32_t r = (palette & 0xf) * 0x11;
	const uint32_t g = ((palette >> 4) & 0xf) * 0x11;
	const uint32_t b = ((palette >> 8) & 0xf) * 0x11;
	return 0xff000000u | (r << 16) | (g << 8) | b;
}


void k2ge::get_tile_data(int offset_x, uint16_t base, int line, int scroll_y, int pal_base,
		uint16_t &pcode, bool &hflip, uint16_t &tile_addr, uint16_t &tile_data) const
{
	const uint16_t map_data = vram[base + offset_x] | (vram[base + offset_x + 1] << 8);
	pcode = pal_base + ((map_data & 0x1e00) >> 7);
	get_tile_addr(map_data, hflip, tile_addr);
	if (bit(map_data, 14))
		tile_addr += (7 - ((scroll_y + line) & 0x07)) * 2;
	else
		tile_addr += ((scroll_y + line) & 0x07) * 2;
	tile_data = vram[tile_addr] | (vram[tile_addr + 1] << 8);
}


void k2ge::draw_scroll_plane(uint16_t *p, uint16_t base, int line, int scroll_x, int scroll_y, int pal_base)
{
	int offset_x = (scroll_x >> 3) * 2;
	int px = scroll_x & 0x07;

	base += (((scroll_y + line) >> 3) & 0x1f) * 0x0040;

	uint16_t pcode, tile_addr, tile_data;
	bool hflip;
	get_tile_data(offset_x, base, line, scroll_y, pal_base, pcode, hflip, tile_addr, tile_data);
	if (hflip)
		tile_data >>= 2 * (scroll_x & 0x07);
	else
		tile_data <<= 2 * (scroll_x & 0x07);

	for (int i = 0; i < SCREEN_WIDTH; i++)
	{
		const uint16_t col = get_pixel(hflip, tile_data);
		if (col)
			p[i] = pcode + col;

		px++;
		if (px >= 8)
		{
			offset_x = (offset_x + 2) & 0x3f;
			get_tile_data(offset_x, base, line, scroll_y, pal_base, pcode, hflip, tile_addr, tile_data);
			px = 0;
		}
	}
}


void k2ge::draw_sprite_plane(uint16_t *p, uint16_t priority, int line, int scroll_x, int scroll_y)
{
	sprite_t spr[64];
	int num_sprites = 0;
	uint8_t spr_y = 0;
	uint8_t spr_x = 0;

	priority <<= 11;

	/* Select sprites */
	for (int i = 0; i < 256; i += 4)
	{
		const uint16_t spr_data = vram[0x800 + i] | (vram[0x801 + i] << 8);
		const uint8_t x = vram[0x802 + i];
		const uint8_t y = vram[0x803 + i];

		spr_x = bit(spr_data, 10) ? (spr_x + x) : (scroll_x + x);
		spr_y = bit(spr_data, 9) ? (spr_y + y) : (scroll_y + y);

		if ((spr_data & 0x1800) == priority)
		{
			if ((line >= spr_y || spr_y > 0xf8) && line < ((spr_y + 8) & 0xff))
			{
				spr[num_sprites].spr_data = spr_data;
				spr[num_sprites].y = spr_y;
				spr[num_sprites].x = spr_x;
				spr[num_sprites].index = i >> 2;
				num_sprites++;
			}
		}
	}

	/* Draw sprites */
	for (int i = num_sprites - 1; i >= 0; i--)
	{
		const uint16_t pcode = (vram[0x0c00 + spr[i].index] & 0x0f) << 2;

		bool hflip;
		uint16_t tile_addr;
		get_tile_addr(spr[i].spr_data, hflip, tile_addr);
		if (bit(spr[i].spr_data, 14))
			tile_addr += (7 - ((line - spr[i].y) & 0x07)) * 2;
		else
			tile_addr += ((line - spr[i].y) & 0x07) * 2;
		uint16_t tile_data = vram[tile_addr] | (vram[tile_addr + 1] << 8);

		for (int j = 0; j < 8; j++)
		{
			spr_x = spr[i].x + j;

			const uint16_t col = get_pixel(hflip, tile_data);
			if (spr_x < SCREEN_WIDTH && col)
				p[spr_x] = pcode + col;
		}
	}
}


void k2ge::draw(int line)
{
	uint16_t p[SCREEN_WIDTH];
	const uint16_t oowcol = oow_pen();

	if (line < m_wba_v || line >= m_wba_v + m_wsi_v)
	{
		for (int i = 0; i < SCREEN_WIDTH; i++)
			p[i] = oowcol;
	}
	else
	{
		const uint16_t bgcol = bg_pen();
		for (int i = 0; i < SCREEN_WIDTH; i++)
			p[i] = bgcol;

		if (bit(vram[0x030], 7))
		{
			draw_sprite_plane(p, 1, line, vram[0x020], vram[0x021]);
			draw_scroll_plane(p, 0x1000, line, vram[0x032], vram[0x033], 0x40);
			draw_sprite_plane(p, 2, line, vram[0x020], vram[0x021]);
			draw_scroll_plane(p, 0x1800, line, vram[0x034], vram[0x035], 0x80);
			draw_sprite_plane(p, 3, line, vram[0x020], vram[0x021]);
		}
		else
		{
			draw_sprite_plane(p, 1, line, vram[0x020], vram[0x021]);
			draw_scroll_plane(p, 0x1800, line, vram[0x034], vram[0x035], 0x80);
			draw_sprite_plane(p, 2, line, vram[0x020], vram[0x021]);
			draw_scroll_plane(p, 0x1000, line, vram[0x032], vram[0x033], 0x40);
			draw_sprite_plane(p, 3, line, vram[0x020], vram[0x021]);
		}

		for (int i = 0; i < m_wba_h && i < SCREEN_WIDTH; i++)
			p[i] = oowcol;

		for (int i = m_wba_h + m_wsi_h; i < SCREEN_WIDTH; i++)
			p[i] = oowcol;
	}

	uint32_t *out = &framebuffer[line * SCREEN_WIDTH];
	for (int i = 0; i < SCREEN_WIDTH; i++)
		out[i] = pen_to_argb(p[i]);
}


void k2ge::begin_line(int y)
{
	current_line = y;

	/* Check for start of VBlank */
	if (y >= SCREEN_HEIGHT)
	{
		vram[0x010] |= 0x40;
		if (bit(vram[0x000], 7) && vblank_pin_w)
			vblank_pin_w(1);
	}

	/* Check for end of VBlank */
	if (y == 0)
	{
		m_wba_h = vram[0x002];
		m_wba_v = vram[0x003];
		m_wsi_h = vram[0x004];
		m_wsi_v = vram[0x005];
		vram[0x010] &= ~0x40;
		if (bit(vram[0x000], 7) && vblank_pin_w)
			vblank_pin_w(0);
	}

	/* Check if Hint should be triggered */
	if (y == LINES_PER_FRAME - 1 || y < 151)
	{
		if (bit(vram[0x000], 6) && hblank_pin_w)
			hblank_pin_w(1);
	}

	/* Draw a line when inside visible area */
	if (y && y < SCREEN_HEIGHT + 1)
		draw(y - 1);
}


void k2ge::end_hblank()
{
	if (hblank_pin_w)
		hblank_pin_w(0);
}
