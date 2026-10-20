// The two views of one surface.
//
// A render Mesh (task 12) splits a point of the surface into several vertices
// wherever its uv or normal changes, because the GPU needs one index per
// vertex. The CHE (tasks 03/04/06) needs the opposite: one vertex per point,
// shared by every triangle around it, or it sees seams as holes. The geometry
// algorithms run on the CHE; this file goes from one view to the other.

#pragma once

#include "che.hpp"
#include "obj_loader.hpp"

#include <vector>

struct Welded {
    CHE che;
    // For every vertex of the render mesh, the CHE vertex at the same point. A
    // value computed per CHE vertex (a distance, a normal) reaches the screen
    // through this map.
    std::vector<int> che_of_vertex;
};

// Merges the vertices of `mesh` that sit at exactly the same position, and
// keeps the triangles that still have three different corners: at a pole or
// a collapsed seam several corners merge into one.
Welded weld(const Mesh& mesh);

// A render mesh straight from a CHE: one vertex per CHE vertex with its
// area-weighted normal, no texture coordinates, one submesh with `material`.
// Used once the topology has changed (simplification) and the uv are gone.
Mesh mesh_from_che(const CHE& che, int material);

// A unit vector perpendicular to n, as a tangent for surfaces with no uv.
glm::vec3 any_tangent(const glm::vec3& n);
