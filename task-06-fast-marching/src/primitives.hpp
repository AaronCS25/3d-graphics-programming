// Generated test meshes. Kept apart from the CHE on purpose: the structure
// should not know how any particular surface is sampled, it only receives a
// geometry array and a vertex array like it would from a file.

#pragma once

#include "che.hpp"

// Parametric sphere P(u, v) = r (cos u sin v, sin u sin v, cos v), with u the
// azimuth over [0, 2pi) and v the colatitude over [0, pi].
//
// The seam at u = 2pi reuses the vertices of u = 0 and each pole is a single
// vertex, so the result is a closed manifold with no boundary half-edges.
CHE make_sphere(float radius, int slices, int stacks);

// Parametric torus P(phi, theta) = ((R + r cos theta) cos phi,
//                                   (R + r cos theta) sin phi,
//                                    r sin theta),
// phi around the big ring (major_segments samples), theta around the tube
// (minor_segments samples). Both directions wrap, so it is closed with genus 1:
// a mesh where "distance along the surface" is visibly not Euclidean distance.
CHE make_torus(float major_radius, float minor_radius, int major_segments, int minor_segments);
