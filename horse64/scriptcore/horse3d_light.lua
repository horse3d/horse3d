--[[--
 The <code>horse3d.world.light</code> type represents
 a light source in the game world.

 A light source can be attached @{horse3d.world.object|to an object}
 or exist independently.

 To add a light call @{horse3d.world.add_light}, or
 @{horse3d.scene.add_scene|add a scene} that contains lights.

 @classmod horse3d.world.light
]]

if horse3d.world == nil then
    horse3d.world = {}
end

if horse3d.world.light == nil then
    horse3d.world.light = {}
end


horse3d.world.light._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.world.light[key]
    end
end
local reg = debug.getregistry()
reg.horse3d_world_light_set_metatableref = (
    horse3d.world.light._set_metatableref
)
reg = nil

--[[--
 Move this object to the given position.

 @function horse3d.world.light:set_position
 @tparam table position the position as three numbers along
   each axis, like <code>{0, 0, 0}</code> for the world center
]]
horse3d.world.light.set_position = function(self, position, arg2, arg3)
    if type(position) == "number" and
            type(arg2) == "number" and
            type(arg3) == "number" then
        position = {position, arg2, arg3}
    end
    _lights_setlightposition(self, position)
end

return horse3d
