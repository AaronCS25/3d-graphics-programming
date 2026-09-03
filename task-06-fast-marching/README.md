# Task 06 — Fast Marching on triangle meshes

> Implement the Fast Marching algorithm on triangular meshes and test it on your
> generated models (sphere, torus, etc.) and on meshes like the bunny.
>
> The implementation is similar to Dijkstra; the update step is the key
> difference. Check only section 3 of the reference paper:
> https://dl.acm.org/doi/epdf/10.1145/1409625.1409626
>
> Submit an image showing your result, and update your repository.
>
> For displaying the distance map with a color map, you can choose a color map
> from https://github.com/kbinani/colormap-shaders.

**Status:** 🚧 in progress

## Result

_(pending)_

## Approach

_(pending — the base so far: viewer, PLY loading into the L1 CHE, generated
sphere and torus)_

## Layout

```
src/
├── che.hpp / che.cpp            L1 CHE from task 04, data structure only
├── primitives.hpp / .cpp        generated test meshes: make_sphere, make_torus
├── mesh_io.hpp / .cpp           load_ply(): happly -> centred, triangulated CHE
└── main.cpp                     viewer: window, orbit camera, drag-and-drop
models/                          bunny.ply (Stanford), cow.ply
```

## Controls

| Key / mouse | Action |
|-----|--------|
| Drop a `.ply` on the window | Load it |
| Drag / scroll | Orbit the camera · zoom |
| `1` / `2` | Solid · wireframe |
| `ESC` | Quit |

## Run

```sh
cmake --build --preset <preset> --target task06
./build/<preset>/task-06-fast-marching/task06                      # sphere
./build/<preset>/task-06-fast-marching/task06 torus
./build/<preset>/task-06-fast-marching/task06 models/bunny.ply
./build/<preset>/task-06-fast-marching/task06 sphere models/bunny.ply --check   # stats only
```

## Notes / what I learned

- Meshes downloaded from the internet are not always manifold. The dragon and
  the airplane from the Stanford / Burkardt collections have edges shared by
  three triangles, which leaves the opposite array one-directional after the
  first-match gluing. `build_opposites` now breaks any link with
  `O[O[he]] != he`, so such edges become boundaries and `star()` always
  terminates.
