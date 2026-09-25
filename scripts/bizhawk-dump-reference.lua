-- Dumps reference state from BizHawk's NGP core for the recomp runtime.
--
-- Writes to out/bizhawk/<run>/:
--   domains.txt            memory domain names and sizes
--   boot_<domain>.bin      every domain at the first frame the script sees
--   (SPR_OUT overrides the output folder)
--   ram_frames.bin         main RAM (0x4000-0x6FFF) after each frame
--   input.txt              the buttons pressed on each frame
--
-- Usage (EmuHawk command line):
--   EmuHawk.exe --lua=scripts\bizhawk-dump-reference.lua <rom>
-- Environment variables SPR_FRAMES (default 1200) and SPR_RUN (default "boot")
-- select the capture length and output folder.

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."

local frames = tonumber(os.getenv("SPR_FRAMES") or "") or 1200
local run = os.getenv("SPR_RUN") or "boot"
local out_dir = os.getenv("SPR_OUT") or (repo_root .. "/out/bizhawk/" .. run)
os.execute('mkdir "' .. out_dir:gsub("/", "\\") .. '" 2>nul')

local function bytes_to_string(values)
    local parts = {}
    for i = 1, #values, 4096 do
        parts[#parts + 1] = string.char(table.unpack(values, i, math.min(i + 4095, #values)))
    end
    return table.concat(parts)
end

local function read_range(domain, offset, length)
    return bytes_to_string(memory.read_bytes_as_array(offset, length, domain))
end

local function dump_domain(domain, path)
    local size = memory.getmemorydomainsize(domain)
    local handle = assert(io.open(path, "wb"))
    for offset = 0, size - 1, 0x10000 do
        handle:write(read_range(domain, offset, math.min(0x10000, size - offset)))
    end
    handle:close()
end

-- BizHawk returns a 0-indexed table here.
local domain_table = memory.getmemorydomainlist()
local domains = {}
for index = 0, #domain_table do
    if domain_table[index] then
        domains[#domains + 1] = domain_table[index]
    end
end
local listing = assert(io.open(out_dir .. "/domains.txt", "w"))
for _, name in ipairs(domains) do
    listing:write(string.format("%s\t%X\n", name, memory.getmemorydomainsize(name)))
end
listing:write(string.format("main\t%s\n", mainmemory.getname()))
listing:close()

for _, name in ipairs(domains) do
    local size = memory.getmemorydomainsize(name)
    if size <= 0x400000 then
        dump_domain(name, out_dir .. "/boot_" .. name:gsub("[^%w]", "_") .. ".bin")
    end
end

-- Main RAM window 0x4000-0x6FFF, located inside the main memory domain.
local main_name = mainmemory.getname()
local main_size = mainmemory.getcurrentmemorydomainsize()
local ram_bias = main_size >= 0x7000 and 0x4000 or 0

local ram_file = assert(io.open(out_dir .. "/ram_frames.bin", "wb"))
local input_file = assert(io.open(out_dir .. "/input.txt", "w"))

for frame = 1, frames do
    emu.frameadvance()
    ram_file:write(read_range(main_name, ram_bias, 0x3000))
    local pressed = {}
    for button, down in pairs(joypad.get()) do
        if down == true then
            pressed[#pressed + 1] = button
        end
    end
    table.sort(pressed)
    input_file:write(string.format("%d\t%s\n", emu.framecount(), table.concat(pressed, ",")))
end

ram_file:close()
input_file:close()
client.exit()
