--[[--
  This module allows you to play sounds and music.

  @module horse3d.audio
]]

if horse3d.audio == nil then
    horse3d.audio = {}
end


--[[--
  (Advanced, you usually won't need this)
  Get the names of all the audio devices available.

  @return a @{table} with a list of name strings
]]
horse3d.audio.get_audio_device_names = function()
    return _h3daudio_listsoundcards()
end

--[[--
  (Advanced, you usually won't need this)
  Open a specific @{horse3d.audio.audiodevice|audio
  device/sound card} for audio playback.
  Throws an error if it fails which you can catch with
  @{pcall} if you wish.

  @tparam string name the name of the audio device, must be
     one of the available
     ones returned from @{horse3d.audio.get_audio_device_names}

  @return the @{horse3d.audio.audiodevice|audio device} opened
]]
horse3d.audio.open_audio_device = function(name)
    local dev = _h3daudio_opendevice(name)
    if horse3d.audio._defaultdevice == nil then
        horse3d.audio._defaultdevice = dev
    end
    return dev
end

function _get_device(openifnone)
    local dev_ref = {
        dev=horse3d.audio._defaultdevice
    }
    if dev_ref.dev == nil and openifnone then
        local result, errmsg = pcall(function()
            local names = horse3d.audio.get_audio_device_names()
            dev_ref.dv = horse3d.audio.open_audio_device(names[1])
        end)
    end
    return dev_ref.dev
end

--[[--
  (Advanced, you usually won't need this)
  Return the currently used @{horse3d.audio.audiodevice|audio device}
  if none is specified. May return nil if no sound was ever played.

  @return the @{horse3d.audio.audiodevice|audio device} used by
          default, or nil
]]
horse3d.audio.get_default_device = function()
    return _get_device(false)
end

--[[--
  Play the sound represented by the given sound file, and return
  the respective @{horse3d.audio.sound|sound object}.

  Throws an error if the file can't be opened (e.g. due to a missing file)
  which you can catch with @{pcall} if you wish. If the file is corrupted
  this call may still succeed, but @{horse3d.audio.sound.had_error|the
  sound's :had_error() method} will return true afterwards.

  Please note you may ignore the returned @{horse3d.audio.sound|sound
  object} entirely, and it will finish continue playing anyway - the
  engine will make sure a reference to the sound object
  is internally kept until no longer necessary.
  However, without keeping the sound object you can't stop the sound
  early, or modify volume and such while it is playing.

  @tparam string path the path to the sound file to be played
  @tparam number volume (optional) the sound volume in range 0..1.0.
                        Defaults to 0.7
  @tparam number panning (optional) the stereo placement of the song from
                         left (-1.0) over center (0.0) to right (1.0).
                         Defaults to 0.0
  @tparam boolean loop (optional) whether to loop the sound until
                       @{horse3d.audio.sound.stop|stopped}, defaults to
                       false
  @tparam horse3d.audio.audiodevice device (optional) the
        @{horse3d.audio.audiodevice|audio device} to play on
  @return returns the @{horse3d.audio.sound|sound} that is now playing
]]
horse3d.audio.play_sound = function(path, volume, panning, loop, device)
    if type(path) ~= "string" then
        error("expected arg #1 to be a string pointing " ..
              "to an audio file, not of type " .. tostring(path))
    end
    if device == nil then
        device = _get_device(true)
        if device == nil then
            error("failed to get audio device")
        end
    end
    if type(loop) ~= "boolean" then
        loop = false
    end
    if type(volume) ~= "number" then
        volume = 0.7
    end
    if type(panning) ~= "number" then
        panning = 0
    end
    return _h3daudio_playsound(
        path, volume, panning, loop, device
    )
end

return horse3d
