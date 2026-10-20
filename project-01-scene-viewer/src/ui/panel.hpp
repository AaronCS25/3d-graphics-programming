// The Dear ImGui control panel: scene, timeline, light, view, the selected
// object (material, normals, simplification, Fast Marching) and performance.

#pragma once

#include "app.hpp"

// The panel fills a column of `width` x `height` window units on the left.
void draw_panel(App& app, int width, int height);
