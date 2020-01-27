--[[--
  The <code>horse3d.uiplane.object</code> type represents
  a 2d object on a @{horse3d.uiplane.plane|UI plane}.

  By default there is one UI plane overlaying the screen, but
  you can add more planes that are also placed in the world e.g.
  to represent virtual computer screens.

  To add an object call @{horse3d.uiplane.add_object}.

  <b>Important:</b> ui objects will stick around until
  @{horse3d.uiplane.object.destroy|destroyed}, even if you let go off all
  references. Use @{horse3d.uiplane.get_all_objects} or similar to list
  all existing objects.

  @classmod horse3d.uiplane.object
]]

if horse3d.uiplane == nil then
    horse3d.uiplane = {}
end

if horse3d.uiplane.object == nil then
    horse3d.uiplane.object = {}
end


horse3d.uiplane.object._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.uiplane.object[key]
    end
end
local reg = debug.getregistry()
reg.horse3d_uiplane_object_set_metatableref = (
    horse3d.uiplane.object._set_metatableref
)
reg = nil

--[[--
  Destroy the object, and all its children if any.

  @function horse3d.uiplane.object:destroy
]]
horse3d.uiplane.object.destroy = function(self)
    _uiobject_destroy(self)
end

--[[--
  Move this object to the given position on the
  @{horse3d.uiplane.plane|plane} it is on.

  @function horse3d.uiplane.object:set_position
  @tparam table position the position as two numbers along
  each axis, like <code>{0, 0}</code> for the top left
]]
horse3d.uiplane.object.set_position = function(self, position, arg2)
    if type(position) == "number" and
            type(arg2) == "number" then
        position = {position, arg2}
    end
    _uiobject_setposition(self, position)
end

--[[--
  Return this object's position on the
  @{horse3d.uiplane.plane|plane} it is on.

  @function horse3d.uiplane.object:get_position
  @return the table with the two position values, e.g. `{0, 0}`
]]
horse3d.uiplane.object.get_position = function(self)
    return _uiobject_getposition(self)
end

--[[--
  For @{horse3d.uiplane.add_text_object|text objects,} set a new font
  to use. You may specify a path to a .ttf file, or otherwise
  use built-in "Serif, "Sans Serif", or "Monospace" fonts by specifying
  these names, respectively.

  @function horse3d.uiplane.object:set_font
  @tparam string path_or_name the path to the TTF file, or name of the font
  ]]
horse3d.uiplane.object.set_font = function(self, path_or_name)
    if type(path_or_name) ~= "string" then
        error("font must be specified as string")
    end
    _uiobject_setnewfont(self, path_or_name)
end

--[[--
  (Advanced/internal) Get the internal id of this object.
  Mainly useful for debugging purposes, not for much else.

  @function horse3d.uiplane.object:get_id
  @return the number representing the internal id
]]
horse3d.uiplane.object.get_id = function(self)
    return _uiobject_getobjectid(self)
end

--[[--
  For @{horse3d.uiplane.add_text_object|text objects,} set a new font
  size to use. Specified in point size, corresponding to pixels unless
  you scale the ui plane it is on.

  @function horse3d.uiplane.object:set_font_size
  @tparam number size the new point size
  ]]
horse3d.uiplane.object.set_font_size = function(self, size)
    if type(size) ~= "number" then
        error("point size must be number")
    end
    _uiobject_setnewptsize(self, size)
end

--[[--
  Add another object as a child to this one. This is useful
  if you want to organize things in more complex user interfaces.

  If the other object already had a parent, it will
  automatically be @{horse3d.uiplane.object.remove_from_parent|removed}
  first before added as child to this new one.

  @function horse3d.uiplane.object:add_child
  @tparam horse3d.uiplane.object child_object the object to be added as child
  ]]
horse3d.uiplane.object.add_child = function(self, child_object)
    return _uiobject_addchild(self, child_object)
end

--[[--
  Remove this object from a parent which
  @{horse3d.uiplane.object.add_child|it was made a child of}, leaving
  it without any parent.

  @function horse3d.uiplane.object.remove_from_parent
  ]]
horse3d.uiplane.object.remove_from_parent = function(self)
    _uiobject_removefromparent(self)
end

return horse3d
