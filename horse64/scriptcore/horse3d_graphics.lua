--[[--
  This module holds various misc functionality to control
  how the graphics and visuals operate.

  @module horse3d.graphics
]]

if horse3d.graphics == nil then
    horse3d.graphics = {}
end



--[[--
  Return the current frame rate in frames per second.
  The calculated number is averaged over multiple frames
  for more accurate results.

  @return the current averaged frame rate
]]
horse3d.graphics.get_fps = function()
    return _graphics_getfps()
end

--[[--
  Get the maximum number of lights that Horse3D will show simultaneously
  at any point. Please note you can always have more in your world
  with no problems, other than the far away lights no longer
  effectively showing. (The most important lights are redetermined
  constantly based on camera position.)

  @return the maximum number of lights ever visible in a frame
]]
horse3d.graphics.get_max_shown_lights = function()
    return tonumber(_graphics_getmaxshownlights())
end

--[[--
  Return the current render time of a frame in milliseconds.
  The calculated number is averaged over multiple frames
  for more accurate results.

  @return the current averaged frame render time
]]
horse3d.graphics.get_frame_time_ms = function()
    return _graphics_getframetimems()
end

return horse3d
