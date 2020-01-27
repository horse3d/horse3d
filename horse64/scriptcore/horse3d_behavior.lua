--[[--
  A collection of default @{horse3d.world.object|object} behaviors
  to quickly fill your game with advanced moving characters and interaction.
  These behaviors are polished enough for use in a final product,
  but their use is completely optional - you can write your own
  behaviors instead if you want to.

  To add in a player, use this:
  <code style="white-space:pre">
    local obj = horse3d.world.add_invisible_object({
        behavior=horse3d.behavior.humanoid_player_character()
    })
    obj:set_position({0, 5, 0})  -- warp to desired starting point
  </code>

  To write a completely custom behavior instead, please note
  a behavior is just an update function that modifies the object
  each frame. Check @{02_The_Game_World.md|"The Game World"} for
  more info.

  @module horse3d.behavior
]]

if horse3d.behavior == nil then
    horse3d.behavior = {}
end


--[[--
  An object behavior to provide a humanoid player character.
  Use this with an invisible object to easily add in a first
  person character like this:

  <code style="white-space:pre">
  local obj = horse3d.world.add_invisible_object({
      0.7, -- player width
      1.7, -- player height
      behavior=horse3d.behavior.humanoid_player()
  })
  </code>

  @tparam table options (optional) the options for the
      character controller,
      see above for a full list.
      Example value: <code>{walking_speed=1.0}</code>

  @return a behavior function that can be used for
          an @{horse3d.world.object|object}
]]
horse3d.behavior.humanoid_player = function(options)
    if options == nil then
        options = {}
    end
    local update_func_opts = table.copy(options)
    update_func_opts["move_camera"] = true
    update_func_opts["player_input"] = true
    return horse3d.behavior.humanoid_character(update_func_opts)
end


horse3d.behavior.humanoid_character = function(options)
    local update_func_opts = {
        walking_speed=1.0
    }
    if type(options["player_input"]) == "boolean" then
        update_func_opts["player_input"] = options["player_input"]
    end
    if type(options["move_camera"]) == "boolean" then
        update_func_opts["move_camera"] = options["move_camera"]
    end
    if type(options) == "table" then
        if type(options["walking_speed"]) == "number" then
            update_func_opts["walking_speed"] = tonumber(
                options["walking_speed"]
            )
        end
        if type(options["input_leftright"]) == "function" then
            update_func_opts["input_leftright"] = (
                options["input_leftright"]
            )
        end
        if type(options["input_forwardbackward"]) == "function" then
            update_func_opts["input_forwardbackward"] = (
                options["input_forwardbackward"]
            )
        end
    end
    if update_func_opts["input_leftright"] == nil and
            update_func_opts["player_input"] then
        update_func_opts["input_leftright"] = function()
            if horse3d.keypressed["a"] ~= nil or
                    horse3d.keypressed["left"] ~= nil then
                return -1.0
            elseif horse3d.keypressed["d"] ~= nil or
                    horse3d.keypressed["right"] ~= nil then
                return 1.0
            end
            return 0
        end
    end
    if update_func_opts["input_forwardbackward"] == nil and
            update_func_opts["player_input"] then
        update_func_opts["input_forwardbackward"] = function()
            if horse3d.keypressed["s"] ~= nil or
                    horse3d.keypressed["down"] ~= nil then
                return -1.0
            elseif horse3d.keypressed["w"] ~= nil or
                    horse3d.keypressed["up"] ~= nil then
                return 1.0
            end
            return 0
        end
    end
    local update_func = function(self, dt)
        self:enable_movement("character")
        local forces_scale = self:get_character_forces_scaling()

        local sidemove = 0.0
        if type(update_func_opts["input_leftright"]) == "function" then
            sidemove = math.max(-1, math.min(1.0, tonumber(
                update_func_opts["input_leftright"]()
            )))
        end
        local forwardmove = 0.0
        if type(update_func_opts["input_forwardbackward"])
                == "function" then
            forwardmove = math.max(-1, math.min(1.0, tonumber(
                update_func_opts["input_forwardbackward"]()
            )))
        end
        if not self:get_character_is_on_floor() or
                self:get_character_is_on_strong_slope() then
            sidemove = 0
            forwardmove = 0
        end
        local v = {
            sidemove, forwardmove, 0.0
        }
        if forwardmove ~= 0.0 or sidemove ~= 0.0 then
            v = math.normalize_vec(v)
            v = math.quat_rotate_vec(self:get_rotation_quat(), v)
            local f = 0.8 * update_func_opts["walking_speed"]
            local mass = self:get_mass()
            v[1] = v[1] * dt * f * forces_scale
            v[2] = v[2] * dt * f * forces_scale
            v[3] = v[3] * dt * f * forces_scale
            self:accelerate(v)
        end

        if update_func_opts["move_camera"] then
            horse3d.camera._flycam_enabled = false
            local pos = self:get_position()
            local dimensions = self:get_dimensions()
            pos[3] = pos[3] + 0.42 * dimensions[3]
            horse3d.camera.set_position(pos)
            horse3d.camera._flycam_horirot = (
                horse3d.camera._flycam_horirot -
                horse3d.camera._unprocessed_mousex * 150
            )
            horse3d.camera._flycam_vertirot = (
                horse3d.camera._flycam_vertirot -
                horse3d.camera._unprocessed_mousey * 150
            )
            horse3d.camera._unprocessed_mousex = 0
            horse3d.camera._unprocessed_mousey = 0
            local rot = math.quat_from_euler_angles(
                horse3d.camera._flycam_horirot,
                horse3d.camera._flycam_vertirot,
                horse3d.camera._flycam_rollrot
            )
            local rot2 = math.quat_from_euler_angles(
                horse3d.camera._flycam_horirot, 0, 0
            )
            _camera_setrotation(rot[1], rot[2], rot[3], rot[4])
            self:set_rotation_quat(
                rot2[1], rot2[2], rot2[3], rot2[4]
            )
        end
    end
    return update_func
end

return horse3d
