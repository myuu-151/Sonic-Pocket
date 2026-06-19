-- Sonic Pocket Adventure player-state tracer for BizHawk 2.10.
-- Load this from Tools > Lua Console after entering a stage.

local PLAYER_TASK = 0x6708
local RAM_BASE = 0x4000

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."
local output_path = repo_root .. "\\out\\player-runtime-trace.csv"

local main_name = mainmemory.getname()
local main_size = mainmemory.getcurrentmemorydomainsize()
local address_bias = main_size > PLAYER_TASK + 0x50 and 0 or -RAM_BASE

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

local trace, open_error = io.open(output_path, "w")
if not trace then
    error("Could not open " .. output_path .. ": " .. tostring(open_error))
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
    "collision_radius_x",
    "collision_radius_y"
}, ","), "\n")
trace:flush()

console.log(string.format("Player trace: %s", output_path))
console.log(string.format(
    "Main memory: %s, size 0x%X, absolute-address bias %d",
    main_name,
    main_size,
    address_bias
))

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
    local radius_x = read_u8(PLAYER_TASK + 0x36)
    local radius_y = read_u8(PLAYER_TASK + 0x37)

    trace:write(string.format(
        "%d,0x%08X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
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
        radius_x,
        radius_y
    ))
    trace:flush()

    gui.text(2, 2, string.format("TRACE F:%d", frame), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 14, string.format("STATE:%06X", state), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 26, string.format(
        "X:%d Y:%d A:%02X",
        x_raw >> 8,
        y_raw >> 8,
        angle
    ), 0xFFFFFFFF, 0x80000000)
    gui.text(2, 38, string.format(
        "G:%d VX:%d VY:%d",
        ground_speed,
        x_velocity,
        y_velocity
    ), 0xFFFFFFFF, 0x80000000)

    emu.frameadvance()
end
