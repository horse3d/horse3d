
string.startswith = function(str1, str2)
    if #str1 < #str2 then
        return false
    end
    if str1:sub(1, #str2) == str2 then
        return true
    end
    return false
end

string.basename = function(s)
    return _lfs_basename(s)
end

string.dirname = function(s)
    return _lfs_dirname(s)
end

string.endswith = function(str1, str2)
    if #str1 < #str2 then
        return false
    end
    if str1:sub(#str1 - (#str2 - 1)) == str2 then
        return true
    end
    return false
end

function string.split(str, token, max_splits)
    -- for no separator, split at every char:
    if token == "" then
        local tbl = {}
        local i = 1
        while i < #str do
            table.insert(tbl, str:sub(i, i))
            i = i + 1
        end
        return tbl
    end
    -- regular splitting:
    local result = { str }
    while true do
        if max_splits ~= nil then
            if max_splits <= 0 then
                return result
            end
            max_splits = max_splits - 1
        end
        local oldlen = #result
        local tokenpos = (result[oldlen]):find(token, 1, true)
        if tokenpos == nil then
            return result
        end
        result[oldlen + 1] = (result[oldlen]):sub(tokenpos + #token)
        result[oldlen] = (result[oldlen]):sub(1, tokenpos - 1)
    end
end

-- unit tests for string.split:
assert(#string.split("abc", "b") == 2)
assert(string.split("bb", "")[1] == "b")
assert(string.split("hello, you", ", ")[2] == "you")
assert(#string.split("abc", "b", 0) == 1)
assert(string.split("1, 2, 3, 4", ",", 2)[3] == " 3, 4")
assert(string.split("abcd", ",")[1] == "abcd")


string.endswith = function(str1, str2)
    if #str1 < #str2 then
        return false
    end
    if str1:sub(#str1 - (#str2 - 1)) == str2 then
        return true
    end
    return false
end


function string.strip(str)
    while str:startswith(" ") or str:startswith("\t") do
        str = str:sub(2)
    end
    while str:endswith(" ") or str:endswith("\t") do
        str = str:sub(1, -2)
    end
    return str
end


string.replace = function(base, search, replace)
    --[[-- A replace function that will replace all occurrences of the search
           string in the given base string with the replace string.

           Unlike string:gsub, no special regex pattern processing will take
           place - this stupidly searches exactly for the search string as is,
           and only if it is found character by character exactly as written,
           the occurrence will be replaced. ]]
    if #search == 0 then
        return base
    end
    local startindex = 1
    while true do
        local index = base:find(search, startindex, true)
        if index ~= nil then
            local result = ""
            if index > 1 then
                result = result .. base:sub(1, index-1)
            end
            result = result .. replace
            -- check if there is something left after the replaced piece:
            if index - 1 + #replace < #base + #replace - #search then
                -- there is!
                assert(#result >= startindex)
                startindex = #result
                result = result .. base:sub(index + #search)
                base = result
                -- since stuff is left at the end, continue searching!
            else
                -- we reached the end:
                return result
            end
        else
            -- no further occurances left!
            return base
        end
    end
end
