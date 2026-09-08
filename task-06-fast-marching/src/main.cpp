// Task 06 — Fast Marching on triangle meshes
//
// Loads a mesh into the CHE (generated sphere/torus or a PLY file), computes the
// geodesic distance from a source vertex with the Fast Marching Method, and
// paints the distance map with a colormap.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders
//   4. upload G and the vertex normals to a VBO, V to an EBO
//   5. render loop: clear -> glDrawElements -> glfwSwapBuffers -> glfwPollEvents
//   6. cleanup + glfwTerminate()
//
// Usage:  task06 [sphere | torus | path/to/model.ply ...] [--check]
//         --check prints the statistics of every listed mesh and exits without
//         opening a window. While running, drop a .ply onto the window to load it.

#include "che.hpp"
#include "fast_marching.hpp"
#include "mesh_io.hpp"
#include "primitives.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 750;

// aDistance is the geodesic distance of the vertex normalised to [0, 1] by the
// largest finite distance, or a negative value where the front never arrived.
// It is interpolated across each triangle like any other attribute, so the
// fragment shader sees a smooth distance field, not per-vertex steps.
const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aDistance;

uniform mat4 uModel;
uniform mat4 uViewProjection;

out vec3 vWorldPos;
out vec3 vNormal;
out float vDistance;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(uModel) * aNormal;
    vDistance = aDistance;
    gl_Position = uViewProjection * world;
}
)";

// The colormap functions are copied verbatim from kbinani/colormap-shaders
// (MIT, Copyright (c) 2015 kbinani): shaders/glsl/MATLAB_jet.frag and
// shaders/glsl/IDL_Plasma.frag, with the function names prefixed so both can
// live in one shader. Each maps x in [0, 1] to an RGB colour.
const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in float vDistance;

uniform vec3 uCameraPos;
uniform int uColormap;   // 0 = jet, 1 = plasma
uniform bool uIsolines;  // darken bands so the front's shape is visible

out vec4 FragColor;

// --- MATLAB_jet (kbinani/colormap-shaders) ---------------------------------
float jet_red(float x) {
    if (x < 0.7) {
        return 4.0 * x - 1.5;
    } else {
        return -4.0 * x + 4.5;
    }
}

float jet_green(float x) {
    if (x < 0.5) {
        return 4.0 * x - 0.5;
    } else {
        return -4.0 * x + 3.5;
    }
}

float jet_blue(float x) {
    if (x < 0.3) {
       return 4.0 * x + 0.5;
    } else {
       return -4.0 * x + 2.5;
    }
}

vec4 jet(float x) {
    float r = clamp(jet_red(x), 0.0, 1.0);
    float g = clamp(jet_green(x), 0.0, 1.0);
    float b = clamp(jet_blue(x), 0.0, 1.0);
    return vec4(r, g, b, 1.0);
}

// --- IDL_Plasma (kbinani/colormap-shaders) ---------------------------------
float plasma_red(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 12.16378802377247;
    const float b = 0.05245257017955226;
    const float c = 0.2532139106569052;
    const float d = 0.02076964056039702;
    const float e = 270.124167081014;
    const float f = 1.724941960305955;
    float v = (a * x + b) * sin(2.0 * pi / c * (x - d)) + e * x + f;
    if (v > 255.0) {
        return 255.0 - (v - 255.0);
    } else {
        return v;
    }
}

float plasma_green(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 88.08537391182792;
    const float b = 0.25280516046667;
    const float c = 0.05956080245692388;
    const float d = 106.5684078925541;
    return a * sin(2.0 * pi / b * (x - c)) + d;
}

float plasma_blue(float x) {
    const float pi = 3.141592653589793238462643383279502884197169399;
    const float a = 63.89922420106684;
    const float b = 0.4259605778503662;
    const float c = 0.2529247343450655;
    const float d = 0.5150868195804643;
    const float e = 938.1798072557968;
    const float f = 503.0883490697431;
    float v = (a * x + b) * sin(2.0 * pi / c * x + d * 2.0 * pi) - e * x + f;
    if (v > 255.0) {
        return 255.0 - (v - 255.0);
    } else {
        return mod(v, 255.0);
    }
}

vec4 plasma(float x) {
    float r = clamp(plasma_red(x) / 255.0, 0.0, 1.0);
    float g = clamp(plasma_green(x) / 255.0, 0.0, 1.0);
    float b = clamp(plasma_blue(x) / 255.0, 0.0, 1.0);
    return vec4(r, g, b, 1.0);
}
// ---------------------------------------------------------------------------

void main() {
    vec3 n = normalize(vNormal);
    vec3 toCamera = normalize(uCameraPos - vWorldPos);
    // abs() so back faces (open meshes like the bunny) are lit as well.
    float diffuse = abs(dot(n, toCamera));
    float shade = 0.35 + 0.65 * diffuse;

    // Unreached vertices (disconnected pieces) carry a negative distance.
    if (vDistance < 0.0) {
        FragColor = vec4(vec3(0.45) * shade, 1.0);
        return;
    }

    vec3 base = (uColormap == 0 ? jet(vDistance) : plasma(vDistance)).rgb;

    // Isolines: 20 bands over [0, 1]; the first 15% of each band is darkened.
    // Every band edge is a curve of equal geodesic distance — the front itself.
    if (uIsolines && fract(vDistance * 20.0) < 0.15) {
        base *= 0.35;
    }
    FragColor = vec4(base * shade, 1.0);
}
)";

struct Camera {
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 4.0f;
};

GLenum gPolygonMode = GL_FILL;
std::string gDroppedPath; // set by the drop callback, consumed by the render loop

int gSource = 0;          // vertex the front starts from
bool gSourceDirty = true; // distance map must be recomputed and re-uploaded
int gColormap = 0;        // 0 = jet, 1 = plasma
bool gIsolines = true;

// Right click: the render loop, which knows the camera, resolves it to a vertex.
bool gPickRequested = false;
double gPickX = 0.0;
double gPickY = 0.0;
Camera gCamera;
bool gDragging = false;
double gLastCursorX = 0.0;
double gLastCursorY = 0.0;

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
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
        gPolygonMode = GL_FILL;
        break;
    case GLFW_KEY_2:
        gPolygonMode = GL_LINE;
        break;
    case GLFW_KEY_C:
        gColormap = (gColormap + 1) % 2;
        break;
    case GLFW_KEY_I:
        gIsolines = !gIsolines;
        break;
    case GLFW_KEY_R:
        gSource = 0;
        gSourceDirty = true;
        break;
    default:
        break;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        glfwGetCursorPos(window, &gPickX, &gPickY);
        gPickRequested = true;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    gDragging = action == GLFW_PRESS;
    glfwGetCursorPos(window, &gLastCursorX, &gLastCursorY);
}

void cursorPosCallback(GLFWwindow* /*window*/, double x, double y) {
    if (!gDragging) {
        return;
    }
    constexpr float kSensitivity = 0.008f;
    constexpr float kPitchLimit = 1.55f;

    gCamera.yaw -= static_cast<float>(x - gLastCursorX) * kSensitivity;
    gCamera.pitch += static_cast<float>(y - gLastCursorY) * kSensitivity;
    gCamera.pitch = std::clamp(gCamera.pitch, -kPitchLimit, kPitchLimit);
    gLastCursorX = x;
    gLastCursorY = y;
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    gCamera.distance =
        std::clamp(gCamera.distance - static_cast<float>(yoffset) * 0.25f, 1.5f, 12.0f);
}

// Drag-and-drop: GLFW hands over the paths; only the first one is used. Loading
// is deferred to the render loop so a broken file cannot take down a callback.
void dropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (count > 0) {
        gDroppedPath = paths[0];
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

struct Attributes {
    glm::vec3 position;
    glm::vec3 normal;
    float distance; // t / max_finite, or -1 where the front never arrived
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
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Attributes),
                              reinterpret_cast<void*>(offsetof(Attributes, distance)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // Uploads the geometry together with its distance map. Normalising by the
    // largest finite distance puts every mesh on the same [0, 1] colour scale.
    void upload(const CHE& mesh, const DistanceMap& map) {
        const std::vector<glm::vec3> normals = mesh.vertex_normals();
        const float scale = map.max_finite > 0.0f ? 1.0f / map.max_finite : 1.0f;

        std::vector<Attributes> attributes;
        attributes.reserve(static_cast<size_t>(mesh.n_vertices()));
        for (int v = 0; v < mesh.n_vertices(); ++v) {
            const float t = map.t[static_cast<size_t>(v)];
            attributes.push_back(
                {mesh.G(v), normals[static_cast<size_t>(v)], std::isinf(t) ? -1.0f : t * scale});
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

    void destroy() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
    }
};

// "sphere" and "torus" are generated; anything else is read as a PLY path.
CHE loadMesh(const std::string& name) {
    if (name == "sphere") {
        return make_sphere(1.0f, 64, 32);
    }
    if (name == "torus") {
        return make_torus(0.7f, 0.3f, 96, 40);
    }
    return load_ply(name);
}

void printStats(const std::string& name, const CHE& mesh) {
    const std::string problem = mesh.validate();
    const int euler = mesh.n_vertices() - mesh.n_edges() + mesh.n_triangles();
    std::printf("%s: V=%d E=%d F=%d euler=%d %s, %s\n", name.c_str(), mesh.n_vertices(),
                mesh.n_edges(), mesh.n_triangles(), euler, mesh.is_closed() ? "closed" : "open",
                problem.empty() ? "valid CHE" : problem.c_str());
}

// Vertex under the cursor. Every vertex is projected to the screen with the same
// matrices the GPU uses; among those within a few pixels of the click the
// nearest to the camera wins, so a vertex on the far side cannot be picked
// through the surface. If nothing is that close, the closest on screen wins.
// O(n) per click, which is nothing next to running the distance map.
int pickVertex(const CHE& mesh, const glm::mat4& viewProjection, int width, int height,
               double cursorX, double cursorY) {
    constexpr float kRadiusPixels = 12.0f;
    int best = 0;
    float bestPixelDistance = std::numeric_limits<float>::max();
    float bestDepth = std::numeric_limits<float>::max();

    for (int v = 0; v < mesh.n_vertices(); ++v) {
        const glm::vec4 clip = viewProjection * glm::vec4(mesh.G(v), 1.0f);
        if (clip.w <= 0.0f) {
            continue; // behind the camera
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float px = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
        const float py = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
        const float pixelDistance =
            std::hypot(px - static_cast<float>(cursorX), py - static_cast<float>(cursorY));

        const bool insideRadius = pixelDistance < kRadiusPixels;
        const bool bestInsideRadius = bestPixelDistance < kRadiusPixels;
        const bool better = insideRadius ? (!bestInsideRadius || ndc.z < bestDepth)
                                         : (!bestInsideRadius && pixelDistance < bestPixelDistance);
        if (better) {
            best = v;
            bestPixelDistance = pixelDistance;
            bestDepth = ndc.z;
        }
    }
    return best;
}

void updateTitle(GLFWwindow* window, const std::string& meshName, const CHE& mesh,
                 const DistanceMap& map, double milliseconds) {
    char title[256];
    std::snprintf(title, sizeof(title),
                  "Task 06 - Fast Marching | %s  V=%d F=%d | source v%d  t_max=%.3f  %.1f ms | %s%s",
                  meshName.c_str(), mesh.n_vertices(), mesh.n_triangles(), gSource,
                  static_cast<double>(map.max_finite), milliseconds,
                  gColormap == 0 ? "jet" : "plasma", gIsolines ? " + isolines" : "");
    glfwSetWindowTitle(window, title);
}

// Picks the source for --check. On the generated sphere it is the vertex closest
// to (r, 0, 0), on the equator: from the pole every geodesic runs along a
// meridian, which is a chain of mesh edges, so edge-based Dijkstra would look
// exact there and the test would prove nothing. From the equator the geodesics
// cross the triangles diagonally, which is where the update step matters.
int pickSource(const std::string& name, const CHE& mesh) {
    if (name != "sphere") {
        return 0;
    }
    const float radius = glm::length(mesh.G(0));
    int best = 0;
    for (int v = 1; v < mesh.n_vertices(); ++v) {
        if (glm::distance(mesh.G(v), glm::vec3(radius, 0.0f, 0.0f)) <
            glm::distance(mesh.G(best), glm::vec3(radius, 0.0f, 0.0f))) {
            best = v;
        }
    }
    return best;
}

// Runs the distance map and reports it. On the sphere the exact geodesic between
// two points is the great-circle arc r * acos(p . q / r^2), a ground truth to
// measure the error against.
void checkDistances(const std::string& name, const CHE& mesh) {
    const int source = pickSource(name, mesh);
    const auto start = std::chrono::steady_clock::now();
    const DistanceMap map = fast_marching(mesh, source);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    int unreached = 0;
    for (const float t : map.t) {
        unreached += std::isinf(t) ? 1 : 0;
    }
    std::printf("  fast marching from v%d: %.1f ms, t_max=%.4f, unreached=%d\n", source,
                1000.0 * elapsed, static_cast<double>(map.max_finite), unreached);

    if (name != "sphere") {
        return;
    }
    const float radius = glm::length(mesh.G(0));
    const glm::vec3 p = mesh.G(source);
    double sum = 0.0;
    double worst = 0.0;
    for (int v = 0; v < mesh.n_vertices(); ++v) {
        const float cosine = std::clamp(glm::dot(p, mesh.G(v)) / (radius * radius), -1.0f, 1.0f);
        const float exact = radius * std::acos(cosine);
        const double err = std::abs(static_cast<double>(map.t[static_cast<size_t>(v)] - exact));
        sum += err;
        worst = std::max(worst, err);
    }
    std::printf("  error vs exact arc: mean=%.5f max=%.5f (radius %.2f, antipode exact=%.5f)\n",
                sum / mesh.n_vertices(), worst, static_cast<double>(radius),
                static_cast<double>(radius) * 3.14159265358979323846);
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
        meshNames.emplace_back("sphere");
    }

    // --check: load every mesh on the command line, report, and exit.
    if (checkOnly) {
        int failures = 0;
        for (const std::string& name : meshNames) {
            try {
                const CHE candidate = loadMesh(name);
                printStats(name, candidate);
                checkDistances(name, candidate);
                failures += candidate.validate().empty() ? 0 : 1;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", name.c_str(), e.what());
                ++failures;
            }
        }
        return failures;
    }

    CHE mesh;
    try {
        mesh = loadMesh(meshNames.front());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not load '%s': %s\n", meshNames.front().c_str(), e.what());
        return 1;
    }
    printStats(meshNames.front(), mesh);

    // 1. Window + OpenGL 3.3 core context.
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 06 - Fast Marching", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Context, function pointers, depth test.
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetDropCallback(window, dropCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glEnable(GL_DEPTH_TEST);

    // 3. Shaders.
    const GLuint program = createShaderProgram(kVertexShaderSource, kFragmentShaderSource);
    if (program == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const GLint modelLocation = glGetUniformLocation(program, "uModel");
    const GLint viewProjectionLocation = glGetUniformLocation(program, "uViewProjection");
    const GLint cameraLocation = glGetUniformLocation(program, "uCameraPos");
    const GLint colormapLocation = glGetUniformLocation(program, "uColormap");
    const GLint isolinesLocation = glGetUniformLocation(program, "uIsolines");

    // 4. Geometry. The first upload happens in the loop, when the distance map
    // is computed for the first time (gSourceDirty starts true).
    GpuMesh gpu;
    gpu.create();
    std::string meshName = meshNames.front();
    DistanceMap map;
    double lastRunMilliseconds = 0.0;

    // 5. Render loop.
    while (!glfwWindowShouldClose(window)) {
        // A dropped file replaces the mesh; the source goes back to vertex 0
        // because the old index means nothing on the new mesh.
        if (!gDroppedPath.empty()) {
            try {
                mesh = loadMesh(gDroppedPath);
                printStats(gDroppedPath, mesh);
                meshName = gDroppedPath;
                gSource = 0;
                gSourceDirty = true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", gDroppedPath.c_str(),
                             e.what());
            }
            gDroppedPath.clear();
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        const float aspect = height > 0 ? static_cast<float>(width) / height : 1.0f;

        const glm::vec3 cameraPos(
            gCamera.distance * std::cos(gCamera.pitch) * std::sin(gCamera.yaw),
            gCamera.distance * std::sin(gCamera.pitch),
            gCamera.distance * std::cos(gCamera.pitch) * std::cos(gCamera.yaw));
        const glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 100.0f);
        const glm::mat4 model(1.0f);
        const glm::mat4 viewProjection = projection * view;

        // A right click moves the source to the vertex under the cursor. The
        // cursor is in window coordinates and the framebuffer may be larger
        // (HiDPI), so the click is rescaled to framebuffer pixels first.
        if (gPickRequested) {
            gPickRequested = false;
            int windowWidth = 0;
            int windowHeight = 0;
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            const double sx = windowWidth > 0 ? static_cast<double>(width) / windowWidth : 1.0;
            const double sy = windowHeight > 0 ? static_cast<double>(height) / windowHeight : 1.0;
            gSource = pickVertex(mesh, viewProjection, width, height, gPickX * sx, gPickY * sy);
            gSourceDirty = true;
        }

        // Recompute the distance map only when the source or the mesh changed:
        // the result is static, so there is no reason to run it every frame.
        if (gSourceDirty) {
            gSourceDirty = false;
            const auto start = std::chrono::steady_clock::now();
            map = fast_marching(mesh, gSource);
            lastRunMilliseconds =
                1000.0 *
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            gpu.upload(mesh, map);
        }
        updateTitle(window, meshName, mesh, map, lastRunMilliseconds);

        glClearColor(0.07f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, gPolygonMode);

        glUseProgram(program);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glUniform3fv(cameraLocation, 1, glm::value_ptr(cameraPos));
        glUniform1i(colormapLocation, gColormap);
        glUniform1i(isolinesLocation, gIsolines ? 1 : 0);

        glBindVertexArray(gpu.vao);
        glDrawElements(GL_TRIANGLES, gpu.indices, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    gpu.destroy();
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
