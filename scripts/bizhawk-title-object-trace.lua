-- Sonic Pocket Adventure title object/sprite tracer for BizHawk 2.10.
--
-- Load this from Tools > Lua Console during the title sequence.  It records
-- the title runtime's object list and the post-CopySprites sprite tables so the
-- PC title extractor can be matched from ROM evidence instead of by eye.

local RAM_BASE = 0x4000

local SPRITE_HEAD = 0x49FA
local NEXT_SPRITE_OBJECT = 0x49F8
local SPRITE_OBJECT_SIZE = 0x0C
local MAX_SPRITE_OBJECTS = 30

local SPRITE_RAM = 0x4B6E
local SPRITE_TILE_IDS = 0x4C6E
local SPRITE_TILE_COUNT = 0x4CEE
local REM_SPRITE_OBJECTS = 0x4CEF
local SPRITE_PAL_IDS = 0x4CF0
local NEXT_SPRITE_PAL_ID = 0x4D30

local VDPSPR_SCROLL_X = 0x8020
local VDPSPR_SCROLL_Y = 0x8021

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."

local main_name = mainmemory.getname()
local main_size = mainmemory.getcurrentmemorydomainsize()
local address_bias = main_size > SPRITE_PAL_IDS + 0x40 and 0 or -RAM_BASE

local function read_u8(address)
    local offset = address + address_bias
    if offset < 0 or offset >= main_size then
        error(string.format(
            "Address %06X is outside main-memory domain %s (size %X, bias %d)",
            address,
            main_name,
            main_size,
            address_bias
        ))
    end
    return mainmemory.read_u8(offset)
end

local function read_u16(address)
    return read_u8(address) | (read_u8(address + 1) << 8)
end

local function read_s16(address)
    local value = read_u16(address)
    return value >= 0x8000 and value - 0x10000 or value
end

local function hex_block(address, length)
    local parts = {}
    for index = 0, length - 1 do
        parts[#parts + 1] = string.format("%02X", read_u8(address + index))
    end
    return table.concat(parts)
end

local function ensure_dir(path)
    if package.config:sub(1, 1) == "\\" then
        os.execute('mkdir "' .. path .. '" >nul 2>nul')
    else
        os.execute('mkdir -p "' .. path .. '"')
    end
end

local function output_root_path()
    if repo_root ~= "." then
        return repo_root .. "\\out"
    end
    local user_profile = os.getenv("USERPROFILE")
    if user_profile then
        return user_profile .. "\\Documents\\SonicPocket\\out"
    end
    return "."
end

local function sprite_object_line()
    local entries = {}
    local seen = {}
    local address = read_u16(SPRITE_HEAD)
    local count = 0

    while address ~= 0xFFFF and count < MAX_SPRITE_OBJECTS do
        if address < SPRITE_HEAD or address >= SPRITE_HEAD + SPRITE_OBJECT_SIZE * (MAX_SPRITE_OBJECTS + 1) then
            entries[#entries + 1] = string.format("bad:%04X", address)
            break
        end
        if seen[address] then
            entries[#entries + 1] = string.format("loop:%04X", address)
            break
        end
        seen[address] = true

        local next_address = read_u16(address + 0x00)
        local priority = read_u16(address + 0x02)
        local x = read_s16(address + 0x04)
        local y = read_s16(address + 0x06)
        local sprite_offset = read_u16(address + 0x08)
        local flags_palette = read_u16(address + 0x0A)

        entries[#entries + 1] = string.format(
            "%04X:%04X:%04X:%d:%d:%04X:%04X",
            address,
            next_address,
            priority,
            x,
            y,
            sprite_offset,
            flags_palette
        )

        address = next_address
        count = count + 1
    end

    return count, table.concat(entries, ";")
end

local function copied_sprite_line(sprite_count)
    local entries = {}
    local count = math.min(sprite_count, 64)
    for index = 0, count - 1 do
        local sprite_base = SPRITE_RAM + index * 4
        local tile_id_flags = read_u16(sprite_base + 0)
        local xy = read_u16(sprite_base + 2)
        local y = xy & 0x00FF
        local x = (xy >> 8) & 0x00FF
        local source_tile = read_u16(SPRITE_TILE_IDS + index * 2)
        local palette = read_u8(SPRITE_PAL_IDS + index)
        entries[#entries + 1] = string.format(
            "%02d:%04X:%02X:%02X:%04X:%02X",
            index,
            tile_id_flags,
            x,
            y,
            source_tile,
            palette
        )
    end
    return table.concat(entries, ";")
end

local output_root = output_root_path()
ensure_dir(output_root)
local trace_path = output_root .. "\\title-object-trace-" .. os.date("%Y%m%d-%H%M%S") .. ".csv"
local trace = assert(io.open(trace_path, "w"))

local latest = io.open(output_root .. "\\title-object-trace-latest.txt", "w")
if latest then
    latest:write(trace_path, "\n")
    latest:close()
end

trace:write(table.concat({
    "trace_frame",
    "emu_frame",
    "vdp_sprite_scroll_x",
    "vdp_sprite_scroll_y",
    "next_sprite_object",
    "remaining_sprite_objects",
    "sprite_tile_count",
    "next_sprite_pal_id",
    "active_object_count",
    "active_objects",
    "copied_sprites",
    "sprite_ram_raw",
    "sprite_tile_ids_raw",
    "sprite_pal_ids_raw"
}, ","), "\n")
trace:flush()

console.log("Title object trace: " .. trace_path)
console.log(string.format(
    "Main memory: %s, size 0x%X, absolute-address bias %d",
    main_name,
    main_size,
    address_bias
))

local max_frames = 1800
local trace_frame = 0
while trace_frame < max_frames do
    local emu_frame = emu.framecount()
    local sprite_count = read_u8(SPRITE_TILE_COUNT)
    local active_object_count, active_objects = sprite_object_line()
    local copied_sprites = copied_sprite_line(sprite_count)

    trace:write(string.format(
        "%d,%d,0x%02X,0x%02X,0x%04X,%d,%d,0x%04X,%d,%s,%s,%s,%s,%s\n",
        trace_frame,
        emu_frame,
        read_u8(VDPSPR_SCROLL_X),
        read_u8(VDPSPR_SCROLL_Y),
        read_u16(NEXT_SPRITE_OBJECT),
        read_u8(REM_SPRITE_OBJECTS),
        sprite_count,
        read_u16(NEXT_SPRITE_PAL_ID),
        active_object_count,
        active_objects,
        copied_sprites,
        hex_block(SPRITE_RAM, 0x100),
        hex_block(SPRITE_TILE_IDS, 0x80),
        hex_block(SPRITE_PAL_IDS, 0x40)
    ))

    if trace_frame % 30 == 0 then
        trace:flush()
    end
    gui.text(2, 2, string.format("TITLE OBJ F:%d", trace_frame), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 14, string.format("OBJ:%d SPR:%d", active_object_count, sprite_count), 0xFFFFFFFF, 0x80000000)
    trace_frame = trace_frame + 1
    emu.frameadvance()
end

trace:flush()
trace:close()
console.log("Title object trace complete: " .. trace_path)
