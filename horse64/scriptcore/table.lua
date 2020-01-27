
function table.copy(mytable)
    local newtable = {}
    for k,v in pairs(mytable) do
        newtable[k] = v
    end
    return newtable
end

