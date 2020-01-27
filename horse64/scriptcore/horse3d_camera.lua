--[[--
Functionality to control the camera view into the 3d world.
@module horse3d.camera
]]

if horse3d.camera == nil then
    horse3d.camera = {}
end


horse3d.camera._was_moved_this_frame = false
horse3d.camera._unprocessed_mousex = 0
horse3d.camera._unprocessed_mousey = 0
horse3d.camera._flycam_horirot = 0
horse3d.camera._flycam_vertirot = 0
horse3d.camera._flycam_rollrot = 0
horse3d.camera._flycam_enabled = true

--[[--
  Enable the built-in free flight camera for looking around
  with WSAD and mouse.
  When launching a horse3d, it is enabled by default.
  Calling <code>enable_flycam</code> will detach the camera
  if it was @{horse3d.camera.attach_to|attached to any object}.

  Usage example:

  <code style="white-space:pre">
    horse3d.camera.enable_flycam()
  </code>

  @function horse3d.camera.enable_flycam
]]
horse3d.camera.enable_flycam = function()
    horse3d.camera._unprocessed_mousex = 0
    horse3d.camera._unprocessed_mousey = 0
    horse3d.camera._flycam_enabled = true
end


--[[--
  Disable the built-in free flight camera for looking around.
  Once it is disabled the camera will remain static unless you
  attach it to an object, move it manually, or
  re-enable the flycam with @{horse3d.camera.enable_flycam}.
]]
horse3d.camera.disable_flycam = function()
    horse3d.camera._flycam_enabled = false
end


--[[--
  Warp the camera to the given 3d world position.

  @function horse3d.camera.set_position
  @tparam table pos the target position, e.g. <code>{0, 0, 0}</code>
]]
horse3d.camera.set_position = function(pos, arg2, arg3)
    if type(arg2) == "number" and type(arg3) == "number" then
        pos = {pos, arg2, arg3}
    end
    _camera_setposition(pos[1], pos[2], pos[3])
    horse3d.camera._was_moved_this_frame = true
end


--[[--
  Return the camera's current position in the 3d world.

  Usage example:

  <code style="white-space:pre">
    local pos = horse3d.camera.get_position()
    print(
        "Camera is at x: " .. p[1] ..
        ", y: " .. p[2] ..
        ", z: " .. p[3]
    )
  </code>

  @return the position as a table with 3 numbers corresponding
        to the 3d coordinates, e.g. <code>{0, 0, 0}</code>
]]
horse3d.camera.get_position = function()
    return _camera_getposition()
end


--[[--
  Stick camera to the center of an object, e.g. a for first person game.
  Automatically @{horse3d.camera.disable_flycam|disables the flycam}.

  Usage example:

  <code style="white-space: pre">
    -- Add an invisible character-shaped physics object:
    local player = horse3d.world.add_invisible_object({2, 2, 5})
    player:enable_movement("character")  -- move like human rather than box

    -- Attach camera to it:
    horse3d.camera.attach_to(player)
  </code>

  @tparam horse3d.world.object object the
      @{horse3d.world.object|object} to attach to
]]
horse3d.camera.attach_to = function(object)

end

horse3d.camera.set_rotation = function(rot, arg2, arg3)
    if type(rot) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" then
        rot = {rot, arg2, arg3}
    end
    if type(rot) ~= "table" or #rot ~= 4 then
        error("arguments must be 3 numbers or table of 3 numbers "
              .. "representing euler angles")
    end
    local quat = math.quat_from_euler_angles(rot[1], rot[2], rot[3])
    _camera_setrotation(quat[1], quat[2], quat[3], quat[4])
end


horse3d.camera.set_rotation_quat = function(rot, arg2, arg3, arg4)
    if type(rot) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" and
            type(arg4) == "number" then
        rot = {rot, arg2, arg3, arg4}
    end
    if type(rot) ~= "table" or #rot ~= 4 then
        error("arguments must be 4 numbers or table of 4 numbers "
              .. "representing a quaternion")
    end
    _camera_setrotation(rot[1], rot[2], rot[3], rot[4])
end


horse3d.camera.get_rotation_quat = function()
    return _camera_getrotation()
end


horse3d.camera._update_flycam = function(dt)
    if not horse3d.camera._flycam_enabled then
        return
    end

    if horse3d.keypressed["q"] ~= nil then
        horse3d.camera._flycam_horirot =
            horse3d.camera._flycam_horirot + 0.05 * dt
    elseif horse3d.keypressed["e"] ~= nil then
        horse3d.camera._flycam_horirot =
            horse3d.camera._flycam_horirot - 0.05 * dt
    end
    if horse3d.keypressed["pageup"] ~= nil then
        horse3d.camera._flycam_vertirot =
            horse3d.camera._flycam_vertirot + 0.05 * dt
    elseif horse3d.keypressed["pagedown"] ~= nil then
        horse3d.camera._flycam_vertirot =
            horse3d.camera._flycam_vertirot - 0.05 * dt
    end
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
    --horse3d.camera._flycam_rollrot =
    --    horse3d.camera._flycam_rollrot + 0.02 * dt

    local rot = math.quat_from_euler_angles(
        horse3d.camera._flycam_horirot,
        horse3d.camera._flycam_vertirot,
        horse3d.camera._flycam_rollrot
    )
    _camera_setrotation(rot[1], rot[2], rot[3], rot[4])

    local sidemove = 0.0
    if horse3d.keypressed["a"] ~= nil or
            horse3d.keypressed["left"] ~= nil then
        sidemove = -1.0
    elseif horse3d.keypressed["d"] ~= nil or
            horse3d.keypressed["right"] ~= nil then
        sidemove = 1.0
    end
    local forwardmove = 0.0
    if horse3d.keypressed["w"] ~= nil or
            horse3d.keypressed["up"] ~= nil then
        forwardmove = 1.0
    elseif horse3d.keypressed["s"] ~= nil or
            horse3d.keypressed["down"] ~= nil then
        forwardmove = -1.0
    end
    local v = {
        sidemove, forwardmove, 0.0
    }
    if forwardmove ~= 0.0 or sidemove ~= 0.0 then
        v = math.normalize_vec(v)
        v = math.quat_rotate_vec(rot, v)
        v = math.normalize_vec(v)
        local f = dt * 100
        v[1] = v[1] * dt * f
        v[2] = v[2] * dt * f
        v[3] = v[3] * dt * f
    end
    local t = horse3d.camera.get_position()
    t[1] = t[1] + v[1]
    t[2] = t[2] + v[2]
    t[3] = t[3] + v[3]
    if not horse3d.camera._was_moved_this_frame then
        _camera_setposition(
            t[1], t[2], t[3]
        )
        horse3d.camera._was_moved_this_frame = true
    end
end

return horse3d
