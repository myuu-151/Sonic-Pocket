// spa-recomp: statically recompiles Sonic Pocket Adventure's TLCS-900/H code
// into C++ for the runtime in src/recomp_rt.
//
//   spa-recomp --rom <cart> --functions config/recomp/functions.txt --out <dir>
//              [--listing out/recomp/listing_instructions.txt] [--check-listing]
//
// The ROM is decoded with the interpreter's own opcode tables (decoder.cpp).
// Each routine in the function list becomes one C++ function; code reachable
// from it through jumps is emitted inline with gotos, calls become C++ calls,
// and anything the recompiler cannot follow statically is dispatched at run
// time (falling back to the interpreter for code outside the list).

#include "recomp/decoder.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using recomp::Flow;
using recomp::Instruction;

namespace {

constexpr uint32_t CART_BASE = 0x200000;
constexpr int SHARDS = 32;

struct Function
{
	uint32_t entry = 0;
	std::string name;
	std::map<uint32_t, Instruction> code;
};

std::string hex6(uint32_t value)
{
	char text[16];
	std::snprintf(text, sizeof(text), "%06X", value);
	return text;
}

class Recompiler
{
public:
	explicit Recompiler(std::vector<uint8_t> rom) : m_rom(std::move(rom)) {}

	bool read(uint32_t address, uint8_t &value) const
	{
		if (address < CART_BASE || address >= CART_BASE + m_rom.size())
			return false;
		value = m_rom[address - CART_BASE];
		return true;
	}

	const Instruction *instruction(uint32_t address)
	{
		auto it = m_cache.find(address);
		if (it != m_cache.end())
			return it->second.address == address ? &it->second : nullptr;
		Instruction insn;
		const bool ok = recomp::decode([this](uint32_t a, uint8_t &v) { return read(a, v); }, address, insn);
		if (!ok)
			insn.address = 0xffffffff;
		auto [pos, inserted] = m_cache.emplace(address, std::move(insn));
		return ok ? &pos->second : nullptr;
	}

	bool is_code_address(uint32_t address) const
	{
		return address >= CART_BASE && address < CART_BASE + m_rom.size();
	}

	std::map<uint32_t, std::string> seeds;

	// Explores the routine starting at `entry`. Other routine entries reached
	// by jumps or fall-through end the exploration (they become tail calls);
	// call targets and resume points are reported to `discovered`.
	Function explore(uint32_t entry, std::set<uint32_t> &discovered)
	{
		Function fn;
		fn.entry = entry;
		fn.name = seeds.count(entry) ? seeds[entry] : "sub_" + hex6(entry);
		std::deque<uint32_t> queue{ entry };
		auto internal = [&](uint32_t target) {
			if (!is_code_address(target))
				return;
			if (target != entry && seeds.count(target))
				return;
			queue.push_back(target);
		};
		while (!queue.empty())
		{
			const uint32_t address = queue.front();
			queue.pop_front();
			if (fn.code.count(address))
				continue;
			const Instruction *insn = instruction(address);
			if (!insn)
				continue;
			fn.code.emplace(address, *insn);

			// A constant pointing at a run of unconditional jumps is a jump
			// table (e.g. the animation script opcodes): every entry is a
			// possible indirect jump target.
			if (insn->has_immediate && is_code_address(insn->immediate))
				for (uint32_t entry_address = insn->immediate;;)
				{
					const Instruction *slot = instruction(entry_address);
					if (!slot || slot->flow != Flow::Jump || slot->conditional || !slot->has_static_target)
						break;
					discovered.insert(entry_address);
					entry_address = slot->next;
				}

			if (insn->writes_stack_pointer)
			{
				discovered.insert(insn->next);
				continue;
			}
			switch (insn->flow)
			{
			case Flow::Next:
			case Flow::Invalid:
				internal(insn->next);
				break;
			case Flow::Jump:
				if (insn->has_static_target)
					internal(insn->target);
				if (insn->conditional)
					internal(insn->next);
				break;
			case Flow::Call:
				if (insn->has_static_target && is_code_address(insn->target))
					discovered.insert(insn->target);
				internal(insn->next);
				break;
			case Flow::Return:
				if (insn->conditional)
					internal(insn->next);
				break;
			case Flow::Repeat:
			case Flow::Swi:
				internal(insn->next);
				break;
			case Flow::Djnz:
				internal(insn->target);
				internal(insn->next);
				break;
			case Flow::Halt:
				discovered.insert(insn->next);
				break;
			}
		}
		return fn;
	}

	std::vector<Function> build()
	{
		// Grow the routine set until every call target and resume point is one.
		std::set<uint32_t> pending;
		for (const auto &entry : seeds)
			pending.insert(entry.first);
		std::set<uint32_t> explored;
		while (!pending.empty())
		{
			const uint32_t entry = *pending.begin();
			pending.erase(pending.begin());
			if (!explored.insert(entry).second)
				continue;
			std::set<uint32_t> discovered;
			explore(entry, discovered);
			for (uint32_t address : discovered)
				if (!seeds.count(address) && instruction(address))
				{
					seeds[address] = "sub_" + hex6(address);
					pending.insert(address);
				}
		}
		// Final pass with the complete set, so every routine boundary is known.
		std::vector<Function> functions;
		std::set<uint32_t> ignored;
		for (const auto &entry : seeds)
			if (instruction(entry.first))
				functions.push_back(explore(entry.first, ignored));
		return functions;
	}

private:
	std::vector<uint8_t> m_rom;
	std::map<uint32_t, Instruction> m_cache;
};

std::string function_symbol(uint32_t address) { return "f_" + hex6(address); }

class Emitter
{
public:
	Emitter(const std::map<uint32_t, std::string> &seeds, const std::map<uint32_t, std::string> &listing)
		: m_seeds(seeds), m_listing(listing) {}

	std::string emit(const Function &fn)
	{
		std::ostringstream out;
		m_fn = &fn;
		m_labels.clear();
		collect_labels();

		out << "// " << fn.name << "\n";
		out << "void " << function_symbol(fn.entry) << "(Cpu *c)\n{\n";
		if (fn.code.begin()->first != fn.entry)
			out << "\tgoto L_" << hex6(fn.entry) << ";\n";

		uint32_t expected = 0xffffffff;
		for (const auto &[address, insn] : fn.code)
		{
			if (expected != 0xffffffff && expected != address)
				emit_fallthrough(out, expected);
			if (m_labels.count(address))
				out << "L_" << hex6(address) << ":\n";
			emit_instruction(out, insn);
			expected = falls_through(insn) ? insn.next : 0xffffffff;
		}
		if (expected != 0xffffffff)
			emit_fallthrough(out, expected);
		out << "}\n\n";
		return out.str();
	}

private:
	static bool falls_through(const Instruction &insn)
	{
		if (insn.writes_stack_pointer)
			return false;
		switch (insn.flow)
		{
		case Flow::Jump: return insn.conditional;
		case Flow::Return: return insn.conditional;
		case Flow::Halt: return false;
		default: return true;
		}
	}

	bool internal(uint32_t target) const
	{
		return m_fn->code.count(target) && (target == m_fn->entry || !m_seeds.count(target));
	}

	void collect_labels()
	{
		m_labels.insert(m_fn->entry);
		uint32_t expected = 0xffffffff;
		for (const auto &[address, insn] : m_fn->code)
		{
			if (expected != 0xffffffff && expected != address && internal(expected))
				m_labels.insert(expected);
			if (insn.has_static_target && internal(insn.target))
				m_labels.insert(insn.target);
			if (insn.flow == Flow::Repeat)
				m_labels.insert(address);
			expected = falls_through(insn) ? insn.next : 0xffffffff;
		}
		if (expected != 0xffffffff && internal(expected))
			m_labels.insert(expected);
	}

	// Code for continuing at `target`: a goto inside this routine, a tail call
	// into another routine, or a run-time dispatch.
	std::string transfer(uint32_t target) const
	{
		if (internal(target))
			return "goto L_" + hex6(target) + ";";
		if (m_seeds.count(target))
			return "{ RC_TAIL(" + function_symbol(target) + "); return; }";
		return "{ rt::jump(c); return; }";
	}

	void emit_fallthrough(std::ostringstream &out, uint32_t target)
	{
		if (internal(target))
			out << "\tgoto L_" << hex6(target) << ";\n";
		else if (m_seeds.count(target))
			out << "\tc->m_pc.d = 0x" << hex6(target) << "; RC_TAIL(" << function_symbol(target) << "); return;\n";
		else
			out << "\tc->m_pc.d = 0x" << hex6(target) << "; rt::jump(c); return;\n";
	}

	void emit_instruction(std::ostringstream &out, const Instruction &insn)
	{
		const std::string addr = "0x" + hex6(insn.address);
		const std::string next = "0x" + hex6(insn.next);

		out << "\t// " << hex6(insn.address) << ":";
		for (uint8_t b : insn.bytes)
		{
			char text[4];
			std::snprintf(text, sizeof(text), " %02X", b);
			out << text;
		}
		if (auto it = m_listing.find(insn.address); it != m_listing.end())
			out << "  " << it->second;
		out << "\n";

		out << "\tRC_BEGIN(" << addr << ");\n";
		out << "\tc->m_pc.d = " << next << ";";
		for (const auto &statement : insn.setup)
			out << " " << statement;
		out << "\n\tc->" << insn.handler << "();\n";
		out << "\tRC_END(" << insn.static_cycles << ");\n";

		if (insn.writes_stack_pointer)
		{
			out << "\trt::unwind(c, " << next << ");\n";
			return;
		}

		const std::string taken = insn.conditional ? "if (c->m_pc.d != " + next + ") " : "";
		switch (insn.flow)
		{
		case Flow::Next:
		case Flow::Invalid:
			break;
		case Flow::Jump:
			if (insn.has_static_target)
				out << "\t" << taken << transfer(insn.target) << "\n";
			else
				out << "\t" << taken << "{ rt::jump(c); return; }\n";
			break;
		case Flow::Djnz:
			out << "\tif (c->m_pc.d != " << next << ") " << transfer(insn.target) << "\n";
			break;
		case Flow::Call:
			if (insn.has_static_target && m_seeds.count(insn.target))
				out << "\t" << taken << "{ RC_CALL(" << function_symbol(insn.target) << ", " << next << "); }\n";
			else
				out << "\t" << taken << "{ rt::call(c, " << next << ", c->m_xssp.d + 4); }\n";
			break;
		case Flow::Return:
			out << "\t" << taken << "return;\n";
			break;
		case Flow::Repeat:
			out << "\tif (c->m_pc.d != " << next << ") goto L_" << hex6(insn.address) << ";\n";
			break;
		case Flow::Swi:
			out << "\trt::call(c, " << next << ", c->m_xssp.d + 6);\n";
			break;
		case Flow::Halt:
			out << "\trt::unwind(c, " << next << ");\n";
			break;
		}
	}

	const std::map<uint32_t, std::string> &m_seeds;
	const std::map<uint32_t, std::string> &m_listing;
	const Function *m_fn = nullptr;
	std::set<uint32_t> m_labels;
};

std::vector<uint8_t> read_file(const fs::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::map<uint32_t, std::string> read_seeds(const fs::path &path)
{
	std::map<uint32_t, std::string> seeds;
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream fields(line);
		std::string address, name;
		fields >> address >> name;
		seeds[static_cast<uint32_t>(std::stoul(address, nullptr, 16))] = name;
	}
	return seeds;
}

// listing_instructions.txt: "ADDRESS SIZE text..."
std::map<uint32_t, std::pair<int, std::string>> read_listing(const fs::path &path)
{
	std::map<uint32_t, std::pair<int, std::string>> listing;
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream fields(line);
		std::string address;
		int size = 0;
		fields >> address >> size;
		std::string text;
		std::getline(fields, text);
		if (!text.empty() && text[0] == ' ')
			text.erase(0, 1);
		listing[static_cast<uint32_t>(std::stoul(address, nullptr, 16))] = { size, text };
	}
	return listing;
}

bool write_if_changed(const fs::path &path, const std::string &content)
{
	std::ifstream existing(path, std::ios::binary);
	if (existing)
	{
		std::string old((std::istreambuf_iterator<char>(existing)), {});
		if (old == content)
			return false;
	}
	std::ofstream(path, std::ios::binary) << content;
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	fs::path rom_path, functions_path, out_dir, listing_path;
	bool check_listing = false;
	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (arg == "--rom") rom_path = next();
		else if (arg == "--functions") functions_path = next();
		else if (arg == "--out") out_dir = next();
		else if (arg == "--listing") listing_path = next();
		else if (arg == "--check-listing") check_listing = true;
		else
		{
			std::cerr << "unknown argument " << arg << "\n";
			return 2;
		}
	}
	if (rom_path.empty() || functions_path.empty() || (out_dir.empty() && !check_listing))
	{
		std::cerr << "usage: spa-recomp --rom <cart> --functions <list> --out <dir> [--listing <file>] [--check-listing]\n";
		return 2;
	}

	Recompiler recompiler(read_file(rom_path));
	recompiler.seeds = read_seeds(functions_path);

	std::map<uint32_t, std::pair<int, std::string>> listing;
	if (!listing_path.empty())
		listing = read_listing(listing_path);

	if (check_listing)
	{
		// Every instruction in the disassembly must decode to the same length.
		int bad = 0;
		for (const auto &[address, entry] : listing)
		{
			const Instruction *insn = recompiler.instruction(address);
			const int size = insn ? static_cast<int>(insn->bytes.size()) : 0;
			if (size != entry.first)
			{
				if (++bad <= 20)
					std::printf("%06X: decoded %d bytes (%s), listing %d: %s\n", address, size,
							insn ? insn->handler.c_str() : "-", entry.first, entry.second.c_str());
			}
		}
		std::printf("%zu listing instructions checked, %d length mismatches\n", listing.size(), bad);
		return bad ? 1 : 0;
	}

	std::vector<Function> functions = recompiler.build();
	std::map<uint32_t, std::string> comments;
	for (const auto &[address, entry] : listing)
		comments[address] = entry.second;

	Emitter emitter(recompiler.seeds, comments);
	fs::create_directories(out_dir);

	std::vector<std::string> shard_text(SHARDS);
	size_t total_instructions = 0;
	for (size_t i = 0; i < functions.size(); ++i)
	{
		shard_text[i * SHARDS / functions.size()] += emitter.emit(functions[i]);
		total_instructions += functions[i].code.size();
	}

	std::ostringstream declarations;
	declarations << "// Generated by spa-recomp. Do not edit.\n#pragma once\n\n#include \"recomp_rt/runtime.h\"\n\n";
	for (const auto &fn : functions)
		declarations << "void " << function_symbol(fn.entry) << "(Cpu *c);\n";
	write_if_changed(out_dir / "functions.h", declarations.str());

	for (int shard = 0; shard < SHARDS; ++shard)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "game_%02d.cpp", shard);
		write_if_changed(out_dir / name,
				"// Generated by spa-recomp. Do not edit.\n#include \"functions.h\"\n\n" + shard_text[shard]);
	}

	std::ostringstream table;
	table << "// Generated by spa-recomp. Do not edit.\n#include \"functions.h\"\n\nnamespace rt {\n\n";
	table << "const Entry entries[] = {\n";
	for (const auto &fn : functions)
		table << "\t{ 0x" << hex6(fn.entry) << ", " << function_symbol(fn.entry) << ", \"" << fn.name << "\" },\n";
	table << "};\nconst size_t entry_count = " << functions.size() << ";\n\n} // namespace rt\n";
	write_if_changed(out_dir / "dispatch.cpp", table.str());

	std::printf("recompiled %zu routines, %zu instructions\n", functions.size(), total_instructions);
	return 0;
}
