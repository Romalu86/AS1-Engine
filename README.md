# AS1 Engine 1.22 (WIP, Currently unavailable.)

AS1 Engine is a source release of the reconstructed/reimplemented **Alien Shooter (2003)** engine together with the runtime game data required by this package.

The project targets **Windows / Win32 (x86)** and keeps compatibility with the original Direct3D 8, DirectSound and Ogg Vorbis based runtime.

## Important rights notice

**All rights to Alien Shooter game content belong to Sigma-Team.**

The source-code license in this repository does **not** grant any rights to the original game content. This includes, without limitation, graphics, textures, sprites, fonts, music, sound effects, maps, scripts, text, video, icons, configuration data, original resources and other Alien Shooter assets.

Alien Shooter, its game content and associated intellectual property remain the property of **Sigma-Team**. No ownership of that content is transferred by this repository.

See [LICENSE_GAME_CONTENT.md](LICENSE_GAME_CONTENT.md) and [LICENSE.md](LICENSE.md) before redistributing the package.

## Repository layout

- `sources/` — engine source code.
- `sources/3rdparty/` — bundled Ogg/Vorbis compatibility sources.
- `Font/`, `Maps/`, `Music/`, `Text/`, `Vid/`, `Wav/`, `cursores/` — original game data.
- `objects.res`, `strings.ini`, `AlienShooter.cfg` — runtime game resources/configuration.
- `AS1EngineClean.sln` — Visual Studio solution.
- `licenses/` — third-party license texts.
- `docs/ORIGINAL_GAME_README_RU.txt` — historical readme shipped with the supplied game data.

## Building

### Requirements

- Windows 10 or Windows 11.
- Visual Studio 2022 or a compatible MSVC installation with the **Desktop development with C++** workload.
- Windows SDK.
- Win32/x86 toolchain support.

### Build

1. Open `AS1EngineClean.sln` in Visual Studio.
2. Select `Release | Win32`.
3. Build the solution.
4. Run `AlienShooter.exe` with this repository root as its working directory, or place the executable in this root directory so that `objects.res`, `Maps/`, `Vid/`, `Wav/` and the other runtime data are available through the original relative paths.

The project generates the small Direct3D 8 import library from `sources/win/d3d8_import.def` automatically when it is missing.

## Runtime notes

The codebase intentionally targets original-game behavior rather than redesigning the game. It retains Win32/x86-specific behavior where required for compatibility.

Configuration and profile/save behavior follows the engine's original Windows conventions. Game data is loaded from the package by relative path, so the working directory matters.

## Licensing

This repository is a **mixed-license distribution**:

- Project-contributor source code: MIT, only to the extent the contributors are legally entitled to license those portions. See `LICENSE.md`.
- Alien Shooter game content: **not MIT**; all rights belong to **Sigma-Team**. See `LICENSE_GAME_CONTENT.md`.
- Bundled Xiph.Org Ogg/Vorbis portions: BSD-3-Clause. See `THIRD_PARTY_NOTICES.md` and the license texts under `licenses/`.

The source license must not be interpreted as a license to Sigma-Team assets, trademarks, game content, or any other third-party material.

## Redistribution

Before publishing or redistributing a package that contains the bundled Alien Shooter game data, make sure you have permission from Sigma-Team or another valid legal basis to redistribute that content. The notice in this repository records ownership; it does not itself create redistribution permission.

