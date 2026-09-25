// Static TLCS-900/H instruction decoder for the recompiler.
//
// Decoding mirrors the interpreter exactly: it walks the same MAME opcode
// tables and follows the same prefix and operand rules (op_80..op_F0 and
// prepare_operands in 900tbl.hxx). Instead of computing values it records
// C++ statements that perform the interpreter's operand setup at run time,
// so a recompiled instruction is the interpreter's handler with its decode
// work done ahead of time.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace recomp {

enum class Flow
{
	Next,          // falls through
	Jump,          // JP/JR/JRL/JPM (possibly conditional)
	Call,          // CALL/CALR (possibly conditional)
	Return,        // RET/RETD/RETI, RETcc
	Repeat,        // LDIR/LDDR/CPIR/CPDR family: may re-run itself
	Djnz,          // DJNZ
	Swi,           // SWI: enters the BIOS
	Halt,          // HALT
	Invalid,       // undefined opcode
};

struct Instruction
{
	uint32_t address = 0;
	uint32_t next = 0;             // address of the following instruction
	std::vector<uint8_t> bytes;
	std::string handler;           // interpreter handler, e.g. "op_ADDBRM"
	std::vector<std::string> setup;// statements run before the handler
	int static_cycles = 0;         // cycles the interpreter adds outside the handler
	Flow flow = Flow::Next;
	bool conditional = false;      // condition code other than "always"
	bool has_static_target = false;
	uint32_t target = 0;           // jump/call target when static
	bool writes_stack_pointer = false; // e.g. LD XSP, (mem)
	bool is_retcc = false;
	bool has_immediate = false;    // 24/32-bit immediate operand (possible pointer)
	uint32_t immediate = 0;
};

// Reads ROM/RAM bytes for decoding. Returns false outside readable memory.
using ByteReader = std::function<bool(uint32_t address, uint8_t &value)>;

// Decodes the instruction at `address`. Returns false for unreadable bytes.
bool decode(const ByteReader &read, uint32_t address, Instruction &out);

} // namespace recomp
