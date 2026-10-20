// The demo scenes, and a scene around any file dropped onto the window.

#pragma once

#include "world.hpp"

#include <string>

struct Preset {
    const char* name;
    void (*build)(World&);
};

// The gallery of task 12 set in motion: Spot turns on its pedestal, the Phong
// spheres bounce and the glass one orbits the pedestal.
void build_gallery(World& world);

// The scene of tasks 05/11 (a cube with a material per face, the Earth and the
// Moon orbiting it) with task 06's cow on a turntable at the edge of the floor.
void build_solar(World& world);

// An OBJ or PLY file, scaled to fit, standing on a floor.
void build_from_file(World& world, const std::string& path);

extern const Preset kPresets[];
extern const int kPresetCount;
