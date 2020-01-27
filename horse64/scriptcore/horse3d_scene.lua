--[[--
  This module allows loading up entire scenes with many objects.

  @module horse3d.scene
]]

if horse3d.scene == nil then
    horse3d.scene = {}
end


--[[--
  Parse a scene file, e.g. a GLTF file, or a quake style .map file), and add in
  the level geometry,
  @{horse3d.world.light|lights} and
  @{horse3d.world.object|entities as objects}.

  <b>Entities recognized for .map-based scenes:</b>
  The function looks for
  entities based on the following entity definition:

  <code style="white-space:pre">
&#64;baseclass base(Targetname) size(-16 -16 -24, 16 16 32) color(0 255 0) = PlayerClass []

&#64;PointClass base(PlayerClass) = player_start : "Player start" []

&#64;baseclass base(Targetname) size(-8 -8 -8, 8 8 8) color(150 255 0) = ItemClass []

&#64;PointClass base(ItemClass) = entity : "Generic entity" [
    behavior(string) : "Behavior function name"
    mesh(string) : "Mesh name"
    options(string) : "Object creation options"
]

&#64;baseclass base(Targetname) size(-8 -8 -8, 8 8 8) color(255 150 0) = ItemClass []

&#64;PointClass base(ItemClass) = light : "Light" [
    lightred(float) : "Red" : 0.8
    lightgreen(float) : "Green" : 0.8
    lightblue(float) : "Blue" : 0.8
    range(float) : "Range": 10
    falloff(choices) : "Falloff" =
    [
        0 : "Linear falloff"
        1 : "Inverse distance falloff"
    ]
]
  </code>

  <b>Important: </b>for quake .map files, make sure that the .map
  is also exported as geometry with ending .obj <i>in the same folder.</i>
  Loading the map geometry itself directly from .map is not supported yet.
  No such additional export is necessary for .gltf scenes.

  <b>Available options: </b>You can pass the following options if you wish:

<code style="white-space:pre">{
     -- The scale to apply to the scene geometry and the position
     -- of all contained entities as a whole, default is 1.0:
     scene_scale=1.0,
     -- Whether to apply the scene_scale as @{horse3d.object.set_scale|object
     -- scale} to each contained object, too. Defaults to true:
     scene_scale_applies_to_object_scale=true,
     -- Whether to apply the mesh_scale to the light ranges of the lights
     -- in the scene, defaults to true:
     scene_scale_applies_to_light_range=true,
     -- Specify custom no draw texture search words as described for
     -- horse3d.world.add_object(), see that function for documentation:
     texture_nodraw_keywords=[],
     -- Specify custom no collision texture search words as described for
     -- horse3d.world.add_object(), see that function for documentation:
     texture_nocollision_keywords=[],
    -- A tag to apply to all things added from this scene, defaults to
     -- nil / no tag:
     tag=nil,
}</code>

  @tparam string path the file path to the scene file
  @tparam table options (optional) the options as described above
    for configuring scene scale, a tag that is applied, and more
]]
horse3d.scene.add_scene = function(path, options)
    local scene_scale = 1.0
    local tag = ""
    local scale_objects = true
    local scale_light_ranges = true
    local _invis_keywords = {}
    local _nocol_keywords = {}
    if type(options) == "table" then
        if type(options["scene_scale"]) == "number" then
            scene_scale = tonumber(options["scene_scale"])
        end
        if type(options["tag"]) == "string" then
            tag = tostring(options["tag"])
        end
        if type(options["texture_nodraw_keywords"]) == "table" then
            for _, v in ipairs(options["texture_nodraw_keywords"]) do
                table.insert(_invis_keywords, "" .. tostring(v))
            end
        end
        if type(options["texture_nocollide_keywords"]) == "table" then
            for _, v in ipairs(options["texture_nocollide_keywords"]) do
                table.insert(_nocol_keywords, "" .. tostring(v))
            end
        end
        if type(options["scene_scale_applies_to_light_range"]
                ) == "boolean" then
            if options["scene_scale_applies_to_light_range"] then
                scale_light_ranges = true
            else
                scale_light_ranges = false
            end
        end
        if type(options["scene_scale_applies_to_object_scale"]
                ) == "boolean" then
            if options["scene_scale_applies_to_object_scale"] then
                scale_objects = true
            else
                scale_objects = false
            end
        end
    end

    if (path:lower()):endswith(".map") then
        local mesh_path = nil
        mesh_path_guesses = {
            path:sub(1, #path - #".map") .. ".obj",
            path:sub(1, #path - #".map") .. ".OBJ",
            path .. ".obj",
            path .. ".OBJ",
        }
        local i = 1
        while i <= #mesh_path_guesses do
            if _lfs_exists(mesh_path_guesses[i]) then
                mesh_path = mesh_path_guesses[i]
                break
            end
            i = i + 1
        end
        if mesh_path == nil then
            error("missing OBJ export for map file, expected at " ..
                  mesh_path_guesses[i] .. " - needed to load scene")
        end

        local geometry = horse3d.world.add_object(mesh_path, {
            tag=tag,
            texture_nodraw_keywords=_invis_keywords,
            texture_nocollide_keywords=_nocol_keywords,
        })
        geometry:set_scale(scene_scale)

        local result, errmsg = pcall(function()
            _scene_additemsfromquakemap(
                path, scene_scale, tag,
                scale_objects, scale_light_ranges
            )
        end)
        if result ~= true then
            geometry:destroy()
            error(errmsg)
        end
    elseif (path:lower()):endswith(".gltf") or
            (path:lower()):endswith(".gltb") then
        _scene_additemsandgeometryfromgltfscene(
            path, scene_scale, tag,
            _invis_keywords, _nocol_keywords,
            scale_objects, scale_light_ranges
        )
    else
        error("unknown scene format")
    end
end


return horse3d
