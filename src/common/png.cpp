#include "common/png.h"

#include <fstream>
#include <vector>

namespace {

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0)
{
	static uint32_t table[256];
	static bool ready = false;
	if (!ready)
	{
		for (uint32_t n = 0; n < 256; ++n)
		{
			uint32_t c = n;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		ready = true;
	}
	crc = ~crc;
	for (size_t i = 0; i < size; ++i)
		crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

void put32(std::vector<uint8_t> &out, uint32_t value)
{
	out.push_back(static_cast<uint8_t>(value >> 24));
	out.push_back(static_cast<uint8_t>(value >> 16));
	out.push_back(static_cast<uint8_t>(value >> 8));
	out.push_back(static_cast<uint8_t>(value));
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
	put32(out, static_cast<uint32_t>(data.size()));
	std::vector<uint8_t> body(type, type + 4);
	body.insert(body.end(), data.begin(), data.end());
	out.insert(out.end(), body.begin(), body.end());
	put32(out, crc32(body.data(), body.size()));
}

} // namespace

bool write_png(const std::filesystem::path &path, const uint32_t *pixels, int width, int height)
{
	std::vector<uint8_t> raw;
	raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
	for (int y = 0; y < height; ++y)
	{
		raw.push_back(0);
		for (int x = 0; x < width; ++x)
		{
			const uint32_t p = pixels[y * width + x];
			raw.push_back(static_cast<uint8_t>(p >> 16));
			raw.push_back(static_cast<uint8_t>(p >> 8));
			raw.push_back(static_cast<uint8_t>(p));
		}
	}

	std::vector<uint8_t> z = { 0x78, 0x01 };
	uint32_t a = 1, b = 0;
	for (uint8_t v : raw)
	{
		a = (a + v) % 65521;
		b = (b + a) % 65521;
	}
	for (size_t offset = 0; offset < raw.size(); offset += 65535)
	{
		const size_t len = std::min<size_t>(65535, raw.size() - offset);
		z.push_back(offset + len == raw.size() ? 1 : 0);
		z.push_back(static_cast<uint8_t>(len));
		z.push_back(static_cast<uint8_t>(len >> 8));
		z.push_back(static_cast<uint8_t>(~len));
		z.push_back(static_cast<uint8_t>(~len >> 8));
		z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + len);
	}
	put32(z, (b << 16) | a);

	std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	std::vector<uint8_t> ihdr;
	put32(ihdr, static_cast<uint32_t>(width));
	put32(ihdr, static_cast<uint32_t>(height));
	ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 });
	chunk(out, "IHDR", ihdr);
	chunk(out, "IDAT", z);
	chunk(out, "IEND", {});

	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
	return static_cast<bool>(file);
}
