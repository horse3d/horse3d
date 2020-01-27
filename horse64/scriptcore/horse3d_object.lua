--[[--
  The <code>horse3d.world.object</code> type represents
  a 3d object in the game world.
  Such an object can be shown as a 3d model or sprite, and it can
  have a physics colllision, and attached script for behavior.
  By default the collision hull is static and based on the exact
  3d triangles
  of its shape which is ideal for level geometry. However, if you
  @{horse3d.world.object.enable_movement|enable physical movement}
  the object will have realistic movement based on physical forces but
  will use a simplified collision hull.
  To make the object neither move nor be an obstacle,
  @{horse3d.world.object.disable_collision|disable its collision entirely}.

  To create an object, call @{horse3d.world.add_object}.

  <b>Important:</b> world objects will stick around until
  @{horse3d.world.object.destroy|destroyed}, even if you let go off all
  references. Use @{horse3d.world.get_all_objects} or similar to list
  all existing objects (although this may be very slow if you have many!).

  @classmod horse3d.world.object
]]

if horse3d.world == nil then
    horse3d.world = {}
end

if horse3d.world.object == nil then
    horse3d.world.object = {}
end


horse3d.world.object._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.world.object[key]
    end
    mt.__tostring = function(table)
        return ("horse3d.world.object: " .. tostring(obj:get_id()))
    end
end

local reg = debug.getregistry()
reg.horse3d_world_object_set_metatableref = (
    horse3d.world.object._set_metatableref
)
reg = nil

--[[--
 Warp this object to the given position.

 This is only useful for initial position setup because it will
 reset the realistic physics forces on the object, as well as
 completely ignore collision. (It will happily warp into solid
 matter.)

 To gradually move objects with full collision, use
 @{horse3d.world.object.accelerate|object:accelerate} instead.

 Usage example:

    local myobject = horse3d.world.add_object("mybox.obj")
    myobject:set_position({0, 10, 0})  -- warp in front of world origin

 @function horse3d.world.object:set_position
 @tparam table position the position as three numbers along
 each axis, like <code>{0, 0, 0}</code> for the world center
]]
horse3d.world.object.set_position = function(self, position, arg2, arg3)
    if type(position) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" then
        position = {position, arg2, arg3}
    end
    _world_obj_setposition(self, position)
end


--[[--
  Set a new scale for an object. If the absolute
  @{horse3d.world.object.set_mass|physical mass} of this object was never
  set and is therefore calculated automatically, it will be updated to
  reflect the new volume.

  Note: if the object has
  @{horse3d.world.object.enable_movement|dynamic movement enabled} then
  changing the scale can lead to quirks like the object getting stuck if
  you scale it up in small spaces. Scaling will always be applied
  immediately, disregarding any things that might be in the way.

  @function horse3d.world.object:set_scale
  @tparam number scale the new scale, anything greater than zero
]]
horse3d.world.object.set_scale = function(self, scale)
    _world_obj_setscale(self, scale)
end


--[[--
  Destroy the given object and remove it from the world.
  If you access the object reference afterwards
  (e.g. by calling
  @{horse3d.world.object:get_position|obj:get_position} on it)
  it will trigger an error
  which you can catch with lua's @{pcall}.

  @function horse3d.world.object:destroy
]]
horse3d.world.object.destroy = function(self)
    _world_obj_destroy(self)
end


--[[--
  Get the object's position in the 3d world.

  @function horse3d.world.object:get_position
  @return the position as a 3 number table, e.g. <code{0.0, 0.0, 0.0}</code>
]]
horse3d.world.object.get_position = function(self)
    return _world_obj_getposition(self)
end


--[[--
  Get the object's rotation in the 3d world as a quaternion.
  This is an advanced function,
  for easier use @{horse3d.world.object.get_rotation|getting
  the euler angles} which represent horizontal, vertical, and sidewards
  roll angles is recommended instead.

  @function horse3d.world.object:get_rotation_quat
  @return the rotation quaternion as a 4 number table,
  e.g. <code{0.0, 0.0, 0.0, 0.0}</code>
]]
horse3d.world.object.get_rotation_quat = function(self)
    return _world_obj_getrotation(self)
end


--[[--
  Set the object's rotation in the 3d world as a quaternion.
  This is an advanced function, for easier use
  @{horse3d.world.object.set_rotation|set
  the euler angles} instead.

  @function horse3d.world.object:set_rotation_quat
  @tparam table quat the new rotation quaternion as a 4 number table
]]
horse3d.world.object.set_rotation_quat = function(
        self, quat, arg2, arg3, arg4
        )
    if type(quat) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" and
            type(arg4) == "number" then
        quat = {quat, arg2, arg3, arg4}
    end
    _world_obj_setrotation(self, quat)
end


--[[--
  Set the object's rotation with euler angles.
  The euler angles are three angles which describe rotation
  around horizontal (left/right), vertical (up/down), and
  sideways (rolling).

  Usage example:

    -- Create 3d model from mybox.obj file:
    local obj = horse3d.world.add_object("mybox.obj")

    -- Rotate horizontally 45 degrees to the left:
    obj:set_rotation({45, 0, 0})

  @function horse3d.world.object:set_rotation
  @tparam table angles the new rotation as a 3 number table
  for the 3 euler angles
]]
horse3d.world.object.set_rotation = function(
        self, angles, arg2, arg3, arg4
        )
    if type(angles) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" then
        angles = {angles, arg2, arg3, arg4}
    end
    local quat = math.quat_from_euler_angles(
        angles[1], angles[2], angle[3]
    )
    _world_obj_setrotation(self, quat)
end

--[[--
  If this object has <code>character</code>-shape
  @{horse3d.world.object.enable_movement|movement}, get whether it is
  standing on the ground or in mid-air.
  Cannot be used on other movement types, and will return an error
  if used then.

  If you write your own movement code based on this, it is recommended
  you disable player walking input as long as this function returns true.
  (The @{horse3d.behavior.humanoid_player|humanoid player} behavior
  already does this.)

  @function horse3d.world.object:get_character_is_on_floor
  @return true if object is standing on the ground, false if in the air
]]
horse3d.world.object.get_character_is_on_floor = function(
        self
        )
    return _world_obj_getcharisonfloor(self)
end

--[[--
  (Internal/Advanced) Return the compensation factor the character
  controller guesses based on mass to make sure even giant characters
  can still move.

  <b>You usually won't need this unless you apply custom character
  forces, since the default @{horse3d.behavior.humanoid_player|character
  behaviors} use all of this automatically.</b>

  <i>How this factor works if you <b>do</b> want to accelerate your
  characters manually:

  If you want to apply your own additional movement forces to a character
  via @{horse3d.world.object.accelerate|object:accelerate} then you may
  want to multiply them with this factor. It will make sure that a heavy
  giant character (think of an ogre) will compensate to move appropriately
  with giant legs, albeit still a bit sluggish as any giant would, rather
  than get stuck with the default ant-sized force of human legs.

  In any case, you should always stick to one unit being roughly one
  meter (or about three feet) and expect anything giant in that scale
  to behave as such. This factor really just makes giants work, not behave
  not like giants.</i>

  This function cannot be used on objects with
  @{horse3d.world.object.enable_movement|movement types} other than
  <code>character</code>, and will return an error if used then.

  @function horse3d.world.object:get_character_forces_scaling
  @return the compensation factor for scaling movement forces
]]
horse3d.world.object.get_character_forces_scaling = function(self)
    return _world_obj_getcharscalebasedforcefactors(self)
end

--[[--
  If this object has <code>character</code>-shape
  @{horse3d.world.object.enable_movement|movement}, get whether it is
  on a notably tilted slope.
  Cannot be used on other movement types, and will return an error
  if used then.

  If you write your own movement code based on this, it is recommended
  you disable player walking input as long as this function returns true.
  (The @{horse3d.behavior.humanoid_player|humanoid player} behavior
  already does this.)

  @function horse3d.world.object:get_character_is_on_strong_slope
  @return true if object is standing on a notably tilted slope,
  false if in the air or on even ground
]]
horse3d.world.object.get_character_is_on_strong_slope = function(
        self
        )
    return _world_obj_getcharisonslope(self)
end

--[[--
  Set mass of an object, recommended unit is kg.
  Only has an effect when @{horse3d.world.object.enable_movement|physically
  simulated movement} was enabled.
  <b>A reasonable mass is auto-inferred from the object size
  by default,</b> so there is no need to set this unless you want
  to tweak things.

  @function horse3d.world.object:set_mass
  @tparam number mass the mass of the object in kilogram
]]
horse3d.world.object.set_mass = function(self, mass)
    _world_obj_setmass(self, mass)
end

--[[--
  Get mass of an object, by default this is assumed to be kilogram.

  @function horse3d.world.object:get_mass
  @return the mass of the object as a number, in kilogram
]]
horse3d.world.object.get_mass = function(self)
    return _world_obj_getmass(self)
end

--[[--
 Accelerate the given object with a physical force.
 This will automatically
 @{horse3d.world.object.enable_movement|enable physics dynamics}
 on it if that was turned off.

 Don't ever call this for level geometry, position it with
 @{horse3d.world.object.set_position|object:set_position} instead.

 @function horse3d.world.object:accelerate
 @tparam table force the force as three numbers along
   each axis, like <code>{0, 0.1, 0}</code> to
   accelerate along the Y axis
]]
horse3d.world.object.accelerate = function(self, force)
    if type(force) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" then
        force = {force, arg2, arg3}
    end
    if type(force) ~= "table" or #force ~= 3 or
            type(force[1]) ~= "number" or
            type(force[2]) ~= "number" or
            type(force[3]) ~= "number" then
        error("acceleration force must be a table with 3 numerical values")
    end
    _world_obj_applyforce(self, force)
end


--[[--
 Enable the object to move on its own through gravity and such.
 Due to the complexity of calculating moving objects and their
 collisions accurately this will enable a simpler collision shape.

 You can choose one of multiple shapes with the <code>shape</code> parameter:

    "character",  -- A character controller that supports walking up
                    stairs and more, best suited for humans and alike.
    "box",  -- A 3d box shape that can fall over, and be pushed around.

 Don't enable physics dynamics for level geometry, it must remain static.
 Position it with @{horse3d.world.object.set_position|object:set_position}
 instead.

 By default, movement is disabled for all objects such that they act as
 static collision geometry to others.

 Usage example:

    -- Create an object from an OBJ file named myboxmodel.obj:
    local myobject = horse3d.world.add_object("myboxmodel.obj")

    -- Enable movement:
    myobject:enable_movement("box")

    -- The object will now behave like a cardboard box that can fall over!

 @function horse3d.world.object:enable_movement
 @tparam string shape the collision shape. Defaults to <code>"box"</code>
    for static unanimated mesh objects, and to <code>"character"</code>
    for animated mesh objects as well as
    @{horse3d.world.add_invisible_object|invisible objects}
]]
horse3d.world.object.enable_movement = function(self, shape)
    shape = tostring(shape)
    if shape == "nil" then
        if _world_objectisnomeshtype(self) or (
                _world_objecthas3dmesh(self) and
                _world_object3dmeshhasanimation(self)
                )then
            shape = "character"
        else
            shape = "box"
        end
    end
    _world_obj_enabledynamics(self, shape)
end


--[[--
  Disable this object's physics collision entirely, even just as
  static obstacle to other objects.
  This will disable physical forces on the object which means
  @{horse3d.world.object.accelerate|physical acceleration does nothing},
  and it will set the object to permeable to everything else.

  By default, collision is enabled for all objects while
  @{horse3d.world.object.enable_movement|dynamic movement} is disabled.

  @function horse3d.world.object:disable_collision
  @tparam boolean disabled (optional) whether to disable or re-enable it.
    Defaults to true, which means disabling collision.
]]
horse3d.world.object.disable_collision = function(self, disabled)
    if type(disabled) == "nil" then
        disabled = true
    end
    _world_obj_disablecollision(self, (disabled == true))
end

--[[--
  Tag the given object with this string of your choice.
  Multiple tags are possible, you can also apply a tag
  via @{horse3d.world.add_object|add_object's "tag" option}
  right at object creation already.

  A tag can be any string you want for any meaning you want,
  e.g. you could tag all objects for a castle area <code>"castle"</code>,
  tag the player as <code>"player"</code>, and so forth.
  You can then use @{horse3d.world.get_objects_by_tag} and other
  functions to mass operate on objects only with your given tags.
  It is meant to facilitate you organizing your game world.

  @function horse3d.world.object:add_tag
  @tparam string tag the tag you want to add to the object
]]
horse3d.world.object.add_tag = function(self, tag)
    _world_object_tag(self, tag)
end

--[[--
  (Advanced/internal) Get the internal id of an object.
  This is mainly useful for debugging, it is recommended you
  use @{horse3d.world.object.add_tag|tags} instead to manage
  your objects.

  @function horse3d.world.object:get_id
  @return the object id as a number
]]
horse3d.world.object.get_id = function(self)
    return _world_object_getid(self)
end


--[[--
  Get the object's world dimensions on all axis with
  @{horse3d.world.object.set_scale|scaling} taken into account.

  @function horse3d.world.object:get_dimensions
  @return a table of the dimensions, e.g. <code>{1.0, 1.0, 1.0}</code>
]]
horse3d.world.object.get_dimensions = function(self)
    return _world_objectgetdimensions(self)
end

return horse3d
