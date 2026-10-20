// The textured shapes of task 11 (cube, sphere, floor), as task 12 meshes so
// they go through the same pipeline as a file. Each vertex has its uv and the
// tangent along which u grows.

#pragma once

#include "obj_loader.hpp"

// Cube centred at the origin. Each face is its own square of the image and its
// own submesh, in the order +x, -x, +y, -y, +z, -z, with materials
// first_material to first_material + 5.
Mesh make_cube(float side, int first_material);

// Unit sphere with the spherical mapping of the slides; the seam repeats the
// vertices of theta = 0 with u = 1.
Mesh make_sphere(int slices, int stacks, int material);

// Square of side `size` in the xz plane, facing +y, with the image repeated
// `tiles` times along each side. It is a grid of cells x cells squares rather
// than two triangles, so a distance map drawn on it has vertices to vary on.
Mesh make_floor(float size, float tiles, int cells, int material);
