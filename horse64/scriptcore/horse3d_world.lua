--[[--
 Functionality to modify the 3D world of the game,
 and populating it with moving interactive objects.
 @module horse3d.world
]]

if horse3d.world == nil then
    horse3d.world = {}
end


--[[--
  Add a new @{horse3d.world.object|world object}
  based on a 3d model or a sprite.
  This can also be used for static surroundings like level geometry.

  The options parameter allows you to specify loader and behavior
  options, e.g.:

    {
        -- Set a behavior, e.g. a player that can walk around:
        behavior=horse3d.behavior.humanoid_player_character(),
        -- Specify keywords that if found in any texture names of the loaded
        -- 3d model will turn all polygons using this textures invisible,
        -- while other polygons will be rendered as usual:
        -- (Useful for use in complex multitextured level models for
        -- hidden walls. The affected geometry will still collide!)
        texture_nodraw_keywords=[],
        -- Specify keywords that if found in any texture names of the loaded
        -- 3d model will turn all polygons using this textures as passable
        -- for collision, while they are still rendered:
        texture_nocollision_keywords=[],
        -- Specify a tag which the created object will be tagged with:
        tag=nil,
    }

  Usage example:

    -- Load model file mybox.obj as new object:
    local myobject = horse3d.world.add_object("mybox.obj")

    -- Place the object slightly ahead of the game world's center:
    myobject:set_position({0, 10, 0})

  @tparam string path_name the disk path to the resource,
  e.g. OBJ model or JPG image
  @tparam table options (optional) a table with special options when
  loading the resource, e.g. a scale for loaded 3d meshes:
  <code>{mesh_scale=1.0}</code>
  @return a reference to the created @{horse3d.world.object|world object}
]]
horse3d.world.add_object = function(path_name, options)
    if type(options) == "table" then
        options = table.copy(options)
    else
        options = {}
    end
    if type(options["tag"]) ~= "string" then
        options["tag"] = ""
    end
    local _invis_keywords = {}
    if type(options["texture_nodraw_keywords"]) == "table" then
        for _, v in ipairs(options["texture_nodraw_keywords"]) do
            table.insert(_invis_keywords, "" .. tostring(v))
        end
    end
    local _nocol_keywords = {}
    if type(options["texture_nocollide_keywords"]) == "table" then
        for _, v in ipairs(options["texture_nocollide_keywords"]) do
            table.insert(_nocol_keywords, "" .. tostring(v))
        end
    end
    local _on_update_func = nil
    if type(options["behavior"]) == "function" then
        _on_update_func = options["behavior"]
    end
    path_name = path_name:replace("\\", "/")
    path_name = path_name:replace("//", "/")
    if path_name:find(":") ~= nil or
            path_name:startswith("/") then
        error("invalid path, must be relative")
    end
    local load_path = path_name
    if game_dir ~= nil then
        load_path = game_dir .. "/" .. load_path
    end
    local o = _world_createmeshobject(
        load_path, _invis_keywords, _nocol_keywords,
        _on_update_func, tag
    )
    return o
end


--[[--
  Get all @{horse3d.world.object|objects} with a specific
  @{horse3d.world.object.add_tag|tag} on them.

  @return a @{table} containing a list of all objects with the given tag
]]
function horse3d.world.get_objects_by_tag(tag)
    if type(tag) ~= "string" or #tag == 0 then
        error("tag must be a non-empty string")
    end
    return _world_getallobjs(tag)
end


--[[--
  Get all @{horse3d.world.object|objects} in the
  entire game world.
  <b>If you have many objects this function will be
  really slow, and produce a result that uses up
  a lot of memory.</b> It is recommended you use
  @{horse3d.world.object.add_tag|tags} instead,
  and then get objects by tag only.

  @return a @{table} containing a list of all objects
]]
function horse3d.world.get_all_objects()
    return _world_getallobjs(nil)
end


--[[--
  Add a new @{horse3d.world.object|world object}
  based on an invisible box. This is useful if you want
  to add a character with no visible body (e.g. for a first person game),
  or an invisible game logic object.

  Make sure to set the object to
  @{horse3d.world.object:disable_collision|disable
  the collision shape entirely}
  for an invisible game logic object that isn't supposed to
  collide with anything.

  Usage example:

    -- Add an invisible first person character for walking around:
    local player = horse3d.world.add_invisible_object({0.7, 1.7}, {
        behavior=horse3d.behavior.humanoid_player_character()
    })

    -- (the player will be 0.7 units wide, and 1.7 units tall)

 @function horse3d.world.add_invisible_object
 @tparam table dimensions (optional) the dimensions of the
 invisible object in width and height, e.g. <code>{1.0, 2.0}</code>.
 If not specified, the size will default to
 <code>{0.4, 1.7}</code>.
 @tparam table options (optional) object loading options, see
 @{horse3d.world.add_object} for available options
 @return a reference to the created @{horse3d.world.object|world object}
]]
horse3d.world.add_invisible_object = function(dimensions, arg2, options)
    local sizes = {0.4, 1.7}
    if type(dimenions) == "table" then
        sizes[1] = tonumber(dimensions[1])
        sizes[2] = tonumber(dimensions[2])
        if type(arg2) == "table" then
            options = arg
        end
    elseif type(dimensions) == "number" and type(arg2) == "number" then
        sizes[1] = tonumber(dimensions)
        sizes[2] = tonumber(arg2)
    end
    local tag = ""
    local _on_update_func = nil
    if type(options) == "table" then
        if type(options["behavior"]) == "function" then
            _on_update_func = options["behavior"]
        end
        if type(options["tag"]) == "string" then
            tag = tostring(options["tag"])
        end
    end
    local o = _world_createinvisobject(
        sizes[1], sizes[2], _on_update_func, tag
    )
    return o
end


--[[--
  Change the gravity affecting all objects that have
  @{horse3d.world.object.enable_movement|movement enabled}.

  @tparam table force the gravity as it applies on all three axis,
  e.g. <code>{0.0, 0.0, -1,0}</code> for downwards force
]]
horse3d.world.set_gravity = function(force)

end


--[[--
  Enable debug wireframe drawings of the collision boxes of
  objects. Useful so you can check if you set up all collision
  as intended!

  @tparam boolean enable whether to enable wireframe debug drawing
]]
horse3d.world.enable_debug_draw_collision = function(enable)
    local boolval = (enable == 1 or enable == true or enable == nil)
    _world_set_drawcollisionboxes(boolval)
end

--[[--
  Function to get object back by its
  @{horse3d.world.object.get_id|numeric id}.

  @tparam number id the object id
  @return the object if it still exists, otherwise nil
]]
horse3d.world.get_object_by_id = function(id)
    return _world_object_byid(id)
end

--[[--
  Add a new @{horse3d.world.light|light source} to the world.
  You may want to @{horse3d.world.light:set_position|set its position}
  afterwards to where you want it to be located.

  @return the newly created @{horse3d.world.light}
]]
horse3d.world.add_light = function()
    return _lights_addnew()
end

return horse3d
