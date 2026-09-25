#include "recomp_rt/runtime.h"

#include <algorithm>

namespace rt {

namespace {

// Deep C++ recursion (long chains of tail jumps) is cut off by unwinding;
// the emulated stack holds everything needed to resume.
constexpr int MAX_DEPTH = 2000;

thread_local int t_depth = 0;
Stats t_stats;  // one recompiled game per process

void interpret_until(Cpu *c, uint32_t ret, uint32_t sp)
{
	c->m_prefetch_clear = true;
	while (!(c->m_pc.d == ret && c->m_xssp.d == sp))
	{
		// The frame being waited for was abandoned (stack reloaded or
		// unwound past it): let rt::run() take over from here.
		if (c->m_xssp.d > sp)
			throw Unwind{};
		c->step();
		++t_stats.interpreted_steps;
	}
}

} // namespace


Stats &stats() { return t_stats; }


Fn lookup(uint32_t address)
{
	const Entry *end = entries + entry_count;
	const Entry *it = std::lower_bound(entries, end, address,
			[](const Entry &e, uint32_t a) { return e.address < a; });
	return (it != end && it->address == address) ? it->fn : nullptr;
}


const char *name_of(uint32_t address)
{
	const Entry *end = entries + entry_count;
	const Entry *it = std::lower_bound(entries, end, address,
			[](const Entry &e, uint32_t a) { return e.address < a; });
	return (it != end && it->address == address) ? it->name : nullptr;
}


void unwind(Cpu *c, uint32_t pc)
{
	c->m_pc.d = pc;
	++t_stats.unwinds;
	throw Unwind{};
}


void enter(Cpu *c)
{
	if (++t_depth > MAX_DEPTH)
	{
		--t_depth;
		++t_stats.unwinds;
		(void)c;
		throw Unwind{};
	}
	++t_stats.recompiled_calls;
}


void leave()
{
	--t_depth;
}


void check_return(Cpu *c, uint32_t ret, uint32_t sp)
{
	if (c->m_pc.d != ret || c->m_xssp.d != sp)
	{
		++t_stats.unwinds;
		throw Unwind{};
	}
}


void call(Cpu *c, uint32_t ret, uint32_t sp)
{
	if (Fn fn = lookup(c->m_pc.d))
	{
		enter(c);
		fn(c);
		leave();
		check_return(c, ret, sp);
		return;
	}
	++t_stats.dispatch_misses;
	++t_stats.miss_addresses[c->m_pc.d];
	interpret_until(c, ret, sp);
}


void jump(Cpu *c)
{
	if (Fn fn = lookup(c->m_pc.d))
	{
		enter(c);
		fn(c);
		leave();
		return;
	}
	++t_stats.dispatch_misses;
	++t_stats.miss_addresses[c->m_pc.d];
	throw Unwind{};
}


void interrupt(Cpu *c, uint32_t resume)
{
	// Acceptance pushed PC and SR (6 bytes); the handler's RETI pops them.
	call(c, resume, c->m_xssp.d + 6);
}


void run(Cpu *c)
{
	for (;;)
	{
		try
		{
			for (;;)
			{
				t_depth = 0;
				if (Fn fn = lookup(c->m_pc.d))
				{
					fn(c);
					continue;  // the routine returned: continue at the popped PC
				}
				c->m_prefetch_clear = true;
				c->step();
				++t_stats.interpreted_steps;
			}
		}
		catch (const Unwind &)
		{
		}
	}
}

} // namespace rt
