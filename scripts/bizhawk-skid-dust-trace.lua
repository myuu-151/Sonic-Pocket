-- Sonic Pocket Adventure skid-dust tracer for BizHawk 2.10.
-- Load this from Tools > Lua Console after entering a stage.
--
-- Skid dust uses sprite offsets 0x289E/0x28A4/0x28AA/0x28B0 in
-- out/nsi1/effects/animations.json. This script records only those runtime
-- sprite objects, plus enough player state to align the PC viewer.

local PLAYER_TASK = 0x6708
local RAM_BASE = 0x4000

local SPRITE_HEAD = 0x49FA
local SPRITE_OBJECT_SIZE = 0x0C
local MAX_SPRITE_OBJECTS = 30

local SKID_DUST_SPRITES = {
    [0x289E] = true,
    [0x28A4] = true,
    [0x28AA] = true,
    [0x28B0] = true,
}

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

local function dust_sprite_line()
    local entries = {}
    local seen = {}
    local address = read_u16(SPRITE_HEAD)
    local count = 0

    while address ~= 0xFFFF and count < MAX_SPRITE_OBJECTS do
        if address < SPRITE_HEAD or address >= SPRITE_HEAD + SPRITE_OBJECT_SIZE * (MAX_SPRITE_OBJECTS + 1) then
            break
        end
        if seen[address] then
            break
        end
        seen[address] = true

        local next_address = read_u16(address + 0x00)
        local priority = read_u16(address + 0x02)
        local x = read_s16(address + 0x04)
        local y = read_s16(address + 0x06)
        local sprite_offset = read_u16(address + 0x08)
        local palette_flags = read_u16(address + 0x0A)

        if SKID_DUST_SPRITES[sprite_offset] then
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
        end

        address = next_address
        count = count + 1
    end

    return #entries, table.concat(entries, ";")
end

local output_candidates = {}
if repo_root ~= "." then
    table.insert(output_candidates, repo_root .. "\\out\\skid-dust-trace.csv")
end

local user_profile = os.getenv("USERPROFILE")
if user_profile then
    table.insert(
        output_candidates,
        user_profile .. "\\Documents\\SonicPocket\\out\\skid-dust-trace.csv"
    )
end
table.insert(output_candidates, ".\\skid-dust-trace.csv")

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
    error("Could not create the skid dust trace CSV:\n" .. table.concat(open_errors, "\n"))
end

trace:write(table.concat({
    "frame",
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
    "dust_count",
    "dust_sprites"
}, ","), "\n")
trace:flush()

console.log(string.format("Skid dust trace: %s", output_path))
console.log("Skid hard in the ROM. Stop the script after the smoke chain appears.")

while true do
    local frame = emu.framecount()
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
    local anim_mode = read_u8(PLAYER_TASK + 0x49)
    local dust_count, dust_sprites = dust_sprite_line()

    trace:write(string.format(
        "%d,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,%d,%d,%d,%d,%d,%d,%d,0x%02X,0x%04X,%d,%s\n",
        frame,
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
        dust_count,
        dust_sprites
    ))
    trace:flush()

    gui.text(2, 2, string.format("DUST TRACE F:%d", frame), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 14, string.format("DUST:%d MODE:%02X", dust_count, anim_mode), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 26, string.format("X:%d Y:%d G:%d", x_raw >> 8, y_raw >> 8, ground_speed), 0xFFFFFFFF, 0x80000000)

    emu.frameadvance()
end
