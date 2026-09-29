// Task 11 — Textures
//
// The animated scene of tasks 05/07/08 (a spinning cube, a sphere orbiting it
// and a moon orbiting the sphere) standing above a floor, lit by the animated
// sun of task 10 with its shadow map. Every vertex carries texture coordinates
// and a tangent, the attributes texturing needs.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders, create the shadow map
//   4. build the meshes and upload them
//   5. render loop: animate -> pass 1 (depth from the sun) -> pass 2 (camera)
//   6. cleanup + glfwTerminate()

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
#include <vector>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 750;

// Cube: two rotations about its own centre, as in task 05.
constexpr float kCubeSide = 1.4f;
constexpr float kCubeSpinY = 0.50f; // rad/s
constexpr float kCubeSpinX = 0.21f; // rad/s

// Sphere: orbits the cube and spins on its own axis. Larger than in task 05, so
// the texture on it can be read.
constexpr float kOrbitRadius = 3.0f;
constexpr float kOrbitSpeed = 0.35f; // rad/s
constexpr float kOrbitTilt = 15.0f;  // degrees, tilt of the orbit plane
constexpr float kSphereScale = 0.70f;
constexpr float kSphereSpin = 0.60f; // rad/s

// Moon: the same construction one level down, in the frame of the sphere.
constexpr float kMoonRadius = 1.15f;
constexpr float kMoonSpeed = 1.10f; // rad/s
constexpr float kMoonTilt = 25.0f;  // degrees
constexpr float kMoonScale = 0.18f;

constexpr int kSphereSlices = 64;
constexpr int kSphereStacks = 32;

constexpr float kFloorSize = 12.0f;
constexpr float kFloorY = -2.2f;

// The sun of task 10: its azimuth turns and its elevation rises and falls.
constexpr float kSunAzimuthSpeed = 0.20f;   // rad/s
constexpr float kSunElevation = 50.0f;      // degrees, mean
constexpr float kSunElevationSwing = 15.0f; // degrees
constexpr float kSunElevationSpeed = 0.30f; // rad/s
constexpr float kSunMarkerDistance = 3.8f;
constexpr float kSunMarkerRadius = 0.15f;

// The sun's orthographic box, as deep as it is wide, holding the whole floor
// from any direction (its half diagonal is 8.49).
constexpr int kShadowMapSize = 2048;
constexpr float kLightDistance = 12.0f;
constexpr float kLightHalfExtent = 9.0f;
constexpr float kLightNear = kLightDistance - kLightHalfExtent;
constexpr float kLightFar = kLightDistance + kLightHalfExtent;

camera gCamera;

bool gShadows = true;
bool gPaused = false;
bool gResetRequested = false;
bool gTitleDirty = true;

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

// Task 09's Blinn-Phong model for a directional light, as in task 10. The
// surface colour and the strength of its highlight are arguments, so that they
// can come from a texture instead of a uniform.
const char* kIlluminationSource = R"(
uniform vec3 uLightDir; // towards the light, unit length
uniform vec3 uCameraPos;
uniform vec3 uAmbientLight; // Ia
uniform vec3 uLightColor;   // Id = Is
uniform float uShininess;

vec3 illuminate(vec3 position, vec3 N, vec3 albedo, float specularStrength, float shadow) {
    vec3 L = uLightDir;
    vec3 V = normalize(uCameraPos - position);

    vec3 ambient = albedo * uAmbientLight;

    float lambert = max(dot(N, L), 0.0);
    vec3 diffuse = albedo * uLightColor * lambert;

    float highlight = 0.0;
    if (lambert > 0.0) {
        vec3 H = normalize(L + V);
        highlight = pow(max(dot(N, H), 0.0), 4.0 * uShininess);
    }
    vec3 specular = specularStrength * uLightColor * highlight;

    return ambient + (1.0 - shadow) * (diffuse + specular);
}
)";

// Pass 2. Besides the world position and the normal, the vertex shader hands
// the texture coordinates and the tangent to the fragment shader, which
// receives them interpolated across the triangle.
const char* kSceneVertexSource = R"(
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec4 vLightSpacePos;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUV = aUV;
    vTangent = mat3(uModel) * aTangent; // along the surface, so it moves like a position
    vLightSpacePos = uLightSpace * world;
    gl_Position = uProjection * uView * world;
}
)";

const char* kSceneFragmentSource = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in vec4 vLightSpacePos;

uniform sampler2D uShadowMap;
uniform bool uShadows;
uniform vec3 uColor;
uniform float uSpecular;

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
    vec3 N = normalize(vNormal);
    float shadow = uShadows ? shadowFactor(vLightSpacePos, N, uLightDir) : 0.0;
    FragColor = vec4(illuminate(vWorldPos, N, uColor, uSpecular, shadow), 1.0);
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

// A vertex of a textured mesh. uv says which point of the image lands here;
// tangent is the direction in which u grows along the surface.
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
};

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

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_S:
        gShadows = !gShadows;
        gTitleDirty = true;
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

// Cube centred at the origin, as in task 05, now with texture coordinates. Each
// face is its own square of the image, (0, 0) to (1, 1), which is only
// possible because its four corners are not shared with the neighbouring
// faces. The six faces are consecutive in the index buffer, six indices each,
// so each one can be drawn on its own.
//
// Side faces keep v pointing up, so an image is upright on all four of them.
Mesh makeCube(float side) {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u; // image right; u x v == normal, so p0 -> p1 -> p2
        glm::vec3 v; // image up;    winds counter-clockwise from outside
    };

    const Face faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    };

    // The corners of a face, counter-clockwise, as texture coordinates.
    const glm::vec2 corners[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    const float h = 0.5f * side;
    Mesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& face : faces) {
        const glm::vec3 centre = face.normal * h;
        const GLuint base = static_cast<GLuint>(mesh.vertices.size());
        for (const glm::vec2& uv : corners) {
            const glm::vec3 p = centre + face.u * h * (2.0f * uv.x - 1.0f) +
                                face.v * h * (2.0f * uv.y - 1.0f);
            mesh.vertices.push_back({p, face.normal, uv, face.u});
        }
        for (const GLuint k : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + k);
        }
    }
    return mesh;
}

// Unit sphere with the spherical mapping of the slides: u follows the
// longitude, v the latitude. For a colatitude phi in [0, pi] and an azimuth
// theta in [0, 2pi]:
//
//   P(theta, phi) = (sin phi cos theta, cos phi, -sin phi sin theta)
//   (u, v)        = (theta / 2pi, 1 - phi / pi)
//
// The minus sign makes u grow eastwards seen from outside, so a world map is not
// mirrored. The seam at theta = 2pi repeats the vertices of theta = 0 with u = 1
// instead of u = 0; sharing them would make the last column of triangles
// interpolate u from 1 back to 0 and squeeze the whole map into it.
Mesh makeSphere(int slices, int stacks) {
    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));

    for (int i = 0; i <= stacks; ++i) {
        const float phi = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float theta =
                glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(slices);
            const glm::vec3 p{std::sin(phi) * std::cos(theta), std::cos(phi),
                              -std::sin(phi) * std::sin(theta)};
            const glm::vec2 uv{static_cast<float>(j) / static_cast<float>(slices),
                               1.0f - static_cast<float>(i) / static_cast<float>(stacks)};
            // dP/dtheta without its sin(phi) factor: still defined at the poles.
            const glm::vec3 tangent{-std::sin(theta), 0.0f, -std::cos(theta)};
            mesh.vertices.push_back({p, p, uv, tangent});
        }
    }

    const int columns = slices + 1;
    mesh.indices.reserve(static_cast<size_t>(6 * slices * stacks));
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const GLuint a = static_cast<GLuint>(i * columns + j);
            const GLuint b = a + static_cast<GLuint>(columns);
            for (const GLuint k : {a, b, a + 1u, a + 1u, b, b + 1u}) {
                mesh.indices.push_back(k);
            }
        }
    }
    return mesh;
}

// Square of side `size` in the xz plane, facing +y. Its texture coordinates run
// from 0 to `tiles`, so with GL_REPEAT the image is laid `tiles` times along
// each side instead of being stretched over the whole floor.
Mesh makeFloor(float size, float tiles) {
    const float h = 0.5f * size;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 tangent(1.0f, 0.0f, 0.0f);
    Mesh mesh;
    mesh.vertices = {
        {{-h, 0.0f, h}, up, {0.0f, 0.0f}, tangent},
        {{h, 0.0f, h}, up, {tiles, 0.0f}, tangent},
        {{h, 0.0f, -h}, up, {tiles, tiles}, tangent},
        {{-h, 0.0f, -h}, up, {0.0f, tiles}, tangent},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3}; // counter-clockwise seen from above
    return mesh;
}

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei count = 0;

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
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(GLuint)),
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
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(offsetof(Vertex, tangent)));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);

        count = static_cast<GLsizei>(mesh.indices.size());
    }

    // A range of the index buffer: the cube draws one face at a time.
    void draw(GLsizei first, GLsizei indices) const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices, GL_UNSIGNED_INT,
                       reinterpret_cast<void*>(static_cast<size_t>(first) * sizeof(GLuint)));
    }

    void draw() const {
        draw(0, count);
    }

    void destroy() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
};

// What a surface looks like. For now a colour and the strength of the highlight.
struct Material {
    glm::vec3 color;
    float specular;
};

// One draw of the frame: a mesh (or a range of it), where, and with which
// material. Both render passes walk the same list.
struct Instance {
    const GpuMesh* mesh;
    GLsizei first;
    GLsizei count;
    glm::mat4 model;
    const Material* material;
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
    GLint shadowMap = -1;
    GLint shadows = -1;
    GLint lightDir = -1;
    GLint cameraPos = -1;
    GLint ambientLight = -1;
    GLint lightColor = -1;
    GLint shininess = -1;
    GLint color = -1;
    GLint specular = -1;

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
        shadows = glGetUniformLocation(id, "uShadows");
        lightDir = glGetUniformLocation(id, "uLightDir");
        cameraPos = glGetUniformLocation(id, "uCameraPos");
        ambientLight = glGetUniformLocation(id, "uAmbientLight");
        lightColor = glGetUniformLocation(id, "uLightColor");
        shininess = glGetUniformLocation(id, "uShininess");
        color = glGetUniformLocation(id, "uColor");
        specular = glGetUniformLocation(id, "uSpecular");
        return true;
    }
};

void updateTitle(GLFWwindow* window) {
    char title[128];
    std::snprintf(title, sizeof(title),
                  "Task 11 - Textures | shadows %s [S] | scene %s | camera %s",
                  gShadows ? "on" : "off", gPaused ? "paused" : "running",
                  gCamera.animated() ? "animated" : "mouse");
    glfwSetWindowTitle(window, title);
}

} // namespace

int main() {
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
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 11 - Textures", nullptr, nullptr);
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
    const GLuint markerProgram =
        createShaderProgram({kMarkerVertexSource}, {kMarkerFragmentSource});
    ShadowMap shadowMap;
    const bool shadowMapReady = shadowMap.create(kShadowMapSize);
    if (!sceneReady || depthProgram == 0 || markerProgram == 0 || !shadowMapReady) {
        if (!shadowMapReady) {
            std::fprintf(stderr, "Shadow map framebuffer is incomplete\n");
        }
        shadowMap.destroy();
        glDeleteProgram(scene.id);
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

    // 4. Geometry. The sphere mesh is uploaded once and drawn for the sphere,
    //    the moon and the sun marker.
    GpuMesh cube;
    cube.upload(makeCube(kCubeSide));
    GpuMesh sphere;
    sphere.upload(makeSphere(kSphereSlices, kSphereStacks));
    GpuMesh ground;
    ground.upload(makeFloor(kFloorSize, 4.0f));

    const Material cubeFaces[6] = {
        {{0.95f, 0.35f, 0.40f}, 0.3f}, {{0.98f, 0.62f, 0.28f}, 0.3f},
        {{0.43f, 0.80f, 0.47f}, 0.3f}, {{0.28f, 0.66f, 0.88f}, 0.3f},
        {{0.62f, 0.52f, 0.93f}, 0.3f}, {{0.95f, 0.83f, 0.36f}, 0.3f},
    };
    const Material sphereMaterial{{0.18f, 0.45f, 0.80f}, 0.5f};
    const Material moonMaterial{{0.70f, 0.70f, 0.68f}, 0.05f};
    const Material floorMaterial{{0.78f, 0.78f, 0.74f}, 0.2f};

    const glm::vec3 ambientLight(0.18f); // Ia
    const glm::vec3 lightColor(1.0f);    // Id = Is
    const float shininess = 16.0f;

    // 5. Render loop.
    float sceneTime = 0.0f;
    gCamera.set_animated(true);
    double lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;

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
            updateTitle(window);
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
        const glm::vec3 yAxis(0.0f, 1.0f, 0.0f);

        glm::mat4 cubeModel = glm::rotate(identity, sceneTime * kCubeSpinY, yAxis);
        cubeModel = glm::rotate(cubeModel, sceneTime * kCubeSpinX, glm::vec3(1.0f, 0.0f, 0.0f));

        // Task 05's hierarchy: the anchor places the sphere on its orbit; the
        // spin and scale act in its own frame; the moon starts from the anchor.
        const glm::mat4 orbitPlane =
            glm::rotate(identity, glm::radians(kOrbitTilt), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::mat4 sphereAnchor =
            orbitPlane * glm::rotate(identity, sceneTime * kOrbitSpeed, yAxis) *
            glm::translate(identity, glm::vec3(kOrbitRadius, 0.0f, 0.0f));
        const glm::mat4 sphereModel = sphereAnchor *
                                      glm::rotate(identity, sceneTime * kSphereSpin, yAxis) *
                                      glm::scale(identity, glm::vec3(kSphereScale));
        const glm::mat4 moonModel =
            sphereAnchor *
            glm::rotate(identity, glm::radians(kMoonTilt), glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::rotate(identity, sceneTime * kMoonSpeed, yAxis) *
            glm::translate(identity, glm::vec3(kMoonRadius, 0.0f, 0.0f)) *
            glm::scale(identity, glm::vec3(kMoonScale));

        std::vector<Instance> instances;
        instances.push_back({&ground, 0, ground.count,
                             glm::translate(identity, glm::vec3(0.0f, kFloorY, 0.0f)),
                             &floorMaterial});
        for (GLsizei face = 0; face < 6; ++face) {
            instances.push_back({&cube, 6 * face, 6, cubeModel, &cubeFaces[face]});
        }
        instances.push_back({&sphere, 0, sphere.count, sphereModel, &sphereMaterial});
        instances.push_back({&sphere, 0, sphere.count, moonModel, &moonMaterial});

        // --- Pass 1: depth from the sun into the shadow map. ------------------
        const glm::mat4 lightView =
            glm::lookAt(kLightDistance * lightDir, glm::vec3(0.0f), yAxis);
        const glm::mat4 lightProjection =
            glm::ortho(-kLightHalfExtent, kLightHalfExtent, -kLightHalfExtent, kLightHalfExtent,
                       kLightNear, kLightFar);
        const glm::mat4 lightSpace = lightProjection * lightView;

        if (gShadows) {
            glBindFramebuffer(GL_FRAMEBUFFER, shadowMap.fbo);
            glViewport(0, 0, shadowMap.size, shadowMap.size);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUseProgram(depthProgram);
            glUniformMatrix4fv(depthLightSpaceLocation, 1, GL_FALSE, glm::value_ptr(lightSpace));
            for (const Instance& instance : instances) {
                glUniformMatrix4fv(depthModelLocation, 1, GL_FALSE,
                                   glm::value_ptr(instance.model));
                instance.mesh->draw(instance.first, instance.count);
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

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shadowMap.depth);

        glUseProgram(scene.id);
        glUniform1i(scene.shadowMap, 0);
        glUniform1i(scene.shadows, gShadows ? 1 : 0);
        glUniformMatrix4fv(scene.lightSpace, 1, GL_FALSE, glm::value_ptr(lightSpace));
        glUniformMatrix4fv(scene.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(scene.projection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(scene.lightDir, 1, glm::value_ptr(lightDir));
        glUniform3fv(scene.cameraPos, 1, glm::value_ptr(cameraPos));
        glUniform3fv(scene.ambientLight, 1, glm::value_ptr(ambientLight));
        glUniform3fv(scene.lightColor, 1, glm::value_ptr(lightColor));
        glUniform1f(scene.shininess, shininess);
        for (const Instance& instance : instances) {
            // The inverse transpose keeps normals perpendicular under any scale.
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(instance.model)));
            glUniformMatrix4fv(scene.model, 1, GL_FALSE, glm::value_ptr(instance.model));
            glUniformMatrix3fv(scene.normalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniform3fv(scene.color, 1, glm::value_ptr(instance.material->color));
            glUniform1f(scene.specular, instance.material->specular);
            instance.mesh->draw(instance.first, instance.count);
        }

        // The sun marker sits along the light direction.
        const glm::mat4 markerModel = glm::translate(identity, kSunMarkerDistance * lightDir) *
                                      glm::scale(identity, glm::vec3(kSunMarkerRadius));
        glUseProgram(markerProgram);
        glUniformMatrix4fv(markerModelLocation, 1, GL_FALSE, glm::value_ptr(markerModel));
        glUniformMatrix4fv(markerViewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(markerProjectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(markerColorLocation, 1, glm::value_ptr(lightColor));
        sphere.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    cube.destroy();
    sphere.destroy();
    ground.destroy();
    shadowMap.destroy();
    glDeleteProgram(scene.id);
    glDeleteProgram(depthProgram);
    glDeleteProgram(markerProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
