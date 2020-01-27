--[[--
  A module containing UI functionality. Use it to build your option menus,
  health bars, and more.

  @module horse3d.uiplane
]]

if horse3d.uiplane == nil then
    horse3d.uiplane = {}
end


--[[--
  Create a new 2D sprite @{horse3d.uiplane.object|ui object}.

  Available options:

    {
      -- Pixel offset from left border of the sprite:
      tex_offset_x=0,
      -- Pixel offset from top border of the sprite:
      tex_offset_y=0,
      -- Width in pixels to use from the sprite graphic, 0 for full:
      tex_dimensions_x=0,
      -- Height in pixels to use from the sprite graphic, 0 for full:
      tex_dimensions_y=0,
    }

  @tparam string sprite_path the path to the image file to use for the sprite
  @tparam table options (optional) the options, if any
  @return the newly created @{horse3d.uiplane.object|ui object}
]]
horse3d.uiplane.add_object = function(sprite_path, options)
    if type(sprite_path) ~= "string" then
        error("first argument must be a path string")
    end
    local _xoffset = 0
    local _yoffset = 0
    local _xdim = 0
    local _ydim = 0
    if type(options) == "table" then
        if type(options["tex_offset_x"]) == "number" then
            _xoffset = math.max(0, math.floor(options["tex_offset_x"]))
        end
        if type(options["tex_offset_y"]) == "number" then
            _yoffset = math.max(0, math.floor(options["tex_offset_y"]))
        end
        if type(options["tex_dimensions_x"]) == "number" then
            _xdim = math.max(0, math.floor(
                options["tex_dimensions_x"]
            ))
        end
        if type(options["tex_dimensions_y"]) == "number" then
            _ydim = math.max(0, math.floor(
                options["tex_dimensions_y"]
            ))
        end
    end
    return _uiobject_addnewsprite(
        sprite_path, _xoffset, _yoffset, _xdim, _ydim
    )
end

--[[--
  Get the default @{horse3d.uiplane.plane} where objects
  will be added on by default.

  @return the default @{horse3d.uiplane.plane|ui plane}
]]
horse3d.uiplane.get_default_plane = function()
    return _uiplane_getdefaultplane()
end


--[[--
  Create a new text-based @{horse3d.uiplane.object|ui object}.

  @tparam string text the text to put onto the object
  @tparam number font_size (optional)
  the @{horse3d.uiplane.object.set_font_size|point size}
  of the font, defaults to 24
  @tparam string font_name (optional) the name
  or .ttf path of the font to use
  @return the newly created @{horse3d.uiplane.object|ui object}
]]
horse3d.uiplane.add_text_object = function(text, font_size, font_name)
    if type(text) ~= "string" or #text == 0 then
        error("first argument must be a non-empty string")
    end
    if type(font_size) == "nil" then
        font_size = 24
    end
    if type(font_size) ~= "number" or font_size < 1 then
        error("font size must be a number of 1 or higher")
    end
    if type(font_name) == "nil" then
        font_name = "Sans Serif"
    end
    if type(font_name) ~= "string" then
        error("font name must be a string")
    end
    return _uiobject_addnewtext(text, font_size, font_name)
end

--[[--
  Create a new separate @{horse3d.uiplane.plane|ui plane}.

  This can be useful if you want to place it into the game afterwards
  for e.g. an ingame computer with UI. Otherwise, you can probably
  just stick to using the @{horse3d.uiplane.get_default_plane|default
  plane} that is always attached to the screen.

  By default, new planes will also be attached to the screen until
  you decide to place them somewhere else.

  @tparam table size the size of the plane in pixels, e.g. `{50, 50}`
  ]]
horse3d.uiplane.add_new_plane = function(size)
    return _uiplane_addnewplane()
end

--[[--
  (Advanced/internal) Get an @{horse3d.uiplane.object|ui object}
  back via its @{horse3d.uiplane.object.get_id|numeric id}.

  @tparam number id the internal id of the object
  @return the object if it still exists, otherwise nil
]]
horse3d.uiplane.get_object_by_id = function(id)
    return _uiobject_getbyid(id)
end

--[[--
  (Advanced/internal) Get an @{horse3d.uiplane.plane|ui plane}
  back via its @{horse3d.uiplane.plane.get_id|numeric id}.

  @tparam number id the internal id of the plane
  @return the plane if it still exists, otherwise nil
]]
horse3d.uiplane.get_plane_by_id = function(id)
    return _uiplane_getbyid(id)
end


return horse3d
