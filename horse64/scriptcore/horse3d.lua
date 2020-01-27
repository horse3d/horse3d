--[[--
The Horse3D game engine module.
@module horse3d
]]

local lfs = require("lfs")
local _local_vfs_readvfsfile = _vfs_readvfsfile
local horse3d = {}
function dofile_h3dmod(modfile)
    -- Make sure the inner module gets our module table:
    local fpath1 = "horse3d/scriptcore/" .. modfile
    contents = _local_vfs_readvfsfile(fpath1)
    if contents == nil then
        local fpath2 = (
            lfs.datadir() .. "/horse3d/scriptcore/" .. modfile
        )
        local f = io.open(fpath, "rb")
        local contents = ""
        if f then
            contents = f:read("*a")
            f:close()
        else
            error("failed to open module: " .. fpath)
        end
    end
    contents = "local horse3d = __h3dref\n\n" .. contents

    local function upvalue_index(fn, search_name)
        local i = 1
        while true do
            local name, val = debug.getupvalue(fn, i)
            if not name then
                break
            end
            if name == search_name then
                return i
            end
            i = i + 1
        end
    end

    local functionenv = table.copy(_G)
    functionenv["__h3dref"] = horse3d
    local f = load(contents, modfile)
    debug.setupvalue(f, upvalue_index(f, "_ENV"), functionenv)
    horse3d = f()
end
horse3d._game_was_run = false
horse3d.behavior = {}
dofile_h3dmod("horse3d_behavior.lua")
horse3d.camera = {}
dofile_h3dmod("horse3d_camera.lua")
horse3d.graphics = {}
dofile_h3dmod("horse3d_graphics.lua")
horse3d.keypressed = {}
horse3d.world = {}
dofile_h3dmod("horse3d_world.lua")
horse3d.world.object = {}
dofile_h3dmod("horse3d_object.lua")
horse3d.time = {}
dofile_h3dmod("horse3d_time.lua")
horse3d.scene = {}
dofile_h3dmod("horse3d_scene.lua")
horse3d.audio = {}
dofile_h3dmod("horse3d_audio.lua")
horse3d.audio.audiodevice = {}
dofile_h3dmod("horse3d_audiodevice.lua")
horse3d.audio.sound = {}
dofile_h3dmod("horse3d_audiosound.lua")
horse3d.uiplane = {}
dofile_h3dmod("horse3d_uiplane.lua")
horse3d.uiplane.object = {}
dofile_h3dmod("horse3d_uiplaneobject.lua")
horse3d.vfs = {}
dofile_h3dmod("horse3d_vfs.lua")
horse3d.uiplane.plane = {}
dofile_h3dmod("horse3d_uiplaneplane.lua")
horse3d.window = {}

horse3d.window.size = {1, 1}

horse3d.window.set_windowed = function(value)
    _render3d_setmode(value)
end

horse3d.window.get_windowed = function()
    return _render3d_getiswindowed()
end

horse3d._get_events = function(value)
    local events = _render3d_getevents()
    for _, ev in ipairs(events) do
        if ev.type == "window_resize" then
            horse3d.window.size = {
                ev.width, ev.height
            }
        elseif ev.type == "keydown" then
            horse3d.keypressed[ev.key] = ev.key
            if ev.key == "f11" then
                horse3d.window.set_windowed(
                    not horse3d.window.get_windowed()
                )
            end
        elseif ev.type == "keyup" then
            horse3d.keypressed[ev.key] = nil
        elseif ev.type == "mousemove" then
            if type(ev.movement) == "table" then
                horse3d.camera._unprocessed_mousex = (
                    horse3d.camera._unprocessed_mousex + ev.movement[1]
                )
                horse3d.camera._unprocessed_mousey = (
                    horse3d.camera._unprocessed_mousey + ev.movement[2]
                )
            end
        end
    end
    return events
end

_known_textures = {}

horse3d.graphics.get_texture = function(path_name)
    path_name = path_name:replace("\\", "/")
    path_name = path_name:replace("//", "/")
    if path_name:find(":") ~= nil or
            path_name:startswith("/") then
        error("invalid path, must be relative")
    end
    if _known_textures[path_name] ~= nil then
        return _known_textures[path_name]
    end
    local load_path = path_name
    if game_dir ~= nil then
        load_path = game_dir .. "/" .. load_path
    end
    local tex = _render3d_loadnewtexture(load_path)
    if tex ~= nil then
        _known_textures[path_name] = tex
    end
    return tex
end

horse3d.graphics.draw_2dpolygon = function(
        tex, pos1, pos2, pos3,
        texcoord1, texcoord2, texcoord3
        )
    if type(tex) == "string" then
        tex = horse3d.get_texture(tex)
    end
    if type(texcoord1) == "nil" then
        texcoord1 = {0, 0}
    end
    if type(texcoord2) == "nil" then
        texcoord2 = {1, 0}
    end
    if type(texcoord3) == "nil" then
        texcoord3 = {1, 1}
    end
    _render3d_rendertriangle2d(
        tex, pos1[1], pos1[2],
        pos2[1], pos2[2],
        pos3[1], pos3[2],
        texcoord1[1], texcoord1[2],
        texcoord2[1], texcoord2[2],
        texcoord3[1], texcoord3[2]
    )
end

horse3d.graphics.draw_polygon = function(
        tex, pos1, pos2, pos3,
        texcoord1, texcoord2, texcoord3,
        unlit, transparent, additive)
    if type(tex) == "string" then
        tex = horse3d.get_texture(tex)
    end
    if type(unlit) ~= "boolean" then
        unlit = true
    end
    if type(transparent) ~= "boolean" then
        transparent = false
    end
    if type(additive) ~= "boolean" then
        additive = false
    end
    if type(texcoord1) == "nil" then
        texcoord1 = {0, 0}
    end
    if type(texcoord2) == "nil" then
        texcoord2 = {1, 0}
    end
    if type(texcoord3) == "nil" then
        texcoord3 = {1, 1}
    end
    _render3d_rendertriangle3d(
        tex, pos1[1], pos1[2], pos1[3],
        pos2[1], pos2[2], pos2[3],
        pos3[1], pos3[2], pos3[3],
        texcoord1[1], texcoord1[2],
        texcoord2[1], texcoord2[2],
        texcoord3[1], texcoord3[2],
        unlit, transparent, additive
    )
end

--[[--
  Set a global callback to catch errors.
  The callback will be passed the error and traceback as a string
  as first argument, and you may log the error in some way,
  @{horse3d.prompt_error_message|prompt the user,} or
  @{horse3d.exit_app|exit the application,} or whatever you desire.
  <b>If you don't return <code>"ignore"</code>, then Horse3D will
  still display the error to the user and quit.</b>

  Be careful setting this to a callback that returns
  <code>"ignore"</code> without a good logging mechanism, or you
  may miss out on important errors!

  @function horse3d.set_error_callback
  @tparam function callback set the function to be called with the error
]]
horse3d.set_error_callback = function(callback)
    if type(callback) ~= "function" then
        error("supplied argument must be a function")
    end
    horse3d._on_error = callback
end

--[[--
  Prompt a modal error message to the user in a popup message box.
  <b>Will minimize the output when in fullscreen and freeze your app
  to let the user see it, so this function is very disruptive.</b>
  This function call will block until the user has closed the
  error popup again.

  @function horse3d.prompt_error_message
  @tparam string msg the error message to show
  @tparam string title (optional) the title bar text of the message box
]]
horse3d.prompt_error_message = function(msg, title)
    _graphics_prompterror(msg, title)
end

--[[--
  Terminate the entire application.
  This function will not return, it just terminates Horse3D & your
  application instantly.

  @function horse3d.exit_app
]]
horse3d.exit_app = function()
    _graphics_exit(0)
end

function horse3d._doframe()
    horse3d._game_was_run = true
    local function handle_error(errmsg)
        local err_func = horse3d._on_error
        if type(err_func) == "function" then
            local success = true
            local err = nil
            success, err = pcall(function()
                local result = err_func(errmsg)
                if result ~= "ignore" then
                    error("unhandled by callback: " .. errmsg)
                end
            end)
            if success then
                return true
            end
        end
        return false
    end
    if horse3d._logic_ts == nil then
        horse3d._logic_ts = horse3d.time.gametime()
    end
    local ts_dt = (1.0/40.0)
    local events = horse3d._get_events()
    for _, ev in ipairs(events) do
        local success = true
        local err = nil
        local event_func = _G["on_event"]
        if event_func ~= nil then
            success, err = pcall(function()
                event_func(table.copy(ev))
            end)
        end
        if not success then
            if not handle_error(err) then
                print("horse3d: error: UNHANDLED " ..
                      "ERROR: " .. tostring(err))
                horse3d.prompt_error_message(
                    "Unhandled script error: " .. err,
                    "Horse3D SCRIPT ERROR"
                )
                return false
            end
        else
            if ev.type == "quit" and
                    (type(err) ~= "boolean" or
                     err ~= false) then
                return false
            end
        end
    end
    horse3d.camera._was_moved_this_frame = false
    _world_update()

    local now = horse3d.time.gametime()
    if horse3d._logic_ts + 300 < now then
        horse3d._logic_ts = now - ts_dt * 0.9
    end
    local any_logic_updates_done = false
    local tick_count = 0
    while now > horse3d._logic_ts and
            tick_count < 10 do
        -- Call the update function and update camera:
        local update_func = _G["on_update"]
        if update_func ~= nil then
            update_func(ts_dt)
        end
        horse3d.camera._update_flycam(ts_dt)
        tick_count = tick_count + 1
        horse3d._logic_ts = horse3d._logic_ts + ts_dt

        -- Call any horse3d.time.schedule'd things:
        local scheduledfuncs = _time_getscheduledfuncstable()
        local funccount = #scheduledfuncs
        local i = 1
        while i <= funccount do
            local time = scheduledfuncs[i][1]
            local scheduledfunc = scheduledfuncs[i][2]
            if time < now then
                success, err = pcall(function()
                    scheduledfunc()
                end)
                if not success then
                    if not handle_error(err) then
                        print("horse3d: error: UNHANDLED " ..
                              "ERROR: " .. tostring(err))
                        horse3d.prompt_error_message(
                            "Unhandled script error: " .. err,
                            "Horse3D SCRIPT ERROR"
                        )
                        return false
                    end
                end
                table.remove(scheduledfuncs, i)
                funccount = funccount - 1
            else
                i = i + 1
            end
        end

        any_logic_updates_done = true
    end
    local success, err = pcall(function()
        _world_draw()
        local extra_draw = _G["on_draw"]
        if extra_draw ~= nil then
            extra_draw()
        end
    end)
    _render3d_endframe()
    if not success then
        if not handle_error(err) then
            print("horse3d: error: UNHANDLED " ..
                  "ERROR: " .. tostring(err))
            horse3d.prompt_error_message(
                "Unhandled script error: " .. err,
                "Horse3D SCRIPT ERROR"
            )
            return false
        end
    end
    if not any_logic_updates_done then
        -- We're rendering too fast, slow down a little
        _time_sleepms(ts_dt * 1000)
    end
    return true
end

horse3d._run = function()
    horse3d._game_was_run = true
    while true do
        if not horse3d._doframe() then
            return
        end
    end
end

return horse3d
