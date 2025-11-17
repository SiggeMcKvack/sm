# Requirements
  * A Super Metroid rom (Make sure to rename it to `sm.smc`)
  * CMake 3.15 or newer
  * SDL2 development libraries
  * C compiler (GCC, Clang, or MSVC)
  * Super Metroid repo `git clone --recursive https://github.com/snesrev/sm`

## Installing SDL2

Install SDL2 development libraries for your OS:
 * Ubuntu/Debian: `sudo apt install libsdl2-dev`
 * Fedora Linux: `sudo dnf in sdl2-devel`
 * Arch Linux: `sudo pacman -S sdl2`
 * macOS: `brew install sdl2`
 * Windows: See platform-specific instructions below

# Building with CMake

## Linux/macOS

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)  # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS
```

The executable will be created at `./sm` in the project root.

For Debug builds:
```sh
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Windows (Visual Studio)

CMake can generate Visual Studio projects:

```cmd
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Or open the generated `build/sm.sln` in Visual Studio.

## Windows (MSYS2/MinGW)

In MINGW64 terminal:
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j16
```

## Cleaning

To clean and rebuild:
```sh
rm -rf build       # or: rmdir /s build on Windows
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

# Nintendo Switch

Dependencies and requirements:

  * The `switch-sdl2` library
  * [DevKitPro](https://github.com/devkitPro/installer)
  * [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere)
  
1. Make sure you've installed Atmosphere on your Switch.
2. Please download the DevKitPro version of MSYS2 through their installer, as the default MSYS2 causes issues with windows compiling.
3. Now that you've installed DevKitPro, open up the location you've installed DevKitPro to, then find `mingw64.exe` inside `msys2` located in `devkitPro` folder.
4. Type `pacman -S git switch-dev switch-sdl2 switch-tools` in the terminal to install the `switch-sdl2` library.
5. CD to `switch` folder by typing `cd src/platfrom/switch` in the terminal on the `sm` root folder.
6. type `make` to compile the Switch Port.
7. Transfer the `.ini`, `nro`, `ncap` and your rom file to the Switch.

**OPTIONAL STEP**

```sh
make -j$(nproc) # To build using all cores
```
