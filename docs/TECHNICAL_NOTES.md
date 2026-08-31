# Technical Notes

## Target

- Platform: Windows, Win32/x86.
- Language: C++17 project with a small amount of compatibility-oriented x86-specific code.
- Rendering: Direct3D 8 compatible path.
- Audio: DirectSound / WinMM plus bundled Ogg/Vorbis decode support.

## Runtime data

The game uses relative paths for its original resources. Keep the executable's working directory at the repository/package root, where `objects.res`, `Maps/`, `Vid/`, `Wav/`, `Music/`, `Text/` and other data directories are located.

## Public-source cleanup

The public package excludes internal audit history, recovery prompts, generated ledgers and one-off validation scripts. Technical comments retained in `sources/` describe ABI, compatibility or runtime behavior only.
