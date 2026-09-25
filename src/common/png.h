#pragma once

#include <cstdint>
#include <filesystem>

// Writes 0xAARRGGBB pixels as an uncompressed (stored-deflate) RGB PNG.
bool write_png(const std::filesystem::path &path, const uint32_t *pixels, int width, int height);
