-- Sonic Pocket Adventure player-state tracer for BizHawk 2.10.
-- Load this from Tools > Lua Console after entering a stage.

local PLAYER_TASK = 0x6708
local RAM_BASE = 0x4000

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."

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

local output_candidates = {}
local trace_filename = "player-runtime-trace-" .. os.date("%Y%m%d-%H%M%S") .. ".csv"
if repo_root ~= "." then
    table.insert(output_candidates, repo_root .. "\\out\\" .. trace_filename)
end

-- BizHawk's Lua Console may expose only the script filename through
-- debug.getinfo(), even when its UI knows the full path. Cover the standard
-- project location explicitly without hard-coding a Windows account name.
local user_profile = os.getenv("USERPROFILE")
if user_profile then
    table.insert(
        output_candidates,
        user_profile .. "\\Documents\\SonicPocket\\out\\" .. trace_filename
    )
end
table.insert(output_candidates, ".\\" .. trace_filename)

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
    error("Could not create the trace CSV:\n" .. table.concat(open_errors, "\n"))
end

local output_dir = output_path:match("^(.*[/\\])[^/\\]+$") or ".\\"
local latest_marker = output_dir .. "player-runtime-trace-latest.txt"
local marker = io.open(latest_marker, "w")
if marker then
    marker:write(output_path, "\n")
    marker:close()
end

trace:write(table.concat({
    "frame",
    "state",
    "buttons_current",
    "buttons_pressed",
    "task_flags",
    "movement_flags",
    "surface_angle",
    "collision_plane",
    "x_raw_16_8",
    "x_integer",
    "y_raw_16_8",
    "y_integer",
    "x_word_for_collision",
    "y_word_for_collision",
    "ground_speed_s8_8",
    "x_velocity_s8_8",
    "y_velocity_s8_8",
    "collision_radius_x",
    "collision_radius_y",
    "previous_collision_x_word",
    "previous_collision_y_word",
    "collision_step_x_pixels",
    "collision_step_y_pixels",
    "air_flags_48",
    "air_flags_49",
    "plane2_camera_x",
    "plane2_camera_y",
    "player_screen_x_current",
    "player_screen_y_current",
    "player_screen_x_target",
    "player_screen_y_target",
    "plane2_camera_x_start",
    "plane2_camera_x_end",
    "plane2_camera_y_end",
    "plane2_camera_y_start"
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
    local x_word_for_collision = read_s16(PLAYER_TASK + 0x12)
    local movement_flags = read_u8(PLAYER_TASK + 0x14)
    local y_raw = read_u24(PLAYER_TASK + 0x15)
    local y_word_for_collision = read_s16(PLAYER_TASK + 0x16)
    local ground_speed = read_s16(PLAYER_TASK + 0x18)
    local x_velocity = read_s16(PLAYER_TASK + 0x1A)
    local y_velocity = read_s16(PLAYER_TASK + 0x1C)
    local collision_plane = read_u8(PLAYER_TASK + 0x29)
    local radius_x = read_u8(PLAYER_TASK + 0x36)
    local radius_y = read_u8(PLAYER_TASK + 0x37)
    local previous_collision_x_word = read_s16(PLAYER_TASK + 0x38)
    local previous_collision_y_word = read_s16(PLAYER_TASK + 0x3A)
    local collision_step_x_pixels = math.abs(x_word_for_collision - previous_collision_x_word) + 1
    local collision_step_y_pixels = math.abs(y_word_for_collision - previous_collision_y_word) + 1
    local air_flags_48 = read_u8(PLAYER_TASK + 0x48)
    local air_flags_49 = read_u8(PLAYER_TASK + 0x49)
    local camera_x = read_s16(0x506C)
    local camera_y = read_s16(0x506E)
    local camera_follow_x = read_s16(0x67A4)
    local camera_follow_y = read_s16(0x67A6)
    local camera_follow_x_target = read_s16(0x67B8)
    local camera_follow_y_target = read_s16(0x67BA)
    local camera_min_x = read_s16(0x507A)
    local camera_max_x = read_s16(0x507C)
    local camera_min_y = read_s16(0x507E)
    local camera_max_y = read_s16(0x5080)

    trace:write(string.format(
        "%d,0x%08X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,0x%02X,0x%02X,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        frame,
        state,
        buttons_current,
        buttons_pressed,
        task_flags,
        movement_flags,
        angle,
        collision_plane,
        x_raw,
        x_raw >> 8,
        y_raw,
        y_raw >> 8,
        x_word_for_collision,
        y_word_for_collision,
        ground_speed,
        x_velocity,
        y_velocity,
        radius_x,
        radius_y,
        previous_collision_x_word,
        previous_collision_y_word,
        collision_step_x_pixels,
        collision_step_y_pixels,
        air_flags_48,
        air_flags_49,
        camera_x,
        camera_y,
        camera_follow_x,
        camera_follow_y,
        camera_follow_x_target,
        camera_follow_y_target,
        camera_min_x,
        camera_max_x,
        camera_min_y,
        camera_max_y
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
    gui.text(2, 50, string.format(
        "CAM:%d,%d OFF:%d,%d",
        camera_x,
        camera_y,
        camera_follow_x,
        camera_follow_y
    ), 0xFFFFFFFF, 0x80000000)

    emu.frameadvance()
end
