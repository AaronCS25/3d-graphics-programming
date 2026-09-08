# 3D Graphics Programming — UTEC

Weekly tasks for the **3D Graphics Programming** course at UTEC (2026-2).
Modern OpenGL (3.3 core) in C++17, one self-contained folder per task, one
shared build system.

| Task | Topic | Status |
|------|-------|--------|
| [01](task-01-hello-triangle/) | Hello Triangle — first window, shaders, VBO/VAO | ✅ |
| [02](task-02-sierpinski-triangle/) | Sierpinski triangle — chaos game + recursive subdivision | ✅ |
| [03](task-03-sphere/) | Sphere — CHE (L1) half-edge mesh, parametric sphere, EBO rendering | ✅ |
| [05](task-05-transformations/) | Animated scene — spinning cube, orbiting sphere and moon, model matrices | ✅ |
| [06](task-06-fast-marching/) | Fast Marching — geodesic distance map on the CHE, PLY input, colormap | ✅ |

Legend: ✅ done · 🚧 in progress · ⬜ not started

## Stack

| | |
|---|---|
| Language | C++17 |
| Graphics API | OpenGL 3.3 core profile |
| Windowing / input | [GLFW](https://www.glfw.org/) |
| Function loader | [glad](https://glad.dav1d.de/) |
| Math | [glm](https://github.com/g-truc/glm) |
| Build | [CMake](https://cmake.org/) ≥ 3.21 + [Ninja](https://ninja-build.org/) |
| Dependencies | [vcpkg](https://vcpkg.io) (manifest mode, `vcpkg.json`) |
| Compilers | GCC (MinGW-w64) on Windows · AppleClang on macOS · GCC/Clang on Linux |

## Quick start

Prerequisites: git, CMake, Ninja, a C++17 compiler and vcpkg with `VCPKG_ROOT`
set — see [docs/SETUP.md](docs/SETUP.md) for a per-OS walkthrough.

```sh
git clone https://github.com/AaronCS25/3d-graphics-programming.git
cd 3d-graphics-programming

# macOS / Linux
cmake --preset default
cmake --build --preset default

# Windows (MinGW-w64 GCC)
cmake --preset windows-mingw
cmake --build --preset windows-mingw
```

The first configure lets vcpkg download and build the dependencies (a couple of
minutes); afterwards it is instant. Every task is a separate executable:

```sh
# macOS / Linux
cmake --build --preset default --target task02      # build one task only
./build/default/task-02-sierpinski-triangle/task02  # run it

# Windows (MinGW-w64 GCC)
cmake --build --preset windows-mingw --target task02
./build/windows-mingw/task-02-sierpinski-triangle/task02.exe
```

## Repository layout

```
.
├── CMakeLists.txt          # root: finds deps once, adds every task-* folder
├── CMakePresets.json       # default (mac/linux) · windows-mingw · debug variants
├── vcpkg.json              # all dependencies, declared once
├── docs/SETUP.md           # environment setup per OS
├── _template/              # copy me to start a new task
└── task-NN-short-name/
    ├── README.md           # statement · result · approach · controls · notes
    ├── CMakeLists.txt      # add_executable + link gfx_common
    ├── docs/               # screenshots referenced by the README
    └── src/                # main.cpp, plus any task-specific headers/sources
```

Every task links against the `gfx_common` interface target defined in the root
`CMakeLists.txt`, which carries GLFW, glad and the warning flags. Adding a
library for a future task (e.g. `glm`, `stb`, `assimp`) means adding it to
`vcpkg.json` and to `gfx_common` — nothing else changes.

## Adding a new task

```sh
cp -r _template task-03-short-name
# rename target `taskNN` -> `task03` in task-03-short-name/CMakeLists.txt
# add `add_subdirectory(task-03-short-name)` to the root CMakeLists.txt
# fill in README.md, write src/main.cpp, add a screenshot to docs/ when done
```

## Conventions

- One folder per task, named `task-NN-short-name`; one executable per task, named `taskNN`.
- Each task README follows the same template: statement → result (screenshot) → approach → controls → run → notes.
- Code is formatted with `.clang-format` (LLVM base, 4 spaces, 100 cols).
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/) with the task as scope:
  `feat(task-02): add chaos game point generation`, `docs(task-01): add screenshot`, `chore: bump glfw`.

## References

- [LearnOpenGL](https://learnopengl.com/) — the tutorial series the course follows.
- [docs.gl](https://docs.gl/) — OpenGL API reference.
- [Interactive Computer Graphics (Angel & Shreiner)](https://www.cs.unm.edu/~angel/) — the Sierpinski chaos-game exercise comes from here.
