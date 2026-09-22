// Task 08 — Quaternion Arcball Camera
//
// The scene from tasks 05/07, viewed through the shared camera. Drag to rotate
// its frame with an arcball quaternion; scroll to change its distance. C switches
// between manual control and the animated orbit. Camera math lives in common/.
//
// The shader applies projection * view * model: local -> world -> camera ->
// clip coordinates. All objects share the camera matrices; each keeps its own
// model matrix. Mouse callbacks only forward input to the camera.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;

// The same camera is available to every task through gfx_common.
camera gCamera;

// Cube: two rotations about its own centre, at rates that do not divide each
// other, so the animation never visibly repeats.
constexpr float kCubeSide = 1.4f;
constexpr float kCubeSpinY = 0.80f; // rad/s
constexpr float kCubeSpinX = 0.31f; // rad/s

// Sphere: orbits the cube, and spins on its own axis while it does.
constexpr float kOrbitRadius = 2.7f;
constexpr float kOrbitSpeed = 0.70f;  // rad/s
constexpr float kOrbitTilt = 22.0f;   // degrees, tilt of the orbit plane
constexpr float kSphereScale = 0.45f; // the mesh is a unit sphere
constexpr float kSphereSpin = 1.60f;  // rad/s

// Moon: the same construction one level down, in the frame of the sphere.
constexpr float kMoonRadius = 0.95f;
constexpr float kMoonSpeed = 2.40f; // rad/s
constexpr float kMoonTilt = 35.0f;  // degrees
constexpr float kMoonScale = 0.15f;

constexpr int kSphereSlices = 48;
constexpr int kSphereStacks = 24;
constexpr int kRingSegments = 192;

constexpr float kMinSpeed = 0.1f;
constexpr float kMaxSpeed = 4.0f;
constexpr float kSpeedStep = 1.25f;

// Runtime state driven by the key callback.
bool gPaused = false;
bool gWireframe = false;
bool gShowOrbits = true;
bool gResetRequested = false;
float gSpeed = 1.0f;
bool gTitleDirty = true;

// aColor is modulated by uTint so one mesh can be drawn in several colors, and
// uLit is 0 for the orbit guides, which have no meaningful normal.
const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform vec3 uTint;

out vec3 vNormal;
out vec3 vColor;

void main() {
    vNormal = uNormalMatrix * aNormal;
    vColor = aColor * uTint;
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)";

const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vNormal;
in vec3 vColor;

uniform float uLit;

out vec4 FragColor;

void main() {
    if (uLit == 0.0) {
        FragColor = vec4(vColor, 1.0);
        return; // orbit guides have no surface normal
    }
    vec3 lightDir = normalize(vec3(0.45, 0.80, 0.55));
    vec3 n = normalize(vNormal);

    // Ambient + Lambert, plus a weak fill from behind so silhouettes on the far
    // side of the orbit do not go pure black.
    float diffuse = max(dot(n, lightDir), 0.0);
    float fill = 0.12 * max(dot(n, -lightDir), 0.0);
    vec3 shaded = vColor * (0.42 + 0.70 * diffuse + fill);

    FragColor = vec4(shaded, 1.0);
}
)";

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// The attribute offsets below are written as multiples of sizeof(glm::vec3),
// which is only valid while the struct stays tightly packed.
static_assert(sizeof(Vertex) == 9 * sizeof(float), "Vertex must be tightly packed");

// Indices are optional: a mesh without them is drawn with glDrawArrays.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
};

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei count = 0;
    bool indexed = false;
};

// The uniforms that change between draw calls; view and projection are set once
// per frame and are not part of this.
struct Uniforms {
    GLint model = -1;
    GLint view = -1;
    GLint projection = -1;
    GLint normalMatrix = -1;
    GLint tint = -1;
    GLint lit = -1;
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
    // Toggles fire once per press; holding a key must not flip them repeatedly.
    if (action == GLFW_PRESS) {
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
        case GLFW_KEY_W:
            gWireframe = !gWireframe;
            break;
        case GLFW_KEY_O:
            gShowOrbits = !gShowOrbits;
            break;
        case GLFW_KEY_R:
            gResetRequested = true;
            break;
        default:
            break;
        }
    }

    // Speed is continuous, so auto-repeat is welcome here.
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key) {
        case GLFW_KEY_UP:
            gSpeed = std::min(gSpeed * kSpeedStep, kMaxSpeed);
            gTitleDirty = true;
            break;
        case GLFW_KEY_DOWN:
            gSpeed = std::max(gSpeed / kSpeedStep, kMinSpeed);
            gTitleDirty = true;
            break;
        default:
            break;
        }
    }
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
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

// Cube centred at the origin. Each face carries its own normal and its own
// color, so its four corners cannot be shared with the neighbouring faces:
// 24 vertices and 36 indices, instead of the 8 vertices a smooth cube would use.
Mesh makeCube(float side) {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u; // u x v == normal, which makes p0 -> p1 -> p2 wind
        glm::vec3 v; // counter-clockwise when the face is seen from outside
        glm::vec3 color;
    };

    const Face faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.95f, 0.35f, 0.40f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.98f, 0.62f, 0.28f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.43f, 0.80f, 0.47f}},
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.28f, 0.66f, 0.88f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.62f, 0.52f, 0.93f}},
        {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.83f, 0.36f}},
    };

    const float h = 0.5f * side;
    Mesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& face : faces) {
        const glm::vec3 centre = face.normal * h;
        const GLuint base = static_cast<GLuint>(mesh.vertices.size());
        mesh.vertices.push_back({centre - face.u * h - face.v * h, face.normal, face.color});
        mesh.vertices.push_back({centre + face.u * h - face.v * h, face.normal, face.color});
        mesh.vertices.push_back({centre + face.u * h + face.v * h, face.normal, face.color});
        mesh.vertices.push_back({centre - face.u * h + face.v * h, face.normal, face.color});
        for (const GLuint k : {0u, 1u, 2u, 0u, 2u, 3u}) {
            mesh.indices.push_back(base + k);
        }
    }
    return mesh;
}

// Unit sphere, P(u, v) = (sin v cos u, cos v, sin v sin u), with u the azimuth
// over [0, 2pi] and v the colatitude over [0, pi]. On a unit sphere the normal
// of a point is the point itself.
//
// The seam at u = 2pi duplicates the vertices of u = 0 rather than reusing
// them: the two copies need a different checker parity, and this mesh is only
// ever fed to the rasterizer, never walked as a topology (that is task 03).
Mesh makeSphere(int slices, int stacks) {
    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>((slices + 1) * (stacks + 1)));

    for (int i = 0; i <= stacks; ++i) {
        const float phi = glm::pi<float>() * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j) {
            const float theta =
                glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(slices);
            const glm::vec3 p{std::sin(phi) * std::cos(theta), std::cos(phi),
                              std::sin(phi) * std::sin(theta)};

            // A single-color sphere would hide its own spin: the checker is what
            // makes the axial rotation readable in the video.
            const bool dark = ((i / 2) + (j / 3)) % 2 == 0;
            mesh.vertices.push_back(
                {p, p,
                 dark ? glm::vec3{0.18f, 0.45f, 0.80f} : glm::vec3{0.88f, 0.92f, 0.97f}});
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

// Unit circle in the XZ plane, drawn as a GL_LINE_LOOP to make each orbit
// visible. Shadeless (uLit = 0), so the zero normal is never used.
Mesh makeRing(int segments) {
    Mesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a =
            glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        mesh.vertices.push_back(
            {{std::cos(a), 0.0f, std::sin(a)}, glm::vec3(0.0f), glm::vec3(1.0f)});
    }
    return mesh; // no indices -> glDrawArrays
}

GpuMesh upload(const Mesh& mesh) {
    GpuMesh gpu;
    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);

    glBindVertexArray(gpu.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);

    // One interleaved buffer, three attributes. The location numbers here are
    // the same ones the vertex shader declares in its `layout (location = N)`.
    const GLsizei stride = sizeof(Vertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(sizeof(glm::vec3)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(2 * sizeof(glm::vec3)));
    glEnableVertexAttribArray(2);

    if (!mesh.indices.empty()) {
        glGenBuffers(1, &gpu.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(GLuint)),
                     mesh.indices.data(), GL_STATIC_DRAW);
        gpu.indexed = true;
        gpu.count = static_cast<GLsizei>(mesh.indices.size());
    } else {
        gpu.count = static_cast<GLsizei>(mesh.vertices.size());
    }

    // Unbind the VAO first: the element buffer binding is VAO state, and
    // clearing it while the VAO is still bound would erase it.
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return gpu;
}

void destroy(GpuMesh& mesh) {
    glDeleteVertexArrays(1, &mesh.vao);
    glDeleteBuffers(1, &mesh.vbo);
    if (mesh.indexed) {
        glDeleteBuffers(1, &mesh.ebo);
    }
    mesh = GpuMesh{};
}

void drawMesh(const GpuMesh& mesh, GLenum mode, const glm::mat4& model, const glm::vec3& tint,
              bool lit, const Uniforms& uniforms) {
    glUniformMatrix4fv(uniforms.model, 1, GL_FALSE, glm::value_ptr(model));

    // Normals are directions, not positions, so they do not transform by the
    // model matrix. The inverse transpose is the matrix that keeps them
    // perpendicular to the surface under any rotation and scale.
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    glUniformMatrix3fv(uniforms.normalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));

    glUniform3fv(uniforms.tint, 1, glm::value_ptr(tint));
    glUniform1f(uniforms.lit, lit ? 1.0f : 0.0f);

    glBindVertexArray(mesh.vao);
    if (mesh.indexed) {
        glDrawElements(mode, mesh.count, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(mode, 0, mesh.count);
    }
}

void updateTitle(GLFWwindow* window) {
    char title[128];
    std::snprintf(title, sizeof(title), "Task 08 - Arcball | %s | camera %s [C] | speed %.2fx",
                  gPaused ? "paused" : "running", gCamera.animated() ? "animated" : "mouse",
                  static_cast<double>(gSpeed));
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
    glfwWindowHint(GLFW_SAMPLES, 4); // 4x MSAA: the orbit lines alias badly without it
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 08 - Arcball", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Make the context current and load the GL function pointers.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync, so the animation runs at the display rate
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

    // 3. Shaders.
    const GLuint shaderProgram = createShaderProgram(kVertexShaderSource, kFragmentShaderSource);
    if (shaderProgram == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Uniforms uniforms;
    uniforms.model = glGetUniformLocation(shaderProgram, "uModel");
    uniforms.view = glGetUniformLocation(shaderProgram, "uView");
    uniforms.projection = glGetUniformLocation(shaderProgram, "uProjection");
    uniforms.normalMatrix = glGetUniformLocation(shaderProgram, "uNormalMatrix");
    uniforms.tint = glGetUniformLocation(shaderProgram, "uTint");
    uniforms.lit = glGetUniformLocation(shaderProgram, "uLit");

    // 4. Geometry. The sphere mesh is uploaded once and drawn twice, at two
    //    different scales — that is the whole cost of adding the moon.
    GpuMesh cube = upload(makeCube(kCubeSide));
    GpuMesh sphere = upload(makeSphere(kSphereSlices, kSphereStacks));
    GpuMesh ring = upload(makeRing(kRingSegments));

    // Without the depth test the draw order decides what is visible, and the
    // sphere would stay in front of the cube for the whole orbit.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    // 5. Render loop.
    float sceneTime = 0.0f;
    gCamera.set_animated(true);
    double lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        // The animation is driven by accumulated time, not by a frame counter,
        // so it runs at the same speed on any machine — and pausing is simply
        // not accumulating.
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;

        if (gResetRequested) {
            gResetRequested = false;
            sceneTime = 0.0f;
            gCamera.reset();
        }
        if (!gPaused) {
            sceneTime += deltaTime * gSpeed;
            gCamera.update(deltaTime * gSpeed);
        }
        if (gTitleDirty) {
            gTitleDirty = false;
            updateTitle(window);
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            glfwWaitEvents(); // minimized: no valid aspect ratio or pixels to draw
            lastFrame = glfwGetTime();
            continue;
        }
        glViewport(0, 0, framebufferWidth, framebufferHeight);

        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

        glUseProgram(shaderProgram);

        // View and projection are the same for every object in the frame.
        const glm::mat4 view = gCamera.view();
        const glm::mat4 projection = gCamera.projection(framebufferWidth, framebufferHeight);
        glUniformMatrix4fv(uniforms.view, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uniforms.projection, 1, GL_FALSE, glm::value_ptr(projection));

        const glm::mat4 identity(1.0f);

        // --- The cube: rotation only, about its own centre. ------------------
        glm::mat4 cubeModel =
            glm::rotate(identity, sceneTime * kCubeSpinY, glm::vec3(0.0f, 1.0f, 0.0f));
        cubeModel = glm::rotate(cubeModel, sceneTime * kCubeSpinX, glm::vec3(1.0f, 0.0f, 0.0f));

        // --- The sphere: orbit = a rotation applied *after* a translation. ---
        // The orbit plane is tilted once so the sphere does not slide along a
        // straight horizontal line from where the camera sits.
        const glm::mat4 orbitPlane =
            glm::rotate(identity, glm::radians(kOrbitTilt), glm::vec3(0.0f, 0.0f, 1.0f));

        // Where the sphere *is*. Right to left: push a point out to distance
        // kOrbitRadius along +X, then swing that whole displaced frame around
        // the origin — which is where the cube sits.
        const glm::mat4 sphereAnchor =
            orbitPlane *
            glm::rotate(identity, sceneTime * kOrbitSpeed, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::translate(identity, glm::vec3(kOrbitRadius, 0.0f, 0.0f));

        // What it does once it is there: shrink the unit sphere, spin it on its
        // own axis. These act before the anchor, so they happen in the frame of
        // the sphere and do not disturb the orbit.
        const glm::mat4 sphereModel =
            sphereAnchor *
            glm::rotate(identity, sceneTime * kSphereSpin, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::scale(identity, glm::vec3(kSphereScale));

        // --- The moon: the same construction, one frame deeper. --------------
        // Starting from sphereAnchor instead of the identity is what makes the
        // moon follow the sphere around the cube for free.
        const glm::mat4 moonPlane =
            sphereAnchor *
            glm::rotate(identity, glm::radians(kMoonTilt), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat4 moonModel =
            moonPlane * glm::rotate(identity, sceneTime * kMoonSpeed, glm::vec3(0.0f, 1.0f, 0.0f)) *
            glm::translate(identity, glm::vec3(kMoonRadius, 0.0f, 0.0f)) *
            glm::scale(identity, glm::vec3(kMoonScale));

        drawMesh(cube, GL_TRIANGLES, cubeModel, glm::vec3(1.0f), true, uniforms);
        drawMesh(sphere, GL_TRIANGLES, sphereModel, glm::vec3(1.0f), true, uniforms);
        drawMesh(sphere, GL_TRIANGLES, moonModel, glm::vec3(1.00f, 0.84f, 0.62f), true, uniforms);

        if (gShowOrbits) {
            // The guides are the unit circle scaled to each orbit radius, in the
            // very same frame the orbiting body is anchored to.
            drawMesh(ring, GL_LINE_LOOP, orbitPlane * glm::scale(identity, glm::vec3(kOrbitRadius)),
                     glm::vec3(0.34f), false, uniforms);
            drawMesh(ring, GL_LINE_LOOP, moonPlane * glm::scale(identity, glm::vec3(kMoonRadius)),
                     glm::vec3(0.30f), false, uniforms);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    destroy(cube);
    destroy(sphere);
    destroy(ring);
    glDeleteProgram(shaderProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
