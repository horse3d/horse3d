
function main()
    -- Make some core lua functions VFS capable:
    local _local_vfs_lua_loadfile = _vfs_lua_loadfile
    local _oldloadfile = loadfile
    loadfile = function(arg1, arg2)
        local result1, result2 = _local_vfs_lua_loadfile(arg1)
        if result1 ~= nil or result2 ~= nil then
            return result1, result2
        end
        return _oldloadfile(arg1, arg2)
    end
    dofile = function(arg1)
        local f = loadfile(arg1)
        if not f then
            error("failed to load script: " .. tostring(arg1))
        end
        f()
    end
    local _local_vfs_readvfsfile = _vfs_readvfsfile
    local _oldioopen = io.open
    io.open = function(arg1, arg2)
        local contents = _local_vfs_readvfsfile(arg1)
        if contents ~= nil then
            local tmeta = {
                read = function(self, amount)
                    if amount == "*a" then
                        local result = self.contents:sub(
                            self.offset
                        )
                        return result
                    else
                        local readamount = (
                            tonumber(amount)
                        )
                        if readamount <= 0 then
                            return ""
                        end
                        local result = self.contents:sub(
                            self.offset,
                            self.offset + readamount - 1
                        )
                        self.offset = self.offset + readamount
                        return result
                    end
                end,
                close = function()
                end,
            }
            local t = {
                offset=1,
                contents=contents,
            }
            setmetatable(t, tmeta)
            return t
        end
        return _oldioopen(arg1, arg2)
    end

    -- Load in super vital core modules:
    local _preloaditem = function(item)
        local f = loadfile(item)
        if not f then
            f = loadfile(_lfs_datadir() .. "/" .. item)
        end
        return f()
    end
    local lfs = _preloaditem("horse3d/scriptcore/lfs.lua")
    _preloaditem("horse3d/scriptcore/table.lua")
    _preloaditem("horse3d/scriptcore/string.lua")
    _preloaditem("horse3d/scriptcore/math.lua")

    -- Load in all .h3dpak files in our current directory
    -- (which likely belong to the game) - but don't reload coreapi.h3dpak
    for f in lfs.dir(".") do
        if f:endswith(".h3dpak") and
                f:lower() ~= "coreapi.h3dpak" then
            _vfs_adddatapak(f)
        end
    end

    -- Load up main.lua of game and try to run it:
    local game_main, errmsg = loadfile("main.lua")
    if game_main == nil then
        error("failed to load main.lua: " .. tostring(errmsg))
    end
    local horse3d = require("horse3d")
    local result = game_main()
    if type(_G["on_init"]) == "function" then
        result = on_init()
    end
    if type(horse3d._game_was_run) == "boolean" and
            horse3d._game_was_run == false then
        horse3d._run()
    end
    return result
end

return main()
