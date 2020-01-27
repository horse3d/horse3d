# The Game World

@lookup 02_The_Game_World.md

## Objects

To make a game in Horse3d, you add in @{horse3d.world.object|objects}
to represent your surroundings, like the ground and walls, and to
represent your beings, like players and characters. With attached
script behaviors you make them interact.

As a next step, after adding in objects, you may want to
enable dynamic physical movement, or add a @{Behaviors|behavior}:


## Collision

Objects by default have a collision based on their polygon shape
if static, or based on a
@{horse3d.world.object.enable_movement|simple shape of your choice}
when enabling movement via simulated physics.

To enable such dynamic movement, simply call
@{horse3d.world.object.enable_movement|object:enable_movement}:
```lua
local obj = horse3d.world.add_object("demo-assets/box.obj")
obj:enable_movement("box")
```
Only do this for your dynamic objects, and not your level geometry,
since it will disable polygon-exact collision for these objects.


## Behaviors

Behaviors make objects do things! (Other than just tumbling to the ground.)

In its simplest form, a **behavior** is just a lua function:

```lua
local myfloor = horse3d.world.add_object("demo-assets/floor.obj")
local myobject = horse3d.world.add_object("demo-assets/box.obj",
    behavior=function(self, dt)
        self:accelerate(  -- gradually get jolted forwards
            0, 1.0 * dt, 0.0
        )
    end
)
myobject:set_position(0, 10, 1)  -- spawn in front of camera
```
*(If you test this out, you should see a box moving forwards.)*


Your custom behavior has the parameters `self`, which give you
the object it runs on, and `dt` which is a number representing
a timing factor. Multiply your timing-dependent math with
`dt`, and your code will run the same no matter the frame rate!

Horse3D has many @{horse3d.behavior|predefined behaviors} like a
@{horse3d.behavior.humanoid_player|player for walking around} which you may,
or may not use, at your own discretion.


## Coordinate system

Objects in the game world are placed via X,Y,Z coordinates via
@{horse3d.world.object.set_position|object:set_position}.
The **Z coordinate** always points **upwards**, and from the default
startup camera angle a **positive X coordinate** will be **pointing into
the screen.** The Y coordinate then controls sidewards.

*Advanced technical note about different systems:*
**It is NOT recommended you try to use a different system in Horse3D.**
The default gravity will drag things towards a negative Z value, and the
@{horse3d.world.object.enable_movement|"character" movement type} will
only detect stairs and other complex ground obstacles correctly in Z
direction. Also, @{horse3d.world.object.set_rotation|object:set_rotation}'s
angle parameters assume Z is up as well. Therefore, deviating from this
coordinate system orientation will make things really difficult for you.

*Advanced technical note about 3d models:
all loaded up meshes are expected to use the more common
exchange format of Y pointing up, and will be automatically translated to
a Z up coordinate system when loaded.*


## Logic outside of behaviors

If you need logic to run continuously independently from a
specific object, please note a `while true ...` loop
**will not work**. In general, doing anything that blocks the
lua script will stop your entire game until it's done, so
a `while` loop would just lock up your game.

Instead, one way to do something continuously is a
@{horse3d.time.schedule|scheduled function}:

```lua
-- Define a function which will re-schedule itself:
local function do_continuously()
    -- Do some continuous math in here!

    -- Reschedule:
    horse3d.schedule(1/60, do_continuously)
end

-- Schedule the first function run:
horse3d.time.schedule(1/60, do_continuously)
```

As you can see, this will not *actually run non-stop* but
instead at very small intervals. The intervals are small enough
so that movement done like this will look smooth to the player
later. Beware: if you pick a smaller interval or do a very
complicated task scheduled like this, you may **overload the CPU.**
Therefore, pick a larger interval if your task doesn't
need to do smooth movement.


## Tags

If you load up a lot of objects, you may want to
tag them: use @{horse3d.world.object.add_tag|object:add_tag}
such that you can @{horse3d.world.get_objects_by_tag|query objects by tag,}
and in general access them in more organized ways.

Objects never get removed until you @{horse3d.world.object.destroy|destroy
them}, so tags are a great way to retrieve objects that are still in the
world that your code temporarily forgot about.
