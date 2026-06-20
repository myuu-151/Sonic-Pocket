-- Sonic Pocket Adventure title-screen frame capture for BizHawk 2.10.
--
-- Load this from Tools > Lua Console before, during, or after the title intro.
-- It writes native title screenshots plus a small frame/input CSV so the PC
-- title renderer can be compared against the ROM instead of judged by eye.

local source = debug.getinfo(1, "S").source
local script_path = source:sub(1, 1) == "@" and source:sub(2) or source
local repo_root = script_path:match("^(.*)[/\\]scripts[/\\][^/\\]+$") or "."

local function ensure_dir(path)
    if package.config:sub(1, 1) == "\\" then
        os.execute('mkdir "' .. path .. '" >nul 2>nul')
    else
        os.execute('mkdir -p "' .. path .. '"')
    end
end

local output_root = nil
if repo_root ~= "." then
    output_root = repo_root .. "\\out\\title-teacher"
else
    local user_profile = os.getenv("USERPROFILE")
    if user_profile then
        output_root = user_profile .. "\\Documents\\SonicPocket\\out\\title-teacher"
    else
        output_root = ".\\title-teacher"
    end
end

ensure_dir(output_root)
local capture_dir = output_root .. "\\" .. os.date("%Y%m%d-%H%M%S")
ensure_dir(capture_dir)

local latest = io.open(output_root .. "\\latest.txt", "w")
if latest then
    latest:write(capture_dir, "\n")
    latest:close()
end

local trace_path = capture_dir .. "\\trace.csv"
local trace = assert(io.open(trace_path, "w"))
trace:write("frame,emu_frame,a,b,start,select,up,down,left,right,screenshot\n")

local function button_state()
    local ok, buttons = pcall(joypad.get, 1)
    if not ok or type(buttons) ~= "table" then
        return {}
    end
    return buttons
end

local function bool01(value)
    return value and "1" or "0"
end

local function screenshot(path)
    if client and client.screenshot then
        local ok = pcall(client.screenshot, path)
        if ok then
            return true
        end
    end
    if gui and gui.savescreenshot then
        local ok = pcall(gui.savescreenshot, path)
        if ok then
            return true
        end
    end
    return false
end

local max_frames = 1800 -- 30 seconds at 60 Hz; stop earlier from Lua console if needed.
local frame = 0
console.log("Title capture writing to: " .. capture_dir)

while frame < max_frames do
    local filename = string.format("frame_%05d.png", frame)
    local path = capture_dir .. "\\" .. filename
    local saved = screenshot(path)
    local buttons = button_state()
    trace:write(table.concat({
        tostring(frame),
        tostring(emu.framecount()),
        bool01(buttons.A),
        bool01(buttons.B),
        bool01(buttons.Start),
        bool01(buttons.Select),
        bool01(buttons.Up),
        bool01(buttons.Down),
        bool01(buttons.Left),
        bool01(buttons.Right),
        saved and filename or "",
    }, ","), "\n")
    if frame % 30 == 0 then
        trace:flush()
    end
    frame = frame + 1
    emu.frameadvance()
end

trace:flush()
trace:close()
console.log("Title capture complete: " .. capture_dir)
