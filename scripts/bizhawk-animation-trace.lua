-- Sonic Pocket Adventure animation/sprite-state tracer for BizHawk 2.10.
-- Load this from Tools > Lua Console after entering a stage.
--
-- This is intentionally wider than bizhawk-player-trace.lua: it records the
-- raw player-task animation bytes and the runtime sprite-object list so the PC
-- viewer can be matched against the ROM instead of guessing animation states.

local PLAYER_TASK = 0x6708
local RAM_BASE = 0x4000

local SPRITE_HEAD = 0x49FA
local NEXT_SPRITE_OBJECT = 0x49F8
local REM_SPRITE_OBJECTS = 0x4CEF
local SPRITE_OBJECT_SIZE = 0x0C
local MAX_SPRITE_OBJECTS = 30

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."

local main_name = mainmemory.getname()
local main_size = mainmemory.getcurrentmemorydomainsize()
local address_bias = main_size > PLAYER_TASK + 0x60 and 0 or -RAM_BASE

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

local function read_u24(address)
    return read_u16(address) | (read_u8(address + 2) << 16)
end

local function read_u32(address)
    return read_u16(address) | (read_u16(address + 2) << 16)
end

local function hex_block(address, length)
    local parts = {}
    for index = 0, length - 1 do
        parts[#parts + 1] = string.format("%02X", read_u8(address + index))
    end
    return table.concat(parts)
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
        local palette_flags = read_u16(address + 0x0A)

        entries[#entries + 1] = string.format(
            "%04X:%04X:%04X:%d:%d:%04X:%04X",
            address,
            next_address,
            priority,
            x,
            y,
            sprite_offset,
            palette_flags
        )

        address = next_address
        count = count + 1
    end

    return count, table.concat(entries, ";")
end

local output_candidates = {}
if repo_root ~= "." then
    table.insert(output_candidates, repo_root .. "\\out\\player-animation-trace.csv")
end

local user_profile = os.getenv("USERPROFILE")
if user_profile then
    table.insert(
        output_candidates,
        user_profile .. "\\Documents\\SonicPocket\\out\\player-animation-trace.csv"
    )
end
table.insert(output_candidates, ".\\player-animation-trace.csv")

local trace = nil
local output_path = nil
local open_errors = {}
for _, candidate in ipairs(output_candidates) do
    local handle, open_error = io.open(candidate, "w")
    if handle then
        trace = handle
        output_path = candidate
        break
    end
    table.insert(open_errors, candidate .. ": " .. tostring(open_error))
end

if not trace then
    error("Could not create the animation trace CSV:\n" .. table.concat(open_errors, "\n"))
end

trace:write(table.concat({
    "frame",
    "state",
    "buttons_current",
    "buttons_pressed",
    "task_flags",
    "movement_flags",
    "surface_angle",
    "x_raw_16_8",
    "x_integer",
    "y_raw_16_8",
    "y_integer",
    "ground_speed_s8_8",
    "x_velocity_s8_8",
    "y_velocity_s8_8",
    "anim_mode_49",
    "anim_word_1e",
    "anim_byte_48",
    "spindash_charge_3c",
    "player_task_6708_0058",
    "next_sprite_object",
    "remaining_sprite_objects",
    "active_sprite_count",
    "active_sprites"
}, ","), "\n")
trace:flush()

console.log(string.format("Animation trace: %s", output_path))
console.log(string.format(
    "Main memory: %s, size 0x%X, absolute-address bias %d",
    main_name,
    main_size,
    address_bias
))
console.log("Watch anim_mode_49: skid should become 0x06 when the ROM selects PAniScr_3988DD.")

while true do
    local frame = emu.framecount()
    local state = read_u32(PLAYER_TASK)
    local buttons_current = read_u8(0x4D40)
    local buttons_pressed = read_u8(0x4D41)
    local task_flags = read_u8(PLAYER_TASK + 0x0B)
    local angle = read_u8(PLAYER_TASK + 0x10)
    local x_raw = read_u24(PLAYER_TASK + 0x11)
    local movement_flags = read_u8(PLAYER_TASK + 0x14)
    local y_raw = read_u24(PLAYER_TASK + 0x15)
    local ground_speed = read_s16(PLAYER_TASK + 0x18)
    local x_velocity = read_s16(PLAYER_TASK + 0x1A)
    local y_velocity = read_s16(PLAYER_TASK + 0x1C)
    local anim_word_1e = read_u16(PLAYER_TASK + 0x1E)
    local spindash_charge = read_u8(PLAYER_TASK + 0x3C)
    local anim_byte_48 = read_u8(PLAYER_TASK + 0x48)
    local anim_mode = read_u8(PLAYER_TASK + 0x49)
    local task_tail = hex_block(PLAYER_TASK, 0x58)
    local sprite_count, active_sprites = sprite_object_line()
    local next_sprite = read_u16(NEXT_SPRITE_OBJECT)
    local remaining_sprites = read_u8(REM_SPRITE_OBJECTS)

    trace:write(string.format(
        "%d,0x%08X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,%d,%d,%d,%d,%d,%d,%d,0x%02X,0x%04X,0x%02X,0x%02X,%s,0x%04X,%d,%d,%s\n",
        frame,
        state,
        buttons_current,
        buttons_pressed,
        task_flags,
        movement_flags,
        angle,
        x_raw,
        x_raw >> 8,
        y_raw,
        y_raw >> 8,
        ground_speed,
        x_velocity,
        y_velocity,
        anim_mode,
        anim_word_1e,
        anim_byte_48,
        spindash_charge,
        task_tail,
        next_sprite,
        remaining_sprites,
        sprite_count,
        active_sprites
    ))
    trace:flush()

    gui.text(2, 2, string.format("ANIM TRACE F:%d", frame), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 14, string.format("STATE:%06X MODE49:%02X W1E:%04X", state, anim_mode, anim_word_1e), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 26, string.format("X:%d Y:%d G:%d VX:%d", x_raw >> 8, y_raw >> 8, ground_speed, x_velocity), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 38, string.format("SPR:%d REM:%d NEXT:%04X", sprite_count, remaining_sprites, next_sprite), 0xFFFFFFFF, 0x80000000)

    emu.frameadvance()
end
