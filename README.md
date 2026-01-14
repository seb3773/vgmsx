# vgMSX Player

**vgMSX** is a specialized VGM/VGZ player dedicated to the MSX platform, optimized for Linux.
It is based on the [vgmplay-legacy](https://github.com/vgmrips/vgmplay-legacy) codebase but heavily stripped down and refactored for a strictly focused usage.

## Key Differences & Features

Unlike standard VGM players that support a huge variety of chips from many different systems, **vgMSX** focuses exclusively on the MSX ecosystem.

-   **MSX DNA**: All non-MSX sound chips have been stripped. The core is optimized specifically for chips found in MSX hardware (AY-3-8910, YM2413, YM2151, K051649, etc.).
-   **Linux Only**: The codebase is cleaned and optimized for the Linux environment (x86_64), using standard POSIX features and ALSA for audio output.
-   **Pure Audio**: No WAV logging or complex export features. It just plays music.
-   **Smart Playlist**: No support for `.m3u` files. Instead, playlists are automatically generated when loading a directory or an archive.
-   **Retro TUI**: A refreshed Text User Interface with a typical retro look inspired by the MSX boot sequence:
    -   Simplified controls.
    -   Playing time and progress bar.
    -   **Integrated Playlist View** (Press `ENTER` to toggle).
    -   Boot animation and "MSX Basic" style aesthetic.

## Supported Inputs

You can pass a single file, a folder, or an archive as an argument:

```bash
./vgmsx path/to/music.vgz
./vgmsx path/to/folder/
./vgmsx music_pack.zip
```

### Supported Archive Formats
The player can natively handle archives (extracting them transparently to a temporary folder). Supported extensions include:

-   `.zip`, `.jar`
-   `.7z`
-   `.rar`
-   `.tar`, `.tar.gz`, `.tgz`
-   `.tar.bz2`, `.tbz2`
-   `.tar.xz`, `.txz`
-   `.tar.lzma`
-   `.tar.zst`
-   `.tar.lz4`
-   `.tar.zlf` (Packaged with zELF)
-   `.gz`, `.bz2`, `.xz`, `.lzma`, `.zst` (Standalone compressed files)

> **Note**: This relies on system tools. Ensure you have the relevant decompressors installed (e.g., `zelf`, `unzip`, `7z`, `tar`, `unrar`, `zstd`, `lz4`).

## Controls

| Key | Action |
| :--- | :--- |
| **Space** | Pause / Resume |
| **Right / Left** | Seek Forward / Backward (5s) |
| **Up / Down** | Next / Previous Track |
| **ENTER** | Toggle Playlist View |
| **ESC** | Quit |

## Screenshots

![vgMSX Playlist](https://github.com/seb3773/vgmsx/raw/main/screenshots/screenshot_1.jpg)  
*Playback view with dynamic progress bar and metadata*

![vgMSX Playback](https://github.com/seb3773/vgmsx/raw/main/screenshots/screenshot_2.jpg)  
*Playlist view with MSX-style scrolling and selection*

## MSX VGM Music Resources

Here are some great sources for MSX VGM/VGZ music:

-   [File-Hunter.com VGM Archive](https://download.file-hunter.com/Music/VGM/)
-   [VGMRips - MSX](https://vgmrips.net/packs/system/ascii/msx)
-   [VGMRips - MSX2](https://vgmrips.net/packs/system/ascii/msx2)
-   [VGMRips - MSX2+](https://vgmrips.net/packs/system/ascii/msx2plus)
-   [VGMRips - MSX Turbo R](https://vgmrips.net/packs/system/ascii/msx-turbo-r)  

## WebAssembly Version
Test the WebAssembly version of **vgMSX** here --> [https://seb3773.github.io/vgmsx/](https://seb3773.github.io/vgmsx/)

  
