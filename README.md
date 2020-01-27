
Horse3D
=======

Welcome to *Horse3D*, a **lua game engine.**

[![logo](misc/logo-readme.png)](https://horse3d.wobble.ninja)


- **Standalone.** Get it and start crafting!
  A text editor gets you started, no compilers required.

  This example is enough to make a simple game:
  ```lua
  local horse3d = require("horse3d")

  horse3d.scene.add_scene("demo-assets/monumentoutdoors.gltf")  -- the level!

  local player = horse3d.world.add_invisible_object(0.6, 1.7, {
      behavior=horse3d.behavior.humanoid_player()  -- the player!
  })
  ```

- **Simple.** Graphics, physics, and audio in one, easy to use.
  And it's [all well-documented!](
    https://horse3d.wobble.ninja/api/topics/01_Getting_Started.md.html
  )

- **Cross-platform.** Available for Windows and Linux.
  Android support upcoming.

- **Open.** *Horse3D* is, excluding its logo, all free software
  under BSD-like licensing. [Check the license here!](LICENSE.md)


Build Games
-----------

[Download Horse3D.](https://horse3d.wobble.ninja/#get)


Help & Docs
-----------

* [Documentation, read it!](
    https://horse3d.wobble.ninja/api/topics/01_Getting_Started.md.html
  )

  [![docs screenshot](misc/README_image_docs.png)](
    https://horse3d.wobble.ninja/api/topics/01_Getting_Started.md.html
  )

* Get advice and chat in [Discord](https://discord.gg/4ySSJs5)

  [![chat icon](misc/logo_README_chat.png)](https://discord.gg/4ySSJs5)


Advanced: Features in Detail
----------------------------

If you need more info first, here is a more detailed feature listing:

* **Language:** [Lua 5.3](https://lua.org) is integrated ready
  for your use. (check the
  [API](https://horse3d.wobble.ninja/api)!)

* **Rendering:** OpenGL ES 2/OpenGL 3.1, cross-platform.
  Runs on most older hardware, an Android port is planned.

* **3D Models:** OBJ, GLTF are supported for models,
  [.map](https://quakewiki.org/wiki/Quake_Map_Format), GLTF are
  supported for scene loading. (Scenes can e.g. contain light sources,
  and more.)

* **Audio:** mp3, flac, ogg can be used for streaming audio with
  the integrated decoders.

* **Physics:** [bullet physics](https://pybullet.org) is tightly integrated,
  with automatic use of its Ccd, and a rigid body character controller.

* **Filesystem:** [LuaFileSystem/lfs](
      https://keplerproject.github.io/luafilesystem/manual.html#reference
  ) was partially implemented for easy disk access, access it via
  `local lfs = require("lfs")`.
  Horse3D also has a VFS that allows packaging resource files as .h3dpak
  archives for easier shipping. (Rename a .zip to that ending to create one)

* **Distribution:** one-binary engine, with no dlls or install required.
  All resources can come in a .h3dpak (=ZIP), and otherwise you're good to go.


Advanced: Build it Yourself
---------------------------

Since Horse3D is [open-source](LICENSE.md), you can
[build it yourself by following the custom build instructions.](
https://horse3d.wobble.ninja/api/topics/90_Advanced_Custom_Build.md.html)
Please note this is an advanced task and in no way necessary to
make your game.
However, if you want to contribute to Horse3D itself, this might be your
first step to enable you to do so.
