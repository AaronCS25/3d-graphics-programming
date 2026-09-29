#include "obj_loader.hpp"

#include <tiny_obj_loader.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {

// Two faces meeting at a vertex share its normal only if they bend less than
// this. Smooth surfaces (a cow) have small angles everywhere and come out
// smooth; the corners of a building keep their hard edges.
constexpr float kCreaseAngle = 45.0f; // degrees

std::string directory_of(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Map paths are written relative to the MTL file, often with Windows
// separators by the exporter.
std::string resolve(const std::string& directory, std::string name) {
    if (name.empty()) {
        return name;
    }
    std::replace(name.begin(), name.end(), '\\', '/');
    return directory + name;
}

glm::vec3 vec3_of(const tinyobj::real_t* v) {
    return {v[0], v[1], v[2]};
}

Material convert(const tinyobj::material_t& m, const std::string& directory) {
    Material material;
    material.name = m.name;
    material.ambient = vec3_of(m.ambient);
    material.diffuse = vec3_of(m.diffuse);
    material.specular = vec3_of(m.specular);
    material.shininess = m.shininess;
    material.opacity = m.dissolve;
    material.diffuse_map = resolve(directory, m.diffuse_texname);
    material.specular_map = resolve(directory, m.specular_texname);
    // "norm" names a normal map unambiguously; map_Bump is used by exporters for
    // both normal maps and height maps, which only the image itself can tell.
    material.bump_map =
        resolve(directory, m.normal_texname.empty() ? m.bump_texname : m.normal_texname);
    material.bump_scale =
        m.normal_texname.empty() ? m.bump_texopt.bump_multiplier : m.normal_texopt.bump_multiplier;
    return material;
}

// The key of the de-indexing map: the v/vt/vn tuple of a face corner. A corner
// without vn carries its computed normal instead, bit for bit, so corners of
// the same smooth region still merge into one vertex.
struct CornerKey {
    int position;
    int uv;
    int normal;
    std::uint32_t computed[3];

    bool operator==(const CornerKey& other) const {
        return position == other.position && uv == other.uv && normal == other.normal &&
               std::memcmp(computed, other.computed, sizeof(computed)) == 0;
    }
};

struct CornerKeyHash {
    size_t operator()(const CornerKey& k) const {
        size_t h = std::hash<int>()(k.position);
        const auto mix = [&h](size_t value) { h ^= value + 0x9e3779b9 + (h << 6) + (h >> 2); };
        mix(std::hash<int>()(k.uv));
        mix(std::hash<int>()(k.normal));
        for (const std::uint32_t c : k.computed) {
            mix(std::hash<std::uint32_t>()(c));
        }
        return h;
    }
};

// Normals for the corners that have no vn. Every triangle of the file
// contributes its normal, weighted by its area (the cross product is already
// twice the area), to the corners of its vertices; a corner only takes the
// faces that bend less than the crease angle from its own.
class NormalBuilder {
  public:
    NormalBuilder(const tinyobj::attrib_t& attrib, const std::vector<tinyobj::shape_t>& shapes)
        : m_attrib(attrib), m_faces_of(attrib.vertices.size() / 3) {
        for (const tinyobj::shape_t& shape : shapes) {
            const std::vector<tinyobj::index_t>& indices = shape.mesh.indices;
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                const int face = static_cast<int>(m_face_normals.size());
                const glm::vec3 p0 = position(indices[i].vertex_index);
                const glm::vec3 p1 = position(indices[i + 1].vertex_index);
                const glm::vec3 p2 = position(indices[i + 2].vertex_index);
                m_face_normals.push_back(glm::cross(p1 - p0, p2 - p0));
                for (size_t k = 0; k < 3; ++k) {
                    m_faces_of[static_cast<size_t>(indices[i + k].vertex_index)].push_back(face);
                }
            }
        }
    }

    // Normal of the corner of `face` at vertex `v`. Faces are numbered in the
    // order of the shapes and their triangles, the same walk the loader does.
    glm::vec3 corner_normal(int face, int v) const {
        const float min_cosine = std::cos(glm::radians(kCreaseAngle));
        const glm::vec3 own = unit(m_face_normals[static_cast<size_t>(face)]);
        glm::vec3 sum(0.0f);
        for (const int other : m_faces_of[static_cast<size_t>(v)]) {
            const glm::vec3& n = m_face_normals[static_cast<size_t>(other)];
            if (glm::dot(own, unit(n)) >= min_cosine) {
                sum += n;
            }
        }
        if (glm::dot(sum, sum) == 0.0f) {
            // A degenerate face has no direction of its own: it takes the
            // average of everything around the vertex.
            for (const int other : m_faces_of[static_cast<size_t>(v)]) {
                sum += m_face_normals[static_cast<size_t>(other)];
            }
        }
        return glm::dot(sum, sum) > 0.0f ? glm::normalize(sum) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

  private:
    glm::vec3 position(int v) const {
        return vec3_of(&m_attrib.vertices[3 * static_cast<size_t>(v)]);
    }

    static glm::vec3 unit(const glm::vec3& n) {
        const float length = glm::length(n);
        return length > 0.0f ? n / length : glm::vec3(0.0f);
    }

    const tinyobj::attrib_t& m_attrib;
    std::vector<glm::vec3> m_face_normals;
    std::vector<std::vector<int>> m_faces_of; // triangles around each position
};

// Tangents from the texture coordinates (Lengyel's method). On a triangle the
// edges are e1 = du1 T + dv1 B and e2 = du2 T + dv2 B; solving that 2x2 system
// gives T and B, the directions in which u and v grow. They are accumulated per
// vertex, then T is made perpendicular to the normal.
void compute_tangents(Mesh& mesh) {
    std::vector<glm::vec3> tangents(mesh.vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(mesh.vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const unsigned a = mesh.indices[i];
        const unsigned b = mesh.indices[i + 1];
        const unsigned c = mesh.indices[i + 2];
        const Vertex& v0 = mesh.vertices[a];
        const Vertex& v1 = mesh.vertices[b];
        const Vertex& v2 = mesh.vertices[c];

        const glm::vec3 e1 = v1.position - v0.position;
        const glm::vec3 e2 = v2.position - v0.position;
        const glm::vec2 d1 = v1.uv - v0.uv;
        const glm::vec2 d2 = v2.uv - v0.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) {
            continue; // no texture on this face, or a degenerate one
        }
        const glm::vec3 t = (e1 * d2.y - e2 * d1.y) / det;
        const glm::vec3 bt = (e2 * d1.x - e1 * d2.x) / det;
        for (const unsigned v : {a, b, c}) {
            tangents[v] += t;
            bitangents[v] += bt;
        }
    }

    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        Vertex& vertex = mesh.vertices[v];
        const glm::vec3& n = vertex.normal;
        glm::vec3 t = tangents[v] - n * glm::dot(n, tangents[v]);
        if (glm::dot(t, t) < 1e-20f) {
            // Untextured: any direction along the surface will do.
            t = glm::cross(std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0), n);
        }
        t = glm::normalize(t);
        const float handedness = glm::dot(glm::cross(n, t), bitangents[v]) < 0.0f ? -1.0f : 1.0f;
        vertex.tangent = glm::vec4(t, handedness);
    }
}

} // namespace

Scene load_obj(const std::string& path) {
    const std::string directory = directory_of(path);

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.mtl_search_path = directory;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        throw std::runtime_error(reader.Error().empty() ? "cannot read " + path : reader.Error());
    }
    if (!reader.Warning().empty()) {
        std::fprintf(stderr, "%s", reader.Warning().c_str());
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    Scene scene;
    for (const tinyobj::material_t& m : reader.GetMaterials()) {
        scene.materials.push_back(convert(m, directory));
    }

    scene.bounds_min = glm::vec3(std::numeric_limits<float>::max());
    scene.bounds_max = glm::vec3(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
        const glm::vec3 p = vec3_of(&attrib.vertices[i]);
        scene.bounds_min = glm::min(scene.bounds_min, p);
        scene.bounds_max = glm::max(scene.bounds_max, p);
    }

    const NormalBuilder normals(attrib, shapes);
    int face_base = 0; // first face of the current shape in NormalBuilder's numbering

    for (const tinyobj::shape_t& shape : shapes) {
        const std::vector<tinyobj::index_t>& indices = shape.mesh.indices;
        const std::vector<int>& face_materials = shape.mesh.material_ids;
        const int n_faces = static_cast<int>(indices.size() / 3);

        Mesh mesh;
        mesh.name = shape.name;
        std::unordered_map<CornerKey, unsigned, CornerKeyHash> vertex_of;

        // One submesh per material, in order of first use, so the mesh is drawn
        // with one call per material instead of one per face.
        std::vector<int> order;
        std::unordered_map<int, std::vector<int>> faces_of_material;
        for (int f = 0; f < n_faces; ++f) {
            const int material =
                face_materials.empty() ? -1 : face_materials[static_cast<size_t>(f)];
            auto& faces = faces_of_material[material];
            if (faces.empty()) {
                order.push_back(material);
            }
            faces.push_back(f);
        }

        for (const int material : order) {
            Submesh submesh;
            submesh.first_index = static_cast<int>(mesh.indices.size());
            submesh.material = material;

            for (const int f : faces_of_material[material]) {
                for (int k = 0; k < 3; ++k) {
                    const tinyobj::index_t& corner = indices[static_cast<size_t>(3 * f + k)];
                    CornerKey key{
                        corner.vertex_index, corner.texcoord_index, corner.normal_index, {0, 0, 0}};
                    glm::vec3 normal(0.0f);
                    if (corner.normal_index >= 0) {
                        normal =
                            vec3_of(&attrib.normals[3 * static_cast<size_t>(corner.normal_index)]);
                    }
                    if (glm::dot(normal, normal) > 0.0f) {
                        normal = glm::normalize(normal);
                    } else {
                        normal = normals.corner_normal(face_base + f, corner.vertex_index);
                        std::memcpy(key.computed, &normal, sizeof(key.computed));
                        ++scene.computed_normals;
                    }

                    const auto found = vertex_of.find(key);
                    if (found != vertex_of.end()) {
                        mesh.indices.push_back(found->second);
                        continue;
                    }

                    Vertex vertex{};
                    vertex.position =
                        vec3_of(&attrib.vertices[3 * static_cast<size_t>(corner.vertex_index)]);
                    vertex.normal = normal;
                    if (corner.texcoord_index >= 0) {
                        const size_t t = 2 * static_cast<size_t>(corner.texcoord_index);
                        vertex.uv = glm::vec2(attrib.texcoords[t], attrib.texcoords[t + 1]);
                    }
                    const unsigned id = static_cast<unsigned>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex);
                    mesh.position_ids.push_back(corner.vertex_index);
                    vertex_of.emplace(key, id);
                    mesh.indices.push_back(id);
                }
            }

            submesh.index_count = static_cast<int>(mesh.indices.size()) - submesh.first_index;
            mesh.submeshes.push_back(submesh);
        }

        face_base += n_faces;
        if (!mesh.indices.empty()) {
            compute_tangents(mesh);
            scene.meshes.push_back(std::move(mesh));
        }
    }

    if (scene.meshes.empty()) {
        throw std::runtime_error(path + " has no faces");
    }
    return scene;
}
