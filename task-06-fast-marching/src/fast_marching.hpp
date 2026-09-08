// Fast Marching Method on a triangle mesh.
//
// Sethian 1996 (grids), Kimmel & Sethian 1998 (triangulated surfaces). Solves
// the eikonal equation |grad t| = 1 with t = 0 at the source: t(v) is the time
// at which a front expanding at unit speed over the surface reaches vertex v,
// which is its geodesic distance to the source.
//
// The structure is Dijkstra's: every vertex is FAR (untouched, t = inf), TRIAL
// (has a tentative t and sits in the min-heap) or ALIVE (t is final and never
// recomputed). What differs from Dijkstra is the local update — how a vertex
// gets its tentative t from already-ALIVE neighbours. That step lives in
// fast_marching.cpp and is the whole point of the task.
//
// The algorithm only reads the CHE; it does not belong to it.

#pragma once

#include "che.hpp"

#include <vector>

struct DistanceMap {
    std::vector<float> t;  // arrival time per vertex; +inf where the front never got
    float max_finite = 0.0f; // largest finite t, handy to normalise for a colormap
};

// Geodesic distance from `source` to every vertex of `mesh`.
DistanceMap fast_marching(const CHE& mesh, int source);
