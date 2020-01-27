--[[--
  A class representing an audio device / sound card.

  You can get an audio device manually with
  @{horse3d.audio.open_audio_device}.
  Usually you don't need to use this directly, unless you
  really want to control which specific soundcard is used for
  playback.

  @classmod horse3d.audio.device
]]

if horse3d.audio == nil then
    horse3d.audio = {}
end

if horse3d.audio.device == nil then
    horse3d.audio.device = {}
end


horse3d.audio.device._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.audio.device[key]
    end
end
local reg = debug.getregistry()
reg.horse3d_audio_device_set_metatableref = (
    horse3d.audio.device._set_metatableref
)
reg = nil


--[[--
  Get the given audio device's name.

  @function horse3d.audio.device:get_name
  @return a string with the audio device's name
]]
horse3d.audio.device.get_name = function(self)
    return _h3daudio_getdevicename(self)
end

return horse3d
