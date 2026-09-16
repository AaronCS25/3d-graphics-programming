// Task 04 — Mesh simplification (LOD)
//
// Loads a mesh into the CHE (generated sphere/torus or a PLY file), reduces it
// with the QEM edge-collapse algorithm implemented in che_simplify.cpp, and shows
// the result next to the original with the measured error.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders
//   4. upload G and the vertex normals to a VBO, V to an EBO — twice, since the
//      original stays resident for the side-by-side comparison
//   5. render loop: clear -> glDrawElements -> glfwSwapBuffers -> glfwPollEvents
//   6. cleanup + glfwTerminate()
//
// Usage:  task04 [sphere | torus | path/to/model.ply ...] [--check]
//         --check prints the LOD ladder of every listed mesh and exits without
//         opening a window. While running, drop a .ply onto the window to load it.

#include "che.hpp"
#include "mesh_io.hpp"
#include "primitives.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1100;
constexpr int kWindowHeight = 760;
constexpr float kPointSize = 4.0f;

// The tetrahedron is the floor for a closed surface; below it there is none.
constexpr int kMinTriangles = 4;

const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    gl_Position = uViewProjection * world;
}
)";

const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uCameraPos;
uniform bool uFlat;
uniform bool uShaded;
uniform vec3 uTint;

out vec4 FragColor;

void main() {
    vec3 n = uFlat ? normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)))
                   : normalize(vNormal);

    vec3 base = uTint * (0.5 + 0.5 * n);
    // abs() so back faces (open meshes like the bunny) are lit as well.
    float diffuse = uShaded ? abs(dot(n, normalize(uCameraPos - vWorldPos))) : 1.0;
    FragColor = vec4(base * (0.3 + 0.7 * diffuse), 1.0);
}
)";

struct Camera {
    float yaw = 0.6f;
    float pitch = 0.35f;
    float distance = 5.0f;
};

int gTarget = 0;
GLenum gPolygonMode = GL_FILL;
bool gFlatShading = true;
bool gAutoRotate = true;
bool gCompare = true;
bool gLodDirty = true;
std::string gDroppedPath; // set by the drop callback, consumed by the render loop

Camera gCamera;
bool gDragging = false;
double gLastCursorX = 0.0;
double gLastCursorY = 0.0;

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    // Press only: rebuilding a level is expensive enough that key repeat would
    // queue up work nobody asked for.
    if (action != GLFW_PRESS) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_DOWN:
        gTarget /= 2;
        gLodDirty = true;
        break;
    case GLFW_KEY_UP:
        gTarget *= 2;
        gLodDirty = true;
        break;
    case GLFW_KEY_R:
        gTarget = 0;
        gLodDirty = true;
        break;
    case GLFW_KEY_C:
        gCompare = !gCompare;
        break;
    case GLFW_KEY_1:
        gPolygonMode = GL_FILL;
        break;
    case GLFW_KEY_2:
        gPolygonMode = GL_LINE;
        break;
    case GLFW_KEY_3:
        gPolygonMode = GL_POINT;
        break;
    case GLFW_KEY_S:
        gFlatShading = !gFlatShading;
        break;
    case GLFW_KEY_A:
        gAutoRotate = !gAutoRotate;
        break;
    default:
        break;
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
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
        std::clamp(gCamera.distance - static_cast<float>(yoffset) * 0.25f, 2.0f, 14.0f);
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

// Prints the LOD ladder of one mesh with its error: the evidence behind the
// README table. Returns how many levels failed to be a valid CHE.
int checkLadder(const CHE& original) {
    std::printf("%8s %6s %6s %6s %9s %8s %8s %10s %10s %9s %9s  %s\n", "target", "V", "F",
                "euler", "collapses", "rej.link", "rej.flip", "QEM sum", "QEM max", "err mean",
                "err max", "state");

    int failures = 0;
    for (int target = original.n_triangles(); target >= kMinTriangles; target /= 2) {
        CHE mesh = original;
        const CHE::SimplifyReport report = mesh.simplify(target);
        const std::string problem = mesh.validate();
        const CHE::Deviation deviation = mesh.deviation_from(original);
        const int euler = mesh.n_vertices() - mesh.n_edges() + mesh.n_triangles();

        std::printf("%8d %6d %6d %6d %9d %8d %8d %10.2e %10.2e %9.5f %9.5f  %s\n", target,
                    mesh.n_vertices(), mesh.n_triangles(), euler, report.collapses,
                    report.rejected_link, report.rejected_flip, report.total_cost,
                    report.max_cost, static_cast<double>(deviation.mean),
                    static_cast<double>(deviation.max),
                    problem.empty() ? (mesh.is_closed() ? "closed, valid" : "open, valid")
                                    : problem.c_str());
        failures += problem.empty() ? 0 : 1;
    }
    return failures;
}

void updateTitle(GLFWwindow* window, const std::string& meshName, const CHE& original,
                 const CHE& mesh, const CHE::SimplifyReport& report,
                 const CHE::Deviation& deviation) {
    const char* fill = gPolygonMode == GL_FILL   ? "solid"
                       : gPolygonMode == GL_LINE ? "wireframe"
                                                 : "points";
    char title[256];
    std::snprintf(title, sizeof(title),
                  "Task 04 - LOD | %s  F=%d/%d (%.1f%%) V=%d | QEM=%.2e | err mean=%.4f "
                  "max=%.4f | %s | %s",
                  meshName.c_str(), mesh.n_triangles(), original.n_triangles(),
                  100.0 * mesh.n_triangles() / std::max(original.n_triangles(), 1),
                  mesh.n_vertices(), report.total_cost, static_cast<double>(deviation.mean),
                  static_cast<double>(deviation.max), fill, gCompare ? "compare" : "single");
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
        meshNames.emplace_back("sphere");
    }

    // --check: load every mesh on the command line, print its ladder, and exit.
    if (checkOnly) {
        int failures = 0;
        for (const std::string& name : meshNames) {
            try {
                const CHE candidate = loadMesh(name);
                printStats(name, candidate);
                failures += checkLadder(candidate);
                std::printf("\n");
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", name.c_str(), e.what());
                ++failures;
            }
        }
        std::printf(failures == 0 ? "every level is a valid CHE\n" : "%d level(s) failed\n",
                    failures);
        return failures;
    }

    CHE original;
    try {
        original = loadMesh(meshNames.front());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "could not load '%s': %s\n", meshNames.front().c_str(), e.what());
        return 1;
    }
    printStats(meshNames.front(), original);

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

    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight,
                                          "Task 04 - Mesh simplification", nullptr, nullptr);
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
    glPointSize(kPointSize);

    // 3. Shaders.
    const GLuint program = createShaderProgram(kVertexShaderSource, kFragmentShaderSource);
    if (program == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const GLint modelLocation = glGetUniformLocation(program, "uModel");
    const GLint viewProjectionLocation = glGetUniformLocation(program, "uViewProjection");
    const GLint normalMatrixLocation = glGetUniformLocation(program, "uNormalMatrix");
    const GLint cameraLocation = glGetUniformLocation(program, "uCameraPos");
    const GLint flatLocation = glGetUniformLocation(program, "uFlat");
    const GLint shadedLocation = glGetUniformLocation(program, "uShaded");
    const GLint tintLocation = glGetUniformLocation(program, "uTint");

    // 4. The original stays resident so it can be drawn beside every level.
    std::string meshName = meshNames.front();
    CHE simplified = original;
    CHE::SimplifyReport report;
    CHE::Deviation deviation;

    GpuMesh originalGpu;
    GpuMesh simplifiedGpu;
    originalGpu.create();
    simplifiedGpu.create();
    originalGpu.upload(original);

    float rotation = 0.0f;
    double lastTime = glfwGetTime();

    // 5. Render loop.
    while (!glfwWindowShouldClose(window)) {
        // A dropped file replaces the original; the ladder restarts from full
        // resolution because the old target means nothing on the new mesh.
        if (!gDroppedPath.empty()) {
            try {
                original = loadMesh(gDroppedPath);
                printStats(gDroppedPath, original);
                meshName = gDroppedPath;
                originalGpu.upload(original);
                gTarget = 0;
                gLodDirty = true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", gDroppedPath.c_str(),
                             e.what());
            }
            gDroppedPath.clear();
        }

        if (gLodDirty) {
            gLodDirty = false;
            gTarget = gTarget <= 0 ? original.n_triangles()
                                   : std::clamp(gTarget, kMinTriangles, original.n_triangles());

            simplified = original;
            report = simplified.simplify(gTarget);

            const std::string problem = simplified.validate();
            if (!problem.empty()) {
                std::fprintf(stderr, "CHE invariant violated after simplification: %s\n",
                             problem.c_str());
            }
            deviation = simplified.deviation_from(original);
            simplifiedGpu.upload(simplified);
        }
        updateTitle(window, meshName, original, simplified, report, deviation);

        const double now = glfwGetTime();
        const float delta = static_cast<float>(now - lastTime);
        lastTime = now;
        if (gAutoRotate) {
            rotation += delta * 0.4f;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        const float aspect =
            height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

        const glm::vec3 eye =
            gCamera.distance * glm::vec3(std::cos(gCamera.pitch) * std::sin(gCamera.yaw),
                                         std::sin(gCamera.pitch),
                                         std::cos(gCamera.pitch) * std::cos(gCamera.yaw));
        const glm::mat4 viewProjection =
            glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f) *
            glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glUniform3fv(cameraLocation, 1, glm::value_ptr(eye));
        glUniform1i(flatLocation, gFlatShading && gPolygonMode == GL_FILL);
        glUniform1i(shadedLocation, gPolygonMode == GL_FILL);
        glPolygonMode(GL_FRONT_AND_BACK, gPolygonMode);

        const auto draw = [&](const GpuMesh& gpu, float offsetX, const glm::vec3& tint) {
            const glm::mat4 model =
                glm::translate(glm::mat4(1.0f), glm::vec3(offsetX, 0.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 1.0f, 0.0f));
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE,
                               glm::value_ptr(glm::mat3(glm::transpose(glm::inverse(model)))));
            glUniform3fv(tintLocation, 1, glm::value_ptr(tint));
            glBindVertexArray(gpu.vao);
            glDrawElements(GL_TRIANGLES, gpu.indices, GL_UNSIGNED_INT, nullptr);
        };

        if (gCompare) {
            draw(originalGpu, -1.2f, glm::vec3(0.72f, 0.78f, 0.95f));
            draw(simplifiedGpu, 1.2f, glm::vec3(1.0f, 0.82f, 0.55f));
        } else {
            draw(simplifiedGpu, 0.0f, glm::vec3(1.0f, 0.82f, 0.55f));
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    originalGpu.destroy();
    simplifiedGpu.destroy();
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
