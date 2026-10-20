#include "renderer.hpp"

#include "shapes.hpp"
#include "texture.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <string>

namespace {

constexpr int kShadowMapSize = 4096;
constexpr float kSeedRadius = 0.07f;
constexpr float kSunRadius = 0.2f;

const std::string kShaders = SHADER_DIR;

// For faces with no usemtl, or an index out of range.
const Material& default_material() {
    static const Material material = [] {
        Material m;
        m.name = "(default)";
        m.specular = glm::vec3(0.2f);
        m.shininess = 16.0f;
        return m;
    }();
    return material;
}

} // namespace

const char* const kViewModeNames[kViewModeCount] = {"Lit",
                                                    "Albedo",
                                                    "Normals",
                                                    "Depth (false colour)",
                                                    "Texture coordinates",
                                                    "Geodesic distance",
                                                    "Sunlight and shadow"};

bool Renderer::create() {
    if (!m_scene.load(kShaders + "scene.vert", kShaders + "scene.frag") ||
        !m_depth.load(kShaders + "depth.vert", kShaders + "depth.frag") ||
        !m_marker.load(kShaders + "marker.vert", kShaders + "marker.frag")) {
        return false;
    }
    if (!m_shadow_map.create(kShadowMapSize)) {
        return false;
    }
    m_sphere.upload(make_sphere(24, 12, -1));
    // White multiplies by one; (0.5, 0.5, 1) is the flat tangent-space normal.
    m_white = make_flat_texture(glm::vec3(1.0f));
    m_flat_normal = make_flat_texture(glm::vec3(0.5f, 0.5f, 1.0f));
    return true;
}

void Renderer::destroy() {
    m_scene.destroy();
    m_depth.destroy();
    m_marker.destroy();
    m_shadow_map.destroy();
    m_sphere.destroy();
    glDeleteTextures(1, &m_white);
    glDeleteTextures(1, &m_flat_normal);
}

// When a material has a diffuse map, the map is its colour and Kd is ignored:
// exporters disagree on what they write in Kd next to a map_Kd (task 12). Ks
// keeps scaling map_Ks. A missing map is a 1x1 texture, so the shader never has
// a special case.
void Renderer::bind_material(const World& world, int material, bool textured,
                             const RenderSettings& settings) {
    const bool known = material >= 0 && material < static_cast<int>(world.materials.size());
    const Material& mtl =
        known ? world.materials[static_cast<size_t>(material)] : default_material();
    const MaterialMaps maps =
        known && textured ? world.maps[static_cast<size_t>(material)] : MaterialMaps();

    const bool diffuse = settings.diffuse_maps && maps.diffuse != 0;
    const bool specular = settings.specular_maps && maps.specular != 0;
    const bool normal = settings.normal_maps && maps.normal != 0;

    m_scene.set("uMaterial.Kd", diffuse ? glm::vec3(1.0f) : mtl.diffuse);
    m_scene.set("uMaterial.Ks", mtl.specular);
    m_scene.set("uMaterial.Ns", std::max(mtl.shininess, 1.0f));
    m_scene.set("uMaterial.d", mtl.opacity);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, diffuse ? maps.diffuse : m_white);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, specular ? maps.specular : m_white);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, normal ? maps.normal : m_flat_normal);
}

RenderStats Renderer::draw(const World& world, const Frame& frame, const RenderSettings& settings) {
    RenderStats stats;
    const auto opacity = [&world](int material) {
        return material >= 0 && material < static_cast<int>(world.materials.size())
                   ? world.materials[static_cast<size_t>(material)].opacity
                   : 1.0f;
    };

    // --- Pass 1: depth from the sun. -----------------------------------------
    // The sun's orthographic box wraps the world's shadow sphere. Glass
    // (d < 1) lets the light through and casts no shadow.
    const glm::vec3 centre = world.shadow_centre;
    const float r = world.shadow_radius;
    const glm::mat4 light_view =
        glm::lookAt(centre + 2.0f * r * frame.light_direction, centre, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 light_space = glm::ortho(-r, r, -r, r, r, 3.0f * r) * light_view;

    if (settings.shadows) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_map.fbo);
        glViewport(0, 0, m_shadow_map.size, m_shadow_map.size);
        glClear(GL_DEPTH_BUFFER_BIT);
        m_depth.use();
        m_depth.set("uLightSpace", light_space);
        for (const Object& object : world.objects) {
            m_depth.set("uModel", object.model);
            for (const Submesh& submesh : object.gpu.submeshes) {
                if (opacity(submesh.material) >= 1.0f) {
                    object.gpu.draw(submesh);
                    ++stats.draw_calls;
                }
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // --- Pass 2: the scene from the camera. ----------------------------------
    glViewport(frame.x, 0, frame.width, frame.height);
    glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Unit 0 holds the shadow map for the whole pass; 1 to 3 the maps of the
    // material being drawn.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadow_map.depth);
    m_scene.use();
    m_scene.set("uShadowMap", 0);
    m_scene.set("uDiffuseMap", 1);
    m_scene.set("uSpecularMap", 2);
    m_scene.set("uNormalMap", 3);
    m_scene.set("uShadows", settings.shadows);
    m_scene.set("uPcf", settings.pcf);
    m_scene.set("uBiasScale", settings.bias_scale);
    m_scene.set("uLightSpace", light_space);
    m_scene.set("uView", frame.view);
    m_scene.set("uProjection", frame.projection);
    m_scene.set("uLightDir", frame.light_direction);
    m_scene.set("uCameraPos", frame.camera_position);
    m_scene.set("uAmbientLight", glm::vec3(world.sun.ambient));
    m_scene.set("uLightColor", world.sun.colour);
    m_scene.set("uViewMode", static_cast<int>(settings.view));
    m_scene.set("uColormap", settings.colormap);
    m_scene.set("uIsolines", settings.isolines);
    m_scene.set("uIsolineCount", settings.isoline_count);
    m_scene.set("uDepthNear", frame.depth_near);
    m_scene.set("uDepthFar", frame.depth_far);

    // Opaque surfaces first. Glass is blended over them afterwards without
    // writing depth, so what lies behind it has already been drawn.
    for (const bool transparent : {false, true}) {
        if (transparent) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        for (const Object& object : world.objects) {
            // The inverse transpose keeps normals perpendicular under any scale.
            m_scene.set("uModel", object.model);
            m_scene.set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(object.model))));
            m_scene.set("uFlatNormals", object.normals == NormalSource::Flat);
            for (const Submesh& submesh : object.gpu.submeshes) {
                if ((opacity(submesh.material) < 1.0f) != transparent) {
                    continue;
                }
                bind_material(world, submesh.material, !object.simplified, settings);
                object.gpu.draw(submesh);
                ++stats.draw_calls;
                stats.triangles += submesh.index_count / 3;
            }
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // --- Helpers. -------------------------------------------------------------
    m_marker.use();
    m_marker.set("uView", frame.view);
    m_marker.set("uProjection", frame.projection);
    const glm::mat4 identity(1.0f);

    if (settings.wireframe && frame.selected >= 0 &&
        frame.selected < static_cast<int>(world.objects.size())) {
        // Pulled towards the camera so the lines win the depth test against
        // the faces they lie on.
        const Object& object = world.objects[static_cast<size_t>(frame.selected)];
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        m_marker.set("uModel", object.model);
        m_marker.set("uColor", glm::vec3(0.08f, 0.08f, 0.1f));
        object.gpu.draw();
        ++stats.draw_calls;
        glDisable(GL_POLYGON_OFFSET_LINE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    if (settings.show_seeds) {
        m_marker.set("uColor", glm::vec3(1.0f, 0.2f, 0.9f));
        for (const Object& object : world.objects) {
            for (const int seed : object.seeds) {
                const glm::vec3 p = glm::vec3(object.model * glm::vec4(object.che.G(seed), 1.0f));
                m_marker.set("uModel", glm::translate(identity, p) *
                                           glm::scale(identity, glm::vec3(kSeedRadius)));
                m_sphere.draw();
                ++stats.draw_calls;
            }
        }
    }

    if (settings.show_sun) {
        const glm::vec3 p = centre + 0.6f * r * frame.light_direction;
        m_marker.set("uModel",
                     glm::translate(identity, p) * glm::scale(identity, glm::vec3(kSunRadius)));
        m_marker.set("uColor", world.sun.colour);
        m_sphere.draw();
        ++stats.draw_calls;
    }
    return stats;
}
