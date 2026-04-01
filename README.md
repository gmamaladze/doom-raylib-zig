# DOOM on macOS (Raylib + Zig build system)

A port of the original DOOM released by id Software in 1997 to modern macOS Apple Silicon using Raylib for graphics/audio and Zig as the build system.

The original source code is untouched. All platform-specific code lives in `macdoom/`

<img width="2600" height="1944" alt="CleanShot 2026-03-28 at 19 48 17@2x" src="https://github.com/user-attachments/assets/d833044d-9ab6-43d8-bd46-f3e3a9cbdb2c" />


## Status

Working:
- Rendering (320x200 software renderer → Raylib texture)
- Input (WASD + arrows, mouse)
- Sound effects (8-channel software mixer → Raylib AudioStream)
- Music (MUS→MIDI conversion, playback via macOS AudioToolbox DLS synthesizer)
- Single player

Not yet implemented:
- Multiplayer networking
- Fullscreen / Retina support

## Install via Homebrew

```
brew install --formula Formula/macdoom.rb
```

Place your WAD files in `~/.local/share/doom/` and run:

```
macdoom
```

## Building from Source

Requirements:
- macOS 12+ (Apple Silicon or Intel)
- [Zig](https://ziglang.org/) 0.15+
- [Raylib](https://www.raylib.com/) 5.x (`brew install raylib`)
- `doom1.wad` (shareware) in the `macdoom/` directory

```
cd macdoom
zig build
./zig-out/bin/macdoom
```

The shareware WAD (`doom1.wad`, 4MB) is freely distributable but not included in this repo.

You can also specify custom raylib paths (e.g. on Intel Macs):

```
zig build -Draylib-include-path=/usr/local/include -Draylib-lib-path=/usr/local/lib
```

## Controls

| Key | Action |
|-----|--------|
| W / Up | Move forward |
| S / Down | Move backward |
| A / Left | Turn left |
| D / Right | Turn right |
| F / Ctrl | Fire |
| Space | Use / Open doors |
| Shift | Run |
| Alt | Strafe (hold + A/D) |
| Escape | Menu |
| Tab | Automap |
| 1-9 | Switch weapons |

## Architecture

```
linuxdoom-1.10/           Original source (untouched)
  d_main.c, g_game.c       Game loop, state machine
  r_*.c                     BSP renderer
  p_*.c                     Physics, AI, collisions
  w_wad.c, z_zone.c        WAD loader, zone memory
  i_video.c, i_sound.c     Platform layer (Linux/X11) ← replaced
  ...

macdoom/
  build.zig                 Zig build script
  src/platform/
    i_video_rl.c            Raylib window, framebuffer blit, input
    i_sound_rl.c            Software mixer → Raylib AudioStream
    i_system_rl.c           Timing (gettimeofday), memory (malloc)
    i_net_rl.c              Single-player network stub
    i_main_rl.c             Entry point
  src/compat/
    m_misc_compat.c         64-bit default_t fix (int → intptr_t)
    z_zone_compat.c         64-bit zone allocator alignment
    r_data_compat.c         64-bit texture loading fixes
    r_draw_compat.c         64-bit translation table alignment
    p_setup_compat.c        64-bit line buffer allocation
    g_game_compat.c         Game logic (clean copy)
    values.h, malloc.h      Header shims for macOS
```

DOOM's platform abstraction is clean the game logic (`D_*`, `G_*`, `P_*`, `R_*`, `S_*`, `W_*`, `Z_*`) calls into 5 `I_*` interface functions for video, sound, timing, input, and networking. This port replaces only those 5 files.

## 64-bit porting notes

The original code assumes `sizeof(int) == sizeof(void*)`. On 64-bit macOS, pointers are 8 bytes but int is 4. The issues found and fixed:

- **Pointer-to-int casts in static initializers** (`m_misc.c`): The `default_t` struct stores string pointers as `int`, which truncates on 64-bit and isn't a compile-time constant in Clang.
- **Zone allocator alignment** (`z_zone.c`): Aligned to 4 bytes (32-bit), needs 8 for 64-bit pointer fields in `memblock_t`.
- **WAD struct layout** (`r_data.c`): `maptexture_t` has a `void**` field that's 8 bytes on 64-bit but the WAD binary format has 4 bytes, shifting all subsequent fields.
- **Hardcoded `*4` allocations** (`r_data.c`, `p_setup.c`): Arrays of pointers allocated as `count*4` instead of `count*sizeof(pointer)`.
- **Pointer alignment casts** (`r_data.c`, `r_draw.c`): `(int)pointer & ~0xff` truncates 64-bit addresses. Fixed with `(intptr_t)`.

## License

The original DOOM source is released under the GNU GPL v2 by id Software. See `LICENSE.TXT`.
