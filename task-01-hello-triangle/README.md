# Task 01 — Hello Triangle

> Follow the tutorial presented in <https://learnopengl.com/Getting-started/Hello-Triangle> to render a triangle.
> Create a GitHub repository to add your solutions of the different tasks. Submit the repository link.

**Status:** 🚧 in progress

## Result

<!-- ![screenshot](screenshot.png) -->

## What it does

Opens an 800×600 window with an OpenGL 3.3 core context and renders a single
colored triangle: vertex + fragment shader, one VBO with three vertices,
one VAO describing the layout, `glDrawArrays(GL_TRIANGLES, 0, 3)`.

## Run

```sh
cmake --build --preset <preset> --target task01
./build/<preset>/task-01-hello-triangle/task01
```

## Notes / what I learned

- 
