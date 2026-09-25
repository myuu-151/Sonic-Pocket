#include "recomp/decoder.h"

#include "cpu/tlcs900/tlcs900.h"

#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>

namespace recomp {

namespace {

// Operand kinds, identical to e_operand in 900tbl.hxx.
enum
{
	p_A = 1, p_C8, p_C16, p_C32, p_MC16, p_CC, p_CR8, p_CR16, p_CR32, p_D8, p_D16,
	p_F, p_I3, p_I8, p_I16, p_I24, p_I32, p_M, p_M8, p_M16, p_R, p_SR
};

// TLCS-900/H extra cycles for addressing modes (tlcs900h_device overrides).
constexpr int MEM_INDEX_CYCLES = 1;
constexpr int MEM_ABS8_CYCLES = 1;
constexpr int MEM_ABS16_CYCLES = 2;
constexpr int MEM_ABS24_CYCLES = 3;
constexpr int MEM_GPR_INDIRECT_CYCLES = 1;
constexpr int MEM_GPR_INDEX_CYCLES = 3;
constexpr int MEM_GPR_REG_INDEX_CYCLES = 3;
constexpr int MEM_PREPOST_CYCLES = 1;

using Handler = void (tlcs900_device::*)();
using Table = const tlcs900_device::tlcs900inst *;

const std::string &handler_name(Handler handler)
{
	static const std::vector<std::pair<Handler, std::string>> names = {
#define TLCS900_OP(name) { &tlcs900_device::name, #name },
#include "cpu/tlcs900/tlcs900_ops.inc"
#undef TLCS900_OP
	};
	for (const auto &entry : names)
		if (entry.first == handler)
			return entry.second;
	throw std::runtime_error("unknown TLCS-900 handler in opcode table");
}

std::string hex(uint32_t value)
{
	char text[16];
	std::snprintf(text, sizeof(text), "0x%X", value);
	return text;
}

struct State
{
	const ByteReader &read;
	uint32_t pc;
	Instruction &insn;
	bool ea2_static = false;
	uint32_t ea2 = 0;
	bool ea1_static = false;
	uint32_t ea1 = 0;
	uint32_t imm1 = 0;
	bool p2_is_sp = false;     // m_p2_reg32 refers to XSP

	uint8_t byte()
	{
		uint8_t value;
		if (!read(pc, value))
			throw std::out_of_range("instruction runs into unreadable memory");
		insn.bytes.push_back(value);
		++pc;
		return value;
	}
	uint32_t word() { uint32_t lo = byte(); return lo | (byte() << 8); }
	uint32_t word24() { uint32_t lo = word(); return lo | (byte() << 16); }
	uint32_t word32() { uint32_t lo = word(); return lo | (word() << 16); }

	void emit(std::string statement) { insn.setup.push_back(std::move(statement)); }
};

std::string s8(uint8_t value) { return "(uint32_t)(int32_t)(int8_t)" + hex(value); }
std::string s16(uint32_t value) { return "(uint32_t)(int32_t)(int16_t)" + hex(value & 0xffff); }

// Mirrors the memory-operand prefixes op_C0/op_D0/op_E0/op_F0.
void decode_memory_prefix(State &s, uint8_t prefix, bool word_source)
{
	switch (prefix & 0x07)
	{
	case 0x00:
		s.ea2 = s.byte();
		s.ea2_static = true;
		s.insn.static_cycles += MEM_ABS8_CYCLES;
		break;
	case 0x01:
		s.ea2 = s.word();
		s.ea2_static = true;
		s.insn.static_cycles += MEM_ABS16_CYCLES;
		break;
	case 0x02:
		s.ea2 = s.word24();
		s.ea2_static = true;
		s.insn.static_cycles += MEM_ABS24_CYCLES;
		break;
	case 0x03:
	{
		const uint8_t mode = s.byte();
		switch (mode & 0x03)
		{
		case 0x00:
			s.emit("c->m_ea2.d = *c->get_reg32(" + hex(mode) + ");");
			s.insn.static_cycles += MEM_GPR_INDIRECT_CYCLES;
			break;
		case 0x01:
		{
			const uint32_t d = s.word();
			s.emit("c->m_ea2.d = *c->get_reg32(" + hex(mode) + ") + " + s16(d) + ";");
			s.insn.static_cycles += MEM_GPR_INDEX_CYCLES;
			break;
		}
		case 0x02:
			break;  // undefined: the interpreter leaves m_ea2 unchanged
		case 0x03:
			switch (mode)
			{
			case 0x03:
			{
				const uint8_t base = s.byte();
				const uint8_t index = s.byte();
				s.emit("c->m_ea2.d = *c->get_reg32(" + hex(base) + ") + (uint32_t)(int32_t)(int8_t)*c->get_reg8(" + hex(index) + ");");
				s.insn.static_cycles += MEM_GPR_REG_INDEX_CYCLES;
				break;
			}
			case 0x07:
			{
				const uint8_t base = s.byte();
				const uint8_t index = s.byte();
				s.emit("c->m_ea2.d = *c->get_reg32(" + hex(base) + ") + (uint32_t)(int32_t)(int16_t)*c->get_reg16(" + hex(index) + ");");
				s.insn.static_cycles += MEM_GPR_REG_INDEX_CYCLES;
				break;
			}
			case 0x13:
			{
				const uint32_t d = s.word();
				s.ea2 = s.pc + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d)));
				s.ea2_static = true;
				s.insn.static_cycles += word_source ? MEM_GPR_REG_INDEX_CYCLES : MEM_GPR_INDEX_CYCLES;
				break;
			}
			}
			break;
		}
		break;
	}
	case 0x04:
	{
		const uint8_t reg = s.byte();
		s.emit("{ uint32_t *r = c->get_reg32(" + hex(reg) + "); *r -= " + std::to_string(1 << (reg & 3)) + "; c->m_ea2.d = *r; }");
		s.insn.static_cycles += MEM_PREPOST_CYCLES;
		break;
	}
	case 0x05:
	{
		const uint8_t reg = s.byte();
		s.emit("{ uint32_t *r = c->get_reg32(" + hex(reg) + "); c->m_ea2.d = *r; *r += " + std::to_string(1 << (reg & 3)) + "; }");
		s.insn.static_cycles += MEM_PREPOST_CYCLES;
		break;
	}
	}
	if (s.ea2_static)
		s.emit("c->m_ea2.d = " + hex(s.ea2) + ";");
}

// Mirrors prepare_operands() for one instruction table entry.
void prepare_operands(State &s, const tlcs900_device::tlcs900inst &inst, uint8_t op)
{
	switch (inst.operand1)
	{
	case p_A: s.emit("c->m_p1_reg8 = &c->m_xwa[c->m_regbank].b.l;"); break;
	case p_F: s.emit("c->m_p1_reg8 = &c->m_sr.b.l;"); break;
	case p_SR: s.emit("c->m_p1_reg16 = &c->m_sr.w.l;"); break;
	case p_C8: s.emit("c->m_p1_reg8 = c->get_reg8_current(" + hex(op) + ");"); break;
	case p_C16: s.emit("c->m_p1_reg16 = c->get_reg16_current(" + hex(op) + ");"); break;
	case p_MC16: s.emit("c->m_p1_reg16 = c->get_reg16_current(" + hex((op >> 1) & 0x03) + ");"); break;
	case p_C32:
		s.emit("c->m_p1_reg32 = c->get_reg32_current(" + hex(op) + ");");
		if ((op & 7) == 7)
			s.insn.writes_stack_pointer = true;  // refined by the caller per handler
		break;
	case p_CR8: { const uint8_t code = s.byte(); s.imm1 = code; s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p1_reg8 = c->control_reg8(" + hex(code) + ");"); break; }
	case p_CR16: { const uint8_t code = s.byte(); s.imm1 = code; s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p1_reg16 = c->control_reg16(" + hex(code) + ");"); break; }
	case p_CR32: { const uint8_t code = s.byte(); s.imm1 = code; s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p1_reg32 = c->control_reg32(" + hex(code) + ");"); break; }
	case p_D8:
	{
		const uint8_t d = s.byte();
		s.ea1 = s.pc + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(d)));
		s.ea1_static = true;
		s.emit("c->m_ea1.d = " + hex(s.ea1) + ";");
		break;
	}
	case p_D16:
	{
		const uint32_t d = s.word();
		s.ea1 = s.pc + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d)));
		s.ea1_static = true;
		s.emit("c->m_ea1.d = " + hex(s.ea1) + ";");
		break;
	}
	case p_I3: s.imm1 = op & 0x07; s.emit("c->m_imm1.d = " + hex(s.imm1) + ";"); break;
	case p_I8: s.imm1 = s.byte(); s.emit("c->m_imm1.d = " + hex(s.imm1) + ";"); break;
	case p_I16: s.imm1 = s.word(); s.emit("c->m_imm1.d = " + hex(s.imm1) + ";"); break;
	case p_I24: s.imm1 = s.word24(); s.insn.has_immediate = true; s.insn.immediate = s.imm1; s.emit("c->m_imm1.d = " + hex(s.imm1) + ";"); break;
	case p_I32: s.imm1 = s.word32(); s.insn.has_immediate = true; s.insn.immediate = s.imm1; s.emit("c->m_imm1.d = " + hex(s.imm1) + ";"); break;
	case p_M:
		s.ea1 = s.ea2;
		s.ea1_static = s.ea2_static;
		s.emit("c->m_ea1.d = c->m_ea2.d;");
		break;
	case p_M8: s.ea1 = s.byte(); s.ea1_static = true; s.emit("c->m_ea1.d = " + hex(s.ea1) + ";"); break;
	case p_M16: s.ea1 = s.word(); s.ea1_static = true; s.emit("c->m_ea1.d = " + hex(s.ea1) + ";"); break;
	case p_R:
		s.emit("c->m_p1_reg8 = c->m_p2_reg8; c->m_p1_reg16 = c->m_p2_reg16; c->m_p1_reg32 = c->m_p2_reg32;");
		if (s.p2_is_sp)
			s.insn.writes_stack_pointer = true;
		break;
	default:
		break;
	}

	switch (inst.operand2)
	{
	case p_A: s.emit("c->m_p2_reg8 = &c->m_xwa[c->m_regbank].b.l;"); break;
	case p_F: s.emit("c->m_p2_reg8 = &c->m_f2.b.l;"); break;
	case p_SR: s.emit("c->m_p2_reg16 = &c->m_sr.w.l;"); break;
	case p_C8: s.emit("c->m_p2_reg8 = c->get_reg8_current(" + hex(op) + ");"); break;
	case p_C16: s.emit("c->m_p2_reg16 = c->get_reg16_current(" + hex(op) + ");"); break;
	case p_C32: s.emit("c->m_p2_reg32 = c->get_reg32_current(" + hex(op) + ");"); break;
	case p_CR8: { const uint8_t code = s.byte(); s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p2_reg8 = c->control_reg8(" + hex(code) + ");"); break; }
	case p_CR16: { const uint8_t code = s.byte(); s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p2_reg16 = c->control_reg16(" + hex(code) + ");"); break; }
	case p_CR32: { const uint8_t code = s.byte(); s.emit("c->m_imm1.d = " + hex(code) + "; c->m_p2_reg32 = c->control_reg32(" + hex(code) + ");"); break; }
	case p_D8:
	{
		const uint8_t d = s.byte();
		s.ea2 = s.pc + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(d)));
		s.ea2_static = true;
		s.emit("c->m_ea2.d = " + hex(s.ea2) + ";");
		break;
	}
	case p_D16:
	{
		const uint32_t d = s.word();
		s.ea2 = s.pc + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(d)));
		s.ea2_static = true;
		s.emit("c->m_ea2.d = " + hex(s.ea2) + ";");
		break;
	}
	case p_I3: s.emit("c->m_imm2.d = " + hex(op & 0x07) + ";"); break;
	case p_I8: s.emit("c->m_imm2.d = " + hex(s.byte()) + ";"); break;
	case p_I16: s.emit("c->m_imm2.d = " + hex(s.word()) + ";"); break;
	case p_I32:
	{
		const uint32_t value = s.word32();
		s.insn.has_immediate = true;
		s.insn.immediate = value;
		s.emit("c->m_imm2.d = " + hex(value) + ";");
		break;
	}
	case p_M8: s.ea2 = s.byte(); s.ea2_static = true; s.emit("c->m_ea2.d = " + hex(s.ea2) + ";"); break;
	case p_M16: s.ea2 = s.word(); s.ea2_static = true; s.emit("c->m_ea2.d = " + hex(s.ea2) + ";"); break;
	default:
		break;
	}
}

void classify(State &s, uint8_t op)
{
	Instruction &insn = s.insn;
	const std::string &h = insn.handler;
	const int cond = op & 0x0f;

	if (h == "op_JR" || h == "op_JRL")
	{
		insn.flow = Flow::Jump;
		insn.conditional = cond != 8;
		insn.has_static_target = true;
		insn.target = s.ea2;
		if (cond == 0)
			insn.flow = Flow::Next;  // JR F: never taken
	}
	else if (h == "op_JPI")
	{
		insn.flow = Flow::Jump;
		insn.has_static_target = true;
		insn.target = s.imm1 & 0xffffff;
	}
	else if (h == "op_JPM")
	{
		insn.flow = Flow::Jump;
		insn.conditional = cond != 8;
		insn.has_static_target = s.ea2_static;
		insn.target = s.ea2 & 0xffffff;
		if (cond == 0)
			insn.flow = Flow::Next;
	}
	else if (h == "op_CALLI")
	{
		insn.flow = Flow::Call;
		insn.has_static_target = true;
		insn.target = s.imm1 & 0xffffff;
	}
	else if (h == "op_CALLM")
	{
		insn.flow = Flow::Call;
		insn.conditional = cond != 8;
		insn.has_static_target = s.ea2_static;
		insn.target = s.ea2 & 0xffffff;
		if (cond == 0)
			insn.flow = Flow::Next;
	}
	else if (h == "op_CALR")
	{
		insn.flow = Flow::Call;
		insn.has_static_target = true;
		insn.target = s.ea1 & 0xffffff;
	}
	else if (h == "op_RET" || h == "op_RETD" || h == "op_RETI")
	{
		insn.flow = Flow::Return;
	}
	else if (h == "op_RETCC")
	{
		insn.flow = Flow::Return;
		insn.conditional = cond != 8;
		insn.is_retcc = true;
		if (cond == 0)
			insn.flow = Flow::Next;
	}
	else if (h == "op_DJNZB" || h == "op_DJNZW")
	{
		insn.flow = Flow::Djnz;
		insn.conditional = true;
		insn.has_static_target = true;
		insn.target = s.ea2;
	}
	else if (h == "op_LDIR" || h == "op_LDDR" || h == "op_CPIR" || h == "op_CPDR" ||
			h == "op_LDIRW" || h == "op_LDDRW" || h == "op_CPIRW" || h == "op_CPDRW")
	{
		insn.flow = Flow::Repeat;
	}
	else if (h == "op_SWI" || h == "op_SWI900")
	{
		insn.flow = Flow::Swi;
	}
	else if (h == "op_HALT")
	{
		insn.flow = Flow::Halt;
	}
	else if (h == "op_DB")
	{
		insn.flow = Flow::Invalid;
	}

	// Only plain loads into XSP abandon the current call structure.
	if (insn.writes_stack_pointer &&
			!(h == "op_LDLRI" || h == "op_LDLRM" || h == "op_LDLRR" || h == "op_LDAL"))
		insn.writes_stack_pointer = false;
}

} // namespace


bool decode(const ByteReader &read, uint32_t address, Instruction &out)
{
	out = Instruction{};
	out.address = address;
	State s{ read, address, out };

	try
	{
		uint8_t op = s.byte();
		const tlcs900_device::tlcs900inst *inst = &tlcs900h_device::s_mnemonic[op];
		const std::string &top = handler_name(inst->opfunc);
		Table table = nullptr;

		if (top == "op_80" || top == "op_88" || top == "op_90")
		{
			s.emit("c->m_p1_reg32 = c->get_reg32_current(" + hex(static_cast<uint8_t>(op - 1)) + ");");
			s.emit("c->m_p2_reg32 = c->get_reg32_current(" + hex(op) + ");");
			s.p2_is_sp = (op & 7) == 7;
			if (top == "op_88")
			{
				const uint8_t d = s.byte();
				s.emit("c->m_ea2.d = *c->get_reg32_current(" + hex(op) + ") + " + s8(d) + ";");
				s.insn.static_cycles += MEM_INDEX_CYCLES;
			}
			else
				s.emit("c->m_ea2.d = *c->get_reg32_current(" + hex(op) + ");");
			table = top == "op_90" ? tlcs900h_device::s_mnemonic_90 : tlcs900h_device::s_mnemonic_80;
		}
		else if (top == "op_98" || top == "op_A0" || top == "op_A8" || top == "op_B0" || top == "op_B8")
		{
			if (top == "op_98" || top == "op_A8" || top == "op_B8")
			{
				const uint8_t d = s.byte();
				s.emit("c->m_ea2.d = *c->get_reg32_current(" + hex(op) + ") + " + s8(d) + ";");
				s.insn.static_cycles += MEM_INDEX_CYCLES;
			}
			else
				s.emit("c->m_ea2.d = *c->get_reg32_current(" + hex(op) + ");");
			if (top == "op_98") table = tlcs900h_device::s_mnemonic_98;
			else if (top == "op_A0" || top == "op_A8") table = tlcs900h_device::s_mnemonic_a0;
			else if (top == "op_B0") table = tlcs900h_device::s_mnemonic_b0;
			else table = tlcs900h_device::s_mnemonic_b8;
		}
		else if (top == "op_C0" || top == "op_D0" || top == "op_E0" || top == "op_F0")
		{
			decode_memory_prefix(s, op, top == "op_D0");
			if (top == "op_C0") table = tlcs900h_device::s_mnemonic_c0;
			else if (top == "op_D0") table = tlcs900h_device::s_mnemonic_d0;
			else if (top == "op_E0") table = tlcs900h_device::s_mnemonic_e0;
			else table = tlcs900h_device::s_mnemonic_f0;
		}
		else if (top == "oC8")
		{
			if (op & 0x08)
			{
				s.emit("c->m_p2_reg8 = c->get_reg8_current(" + hex(op) + ");");
				s.emit("c->m_p2_reg16 = c->get_reg16_current(" + hex((op >> 1) & 0x03) + ");");
			}
			else
			{
				const uint8_t code = s.byte();
				s.emit("c->m_p2_reg8 = c->get_reg8(" + hex(code) + ");");
				s.emit("c->m_p2_reg16 = c->get_reg16(" + hex(code) + ");");
			}
			table = tlcs900h_device::s_mnemonic_c8;
		}
		else if (top == "oD8")
		{
			if (op & 0x08)
			{
				s.emit("c->m_p2_reg16 = c->get_reg16_current(" + hex(op) + ");");
				s.emit("c->m_p2_reg32 = c->get_reg32_current(" + hex(op) + ");");
				s.p2_is_sp = (op & 7) == 7;
			}
			else
			{
				const uint8_t code = s.byte();
				s.emit("c->m_p2_reg16 = c->get_reg16(" + hex(code) + ");");
				s.emit("c->m_p2_reg32 = c->get_reg32(" + hex(code) + ");");
				s.p2_is_sp = (code & 0xfc) == 0xfc;
			}
			table = tlcs900h_device::s_mnemonic_d8;
		}
		else if (top == "op_E8")
		{
			if (op & 0x08)
			{
				s.emit("c->m_p2_reg32 = c->get_reg32_current(" + hex(op) + ");");
				s.p2_is_sp = (op & 7) == 7;
			}
			else
			{
				const uint8_t code = s.byte();
				s.emit("c->m_p2_reg32 = c->get_reg32(" + hex(code) + ");");
				s.p2_is_sp = (code & 0xfc) == 0xfc;
			}
			table = tlcs900h_device::s_mnemonic_e8;
		}

		if (table)
		{
			op = s.byte();
			inst = &table[op];
		}

		out.handler = handler_name(inst->opfunc);
		out.static_cycles += inst->cycles;
		prepare_operands(s, *inst, op);
		s.emit("c->m_op = " + hex(op) + ";");
		classify(s, op);
	}
	catch (const std::out_of_range &)
	{
		return false;
	}

	out.next = s.pc;
	return true;
}

} // namespace recomp
