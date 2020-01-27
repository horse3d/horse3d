--[[--
  Functionality to deal with the @{86_Misc_VFS.md|VFS of Horse3D}.
  @module horse3d.vfs
]]

if horse3d.vfs == nil then
    horse3d.vfs = {}
end


--[[--
  Load in an additional .h3dpak file. Its content will be added in
  virtually at your game folder path,
  e.g. if the .h3dpak file contains "somefolder/somefile.txt" then you
  can load it up via `io.open("somefolder/somefile.txt")` no matter
  where the .h3dpak itself was located.

  @tparam string path a path pointing to a .h3dpak file on disk
  ]]
horse3d.vfs.add_h3dpak = function(path)
    if (type(path) ~= "string") then
        error("argument must be string pointing to .h3dpak file")
    end
    _vfs_adddatapak(path)
end

return horse3d
