# Environment setup

The whole repo builds with **CMake + Ninja + vcpkg** on every OS; only the
compiler and the vcpkg triplet differ. You need:

1. **git**
2. **CMake ≥ 3.21** and **Ninja**
3. A **C++17 compiler**
4. **vcpkg** cloned somewhere, with the `VCPKG_ROOT` environment variable
   pointing at it (the presets read `$env{VCPKG_ROOT}`).

You never call `vcpkg install` by hand: `vcpkg.json` at the repo root lists the
dependencies and CMake installs them automatically during configure.

---

## Windows (MinGW-w64 GCC — no Visual Studio required)

1. **Compiler + tools.** Any MinGW-w64 GCC ≥ 12 works (MSYS2 `ucrt64`, WinLibs,
   the one bundled with Strawberry Perl…). Make sure `gcc`, `g++`, `cmake`,
   `ninja` are on `PATH`:
   ```powershell
   gcc --version; cmake --version; ninja --version
   ```
2. **vcpkg.**
   ```powershell
   git clone https://github.com/microsoft/vcpkg $HOME\vcpkg
   & $HOME\vcpkg\bootstrap-vcpkg.bat
   [Environment]::SetEnvironmentVariable("VCPKG_ROOT", "$HOME\vcpkg", "User")
   ```
   Open a new terminal so `VCPKG_ROOT` is picked up.
3. **Build.** The `windows-mingw` preset uses the `x64-mingw-static` triplet
   (dependencies are linked statically — the resulting `.exe` has no DLLs to
   ship).
   ```powershell
   cmake --preset windows-mingw
   cmake --build --preset windows-mingw
   .\build\windows-mingw\task-01-hello-triangle\task01.exe
   ```

> Why not MSVC / Visual Studio? It works too (use a `windows-msvc` preset with
> the `x64-windows` triplet), but MinGW keeps the same toolchain family (GCC/
> Clang) as macOS/Linux and avoids a 10 GB install.

## macOS

1. **Compiler + tools.**
   ```sh
   xcode-select --install          # AppleClang
   brew install cmake ninja
   ```
2. **vcpkg.**
   ```sh
   git clone https://github.com/microsoft/vcpkg ~/vcpkg
   ~/vcpkg/bootstrap-vcpkg.sh
   echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.zshrc && source ~/.zshrc
   ```
3. **Build.**
   ```sh
   cmake --preset default
   cmake --build --preset default
   ./build/default/task-01-hello-triangle/task01
   ```

macOS only supports OpenGL up to 4.1 core and *requires* the forward-compat
hint — every task sets it:
```cpp
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
```

## Linux (Debian/Ubuntu)

```sh
sudo apt install build-essential cmake ninja-build git curl zip unzip tar \
     pkg-config libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
     libxi-dev libgl1-mesa-dev          # X11/GL headers needed to build GLFW
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.bashrc && source ~/.bashrc
cmake --preset default && cmake --build --preset default
```

---

## Editors / IDEs

- **VS Code:** install *C/C++* and *CMake Tools*. CMake Tools detects
  `CMakePresets.json` — pick the preset from the status bar, then Build/Run.
  `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so *clangd* also works out of the box.
- **CLion:** open the folder; it reads `CMakePresets.json` directly
  (Settings → Build → CMake → enable the preset you want).

## Debug builds

```sh
cmake --preset debug          # or windows-mingw-debug
cmake --build --preset debug
```
Each preset builds into its own `build/<preset>/` folder, so Release and Debug
never collide.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake` | `VCPKG_ROOT` is not set in this shell — open a new terminal or set it. |
| `find_package(glfw3)` fails | Configure did not run vcpkg. Delete `build/` and re-run `cmake --preset …`. |
| Window opens and closes instantly / black window | Check the console: shader compile/link errors are printed there. |
| Windows: `x64-mingw-static` triplet "community, less likely to succeed" warning | Informational only; GLFW and glad build fine with it. |
