--[[--
  An implementation of the <a href="https://json.org" rel=noopener>JSON
  data format</a> which you can use to convert a lua @{table} with its
  members, or other simple types like a <code>number</code>, to/from a
  JSON string.

  You can use this to e.g. convert all your savegame data kept
  in a table to a simple string you can save to file.

  @module json
]]

local json = {}

--[[--
  Parse the given JSON from a string, and decode it to the
  corresponding lua @{table} object. Will raise an error if
  parsing fails, which you may catch via @{pcall}.

  @tparam string json the JSON string to be converted to the
                      corresponding lua object (e.g. for savegame loading)
  @return the resulting lua table
]]
json.decode = function(json)
    return _json_decode(tostring(json))
end


--[[--
  Takes a lua @{table}, or <code>number</code>, or other supported basic
  type, and returns the corresponding JSON string.
  Please note <cod>function</code>s, and
  @{horse3d.world.object|world objects} or other
  horse3d-specific objects, can <b>not</b> be stored as JSON.

  @param value the lua value to be converted to a JSON string
               (e.g. for savegame saving)
  @return the resulting JSON string which can be written to file
]]
json.encode = function(value)
    return _json_encode(tostring(value))
end


return json
