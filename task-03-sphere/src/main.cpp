// Task 03 — Sphere
//
// Builds a cube and a parametric sphere, stores both in the CHE (see che.hpp)
// and renders them with an element buffer fed straight from the vertex array V.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders
//   4. upload G and the vertex normals to a VBO, V to an EBO
//   5. render loop: clear -> glDrawElements -> glfwSwapBuffers -> glfwPollEvents
//   6. cleanup + glfwTerminate()
//
// Run with --check to print the CHE invariants for a range of meshes instead of
// opening a window.

#include "che.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 750;

constexpr int kMinSlices = 3;
constexpr int kMaxSlices = 256;
constexpr int kInitialSlices = 32;

constexpr float kSphereRadius = 1.0f;
constexpr float kCubeSide = 1.6f;
constexpr float kPointSize = 4.0f;

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

// uFlat picks where the normal comes from. The flat one is rebuilt per fragment
// from the screen-space derivatives of the world position, which recovers the
// true face normal without splitting the shared vertices the CHE relies on.
const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;

uniform vec3 uCameraPos;
uniform bool uFlat;
uniform bool uShaded;

out vec4 FragColor;

void main() {
    vec3 n = uFlat ? normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)))
                   : normalize(vNormal);

    // The surface colour is the normal direction itself, which makes the
    // orientation of every face readable. Wireframe and point modes skip the
    // diffuse term: shading a line by its normal only makes it disappear.
    vec3 base = 0.5 + 0.5 * n;
    float diffuse = uShaded ? max(dot(n, normalize(uCameraPos - vWorldPos)), 0.0) : 1.0;
    FragColor = vec4(base * (0.3 + 0.7 * diffuse), 1.0);
}
)";

enum class Shape { Cube, Sphere };

struct Camera {
    float yaw = 0.6f;
    float pitch = 0.5f;
    float distance = 4.0f;
};

Shape gShape = Shape::Sphere;
int gSlices = kInitialSlices;
GLenum gPolygonMode = GL_FILL;
// Faceted by default: it is what a cube actually looks like, and on the
// sphere it exposes the tessellation the resolution keys control.
bool gFlatShading = true;
bool gAutoRotate = true;
bool gMeshDirty = true;

Camera gCamera;
bool gDragging = false;
double gLastCursorX = 0.0;
double gLastCursorY = 0.0;

// The sphere is generated with half as many stacks as slices, which keeps the
// quads roughly square: u spans 2pi while v spans only pi.
int stacks_for(int slices) {
    return std::max(slices / 2, 2);
}

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_SPACE:
        gShape = gShape == Shape::Cube ? Shape::Sphere : Shape::Cube;
        gMeshDirty = true;
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
    case GLFW_KEY_UP:
        gSlices = std::min(gSlices * 2, kMaxSlices);
        gMeshDirty = true;
        break;
    case GLFW_KEY_DOWN:
        gSlices = std::max(gSlices / 2, kMinSlices);
        gMeshDirty = true;
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
        std::clamp(gCamera.distance - static_cast<float>(yoffset) * 0.25f, 1.8f, 12.0f);
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

// Uploads G interleaved with the vertex normals, and V as the element buffer.
// V is already the index list glDrawElements wants — that is the whole point of
// storing connectivity as half-edges over a shared vertex array.
void upload(const CHE& mesh, GLuint vbo, GLuint ebo) {
    const std::vector<glm::vec3> normals = mesh.vertex_normals();

    std::vector<Attributes> attributes;
    attributes.reserve(static_cast<size_t>(mesh.n_vertices()));
    for (int v = 0; v < mesh.n_vertices(); ++v) {
        attributes.push_back({mesh.G(v), normals[static_cast<size_t>(v)]});
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(attributes.size() * sizeof(Attributes)),
                 attributes.data(), GL_STATIC_DRAW);

    // The indices are int, drawn as GL_UNSIGNED_INT: every value is a valid
    // vertex index, so it is non-negative and both types share a bit pattern.
    const std::vector<int>& indices = mesh.indices();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(int)),
                 indices.data(), GL_STATIC_DRAW);
}

CHE build(Shape shape, int slices) {
    return shape == Shape::Cube ? make_cube(kCubeSide)
                                : make_sphere(kSphereRadius, slices, stacks_for(slices));
}

void updateTitle(GLFWwindow* window, const CHE& mesh) {
    const char* fill = gPolygonMode == GL_FILL   ? "solid"
                       : gPolygonMode == GL_LINE ? "wireframe"
                                                 : "points";
    char title[192];
    std::snprintf(title, sizeof(title), "Task 03 - %s | V=%d E=%d F=%d | %s | %s",
                  gShape == Shape::Cube ? "cube" : "sphere", mesh.n_vertices(), mesh.n_edges(),
                  mesh.n_triangles(), fill, gFlatShading ? "flat" : "smooth");
    glfwSetWindowTitle(window, title);
}

// Prints the CHE invariants for a spread of meshes. Kept out of the render path
// so the evidence survives into task 04, where the structure gets mutated.
int runChecks() {
    const auto report = [](const char* name, const CHE& mesh) {
        const std::string problem = mesh.validate();
        const int euler = mesh.n_vertices() - mesh.n_edges() + mesh.n_triangles();

        std::vector<int> valence(static_cast<size_t>(mesh.n_vertices()), -1);
        bool consistent = true;
        for (int he = 0; he < mesh.n_half_edges(); ++he) {
            const size_t v = static_cast<size_t>(mesh.V(he));
            const int fan = static_cast<int>(mesh.star(he).size());
            consistent = consistent && (valence[v] == -1 || valence[v] == fan);
            consistent = consistent && static_cast<int>(mesh.link(he).size()) == fan;
            valence[v] = fan;
        }
        int corners = 0;
        for (const int d : valence) {
            corners += d;
        }

        bool outward = true;
        for (int t = 0; t < mesh.n_triangles(); ++t) {
            const glm::vec3 centroid =
                (mesh.G(mesh.V(3 * t)) + mesh.G(mesh.V(3 * t + 1)) + mesh.G(mesh.V(3 * t + 2))) /
                3.0f;
            outward = outward && glm::dot(mesh.triangle_normal(t), centroid) > 0.0f;
        }

        const bool ok = problem.empty() && euler == 2 && mesh.is_closed() && outward &&
                        consistent && corners == 3 * mesh.n_triangles();
        std::printf("%-14s V=%6d E=%6d F=%6d  euler=%d  closed=%s outward=%s stars=%s  %s\n", name,
                    mesh.n_vertices(), mesh.n_edges(), mesh.n_triangles(), euler,
                    mesh.is_closed() ? "y" : "N", outward ? "y" : "N", consistent ? "y" : "N",
                    problem.empty() ? (ok ? "PASS" : "FAIL") : problem.c_str());
        return ok ? 0 : 1;
    };

    int failures = 0;
    failures += report("cube", make_cube(kCubeSide));
    for (const int slices : {3, 8, 16, 32, 64, 128}) {
        char name[32];
        std::snprintf(name, sizeof(name), "sphere %dx%d", slices, stacks_for(slices));
        failures += report(name, make_sphere(kSphereRadius, slices, stacks_for(slices)));
    }

    const CHE sphere = make_sphere(kSphereRadius, 64, 32);
    float deviation = 0.0f;
    for (int v = 0; v < sphere.n_vertices(); ++v) {
        deviation = std::max(deviation, std::abs(glm::length(sphere.G(v)) - kSphereRadius));
    }
    std::printf("radial deviation from the analytic sphere: %.3e\n",
                static_cast<double>(deviation));

    std::printf(failures == 0 ? "all checks passed\n" : "%d check(s) failed\n", failures);
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--check") == 0) {
        return runChecks();
    }

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
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 03 - Sphere", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Context, function pointers, and the depth buffer this task finally needs.
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
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

    // 4. One VAO describing an interleaved position+normal VBO plus the EBO.
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
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

    CHE mesh;
    int indexCount = 0;
    float rotation = 0.0f;
    double lastTime = glfwGetTime();

    // 5. Render loop.
    while (!glfwWindowShouldClose(window)) {
        if (gMeshDirty) {
            gMeshDirty = false;
            mesh = build(gShape, gSlices);

            const std::string problem = mesh.validate();
            if (!problem.empty()) {
                std::fprintf(stderr, "CHE invariant violated: %s\n", problem.c_str());
            }

            upload(mesh, vbo, ebo);
            indexCount = mesh.n_half_edges();
        }
        updateTitle(window, mesh);

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
        const glm::mat4 model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 viewProjection =
            glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f) *
            glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));

        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform3fv(cameraLocation, 1, glm::value_ptr(eye));

        // Lines and points have degenerate derivatives, so the flat normal is
        // only available while rasterizing filled triangles.
        glUniform1i(flatLocation, gFlatShading && gPolygonMode == GL_FILL);
        glUniform1i(shadedLocation, gPolygonMode == GL_FILL);

        glPolygonMode(GL_FRONT_AND_BACK, gPolygonMode);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
