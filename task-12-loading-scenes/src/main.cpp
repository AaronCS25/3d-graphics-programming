// Task 12 — Loading Scenes
//
// Loads a complete scene from a Wavefront OBJ file: its objects, the MTL
// materials they use (Ka, Kd, Ks, Ns, d) and the texture maps those materials
// name (map_Kd, map_Ks, map_Bump / norm). The scene is lit with the Blinn-Phong
// model of task 09 and the animated sun and shadow map of tasks 10 and 11, now
// with the coefficients of every surface taken from the file.
//
//   1. read the OBJ and its MTL (obj_loader), de-indexing v/vt/vn tuples
//   2. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   3. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   4. compile & link shaders, create the shadow map
//   5. upload the meshes, load each texture once (texture)
//   6. render loop: sun -> pass 1 (depth from the sun) -> pass 2 (camera)
//   7. cleanup + glfwTerminate()
//
// Usage:  task12 [gallery | sibenik | path/to/scene.obj] [--check]
//         --check prints what was read from the file and exits without opening
//         a window. While running, drop an .obj onto the window to load it.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

#include "obj_loader.hpp"
#include "texture.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 800;

// Every scene is scaled so that its longest side measures this, centred on the
// y axis and standing on y = 0: the camera, the sun and its shadow box then work
// for a cathedral modelled in metres and for a cow modelled in units.
constexpr float kSceneSize = 16.0f;

// The sun of task 10: its azimuth turns and its elevation rises and falls
// around the mean elevation of each scene.
constexpr float kSunAzimuthSpeed = 0.15f;   // rad/s
constexpr float kSunElevationSwing = 10.0f; // degrees
constexpr float kSunElevationSpeed = 0.25f; // rad/s
constexpr float kSunMarkerRadius = 0.2f;

constexpr int kShadowMapSize = 4096;

// The built-in scenes and how each one is shown. The cathedral is seen from
// inside, from the altar down the nave towards the rose window over the door,
// with a low sun that comes in through the windows.
struct Preset {
    const char* name;
    const char* file; // relative to SCENE_DIR
    float yaw;        // degrees about y, applied to the whole scene
    glm::vec3 target;
    float orbit_radius;
    float orbit_height;
    float height_swing;
    bool animated_camera;
    float ambient;       // Ia
    float sun_elevation; // degrees
};

// clang-format off
const Preset kPresets[] = {
    // name      file                    yaw    target              orbit  height swing  animated Ia    sun
    {"gallery", "gallery/gallery.obj",   0.0f, {0.0f, 1.2f, 0.0f}, 12.0f, 4.5f, 1.2f, true,  0.20f, 50.0f},
    {"sibenik", "sibenik/sibenik.obj", -90.0f, {0.0f, 3.0f, 0.0f},  6.8f, 0.3f, 0.0f, false, 0.35f, 30.0f},
};
// clang-format on
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

camera gCamera;

bool gUseDiffuseMaps = true;
bool gUseSpecularMaps = true;
bool gUseNormalMaps = true;
bool gAmbientFromKa = false;
bool gShadows = true;
bool gPaused = false;
bool gResetRequested = false;
bool gTitleDirty = true;
bool gNextSceneRequested = false; // set by TAB, consumed by the render loop
std::string gDroppedPath;         // set by the drop callback, consumed by the render loop

const char* kVersionSource = "#version 330 core\n";

// Pass 1: the scene seen from the sun; only the depth buffer is written.
const char* kDepthVertexSource = R"(
layout (location = 0) in vec3 aPos;

uniform mat4 uLightSpace;
uniform mat4 uModel;

void main() {
    gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
}
)";

const char* kDepthFragmentSource = R"(
void main() {
}
)";

// Pass 2. The attributes and the material follow the table of the slides: one
// vertex attribute per OBJ list, one uniform per MTL coefficient, one sampler
// per map.
const char* kSceneVertexSource = R"(
layout (location = 0) in vec3 aPos;       // v
layout (location = 1) in vec3 aNormal;    // vn
layout (location = 2) in vec2 aTexCoords; // vt
layout (location = 3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;
out vec4 vTangent;
out vec4 vLightSpacePos;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vTexCoords = aTexCoords;
    vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
    vLightSpacePos = uLightSpace * world;
    gl_Position = uProjection * uView * world;
}
)";

const char* kSceneFragmentSource = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;
in vec4 vTangent;
in vec4 vLightSpacePos;

struct Material {
    vec3 Ka;
    vec3 Kd;
    vec3 Ks;
    float Ns;
    float d;
};

uniform Material uMaterial;
uniform sampler2D uDiffuseMap;  // map_Kd
uniform sampler2D uSpecularMap; // map_Ks
uniform sampler2D uNormalMap;   // map_Bump / norm
uniform bool uAmbientFromKa;

uniform sampler2D uShadowMap;
uniform bool uShadows;
uniform vec3 uLightDir; // towards the light, unit length
uniform vec3 uCameraPos;
uniform vec3 uAmbientLight; // Ia
uniform vec3 uLightColor;   // Id = Is

out vec4 FragColor;

// Task 10's shadow test: bias counted in texels, 3x3 PCF.
float shadowFactor(vec4 lightSpacePos, vec3 N, vec3 L) {
    vec3 p = lightSpacePos.xyz / lightSpacePos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) {
        return 0.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = texel.x * max(10.0 * (1.0 - dot(N, L)), 1.0);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            shadow += p.z - bias > closest ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main() {
    // Seen from behind, a face is lit on the side the camera is on: a scene
    // file does not promise that every wall faces the viewer.
    vec3 geometric = normalize(vNormal);
    if (!gl_FrontFacing) {
        geometric = -geometric;
    }

    // Task 11's TBN matrix; tangent.w flips B where the texture is mirrored.
    vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, geometric) * geometric);
    vec3 B = cross(geometric, T) * vTangent.w;
    vec3 mapped = texture(uNormalMap, vTexCoords).rgb * 2.0 - 1.0;
    vec3 N = normalize(mat3(T, B, geometric) * mapped);

    vec3 texel = texture(uDiffuseMap, vTexCoords).rgb;
    vec3 albedo = uMaterial.Kd * texel;
    vec3 ks = uMaterial.Ks * texture(uSpecularMap, vTexCoords).rgb;
    vec3 ka = uAmbientFromKa ? uMaterial.Ka * texel : albedo;

    vec3 L = uLightDir;
    vec3 V = normalize(uCameraPos - vWorldPos);
    float shadow = uShadows ? shadowFactor(vLightSpacePos, geometric, L) : 0.0;

    vec3 ambient = ka * uAmbientLight;
    float lambert = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * uLightColor * lambert;
    float highlight = 0.0;
    if (lambert > 0.0) {
        // Blinn-Phong with 4 Ns, which matches the Phong lobe of exponent Ns.
        vec3 H = normalize(L + V);
        highlight = pow(max(dot(N, H), 0.0), 4.0 * uMaterial.Ns);
    }
    vec3 specular = ks * uLightColor * highlight;

    FragColor = vec4(ambient + (1.0 - shadow) * (diffuse + specular), uMaterial.d);
}
)";

// The sun marker is drawn in the light's colour, without shading.
const char* kMarkerVertexSource = R"(
layout (location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)";

const char* kMarkerFragmentSource = R"(
uniform vec3 uColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    if (action == GLFW_PRESS) {
        double x = 0.0, y = 0.0;
        int width = 0, height = 0;
        glfwGetCursorPos(window, &x, &y);
        glfwGetWindowSize(window, &width, &height);
        gCamera.begin_drag(x, y, width, height);
        gTitleDirty = true;
    } else if (action == GLFW_RELEASE) {
        gCamera.end_drag();
    }
}

void cursorPosCallback(GLFWwindow* /*window*/, double x, double y) {
    gCamera.drag(x, y);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    gCamera.zoom(yoffset);
    gTitleDirty = true;
}

void windowSizeCallback(GLFWwindow* /*window*/, int /*width*/, int /*height*/) {
    gCamera.end_drag(); // the old drag was mapped to different window dimensions
}

void focusCallback(GLFWwindow* /*window*/, int focused) {
    if (!focused) {
        gCamera.end_drag(); // do not keep dragging after switching applications
    }
}

void dropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (count > 0) {
        gDroppedPath = paths[0];
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_1:
        gUseDiffuseMaps = !gUseDiffuseMaps;
        gTitleDirty = true;
        break;
    case GLFW_KEY_2:
        gUseSpecularMaps = !gUseSpecularMaps;
        gTitleDirty = true;
        break;
    case GLFW_KEY_3:
        gUseNormalMaps = !gUseNormalMaps;
        gTitleDirty = true;
        break;
    case GLFW_KEY_A:
        gAmbientFromKa = !gAmbientFromKa;
        gTitleDirty = true;
        break;
    case GLFW_KEY_S:
        gShadows = !gShadows;
        gTitleDirty = true;
        break;
    case GLFW_KEY_TAB:
        gNextSceneRequested = true;
        break;
    case GLFW_KEY_SPACE:
        gPaused = !gPaused;
        gTitleDirty = true;
        break;
    case GLFW_KEY_C:
        gCamera.set_animated(!gCamera.animated());
        gTitleDirty = true;
        break;
    case GLFW_KEY_R:
        gResetRequested = true;
        break;
    default:
        break;
    }
}

// glShaderSource concatenates the pieces in order, so the #version line goes
// first.
GLuint compileShader(GLenum type, const char* source) {
    const char* sources[] = {kVersionSource, source};
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s shader compilation failed:\n%s\n",
                     type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (vertexShader == 0) {
        return 0;
    }
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader program linking failed:\n%s\n", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// Step 4 of the slides' pipeline: the de-indexed vertices go to a VBO with one
// attribute pointer per field of Vertex, the triangles to an EBO.
struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    std::vector<Submesh> submeshes;

    void upload(const Mesh& mesh) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                     mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned)),
                     mesh.indices.data(), GL_STATIC_DRAW);

        const GLsizei stride = sizeof(Vertex);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, uv)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, tangent)));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);

        submeshes = mesh.submeshes;
    }

    void draw(const Submesh& submesh) const {
        glBindVertexArray(vao);
        glDrawElements(
            GL_TRIANGLES, submesh.index_count, GL_UNSIGNED_INT,
            reinterpret_cast<void*>(static_cast<size_t>(submesh.first_index) * sizeof(unsigned)));
    }

    void destroy() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
};

// A material ready to draw: the MTL coefficients and one texture per map, 0
// where the file names none (or the image could not be read).
struct GpuMaterial {
    Material mtl;
    GLuint diffuse = 0;
    GLuint specular = 0;
    GLuint normal = 0;
};

// Whatever the current scene put on the GPU, plus what the frame needs to know
// about its size.
struct GpuScene {
    std::string name;
    std::vector<GpuMesh> meshes;
    std::vector<GpuMaterial> materials; // the last one is the default, for usemtl-less faces
    TextureCache textures;
    glm::mat4 model{1.0f};
    glm::vec3 centre{0.0f}; // of the bounding box, after scaling
    float radius = 1.0f;    // half the diagonal of the bounding box, after scaling
    int triangles = 0;

    void upload(const Scene& scene, float yaw) {
        for (const Mesh& mesh : scene.meshes) {
            meshes.emplace_back();
            meshes.back().upload(mesh);
            triangles += static_cast<int>(mesh.indices.size() / 3);
        }

        for (const Material& mtl : scene.materials) {
            GpuMaterial material;
            material.mtl = mtl;
            material.diffuse = mtl.diffuse_map.empty() ? 0 : textures.colour(mtl.diffuse_map);
            material.specular = mtl.specular_map.empty() ? 0 : textures.colour(mtl.specular_map);
            material.normal =
                mtl.bump_map.empty() ? 0 : textures.normal(mtl.bump_map, mtl.bump_scale);
            materials.push_back(material);
        }
        materials.emplace_back();
        materials.back().mtl.name = "(default)";
        materials.back().mtl.specular = glm::vec3(0.2f);
        materials.back().mtl.shininess = 16.0f;

        // Longest side to kSceneSize, centred on the y axis, standing on y = 0.
        const glm::vec3 extent = scene.bounds_max - scene.bounds_min;
        const float scale = kSceneSize / std::max({extent.x, extent.y, extent.z, 1e-6f});
        const glm::vec3 anchor(0.5f * (scene.bounds_min.x + scene.bounds_max.x), scene.bounds_min.y,
                               0.5f * (scene.bounds_min.z + scene.bounds_max.z));
        model = glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
                glm::translate(glm::mat4(1.0f), -anchor);
        centre = glm::vec3(0.0f, 0.5f * extent.y * scale, 0.0f);
        radius = 0.5f * glm::length(extent) * scale;
    }

    const GpuMaterial& material(int id) const {
        return id >= 0 && id + 1 < static_cast<int>(materials.size()) ? materials[id]
                                                                      : materials.back();
    }

    void destroy() {
        for (GpuMesh& mesh : meshes) {
            mesh.destroy();
        }
        meshes.clear();
        materials.clear();
        textures.destroy();
        triangles = 0;
    }
};

// The target of pass 1: a framebuffer whose only attachment is a depth texture.
struct ShadowMap {
    GLuint fbo = 0;
    GLuint depth = 0;
    int size = 0;

    bool create(int resolution) {
        size = resolution;
        glGenTextures(1, &depth);
        glBindTexture(GL_TEXTURE_2D, depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, size, size, 0, GL_DEPTH_COMPONENT,
                     GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void destroy() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &depth);
    }
};

// The scene program and its uniform locations.
struct SceneProgram {
    GLuint id = 0;
    GLint model = -1;
    GLint view = -1;
    GLint projection = -1;
    GLint normalMatrix = -1;
    GLint lightSpace = -1;
    GLint ka = -1;
    GLint kd = -1;
    GLint ks = -1;
    GLint ns = -1;
    GLint d = -1;
    GLint diffuseMap = -1;
    GLint specularMap = -1;
    GLint normalMap = -1;
    GLint ambientFromKa = -1;
    GLint shadowMap = -1;
    GLint shadows = -1;
    GLint lightDir = -1;
    GLint cameraPos = -1;
    GLint ambientLight = -1;
    GLint lightColor = -1;

    bool create() {
        id = createShaderProgram(kSceneVertexSource, kSceneFragmentSource);
        if (id == 0) {
            return false;
        }
        model = glGetUniformLocation(id, "uModel");
        view = glGetUniformLocation(id, "uView");
        projection = glGetUniformLocation(id, "uProjection");
        normalMatrix = glGetUniformLocation(id, "uNormalMatrix");
        lightSpace = glGetUniformLocation(id, "uLightSpace");
        ka = glGetUniformLocation(id, "uMaterial.Ka");
        kd = glGetUniformLocation(id, "uMaterial.Kd");
        ks = glGetUniformLocation(id, "uMaterial.Ks");
        ns = glGetUniformLocation(id, "uMaterial.Ns");
        d = glGetUniformLocation(id, "uMaterial.d");
        diffuseMap = glGetUniformLocation(id, "uDiffuseMap");
        specularMap = glGetUniformLocation(id, "uSpecularMap");
        normalMap = glGetUniformLocation(id, "uNormalMap");
        ambientFromKa = glGetUniformLocation(id, "uAmbientFromKa");
        shadowMap = glGetUniformLocation(id, "uShadowMap");
        shadows = glGetUniformLocation(id, "uShadows");
        lightDir = glGetUniformLocation(id, "uLightDir");
        cameraPos = glGetUniformLocation(id, "uCameraPos");
        ambientLight = glGetUniformLocation(id, "uAmbientLight");
        lightColor = glGetUniformLocation(id, "uLightColor");
        return true;
    }
};

// Textures that stand in for a missing map: white multiplies by one, and
// (0.5, 0.5, 1) is the tangent-space normal (0, 0, 1), no relief.
struct FallbackTextures {
    GLuint white = 0;
    GLuint flatNormal = 0;
};

// Binds the maps of a material and sets its coefficients. When a material has a
// diffuse map, the map is its colour and Kd is ignored: exporters disagree on
// what they write in Kd next to a map_Kd (the breakfast room has Kd 0, Sibenik a
// grey), so the image is the only reliable colour. Ks keeps scaling map_Ks.
void bindMaterial(const SceneProgram& program, const GpuMaterial& material,
                  const FallbackTextures& fallback) {
    const bool diffuseMap = gUseDiffuseMaps && material.diffuse != 0;
    const bool specularMap = gUseSpecularMaps && material.specular != 0;
    const bool normalMap = gUseNormalMaps && material.normal != 0;

    const Material& mtl = material.mtl;
    const glm::vec3 kd = diffuseMap ? glm::vec3(1.0f) : mtl.diffuse;
    glUniform3fv(program.ka, 1, glm::value_ptr(mtl.ambient));
    glUniform3fv(program.kd, 1, glm::value_ptr(kd));
    glUniform3fv(program.ks, 1, glm::value_ptr(mtl.specular));
    glUniform1f(program.ns, std::max(mtl.shininess, 1.0f));
    glUniform1f(program.d, mtl.opacity);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, diffuseMap ? material.diffuse : fallback.white);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, specularMap ? material.specular : fallback.white);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, normalMap ? material.normal : fallback.flatNormal);
}

// Unit sphere, positions only, for the sun marker.
GpuMesh makeMarker() {
    constexpr int kSlices = 24;
    constexpr int kStacks = 12;
    Mesh mesh;
    for (int i = 0; i <= kStacks; ++i) {
        const float phi = glm::pi<float>() * static_cast<float>(i) / kStacks;
        for (int j = 0; j <= kSlices; ++j) {
            const float theta = glm::two_pi<float>() * static_cast<float>(j) / kSlices;
            const glm::vec3 p(std::sin(phi) * std::cos(theta), std::cos(phi),
                              std::sin(phi) * std::sin(theta));
            mesh.vertices.push_back({p, p, glm::vec2(0.0f), glm::vec4(0.0f)});
        }
    }
    for (int i = 0; i < kStacks; ++i) {
        for (int j = 0; j < kSlices; ++j) {
            const unsigned a = static_cast<unsigned>(i * (kSlices + 1) + j);
            const unsigned b = a + kSlices + 1;
            for (const unsigned k : {a, b, a + 1, a + 1, b, b + 1}) {
                mesh.indices.push_back(k);
            }
        }
    }
    mesh.submeshes.push_back({0, static_cast<int>(mesh.indices.size()), -1});
    GpuMesh marker;
    marker.upload(mesh);
    return marker;
}

// A built-in scene by name, or any path to an .obj.
std::string scenePath(const std::string& name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) {
            return std::string(SCENE_DIR) + preset.file;
        }
    }
    return name;
}

const Preset* presetOf(const std::string& name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
}

float yawOf(const std::string& name) {
    const Preset* preset = presetOf(name);
    return preset != nullptr ? preset->yaw : 0.0f;
}

std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void printStats(const std::string& name, const Scene& scene) {
    size_t positions = 0;
    size_t vertices = 0;
    size_t triangles = 0;
    for (const Mesh& mesh : scene.meshes) {
        vertices += mesh.vertices.size();
        triangles += mesh.indices.size() / 3;
        std::vector<int> ids = mesh.position_ids;
        std::sort(ids.begin(), ids.end());
        positions += static_cast<size_t>(std::unique(ids.begin(), ids.end()) - ids.begin());
    }
    std::printf("%s\n", name.c_str());
    std::printf("  %zu objects, %zu materials, %zu triangles\n", scene.meshes.size(),
                scene.materials.size(), triangles);
    std::printf("  %zu positions (v) -> %zu vertices after de-indexing v/vt/vn\n", positions,
                vertices);
    std::printf("  %d face corners without vn: normals computed\n", scene.computed_normals);
    std::printf("  bounds (%.2f, %.2f, %.2f) - (%.2f, %.2f, %.2f)\n", scene.bounds_min.x,
                scene.bounds_min.y, scene.bounds_min.z, scene.bounds_max.x, scene.bounds_max.y,
                scene.bounds_max.z);
}

void printMaterials(const Scene& scene) {
    const auto map = [](const std::string& path) {
        if (path.empty()) {
            return std::string("-");
        }
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file != nullptr) {
            std::fclose(file);
        }
        return baseName(path) + (file != nullptr ? "" : " (MISSING)");
    };
    for (const Material& m : scene.materials) {
        std::printf("  %-20s Kd (%.2f %.2f %.2f)  Ks (%.2f %.2f %.2f)  Ns %6.1f  d %.2f\n",
                    m.name.c_str(), m.diffuse.r, m.diffuse.g, m.diffuse.b, m.specular.r,
                    m.specular.g, m.specular.b, m.shininess, m.opacity);
        if (!m.diffuse_map.empty() || !m.specular_map.empty() || !m.bump_map.empty()) {
            std::printf("  %-20s map_Kd %s  map_Ks %s  bump %s\n", "", map(m.diffuse_map).c_str(),
                        map(m.specular_map).c_str(), map(m.bump_map).c_str());
        }
    }
}

void updateTitle(GLFWwindow* window, const GpuScene& scene) {
    char title[320];
    std::snprintf(title, sizeof(title),
                  "Task 12 - Loading Scenes | %s  %zu objects, %zu materials, %d tris | "
                  "map_Kd %s [1] | map_Ks %s [2] | bump %s [3] | ambient %s [A] | shadows %s [S]"
                  " | sun %s",
                  scene.name.c_str(), scene.meshes.size(), scene.materials.size() - 1,
                  scene.triangles, gUseDiffuseMaps ? "on" : "off", gUseSpecularMaps ? "on" : "off",
                  gUseNormalMaps ? "on" : "off", gAmbientFromKa ? "Ka" : "Kd",
                  gShadows ? "on" : "off", gPaused ? "paused" : "moving");
    glfwSetWindowTitle(window, title);
}

// The camera for a scene: the preset's orbit for a built-in scene, otherwise
// an orbit at about twice the radius of the bounding sphere, which frames most
// scenes (the whole sphere would need R / sin 22.5 = 2.6 R, but scenes are
// rarely that round).
void frameCamera(const std::string& name, const GpuScene& scene) {
    const Preset* preset = presetOf(name);
    if (preset != nullptr) {
        gCamera = camera(preset->target, preset->orbit_radius, preset->orbit_height,
                         preset->height_swing);
        gCamera.set_animated(preset->animated_camera);
    } else {
        gCamera =
            camera(scene.centre, 1.7f * scene.radius, 0.7f * scene.radius, 0.2f * scene.radius);
        gCamera.set_animated(true);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string sceneName = kPresets[0].name;
    bool checkOnly = false;
    std::vector<std::string> checkNames;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            checkOnly = true;
        } else {
            checkNames.emplace_back(argv[i]);
        }
    }
    if (!checkNames.empty()) {
        sceneName = checkNames.front();
    }

    // --check: load every scene on the command line (or all the built-in ones),
    // report, and exit.
    if (checkOnly) {
        if (checkNames.empty()) {
            for (const Preset& preset : kPresets) {
                checkNames.emplace_back(preset.name);
            }
        }
        int failures = 0;
        for (const std::string& name : checkNames) {
            try {
                const Scene scene = load_obj(scenePath(name));
                printStats(name, scene);
                printMaterials(scene);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", name.c_str(), e.what());
                ++failures;
            }
        }
        return failures;
    }

    // 1. The scene, before any window: a broken file fails fast.
    Scene cpuScene;
    try {
        cpuScene = load_obj(scenePath(sceneName));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not load '%s': %s\n", sceneName.c_str(), e.what());
        return 1;
    }
    printStats(sceneName, cpuScene);

    // 2. Window + OpenGL 3.3 core context.
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 12 - Loading Scenes", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 3. Context, function pointers, depth test.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetWindowSizeCallback(window, windowSizeCallback);
    glfwSetWindowFocusCallback(window, focusCallback);
    glfwSetDropCallback(window, dropCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 4. Shaders and the shadow map.
    SceneProgram program;
    const bool programReady = program.create();
    const GLuint depthProgram = createShaderProgram(kDepthVertexSource, kDepthFragmentSource);
    const GLuint markerProgram = createShaderProgram(kMarkerVertexSource, kMarkerFragmentSource);
    ShadowMap shadowMap;
    const bool shadowMapReady = shadowMap.create(kShadowMapSize);
    if (!programReady || depthProgram == 0 || markerProgram == 0 || !shadowMapReady) {
        if (!shadowMapReady) {
            std::fprintf(stderr, "Shadow map framebuffer is incomplete\n");
        }
        shadowMap.destroy();
        glDeleteProgram(program.id);
        glDeleteProgram(depthProgram);
        glDeleteProgram(markerProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const GLint depthLightSpaceLocation = glGetUniformLocation(depthProgram, "uLightSpace");
    const GLint depthModelLocation = glGetUniformLocation(depthProgram, "uModel");
    const GLint markerModelLocation = glGetUniformLocation(markerProgram, "uModel");
    const GLint markerViewLocation = glGetUniformLocation(markerProgram, "uView");
    const GLint markerProjectionLocation = glGetUniformLocation(markerProgram, "uProjection");
    const GLint markerColorLocation = glGetUniformLocation(markerProgram, "uColor");

    // 5. The scene on the GPU.
    const FallbackTextures fallback{make_flat_texture(glm::vec3(1.0f)),
                                    make_flat_texture(glm::vec3(0.5f, 0.5f, 1.0f))};
    GpuMesh marker = makeMarker();
    GpuScene scene;
    scene.name = baseName(sceneName);
    scene.upload(cpuScene, yawOf(sceneName));
    cpuScene = Scene();
    frameCamera(sceneName, scene);

    const glm::vec3 lightColor(1.0f); // Id = Is

    // 6. Render loop.
    float sceneTime = 0.0f;
    double lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;

        // TAB moves to the next built-in scene; a dropped file replaces the
        // current one. Either way the old scene is released first.
        std::string nextScene;
        if (gNextSceneRequested) {
            gNextSceneRequested = false;
            int current = -1;
            for (int i = 0; i < kPresetCount; ++i) {
                current = sceneName == kPresets[i].name ? i : current;
            }
            nextScene = kPresets[(current + 1) % kPresetCount].name;
        } else if (!gDroppedPath.empty()) {
            nextScene = gDroppedPath;
            gDroppedPath.clear();
        }
        if (!nextScene.empty()) {
            try {
                const Scene loaded = load_obj(scenePath(nextScene));
                printStats(nextScene, loaded);
                scene.destroy();
                scene.name = baseName(nextScene);
                scene.upload(loaded, yawOf(nextScene));
                sceneName = nextScene;
                frameCamera(sceneName, scene);
                sceneTime = 0.0f;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", nextScene.c_str(), e.what());
            }
            gTitleDirty = true;
            lastFrame = glfwGetTime(); // loading is not animation time
        }

        if (gResetRequested) {
            gResetRequested = false;
            sceneTime = 0.0f;
            frameCamera(sceneName, scene);
        }
        if (!gPaused) {
            sceneTime += deltaTime;
            gCamera.update(deltaTime);
        }
        if (gTitleDirty) {
            gTitleDirty = false;
            updateTitle(window, scene);
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            glfwWaitEvents(); // minimized: no valid aspect ratio or pixels to draw
            lastFrame = glfwGetTime();
            continue;
        }

        // --- The sun. ------------------------------------------------------------
        const Preset* preset = presetOf(sceneName);
        const glm::vec3 ambientLight(preset != nullptr ? preset->ambient : 0.2f); // Ia
        const float sunElevation = preset != nullptr ? preset->sun_elevation : 50.0f;
        const float azimuth = sceneTime * kSunAzimuthSpeed;
        const float elevation = glm::radians(
            sunElevation + kSunElevationSwing * std::sin(sceneTime * kSunElevationSpeed));
        const glm::vec3 lightDir(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
                                 std::cos(elevation) * std::sin(azimuth));

        // --- Pass 1: depth from the sun into the shadow map. ------------------
        // The sun's orthographic box wraps the bounding sphere of the scene.
        const glm::mat4 lightView = glm::lookAt(scene.centre + 2.0f * scene.radius * lightDir,
                                                scene.centre, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 lightProjection =
            glm::ortho(-scene.radius, scene.radius, -scene.radius, scene.radius, scene.radius,
                       3.0f * scene.radius);
        const glm::mat4 lightSpace = lightProjection * lightView;

        // Glass (d < 1) lets the sunlight through: it casts no shadow.
        if (gShadows) {
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
            glViewport(0, 0, shadowMap.size, shadowMap.size);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUseProgram(depthProgram);
            glUniformMatrix4fv(depthLightSpaceLocation, 1, GL_FALSE, glm::value_ptr(lightSpace));
            glUniformMatrix4fv(depthModelLocation, 1, GL_FALSE, glm::value_ptr(scene.model));
            for (const GpuMesh& mesh : scene.meshes) {
                for (const Submesh& submesh : mesh.submeshes) {
                    if (scene.material(submesh.material).mtl.opacity >= 1.0f) {
                        mesh.draw(submesh);
                    }
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // --- Pass 2: the scene from the camera. --------------------------------
        glViewport(0, 0, width, height);
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 view = gCamera.view();
        const glm::mat4 projection = gCamera.projection(width, height);
        const glm::vec3 cameraPos = gCamera.position();
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(scene.model)));

        // Unit 0 holds the shadow map for the whole pass; units 1 to 3 the
        // maps of the material being drawn.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depth);
        glUseProgram(program.id);
        glUniform1i(program.shadowMap, 0);
        glUniform1i(program.diffuseMap, 1);
        glUniform1i(program.specularMap, 2);
        glUniform1i(program.normalMap, 3);
        glUniform1i(program.shadows, gShadows ? 1 : 0);
        glUniform1i(program.ambientFromKa, gAmbientFromKa ? 1 : 0);
        glUniformMatrix4fv(program.lightSpace, 1, GL_FALSE, glm::value_ptr(lightSpace));
        glUniformMatrix4fv(program.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(program.projection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(program.model, 1, GL_FALSE, glm::value_ptr(scene.model));
        glUniformMatrix3fv(program.normalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform3fv(program.lightDir, 1, glm::value_ptr(lightDir));
        glUniform3fv(program.cameraPos, 1, glm::value_ptr(cameraPos));
        glUniform3fv(program.ambientLight, 1, glm::value_ptr(ambientLight));
        glUniform3fv(program.lightColor, 1, glm::value_ptr(lightColor));

        // Opaque surfaces first. Glass is blended over them afterwards, without
        // writing depth, so what lies behind it has already been drawn.
        for (const bool transparent : {false, true}) {
            if (transparent) {
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
            }
            for (const GpuMesh& mesh : scene.meshes) {
                for (const Submesh& submesh : mesh.submeshes) {
                    const GpuMaterial& material = scene.material(submesh.material);
                    if ((material.mtl.opacity < 1.0f) != transparent) {
                        continue;
                    }
                    bindMaterial(program, material, fallback);
                    mesh.draw(submesh);
                }
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        // The sun marker sits along the light direction, above the scene.
        const glm::mat4 markerModel =
            glm::translate(glm::mat4(1.0f), scene.centre + 0.6f * scene.radius * lightDir) *
            glm::scale(glm::mat4(1.0f), glm::vec3(kSunMarkerRadius));
        glUseProgram(markerProgram);
        glUniformMatrix4fv(markerModelLocation, 1, GL_FALSE, glm::value_ptr(markerModel));
        glUniformMatrix4fv(markerViewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(markerProjectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(markerColorLocation, 1, glm::value_ptr(lightColor));
        marker.draw(marker.submeshes.front());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 7. Cleanup.
    scene.destroy();
    marker.destroy();
    glDeleteTextures(1, &fallback.white);
    glDeleteTextures(1, &fallback.flatNormal);
    shadowMap.destroy();
    glDeleteProgram(program.id);
    glDeleteProgram(depthProgram);
    glDeleteProgram(markerProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
