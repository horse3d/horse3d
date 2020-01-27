--[[--
  The <code>horse3d.uiplane.plane</code> type represents
  a 2d plane representing a virtual screen for the UI.
  You only need to bother with this directly if you want
  multiple UI planes, to e.g. place them in the level for
  ingame screens.

  To get the default plane, call @{horse3d.uiplane.get_default_plane}.

  To add a new one, use @{horse3d.uiplane.add_new_plane}.

  @classmod horse3d.uiplane.plane
]]

if horse3d.uiplane == nil then
    horse3d.uiplane = {}
end

if horse3d.uiplane.plane == nil then
    horse3d.uiplane.plane = {}
end


horse3d.uiplane.plane._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.uiplane.plane[key]
    end
end
local reg = debug.getregistry()
reg.horse3d_uiplane_plane_set_metatableref = (
    horse3d.uiplane.plane._set_metatableref
)
reg = nil


--[[--
  Destroy the given UI plane.
  This will also destroy all @{horse3d.uiplane.object|objects}
  currently on this plane.

  @function horse3d.uiplane.plane:destroy
]]
horse3d.uiplane.plane.destroy = function(self)
    _uiplane_destroy(self)
end

--[[--
  Resize the UI plane to the given size.

  @function horse3d.uiplane.plane:resize
  @tparam table size the new size as table of two numbers, e.g. `{50, 50}`
  ]]
horse3d.uiplane.plane.resize = function(self, size, arg2)
    if (type(size) == "number" and type(arg2) == "number") then
        size = {size, arg2}
    end
    _uiplane_resize(self, size[1], size[2])
end

--[[--
  Set the UI plane's render offset to the top-left corner, given
  it's attached to the screen. (It is ignored for 3d-placed UI planes.)
  This can be used to move it with all its contents around on the screen.

  @function horse3d.uiplane.plane:set_offset
  @tparam table offset the new offset as table of two
  numbers, e.g. `{50, 50}`
  ]]
horse3d.uiplane.plane.set_offset = function(self, offset, arg2)
    if (type(offset) == "number" and type(arg2) == "number") then
        offset = {offset, arg2}
    end
    _uiplane_setoffset(self, offset[1], offset[2])
end

--[[--
  Get the UI plane's @{horse3d.uiplane.plane.set_offset|render offset}.

  @function horse3d.uiplane.plane:get_offset
  @return return table with the current offset, e.g. `{0, 0}`
  ]]
horse3d.uiplane.plane.get_offset = function(self)
    return _uiplane_getoffset(self)
end

--[[--
  (Advanced/internal) Get the internal id of this plane.
  Mainly useful for debugging purposes, not for much else.

  @function horse3d.uiplane.plane:get_id
  @return the number representing the internal id
]]
horse3d.uiplane.plane.get_id = function(self)
    return _uiplane_getplaneid(self)
end

return horse3d
