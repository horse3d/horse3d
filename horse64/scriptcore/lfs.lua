
local lfs = {}

lfs.dir = function(path)
    local result = _lfs_dir(path)
    local i = 0
    return function()
        i = i + 1
        return result[i]
    end
end

lfs.currentdir = function()
    return _lfs_currentdir()
end

lfs.attributes = function(path, aname)
    if type(path) ~= "string" then
        return nil, "path is not of type string: " .. tostring(path)
    end
    if not _lfs_exists(path) then
        return nil, "path does not exist"
    end
    local result = {
        ino=666,
        size=_lfs_getsize(path),
        mode="file"
    }
    if _lfs_isdir(path) then
        result["mode"] = "directory"
    end
    if aname ~= nil then
        return result[aname]
    end
    return result
end

lfs.mkdir = function(path)
    return _lfs_mkdir(path)
end


lfs.datadir = function()
    return _lfs_datadir()
end

return lfs
