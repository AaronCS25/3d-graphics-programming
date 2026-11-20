#include "scene_build.hpp"

#include <stb_image.h>

#include <cstdio>
#include <map>
#include <string>

namespace {

Vec3 to_vec3(const glm::vec3& v) {
    return {v.x, v.y, v.z};
}

// Reads an image as RGB, appends it to the shared texel array and returns its
// index, or -1 if it cannot be read. Single-channel images come out grey.
int load_texture(const std::string& path, HostScene& out) {
    stbi_set_flip_vertically_on_load(1); // row 0 at v = 0, as OpenGL reads it
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr) {
        std::fprintf(stderr, "could not load '%s': %s\n", path.c_str(), stbi_failure_reason());
        return -1;
    }
    const size_t bytes = static_cast<size_t>(width) * height * 3;
    out.textures.push_back({static_cast<int>(out.texels.size()), width, height});
    out.texels.insert(out.texels.end(), pixels, pixels + bytes);
    stbi_image_free(pixels);
    return static_cast<int>(out.textures.size()) - 1;
}

} // namespace

HostScene build_scene(const Scene& scene, const glm::mat4& model) {
    HostScene out;

    // Materials. As in task 12, a map_Kd replaces Kd (whose value in the file
    // is only the mean colour of the image). Many materials can share an image:
    // each file is read once.
    std::map<std::string, int> loaded;
    for (const Material& m : scene.materials) {
        Diffuse material{to_vec3(m.diffuse), -1};
        if (!m.diffuse_map.empty()) {
            const auto found = loaded.find(m.diffuse_map);
            material.texture = found != loaded.end()
                                   ? found->second
                                   : (loaded[m.diffuse_map] = load_texture(m.diffuse_map, out));
            if (material.texture >= 0) {
                material.albedo = vec3(1.0f);
            }
        }
        out.materials.push_back(material);
    }
    // Faces without usemtl: OBJ's default grey.
    const int fallback = static_cast<int>(out.materials.size());
    out.materials.push_back({vec3(0.8f), -1});

    // The model matrix is a rotation, a uniform scale and a translation, so
    // its upper 3x3 part also turns the normals (they are normalized again).
    const glm::mat3 normal_matrix(model);

    for (const Mesh& mesh : scene.meshes) {
        if (mesh.indices.empty()) {
            continue;
        }
        std::vector<glm::vec3> world(mesh.vertices.size());
        std::vector<glm::vec3> normals(mesh.vertices.size());
        for (size_t i = 0; i < world.size(); ++i) {
            world[i] = glm::vec3(model * glm::vec4(mesh.vertices[i].position, 1.0f));
            normals[i] = glm::normalize(normal_matrix * mesh.vertices[i].normal);
        }

        Object object;
        object.first = static_cast<int>(out.triangles.size());
        glm::vec3 box_min = world[mesh.indices[0]];
        glm::vec3 box_max = box_min;
        for (const Submesh& submesh : mesh.submeshes) {
            const int material = submesh.material >= 0 ? submesh.material : fallback;
            const int end = submesh.first_index + submesh.index_count;
            for (int i = submesh.first_index; i + 2 < end; i += 3) {
                const unsigned ia = mesh.indices[i];
                const unsigned ib = mesh.indices[i + 1];
                const unsigned ic = mesh.indices[i + 2];
                const glm::vec3 a = world[ia];
                const glm::vec3 b = world[ib];
                const glm::vec3 c = world[ic];
                // A zero-area triangle cannot be hit, but rounding can make
                // Möller-Trumbore report a hit anyway (task 13).
                const glm::vec3 n = glm::cross(b - a, c - a);
                if (glm::dot(n, n) < 1e-12f) {
                    continue;
                }
                out.triangles.push_back({to_vec3(a), to_vec3(b - a), to_vec3(c - a)});

                Shading shading;
                shading.n0 = to_vec3(normals[ia]);
                shading.n1 = to_vec3(normals[ib]);
                shading.n2 = to_vec3(normals[ic]);
                const glm::vec2 uv[3] = {mesh.vertices[ia].uv, mesh.vertices[ib].uv,
                                         mesh.vertices[ic].uv};
                for (int k = 0; k < 3; ++k) {
                    shading.uv[2 * k] = uv[k].x;
                    shading.uv[2 * k + 1] = uv[k].y;
                }
                shading.material = material;
                out.shading.push_back(shading);

                for (const glm::vec3& p : {a, b, c}) {
                    box_min = glm::min(box_min, p);
                    box_max = glm::max(box_max, p);
                }
            }
        }
        object.count = static_cast<int>(out.triangles.size()) - object.first;
        object.box_min = to_vec3(box_min);
        object.box_max = to_vec3(box_max);
        if (object.count > 0) {
            out.objects.push_back(object);
        }
    }
    return out;
}
