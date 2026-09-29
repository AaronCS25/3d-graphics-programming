// Task 10 — Shadow Mapping
//
// An animated scene lit by a directional light (a sun) that circles overhead: a
// floor, an occluder in the middle (generated torus/sphere or a PLY file) that
// spins in place, and two spheres orbiting around it. The camera is the shared
// arcball camera from task 08; the lighting is the Blinn-Phong model of task 09.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders
//   4. upload every CHE (G + vertex normals to a VBO, V to an EBO)
//   5. render loop: animate -> draw every instance -> glfwSwapBuffers
//   6. cleanup + glfwTerminate()
//
// Usage:  task10 [sphere | torus | path/to/model.ply ...] [--check]
//         The first mesh is the occluder. --check prints the statistics of every
//         listed mesh and exits without opening a window. While running, drop a
//         .ply onto the window to use it as the occluder.

#include "che.hpp"
#include "mesh_io.hpp"
#include "primitives.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

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
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 750;

constexpr float kFloorSize = 10.0f;
constexpr float kFloorY = -1.0f;
constexpr float kOccluderSpin = 0.35f; // rad/s

// The sun circles the scene while its elevation rises and falls, so shadows
// both turn around the objects and stretch and shrink.
constexpr float kSunAzimuthSpeed = 0.30f;   // rad/s
constexpr float kSunElevation = 50.0f;      // degrees, mean
constexpr float kSunElevationSwing = 15.0f; // degrees
constexpr float kSunElevationSpeed = 0.45f; // rad/s
constexpr float kSunMarkerDistance = 3.2f;
constexpr float kSunMarkerRadius = 0.12f;

// The sun's rays are parallel, so its "camera" is an orthographic box. It sits
// kLightDistance away along the light direction, looking at the origin, and is
// wide and deep enough to hold the whole floor from any direction (the floor's
// half diagonal is 7.07).
constexpr int kMinShadowMapSize = 256;
constexpr int kMaxShadowMapSize = 4096;
constexpr float kLightDistance = 10.0f;
constexpr float kLightHalfExtent = 7.5f;
constexpr float kLightNear = kLightDistance - kLightHalfExtent;
constexpr float kLightFar = kLightDistance + kLightHalfExtent;

// Two spheres orbit the occluder: one low, passing through its shadow, and one
// high, casting its own shadow onto the occluder and the floor.
struct Orbiter {
    float radius;
    float height;
    float bob;   // vertical amplitude
    float speed; // rad/s, negative = clockwise
    float scale;
    glm::vec3 color;
};

const Orbiter kOrbiters[] = {
    {2.2f, -0.55f, 0.25f, 0.55f, 0.40f, {0.30f, 0.62f, 0.90f}},
    {1.6f, 1.60f, 0.20f, -0.80f, 0.28f, {0.95f, 0.80f, 0.35f}},
};

camera gCamera(glm::vec3(0.0f, -0.3f, 0.0f), 9.0f, 4.5f, 1.0f);

bool gUseBias = true; // off shows shadow acne
bool gUsePcf = true;  // off shows the hard, texel-sized edges
int gShadowMapSize = 2048;
bool gShowShadowMap = false;
bool gPaused = false;
bool gResetRequested = false;
bool gTitleDirty = true;
std::string gDroppedPath; // set by the drop callback, consumed by the render loop

const char* kVersionSource = "#version 330 core\n";

// Pass 1: the scene seen from the light. Only gl_Position matters: the depth
// buffer, which is the shadow map, is filled by the rasterizer on its own.
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

// Task 09's reflection model, Blinn-Phong variant, for a directional light: L
// is the same for every fragment instead of pointing at a position.
const char* kIlluminationSource = R"(
uniform vec3 uLightDir; // towards the light, unit length
uniform vec3 uCameraPos;
uniform vec3 uAmbientLight; // Ia
uniform vec3 uLightColor;   // Id = Is
uniform vec3 uKa;
uniform vec3 uKd;
uniform vec3 uKs;
uniform float uShininess;

// shadow = 1 blocks the light that comes straight from the sun: diffuse and
// specular. The ambient term stands for light arriving from everywhere else.
vec3 illuminate(vec3 position, vec3 normal, float shadow) {
    vec3 N = normalize(normal);
    vec3 L = uLightDir;
    vec3 V = normalize(uCameraPos - position);

    vec3 ambient = uKa * uAmbientLight;

    float lambert = max(dot(N, L), 0.0);
    vec3 diffuse = uKd * uLightColor * lambert;

    float highlight = 0.0;
    if (lambert > 0.0) {
        vec3 H = normalize(L + V);
        highlight = pow(max(dot(N, H), 0.0), 4.0 * uShininess);
    }
    vec3 specular = uKs * uLightColor * highlight;

    return ambient + (1.0 - shadow) * (diffuse + specular);
}
)";

// Pass 2: the scene seen from the camera. Each vertex is also transformed with
// the light's matrices, so the fragment knows where it falls in the shadow map.
const char* kSceneVertexSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightSpacePos;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vLightSpacePos = uLightSpace * world;
    gl_Position = uProjection * uView * world;
}
)";

const char* kSceneFragmentSource = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightSpacePos;

uniform sampler2D uShadowMap;
uniform bool uUseBias;
uniform bool uUsePcf;

out vec4 FragColor;

// 1 if something between the sun and this fragment was recorded in pass 1, 0 if
// not; with PCF, the fraction of the texels around it that are blocked.
float shadowFactor(vec4 lightSpacePos, vec3 N, vec3 L) {
    // Perspective divide (w = 1 for an orthographic light), then from NDC
    // [-1, 1] to texture coordinates and depth [0, 1].
    vec3 p = lightSpacePos.xyz / lightSpacePos.w;
    p = p * 0.5 + 0.5;

    // Beyond the light's far plane nothing was recorded, so nothing blocks it.
    if (p.z > 1.0) {
        return 0.0;
    }

    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));

    // Each texel stores one depth for a small patch of surface. Compared with
    // that patch, a lit surface is half in front of and half behind its own
    // stored depth, and shadows itself in stripes: shadow acne. The bias moves
    // the comparison slightly towards the light, more where the surface is
    // tilted away from it and one texel spans a larger depth range.
    //
    // The slides' max(0.05 (1 - N.L), 0.005) is written for a light box a few
    // units deep. Ours is as deep as it is wide, so one texel across is also
    // one texel of depth, and the same bias is counted here in texels: 1 to 10,
    // whatever the resolution of the map.
    float bias = uUseBias ? texel.x * max(10.0 * (1.0 - dot(N, L)), 1.0) : 0.0;

    if (!uUsePcf) {
        return p.z - bias > texture(uShadowMap, p.xy).r ? 1.0 : 0.0;
    }

    // Percentage-closer filtering: the same test against the 3x3 texels around
    // p, averaged. Depths are not blended, the answers are, so an edge fades
    // over about one texel instead of stepping from lit to dark.
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
    float shadow = shadowFactor(vLightSpacePos, normalize(vNormal), uLightDir);
    FragColor = vec4(illuminate(vWorldPos, vNormal, shadow), 1.0);
}
)";

// Debug view of pass 1: the shadow map drawn as a grey image in a corner. The
// quad is generated from gl_VertexID, so it needs no vertex buffer.
const char* kOverlayVertexSource = R"(
out vec2 vUV;

void main() {
    vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vUV = corners[gl_VertexID];
    gl_Position = vec4(2.0 * vUV - 1.0, 0.0, 1.0);
}
)";

// The orthographic depth is linear, so it can be shown as it is: dark = near
// the sun, white = far or nothing drawn.
const char* kOverlayFragmentSource = R"(
in vec2 vUV;

uniform sampler2D uDepthMap;

out vec4 FragColor;

void main() {
    FragColor = vec4(vec3(texture(uDepthMap, vUV).r), 1.0);
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

// Drag-and-drop: GLFW hands over the paths; only the first one is used. Loading
// is deferred to the render loop so a broken file cannot take down a callback.
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
    case GLFW_KEY_SPACE:
        gPaused = !gPaused;
        gTitleDirty = true;
        break;
    case GLFW_KEY_C:
        gCamera.set_animated(!gCamera.animated());
        gTitleDirty = true;
        break;
    case GLFW_KEY_M:
        gShowShadowMap = !gShowShadowMap;
        break;
    case GLFW_KEY_B:
        gUseBias = !gUseBias;
        gTitleDirty = true;
        break;
    case GLFW_KEY_P:
        gUsePcf = !gUsePcf;
        gTitleDirty = true;
        break;
    case GLFW_KEY_UP:
        gShadowMapSize = std::min(gShadowMapSize * 2, kMaxShadowMapSize);
        gTitleDirty = true;
        break;
    case GLFW_KEY_DOWN:
        gShadowMapSize = std::max(gShadowMapSize / 2, kMinShadowMapSize);
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
// first and a shared function can be spliced in before the stage that calls it.
GLuint compileShader(GLenum type, std::vector<const char*> sources) {
    sources.insert(sources.begin(), kVersionSource);
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
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

GLuint createShaderProgram(const std::vector<const char*>& vertexSources,
                           const std::vector<const char*>& fragmentSources) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSources);
    if (vertexShader == 0) {
        return 0;
    }
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSources);
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

struct Attributes {
    glm::vec3 position;
    glm::vec3 normal;
};

// A CHE resident on the GPU. V goes to the element buffer untouched.
struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indices = 0;

    void create() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Attributes),
                              reinterpret_cast<void*>(offsetof(Attributes, position)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Attributes),
                              reinterpret_cast<void*>(offsetof(Attributes, normal)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void upload(const CHE& mesh) {
        const std::vector<glm::vec3> normals = mesh.vertex_normals();

        std::vector<Attributes> attributes;
        attributes.reserve(static_cast<size_t>(mesh.n_vertices()));
        for (int v = 0; v < mesh.n_vertices(); ++v) {
            attributes.push_back({mesh.G(v), normals[static_cast<size_t>(v)]});
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(attributes.size() * sizeof(Attributes)),
                     attributes.data(), GL_STATIC_DRAW);

        const std::vector<int>& element = mesh.indices();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(element.size() * sizeof(int)), element.data(),
                     GL_STATIC_DRAW);
        glBindVertexArray(0);

        indices = mesh.n_half_edges();
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices, GL_UNSIGNED_INT, nullptr);
    }

    void destroy() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
};

// One object of the frame: which mesh, where, and in which colour. The scene is
// rebuilt as a list every frame so that each render pass walks the same list.
struct Instance {
    const GpuMesh* mesh;
    glm::mat4 model;
    glm::vec3 color;
};

// The target of pass 1: a framebuffer whose only attachment is a depth texture.
// Pass 2 then reads that texture as the shadow map.
struct ShadowMap {
    GLuint fbo = 0;
    GLuint depth = 0;
    int size = 0;

    bool create(int resolution) {
        glGenTextures(1, &depth);
        resize(resolution);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // Outside the map the border answers depth 1, the far plane: nothing
        // recorded there can be in front of the fragment, so it stays lit.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        glDrawBuffer(GL_NONE); // no colour attachment: nothing to write or read
        glReadBuffer(GL_NONE);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    // New storage for the same texture object: the framebuffer attachment and
    // the sampling parameters stay as they are.
    void resize(int resolution) {
        size = resolution;
        glBindTexture(GL_TEXTURE_2D, depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, size, size, 0, GL_DEPTH_COMPONENT,
                     GL_FLOAT, nullptr);
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
    GLint shadowMap = -1;
    GLint useBias = -1;
    GLint usePcf = -1;
    GLint lightDir = -1;
    GLint cameraPos = -1;
    GLint ambientLight = -1;
    GLint lightColor = -1;
    GLint ka = -1;
    GLint kd = -1;
    GLint ks = -1;
    GLint shininess = -1;

    bool create() {
        id = createShaderProgram({kSceneVertexSource}, {kIlluminationSource, kSceneFragmentSource});
        if (id == 0) {
            return false;
        }
        model = glGetUniformLocation(id, "uModel");
        view = glGetUniformLocation(id, "uView");
        projection = glGetUniformLocation(id, "uProjection");
        normalMatrix = glGetUniformLocation(id, "uNormalMatrix");
        lightSpace = glGetUniformLocation(id, "uLightSpace");
        shadowMap = glGetUniformLocation(id, "uShadowMap");
        useBias = glGetUniformLocation(id, "uUseBias");
        usePcf = glGetUniformLocation(id, "uUsePcf");
        lightDir = glGetUniformLocation(id, "uLightDir");
        cameraPos = glGetUniformLocation(id, "uCameraPos");
        ambientLight = glGetUniformLocation(id, "uAmbientLight");
        lightColor = glGetUniformLocation(id, "uLightColor");
        ka = glGetUniformLocation(id, "uKa");
        kd = glGetUniformLocation(id, "uKd");
        ks = glGetUniformLocation(id, "uKs");
        shininess = glGetUniformLocation(id, "uShininess");
        return true;
    }
};

// "sphere" and "torus" are generated; anything else is read as a PLY path.
CHE loadMesh(const std::string& name) {
    if (name == "sphere") {
        return make_sphere(1.0f, 48, 24);
    }
    if (name == "torus") {
        return make_torus(0.7f, 0.3f, 96, 40);
    }
    return load_ply(name);
}

// The generated torus lies in the xz plane; stood on its rim it casts a
// ring-shaped shadow, which reads much better than a flat one.
glm::mat4 uprightFor(const std::string& name) {
    if (name == "torus") {
        return glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    return glm::mat4(1.0f);
}

// How far up the occluder must move for its lowest point to touch the floor.
// Spinning about y never changes heights, so this is computed once per mesh.
float restingLift(const CHE& mesh, const glm::mat4& upright) {
    float lowest = std::numeric_limits<float>::max();
    for (int v = 0; v < mesh.n_vertices(); ++v) {
        lowest = std::min(lowest, (upright * glm::vec4(mesh.G(v), 1.0f)).y);
    }
    return kFloorY - lowest;
}

void printStats(const std::string& name, const CHE& mesh) {
    const std::string problem = mesh.validate();
    const int euler = mesh.n_vertices() - mesh.n_edges() + mesh.n_triangles();
    std::printf("%s: V=%d E=%d F=%d euler=%d %s, %s\n", name.c_str(), mesh.n_vertices(),
                mesh.n_edges(), mesh.n_triangles(), euler, mesh.is_closed() ? "closed" : "open",
                problem.empty() ? "valid CHE" : problem.c_str());
}

void updateTitle(GLFWwindow* window, const std::string& occluderName) {
    char title[256];
    std::snprintf(title, sizeof(title),
                  "Task 10 - Shadow Mapping | %s | map %dx%d [M] | bias %s [B] | PCF %s [P] | "
                  "scene %s | camera %s",
                  occluderName.c_str(), gShadowMapSize, gShadowMapSize, gUseBias ? "on" : "off",
                  gUsePcf ? "on" : "off", gPaused ? "paused" : "running",
                  gCamera.animated() ? "animated" : "mouse");
    glfwSetWindowTitle(window, title);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> meshNames;
    bool checkOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            checkOnly = true;
        } else {
            meshNames.emplace_back(argv[i]);
        }
    }
    if (meshNames.empty()) {
        meshNames.emplace_back("torus");
    }

    // --check: load every mesh on the command line, report, and exit.
    if (checkOnly) {
        int failures = 0;
        for (const std::string& name : meshNames) {
            try {
                const CHE candidate = loadMesh(name);
                printStats(name, candidate);
                failures += candidate.validate().empty() ? 0 : 1;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", name.c_str(), e.what());
                ++failures;
            }
        }
        return failures;
    }

    CHE occluder;
    try {
        occluder = loadMesh(meshNames.front());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not load '%s': %s\n", meshNames.front().c_str(), e.what());
        return 1;
    }
    printStats(meshNames.front(), occluder);

    // 1. Window + OpenGL 3.3 core context.
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
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 10 - Shadow Mapping", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Context, function pointers, depth test.
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

    // 3. Shaders and the shadow map.
    SceneProgram scene;
    const bool sceneReady = scene.create();
    const GLuint depthProgram = createShaderProgram({kDepthVertexSource}, {kDepthFragmentSource});
    const GLuint overlayProgram =
        createShaderProgram({kOverlayVertexSource}, {kOverlayFragmentSource});
    const GLuint markerProgram =
        createShaderProgram({kMarkerVertexSource}, {kMarkerFragmentSource});
    ShadowMap shadowMap;
    const bool shadowMapReady = shadowMap.create(gShadowMapSize);
    if (!sceneReady || depthProgram == 0 || overlayProgram == 0 || markerProgram == 0 ||
        !shadowMapReady) {
        if (!shadowMapReady) {
            std::fprintf(stderr, "Shadow map framebuffer is incomplete\n");
        }
        shadowMap.destroy();
        glDeleteProgram(scene.id);
        glDeleteProgram(depthProgram);
        glDeleteProgram(overlayProgram);
        glDeleteProgram(markerProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const GLint depthLightSpaceLocation = glGetUniformLocation(depthProgram, "uLightSpace");
    const GLint depthModelLocation = glGetUniformLocation(depthProgram, "uModel");
    const GLint overlayDepthMapLocation = glGetUniformLocation(overlayProgram, "uDepthMap");
    const GLint markerModelLocation = glGetUniformLocation(markerProgram, "uModel");
    const GLint markerViewLocation = glGetUniformLocation(markerProgram, "uView");
    const GLint markerProjectionLocation = glGetUniformLocation(markerProgram, "uProjection");
    const GLint markerColorLocation = glGetUniformLocation(markerProgram, "uColor");

    // 4. Geometry. The unit sphere is uploaded once and drawn for both orbiters.
    std::string occluderName = meshNames.front();
    glm::mat4 upright = uprightFor(occluderName);
    float lift = restingLift(occluder, upright);

    GpuMesh occluderGpu;
    occluderGpu.create();
    occluderGpu.upload(occluder);

    GpuMesh sphereGpu;
    sphereGpu.create();
    sphereGpu.upload(make_sphere(1.0f, 48, 24));

    GpuMesh floorGpu;
    floorGpu.create();
    floorGpu.upload(make_plane(kFloorSize));

    // The overlay quad has no attributes, but a core profile still needs a VAO bound.
    GLuint emptyVao = 0;
    glGenVertexArrays(1, &emptyVao);

    // ka = kd per object; ks is grey, so highlights take the colour of the light.
    const glm::vec3 ambientLight(0.18f); // Ia
    const glm::vec3 lightColor(1.0f);    // Id = Is
    const glm::vec3 ks(0.35f);
    const float shininess = 16.0f;
    const glm::vec3 occluderColor(0.85f, 0.42f, 0.30f);
    const glm::vec3 floorColor(0.78f, 0.78f, 0.74f);

    // 5. Render loop.
    float sceneTime = 0.0f;
    double lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;

        if (!gDroppedPath.empty()) {
            try {
                occluder = loadMesh(gDroppedPath);
                printStats(gDroppedPath, occluder);
                occluderName = gDroppedPath;
                upright = uprightFor(occluderName);
                lift = restingLift(occluder, upright);
                occluderGpu.upload(occluder);
                gTitleDirty = true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", gDroppedPath.c_str(),
                             e.what());
            }
            gDroppedPath.clear();
        }

        if (gResetRequested) {
            gResetRequested = false;
            sceneTime = 0.0f;
            gCamera.reset();
        }
        if (!gPaused) {
            sceneTime += deltaTime;
            gCamera.update(deltaTime);
        }
        if (gTitleDirty) {
            gTitleDirty = false;
            updateTitle(window, occluderName);
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            glfwWaitEvents(); // minimized: no valid aspect ratio or pixels to draw
            lastFrame = glfwGetTime();
            continue;
        }

        // --- Animation: the sun and the instances of this frame. -------------
        const float azimuth = sceneTime * kSunAzimuthSpeed;
        const float elevation = glm::radians(
            kSunElevation + kSunElevationSwing * std::sin(sceneTime * kSunElevationSpeed));
        const glm::vec3 lightDir(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
                                 std::cos(elevation) * std::sin(azimuth));

        const glm::mat4 identity(1.0f);
        std::vector<Instance> instances;
        instances.push_back({&floorGpu, glm::translate(identity, glm::vec3(0.0f, kFloorY, 0.0f)),
                             floorColor});
        instances.push_back({&occluderGpu,
                             glm::translate(identity, glm::vec3(0.0f, lift, 0.0f)) *
                                 glm::rotate(identity, sceneTime * kOccluderSpin,
                                             glm::vec3(0.0f, 1.0f, 0.0f)) *
                                 upright,
                             occluderColor});
        for (const Orbiter& orbiter : kOrbiters) {
            const float angle = sceneTime * orbiter.speed;
            const glm::vec3 centre(orbiter.radius * std::cos(angle),
                                   orbiter.height + orbiter.bob * std::sin(2.0f * angle),
                                   orbiter.radius * std::sin(angle));
            instances.push_back({&sphereGpu,
                                 glm::translate(identity, centre) *
                                     glm::scale(identity, glm::vec3(orbiter.scale)),
                                 orbiter.color});
        }

        // --- Pass 1: depth from the light into the shadow map. ----------------
        // lightDir is never vertical (elevation <= 65 degrees), so +y is a valid
        // up vector for the light's view.
        const glm::mat4 lightView = glm::lookAt(kLightDistance * lightDir, glm::vec3(0.0f),
                                                glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 lightProjection =
            glm::ortho(-kLightHalfExtent, kLightHalfExtent, -kLightHalfExtent, kLightHalfExtent,
                       kLightNear, kLightFar);
        const glm::mat4 lightSpace = lightProjection * lightView;

        if (shadowMap.size != gShadowMapSize) {
            shadowMap.resize(gShadowMapSize);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
        glViewport(0, 0, shadowMap.size, shadowMap.size);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(depthProgram);
        glUniformMatrix4fv(depthLightSpaceLocation, 1, GL_FALSE, glm::value_ptr(lightSpace));
        for (const Instance& instance : instances) {
            glUniformMatrix4fv(depthModelLocation, 1, GL_FALSE, glm::value_ptr(instance.model));
            instance.mesh->draw();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // --- Pass 2: the scene from the camera, tested against the map. -------
        glViewport(0, 0, width, height);
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 view = gCamera.view();
        const glm::mat4 projection = gCamera.projection(width, height);
        const glm::vec3 cameraPos = gCamera.position();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depth);

        glUseProgram(scene.id);
        glUniform1i(scene.shadowMap, 0);
        glUniform1i(scene.useBias, gUseBias ? 1 : 0);
        glUniform1i(scene.usePcf, gUsePcf ? 1 : 0);
        glUniformMatrix4fv(scene.lightSpace, 1, GL_FALSE, glm::value_ptr(lightSpace));
        glUniformMatrix4fv(scene.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(scene.projection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(scene.lightDir, 1, glm::value_ptr(lightDir));
        glUniform3fv(scene.cameraPos, 1, glm::value_ptr(cameraPos));
        glUniform3fv(scene.ambientLight, 1, glm::value_ptr(ambientLight));
        glUniform3fv(scene.lightColor, 1, glm::value_ptr(lightColor));
        glUniform3fv(scene.ks, 1, glm::value_ptr(ks));
        glUniform1f(scene.shininess, shininess);
        for (const Instance& instance : instances) {
            // The inverse transpose keeps normals perpendicular under any scale.
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(instance.model)));
            glUniformMatrix4fv(scene.model, 1, GL_FALSE, glm::value_ptr(instance.model));
            glUniformMatrix3fv(scene.normalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3fv(scene.ka, 1, glm::value_ptr(instance.color));
            glUniform3fv(scene.kd, 1, glm::value_ptr(instance.color));
            instance.mesh->draw();
        }

        // The marker sits along the light direction, where the sun "is".
        const glm::mat4 markerModel =
            glm::translate(identity, kSunMarkerDistance * lightDir) *
            glm::scale(identity, glm::vec3(kSunMarkerRadius));
        glUseProgram(markerProgram);
        glUniformMatrix4fv(markerModelLocation, 1, GL_FALSE, glm::value_ptr(markerModel));
        glUniformMatrix4fv(markerViewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(markerProjectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(markerColorLocation, 1, glm::value_ptr(lightColor));
        sphereGpu.draw();

        // The shadow map itself, in the bottom-left corner, over everything.
        if (gShowShadowMap) {
            const int side = std::min(width, height) / 3;
            glViewport(0, 0, side, side);
            glDisable(GL_DEPTH_TEST);
            glUseProgram(overlayProgram);
            glUniform1i(overlayDepthMapLocation, 0);
            glBindVertexArray(emptyVao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glEnable(GL_DEPTH_TEST);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    occluderGpu.destroy();
    sphereGpu.destroy();
    floorGpu.destroy();
    glDeleteVertexArrays(1, &emptyVao);
    shadowMap.destroy();
    glDeleteProgram(scene.id);
    glDeleteProgram(depthProgram);
    glDeleteProgram(overlayProgram);
    glDeleteProgram(markerProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
