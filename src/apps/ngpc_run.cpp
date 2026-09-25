// Headless runner for the NGPC machine: boots the cartridge, runs frames,
// writes screenshots and compares work RAM with a BizHawk reference capture
// produced by scripts/bizhawk-dump-reference.lua.

#include "common/png.h"
#include "ngpc/machine.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> read_file(const fs::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

// BizHawk input log: "<framecount>\t<Button,Button,...>" per line.
std::map<int, uint8_t> read_input_log(const fs::path &path)
{
	std::map<int, uint8_t> inputs;
	std::ifstream file(path);
	std::string line;
	int index = 0;
	while (std::getline(file, line))
	{
		const auto tab = line.find('\t');
		const std::string names = tab == std::string::npos ? "" : line.substr(tab + 1);
		uint8_t buttons = 0;
		std::stringstream stream(names);
		std::string name;
		while (std::getline(stream, name, ','))
		{
			if (name.find("Up") != std::string::npos) buttons |= ngpc::BUTTON_UP;
			else if (name.find("Down") != std::string::npos) buttons |= ngpc::BUTTON_DOWN;
			else if (name.find("Left") != std::string::npos) buttons |= ngpc::BUTTON_LEFT;
			else if (name.find("Right") != std::string::npos) buttons |= ngpc::BUTTON_RIGHT;
			else if (name.find("Option") != std::string::npos) buttons |= ngpc::BUTTON_OPTION;
			else if (name.size() >= 1 && name.back() == 'A') buttons |= ngpc::BUTTON_A;
			else if (name.size() >= 1 && name.back() == 'B') buttons |= ngpc::BUTTON_B;
		}
		inputs[index++] = buttons;
	}
	return inputs;
}

void usage()
{
	std::cerr <<
		"usage: ngpc-run --rom <file> [--frames N] [--out dir] [--png-every N]\n"
		"                [--png-frames a,b,c] [--reference ram_frames.bin]\n"
		"                [--reference-offset K] [--input input.txt]\n";
}

} // namespace

int main(int argc, char **argv)
{
	fs::path rom_path;
	fs::path out_dir = "out/ngpc-run";
	fs::path reference_path;
	fs::path input_path;
	fs::path dump_ram_path;
	int frames = 600;
	int png_every = 0;
	int reference_offset = 0;
	int64_t trace_count = 0;
	std::set<int> png_frames;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		auto next = [&]() -> std::string {
			if (i + 1 >= argc) { usage(); std::exit(2); }
			return argv[++i];
		};
		if (arg == "--rom") rom_path = next();
		else if (arg == "--frames") frames = std::stoi(next());
		else if (arg == "--out") out_dir = next();
		else if (arg == "--png-every") png_every = std::stoi(next());
		else if (arg == "--png-frames")
		{
			std::stringstream list(next());
			std::string item;
			while (std::getline(list, item, ','))
				png_frames.insert(std::stoi(item));
		}
		else if (arg == "--reference") reference_path = next();
		else if (arg == "--reference-offset") reference_offset = std::stoi(next());
		else if (arg == "--input") input_path = next();
		else if (arg == "--trace") trace_count = std::stoll(next());
		else if (arg == "--dump-ram") dump_ram_path = next();
		else { usage(); return 2; }
	}
	if (rom_path.empty()) { usage(); return 2; }

	ngpc::Machine machine;
	if (const std::string error = machine.load_cartridge(read_file(rom_path)); !error.empty())
	{
		std::cerr << "cannot load " << rom_path << ": " << error << '\n';
		return 1;
	}
	machine.boot();
	machine.trace_instructions(trace_count);
	fs::create_directories(out_dir);

	std::vector<uint8_t> reference;
	if (!reference_path.empty())
		reference = read_file(reference_path);
	const size_t ram_size = 0x3000;
	const int reference_frames = static_cast<int>(reference.size() / ram_size);
	std::map<int, uint8_t> inputs;
	if (!input_path.empty())
		inputs = read_input_log(input_path);

	std::ofstream ram_dump;
	if (!dump_ram_path.empty())
		ram_dump.open(dump_ram_path, std::ios::binary);

	int first_mismatch = -1;
	for (int frame = 0; frame < frames; ++frame)
	{
		if (auto it = inputs.find(frame); it != inputs.end())
			machine.set_buttons(it->second);
		try
		{
			machine.run_frame();
		}
		catch (const std::exception &error)
		{
			std::fprintf(stderr, "stopped in frame %d: %s (PC=%06X)\n", frame, error.what(), machine.cpu().m_pc.d);
			return 1;
		}

		if (ram_dump)
			ram_dump.write(reinterpret_cast<const char *>(machine.work_ram()), 0x3000);

		if ((png_every > 0 && frame % png_every == 0) || png_frames.count(frame))
		{
			char name[64];
			std::snprintf(name, sizeof(name), "frame_%05d.png", frame);
			write_png(out_dir / name, machine.framebuffer(), k2ge::SCREEN_WIDTH, k2ge::SCREEN_HEIGHT);
		}

		const int ref_frame = frame + reference_offset;
		if (first_mismatch < 0 && ref_frame >= 0 && ref_frame < reference_frames)
		{
			const uint8_t *ref = &reference[static_cast<size_t>(ref_frame) * ram_size];
			const uint8_t *ours = machine.work_ram();
			std::vector<int> diffs;
			for (size_t a = 0; a < ram_size; ++a)
				if (ref[a] != ours[a])
					diffs.push_back(static_cast<int>(a));
			if (!diffs.empty())
			{
				first_mismatch = frame;
				std::printf("first RAM mismatch at frame %d (reference frame %d): %zu bytes differ\n",
						frame, ref_frame, diffs.size());
				for (size_t k = 0; k < diffs.size() && k < 48; ++k)
				{
					const int a = diffs[k];
					std::printf("  %04X: ours %02X ref %02X\n", 0x4000 + a, ours[a], ref[a]);
				}
			}
		}
	}

	std::ofstream(out_dir / "ram_final.bin", std::ios::binary)
		.write(reinterpret_cast<const char *>(machine.work_ram()), 0x3000);
	write_png(out_dir / "final.png", machine.framebuffer(), k2ge::SCREEN_WIDTH, k2ge::SCREEN_HEIGHT);
	std::printf("ran %d frames; PC=%06X\n", frames, machine.cpu().m_pc.d);
	if (!reference.empty() && first_mismatch < 0)
		std::printf("RAM matched the reference for all compared frames\n");
	return 0;
}
