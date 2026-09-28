// Loading meshes from disk into the CHE.
//
// The PLY parsing itself is delegated to happly (nmwsharp/happly, MIT), a single
// header that understands ASCII and binary PLY. What this file adds is the
// glue: PLY faces may be polygons, the CHE wants triangles; and the model may
// live at any scale, the viewer wants it centred inside a unit-ish box.

#pragma once

#include "che.hpp"

#include <string>

// Reads a PLY file and returns it as an L1 CHE. Throws std::runtime_error with
// a readable message if the file is missing or malformed.
CHE load_ply(const std::string& path);
