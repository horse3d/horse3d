--[[--
  A class representing a currently or previously playing sound.

  To create a new sound, use @{horse3d.audio.play_sound}.

  A sound will automatically cease to exist once it is done playing,
  so @{horse3d.world.object.destroy|unlike world objects,} no manual removal
  is necessary.

  @classmod horse3d.audio.sound
]]

if horse3d.audio == nil then
    horse3d.audio = {}
end

if horse3d.audio.sound == nil then
    horse3d.audio.sound = {}
end


horse3d.audio.sound._set_metatableref = function(obj)
    local mt = getmetatable(obj)
    mt.__index = function(table, key)
        if key == "_metatable" then
            return nil
        end
        return horse3d.audio.sound[key]
    end
end
local reg = debug.getregistry()
reg.horse3d_audio_sound_set_metatableref = (
    horse3d.audio.sound._set_metatableref
)
reg = nil


--[[--
  Stop the sound early, if it is still playing.
  Does nothing if the given sound has already stopped playing.

  @function horse3d.audio.sound:stop
]]
horse3d.audio.sound.stop = function(self)
    _h3daudio_stopsound(self)
end

--[[--
  Get information about whether the given sound is still playing.

  @function horse3d.audio.sound:is_playing
  @return true if sound still playing, otherwise false
]]
horse3d.audio.sound.is_playing = function(self)
    return _h3daudio_soundisplaying(self)
end

--[[--
  Returns true if the sound stopped due to a playback error,
  rather than finished playing regularly.

  This might happen e.g. if the sound file was damaged and couldn't
  be read to the end as expected.

  @function horse3d.audio.sound:had_error
  @return true if there was an error, false if everything was fine
]]
horse3d.audio.sound.had_error = function(self)
    return _h3daudio_soundhaderror(self)
end

--[[--
  Set the new volume for this sound in the range 0.0 to 1.0.
  Has no effect if the sound is no longer playing.

  @function horse3d.audio.sound:set_volume
  @tparam number volume the new volume in range 0.0 to 1.0
]]
horse3d.audio.sound.set_volume = function(self, volume)
    _h3daudio_soundsetvolume(self, volume)
end

--[[--
  Set the new stereo panning for this sound
  in the range 1.0 to -1.0.
  Has no effect if the sound is no longer playing.

  @function horse3d.audio.sound:set_panning
  @tparam number panning the new panning in range 1.0 to -1.0
]]
horse3d.audio.sound.set_panning = function(self, panning)
    _h3daudio_soundsetpanning(self, panning)
end

--[[--
  Get the current playback volume of the given sound.
  Will return 0.0 if the sound is no longer
  @{horse3d.audio.sound.is_playing|playing}.

  @function horse3d.audio.sound:get_volume
  @return the volume in range 0.0 to 1.0
]]
horse3d.audio.sound.get_volume = function(self)
    return _h3daudio_soundgetvolume(self)
end

--[[--
  Get the current stereo panning of the given sound.
  Will return 0.0 if the sound is no longer
  @{horse3d.audio.sound.is_playing|playing}

  @function horse3d.audio.sound:get_panning
  @return the stereo panning in range
          1.0 (left) over 0.0 (center) to -1.0 (right)
]]
horse3d.audio.sound.get_panning = function(self)
    return _h3daudio_soundgetpanning(self)
end

return horse3d
