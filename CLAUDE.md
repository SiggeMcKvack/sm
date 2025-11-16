# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a decompilation/reverse engineering project of Super Metroid (SNES). The codebase recreates the game's logic in C, running alongside the original ROM to verify correctness through frame-by-frame comparison. When a mismatch is detected, it saves a snapshot and displays a countdown timer from 300.

**Required ROM**: `sm.smc` (SHA1: da957f0d63d14cb441d215462904c4fa8519c613) must be placed in the root directory.

## Build Commands

### Standard Build (Linux/macOS)
```bash
make                # Basic build
make -j$(nproc)     # Parallel build using all cores
make clean all      # Clean and rebuild
CC=clang make       # Use specific compiler
```

### Windows (MSYS2/MinGW64)
```bash
make                # Basic build
make -j16           # Parallel build (adjust thread count)
```

### Windows (Visual Studio)
- Open `sm.sln` in Visual Studio 2022
- Change build target from `Debug` to `Release`
- Build solution

### Nintendo Switch
Navigate to `src/platform/switch/` and run:
```bash
make                # Basic build
make -j$(nproc)     # Parallel build
```

## Code Architecture

### File Organization

The codebase is organized into banks matching the original SNES ROM memory layout:

- **`src/sm_80.c` through `src/sm_b4.c`**: Game logic files organized by SNES memory bank (0x80-0xB4). Each bank contains different game systems and functions.
- **`src/snes/`**: SNES hardware emulation layer
  - `cpu.c/h`: 65816 CPU emulation
  - `ppu.c/h`: Picture Processing Unit (graphics)
  - `apu.c/h`, `spc.c/h`, `dsp.c/h`: Audio processing
  - `dma.c/h`: Direct Memory Access
  - `cart.c/h`: Cartridge handling
  - `input.c/h`: Controller input
- **`src/main.c`**: Entry point, SDL integration, rendering loop
- **`src/sm_cpu_infra.c`**: Frame comparison infrastructure between reimplementation and original ROM
- **`src/sm_rtl.c`**: Runtime library functions
- **`src/config.c/h`**: Configuration file (sm.ini) parsing
- **`src/spc_player.c/h`**: SPC audio playback
- **`src/funcs.h`**: Master function declarations (extensive)
- **`src/variables.h`**: Global game state variables
- **`src/enemy_types.h`**: Enemy/entity type definitions
- **`src/ida_types.h`**: Type definitions from IDA Pro disassembly
- **`third_party/`**: External dependencies (TCC, SDL2, OpenGL)

### Key Architecture Patterns

1. **Dual Execution Model**: The engine can run both the C reimplementation and compare against the original ROM execution frame-by-frame (controlled by `g_runmode` in `sm_cpu_infra.c`).

2. **Bank-Based Code Organization**: Functions are organized by SNES memory bank (0x80-0xB4), preserving the original game's code structure. This is critical for understanding function locations.

3. **SNES Type System**: Uses custom typedefs (`uint8`, `uint16`, `uint32`, `Pair`) defined in `types.h` to match SNES data types.

4. **SDL2 Integration**: The main loop uses SDL2 for windowing, input, and audio. Three rendering backends are supported: SDL, SDL-Software, and OpenGL.

5. **Configuration**: Runtime behavior is controlled via `sm.ini` including graphics options (renderer, scaling, Mode7 enhancements), audio settings, and key bindings.

### Important Naming Conventions

- **`eproj`**: Enemy projectiles (renamed from `enemy_projectile`)
- Bank-prefixed files: `sm_XX.c` where XX is the hexadecimal bank number (e.g., `sm_80.c` = bank 0x80)

## Widescreen Support

The codebase includes widescreen aspect ratio support similar to the zelda3 implementation.

### Configuration (sm.ini)

```ini
# Widescreen aspect ratio support
ExtendedAspectRatio = 16:9
```

**Supported aspect ratios:**
- `4:3` - Standard SNES (default, no widescreen)
- `16:9` - Widescreen (adds ~70 pixels per side at 224 height)
- `16:10` - Widescreen alternative (~51 pixels per side)
- `18:9` - Ultra-wide (~85 pixels per side)

**Modifiers (comma-separated):**
- `extend_y` - Use 240 scanlines instead of 224 (full SNES vertical resolution)
- `unchanged_sprites` - Keep original sprite spawn/despawn boundaries (disables `kFeatures0_ExtendScreen64`)
- `no_visual_fixes` - Disable widescreen-specific visual fixes (disables `kFeatures0_WidescreenVisualFixes`)

**Examples:**
```ini
ExtendedAspectRatio = extend_y, 16:9
ExtendedAspectRatio = 16:9, unchanged_sprites
```

### Architecture

**Key files:**
- `src/features.h` - Feature flags (`kFeatures0_ExtendScreen64`, `kFeatures0_WidescreenVisualFixes`)
- `src/config.c` - INI parsing and aspect ratio calculation (lines 375-418)
- `src/snes/ppu.h` - PPU constants (`kPpuExtraLeftRight`, `kPpuXPixels`) and fields (`extraLeftCur`, `extraRightCur`, `extraLeftRight`)
- `src/main.c` - Dynamic pixel buffer allocation and PPU initialization (lines 340-348, 403-411)

**How it works:**
1. INI parser calculates extra pixels using formula: `(height × aspect_width / aspect_height - 256) / 2`
2. Pixel buffers are dynamically allocated to accommodate maximum width (256 + 96*2 = 448 pixels)
3. PPU `extraLeftCur` and `extraRightCur` control visible widescreen area (can be adjusted per-frame)
4. Maximum compile-time limit: 96 pixels per side (`kPpuExtraLeftRight`)
5. Feature flags control sprite boundary extensions and visual fixes

**Status:**
- ✅ Basic widescreen rendering
- ✅ Dynamic per-room adjustment (adjusts based on scroll limits - prevents rendering outside valid room boundaries)
- ⏹ Sprite spawn/despawn boundary adjustments (infrastructure ready via `kFeatures0_ExtendScreen64`, specific sprite code adjustments TBD based on testing)
- ⏹ Widescreen-specific visual fixes (infrastructure ready via `kFeatures0_WidescreenVisualFixes`, specific fixes TBD based on testing)

**Implementation details:**
- Dynamic adjustment: `ConfigurePpuSideSpace()` in `src/sm_cpu_infra.c:1065` checks scroll limits each frame
- Sprite boundaries: Use `enhanced_features0 & kFeatures0_ExtendScreen64` to check if sprite boundary extension is enabled
- Visual fixes: Use `enhanced_features0 & kFeatures0_WidescreenVisualFixes` to conditionally apply widescreen fixes

### Debug Output

Enable console debug output for bug reporting:

```ini
[General]
DebugDisplay = 1
```

**Output format (once per second):**
```
[Frame:3600 Room:0x91F8 Area:0(Crateria) ScrollX:512(256-1024) ScrollY:256(0-512) WS:L70/R70/96 Samus:(640,384)]
```

**Fields:**
- `Frame` - Frame counter since startup
- `Room` - Room index in hex (use this for bug reports)
- `Area` - Area index and name (0=Crateria, 1=Brinstar, 2=Norfair, 3=WreckedShip, 4=Maridia, 5=Tourian, 6=Ceres, 7=Debug)
- `ScrollX/Y` - Current scroll position (min-max range)
- `WS` - Widescreen state: L=left pixels, R=right pixels, last number=max configured
- `Samus` - Samus X,Y position

## Development Guidelines

- This is a reverse engineering project - maintain compatibility with the original ROM's behavior
- Changes should not break the frame-by-frame comparison unless intentionally adding enhancements
- The code is explicitly noted as "messy" (early version) - refactoring should preserve functionality
- When adding features, consider the configuration system in `sm.ini` for toggleable enhancements

🦎
