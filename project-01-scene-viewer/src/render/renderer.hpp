// Draws a World in two passes (task 10): depth from the sun into the shadow
// map, then the scene from the camera with its materials, followed by the
// helpers: seeds, wireframe of the selection and the sun.

#pragma once

#include "gpu_mesh.hpp"
#include "shader.hpp"
#include "shadow_map.hpp"
#include "world.hpp"

#include <glm/glm.hpp>

enum class ViewMode { Lit, Albedo, Normals, Depth, TexCoords, Distance, Light };
constexpr int kViewModeCount = 7;
extern const char* const kViewModeNames[kViewModeCount];

struct RenderSettings {
    ViewMode view = ViewMode::Lit;
    bool diffuse_maps = true;
    bool specular_maps = true;
    bool normal_maps = true;
    bool shadows = true;
    bool pcf = true;
    float bias_scale = 1.0f;
    int colormap = 0; // 0 jet, 1 plasma
    bool isolines = true;
    float isoline_count = 20.0f;
    bool wireframe = true; // over the selected object
    bool show_seeds = true;
    bool show_sun = true;
};

// What changes every frame and does not belong to the world.
struct Frame {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 camera_position{0.0f};
    glm::vec3 light_direction{0.0f, 1.0f, 0.0f};
    float depth_near = 0.1f; // range of the false-colour depth view
    float depth_far = 30.0f;
    int x = 0; // the viewport, in framebuffer pixels: right of the panel
    int width = 1;
    int height = 1;
    int selected = -1;
};

struct RenderStats {
    int draw_calls = 0;
    int triangles = 0;
};

class Renderer {
  public:
    bool create();
    void destroy();
    RenderStats draw(const World& world, const Frame& frame, const RenderSettings& settings);

  private:
    void bind_material(const World& world, int material, bool textured,
                       const RenderSettings& settings);

    Shader m_scene;
    Shader m_depth;
    Shader m_marker;
    ShadowMap m_shadow_map;
    GpuMesh m_sphere; // the sun and the seeds
    GLuint m_white = 0;
    GLuint m_flat_normal = 0;
};
