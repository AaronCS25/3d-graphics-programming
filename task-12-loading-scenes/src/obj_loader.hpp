// Loading Wavefront OBJ scenes and their MTL materials.
//
// The text parsing is delegated to tinyobjloader (tinyobjloader/tinyobjloader,
// MIT): it reads the v / vt / vn lists, the faces and every newmtl block of the
// libraries named by mtllib. What this file adds is the step the GPU needs
// after that, the one the slides call "de-index the mesh": a face corner in an
// OBJ is a v/vt/vn tuple of three independent indices, while a vertex buffer
// has a single index per vertex. Every distinct tuple becomes one vertex.
//
// Nothing here touches OpenGL, so the result can be inspected (--check) or
// processed on the CPU before it is uploaded.

#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

// A vertex of the vertex buffer: one distinct v/vt/vn tuple of the file.
// tangent.xyz is the direction in which u grows along the surface and
// tangent.w (+1 or -1) says whether v grows along N x T or against it, which
// flips where the texture is mirrored.
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

// One newmtl block. Colours and exponents are the Phong coefficients of the
// slides; the maps are paths already resolved against the MTL's directory,
// empty when the material has none.
struct Material {
    std::string name;
    glm::vec3 ambient{0.0f};  // Ka
    glm::vec3 diffuse{0.8f};  // Kd
    glm::vec3 specular{0.0f}; // Ks
    float shininess = 1.0f;   // Ns, the Phong exponent, [0, 1000]
    float opacity = 1.0f;     // d (Tr = 1 - d)
    std::string diffuse_map;  // map_Kd
    std::string specular_map; // map_Ks
    std::string bump_map;     // norm, or map_Bump / bump: normal map or height map
    float bump_scale = 1.0f;  // -bm option of map_Bump
};

// The triangles of a mesh that share a material: a range of its index buffer,
// drawn with one draw call. material is -1 for faces without usemtl.
struct Submesh {
    int first_index = 0;
    int index_count = 0;
    int material = -1;
};

// One object (o) or group (g) of the file.
struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
    std::vector<Submesh> submeshes;
    // For every vertex, the index of its position in the file's v list. The
    // tuples split one point of the surface into several vertices wherever its
    // uv or normal changes; this is what welds them back together.
    std::vector<int> position_ids;
};

struct Scene {
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
    int computed_normals = 0; // face corners that had no vn in the file
};

// Reads an OBJ file and the MTL libraries it names. Polygons are triangulated;
// corners without a normal get one computed from the faces around them. Throws
// std::runtime_error with a readable message if the file cannot be parsed.
Scene load_obj(const std::string& path);
