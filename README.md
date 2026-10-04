# Alien Shooter Engine 1.20

A clean Visual Studio 2022 Win32/x86 source project for **Alien Shooter 1.20 (GOG)**, intended for preservation, maintenance, study and source-level modification of the original game engine.

This repository contains the engine and game source code, Visual Studio project files, the Win32 resource payload required by the executable, and the bundled Win32 Ogg/Vorbis dependencies used by the project. It does **not** include the original Alien Shooter game data required to run the game.

## Requirements

- Windows.
- Visual Studio 2022 with the **Desktop development with C++** workload.
- **MSVC v143** toolset.
- **Windows 10 SDK 10.0.19041.0**.
- Win32/x86 build support.
- A legally obtained **Alien Shooter 1.20 (GOG)** installation for runtime game data.

## Build

1. Open `AlienShooter.sln` in Visual Studio 2022.
2. Select `Release | Win32` for the normal build, or `Debug | Win32` for a debug build.
3. Build the `AlienShooter` project.

The project uses the following output directories:

- Release: `o\Release\`
- Debug: `o\Debug\`
- Intermediate files: `b\Release\` or `b\Debug\`

The generated executable is `AlienShooter.exe`.

The build creates the required Win32 `d3d8.lib` import library from `sources\win\imports\d3d8.def` in the intermediate directory. No separate Direct3D 8 import library needs to be added to the repository.

The required Win32 static Ogg/Vorbis libraries are included under `sources\3rdparty\win\xiph\lib\Win32\Release\`.

## Running

The source release does not contain the original **Alien Shooter** game data, maps, music, sounds or other game assets.

For runtime testing, use a legally obtained Alien Shooter 1.20 GOG installation and place the built `AlienShooter.exe` where it can access that installation's game data. Keeping a backup of the original executable is recommended.

## Repository layout

- `sources\core\` — application, configuration, resource, stream, string, logging and shared runtime services.
- `sources\graphics\` — graphics primitives, gamma, textures and rendering support.
- `sources\images\` — image and picture handling.
- `sources\vid\` — VID loading and software/hardware rendering paths, surfaces, lighting and fonts.
- `sources\sound\` — shared sound-engine code.
- `sources\script\` — script runtime structures, action constants and native command definitions.
- `sources\game\` — shared game startup definitions.
- `sources\win\` — Win32 application layer, entry point, imports, resources and platform sound code.
- `sources\3rdparty\win\` — bundled third-party Ogg/Vorbis headers, licenses and Win32 static libraries.
- `sources\*.cpp` / `sources\*.h` — Alien Shooter game and engine systems, including maps, sprites, units, creatures, buildings, weapons, input, menus and related runtime logic.
- `AlienShooter.sln` / `AlienShooter.vcxproj` — Visual Studio 2022 solution and project.
- `LICENSE.md` — licensing and rights notice for this source package and bundled content.

## Third-party components

The project includes components from **libogg** and **libvorbis**. Their license texts are retained in:

- `sources\3rdparty\win\libogg\COPYING`
- `sources\3rdparty\win\libvorbis\COPYING`

Those components remain subject to their respective licenses.

## Game content

This repository does not grant rights to Alien Shooter game data, artwork, audio, maps, story content, trademarks, logos or other copyrighted game assets.

The included Win32 resource payload required by the executable remains subject to the rights of the relevant copyright holder.

## License

See [LICENSE.md](LICENSE.md) for the licensing and rights notice supplied with this source package.

Alien Shooter names, game data, artwork, audio and related game content remain the property of their respective rights holders. Third-party components remain under their own license terms.
