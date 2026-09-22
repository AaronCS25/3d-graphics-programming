# Shared camera

`camera.hpp` is a header-only `camera` class with a GLM dependency. It owns the
view/projection matrices, quaternion orientation, arcball drag, zoom, reset and
animated/manual switching. It does not depend on GLFW or OpenGL.

Every task already links `gfx_common`, which exposes `gfx_camera`, so all of them
can include it without changing their CMake files. Tasks 07 and 08 use it now;
the earlier exercises keep their original controls.

```cpp
#include <camera.hpp>

camera cam; // target = origin, animated orbit radius = 10, initial height = 3.5
// A smaller model can use: camera cam(glm::vec3(0), 4.0f, 1.5f, 0.5f);

cam.set_animated(true);
cam.update(deltaSeconds); // omit this while the animation is paused
glm::mat4 view = cam.view();
glm::mat4 projection = cam.projection(framebufferWidth, framebufferHeight);
glm::vec3 eye = cam.position(); // also useful for lighting and picking
```

Forward input from the application's callbacks:

| Input | Camera operation |
|-------|------------------|
| Left press | `cam.begin_drag(x, y, windowWidth, windowHeight)` |
| Cursor motion | `cam.drag(x, y)` |
| Left release / focus lost / window resize | `cam.end_drag()` |
| Vertical scroll | `cam.zoom(yoffset)` |
| Toggle animated/manual | `cam.set_animated(!cam.animated())` |
| Reset pose and animation phase | `cam.reset()` |

Use **window coordinates** for both the cursor and the arcball dimensions. Use
**framebuffer pixels** for the projection and OpenGL viewport. Mixing them makes
mouse sensitivity depend on display scaling.

Dragging or scrolling switches to manual mode at the current pose. Enabling
animation blends back to the orbit over 0.6 seconds with quaternion `slerp`,
preserving the zoom scale. Reset keeps the selected mode. Positive scroll moves
closer; distance is clamped to 0.5–50. Projection uses a 45° vertical field of view
and clipping planes at 0.1/100, suitable for the normalized models in this repo.

## Verification

The tests need only GLM, with no window or graphics context:

```sh
cmake --build --preset windows-mingw --target camera_tests
ctest --test-dir build/windows-mingw --output-on-failure
```

Use the corresponding preset/build directory on another OS. Tests cover the
arcball rotation direction, returning to the press position, event-rate and size
independence, opposite rim points, repeated rotations, camera frame invariants,
scroll limits, mode transitions and the task 07 orbit. Configure with
`-DBUILD_TESTING=OFF` to omit the test executable.
