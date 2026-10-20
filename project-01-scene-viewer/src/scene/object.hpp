// One object of the scene: what is drawn, the CHE the geometry algorithms run
// on, and how it moves.

#pragma once

#include "che.hpp"
#include "fast_marching.hpp"
#include "gpu_mesh.hpp"
#include "obj_loader.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Procedural motion, task 05's hierarchy generalised. Every frame
//
//   anchor = parent anchor * T(centre) * tilt * Ry(orbit_speed t + phase) * T(radius, 0, 0)
//            * T(0, bounce, 0)
//   model  = anchor * R(spin_axis, spin_speed t) * placement
//
// so an object orbits a point (or its parent), bounces and spins about its own
// centre. `placement` brings the mesh into its own frame: centred, scaled.
struct Motion {
    int parent = -1; // index of the object whose anchor this one orbits
    glm::vec3 centre{0.0f};
    float orbit_radius = 0.0f;
    float orbit_speed = 0.0f; // rad/s
    float orbit_phase = 0.0f; // rad
    float orbit_tilt = 0.0f;  // degrees, about z
    glm::vec3 spin_axis{0.0f, 1.0f, 0.0f};
    float spin_speed = 0.0f;    // rad/s
    float bounce_height = 0.0f; // |sin| hops, like a ball
    float bounce_speed = 0.0f;  // rad/s
    float bounce_phase = 0.0f;  // rad
};

enum class NormalSource {
    Loaded,     // from the file, or from the loader's crease-angle average
    Recomputed, // the CHE's area-weighted average over the whole star
    Flat,       // one per face, from screen-space derivatives in the shader
};

struct Object {
    std::string name;

    Mesh loaded; // as read or generated, kept to restore
    Mesh mesh;   // what is drawn now: loaded, or rebuilt from a simplified CHE
    GpuMesh gpu;

    CHE che; // the surface of `mesh`, welded
    std::vector<int> che_of_vertex;
    bool simplified = false;
    NormalSource normals = NormalSource::Loaded;

    std::vector<int> seeds; // CHE vertices where the fronts start
    DistanceMap distances;

    glm::mat4 placement{1.0f};
    Motion motion;
    glm::mat4 anchor{1.0f}; // updated every frame
    glm::mat4 model{1.0f};
};

// Welds `mesh` into the CHE. Called on creation and after every topology change.
void rebuild_che(Object& object);

// Runs the Fast Marching Method from the seeds and uploads the normalized
// distance of every drawn vertex (-1 where no front arrived). Returns the time
// the algorithm took, in milliseconds.
double update_distances(Object& object);

// Switches the normals of the drawn mesh and uploads them. Flat normals are
// computed in the shader, so the vertices are left as loaded.
void set_normals(Object& object, NormalSource source);

struct SimplifyResult {
    CHE::SimplifyReport report;
    CHE::Deviation deviation; // to the loaded surface
    int triangles_before = 0;
    double milliseconds = 0.0;
};

// QEM edge collapse (task 04) from the loaded surface down to `fraction` of its
// triangles. The texture coordinates do not survive a change of topology, so
// the result is drawn from the CHE with the colour of its first material.
SimplifyResult simplify(Object& object, float fraction);

// Back to the mesh as loaded.
void restore(Object& object);

// The bounding box of the mesh, in its own coordinates.
void mesh_bounds(const Mesh& mesh, glm::vec3& lo, glm::vec3& hi);
