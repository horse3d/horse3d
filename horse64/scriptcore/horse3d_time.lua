--[[--
  This module contains functions for time management.

  @module horse3d.time
]]

if horse3d.time == nil then
    horse3d.time = {}
end


--[[--
  Get a continuously growing relative time for use in your
  game logic, with fractional parts down to  millisecond
  accuracy.

  <b>Important: this is a time relative to some arbitrary point
  before game start, not an absolute system time,</b>
  so it has no use outside of a single game run. Don't use this
  to store the absolute time in savegames or similar.

  The returned time value is guaranteed to never be smaller
  than a previously returned value during a run of your game.

  @return the current monotonic game time in seconds with fractional part
]]
horse3d.time.gametime = function()
    return _time_getticks()
end

--[[--
  Freeze the entire game for the given amount of milliseconds.
  This function call will hang and only return once the waiting time
  is complete. <b>This is an advanced internal function you should
  probably never need. @{horse3d.time.schedule|Schedule delayed
  functions} instead if you want to delay game logic.</b>

  @tparam number ms the given amount of milliseconds to hang the process
]]
horse3d.time.sleepms = function(ms)
    _time_sleepms()
end

--[[--
  Schedule a function to run at a later time.
  To make a function run more than once, just use
  @{horse3d.time.schedule} again inside the function itself to
  reschedule for another run.

  Example for running something <b>once</b>:
  <code style="white-space:pre;">
    horse3d.time.schedule(5, function()
        print("Five seconds are over. My work is done, bye!")
    end)
  </code>

  Example for running something <b>recurrently</b>:
  <code style="white-space:pre;">
    -- Define a function which will re-schedule itself:
    local function run_each_second()
        print("A second has passed. See you in another!")
        horse3d.schedule(1, run_each_second)
    end

    -- Schedule the first function run:
    horse3d.time.schedule(1, run_each_second)
  </code>

  @tparam number delay the delay after which to run the function
  @tparam function func the function to be run after the delay
]]
horse3d.time.schedule = function(delay, func)
    _time_schedulefunc(delay, func)
end

return horse3d
