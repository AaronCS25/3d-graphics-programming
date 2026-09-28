// Task 09 — Phong and Blinn-Phong illumination
//
// Loads a mesh into the CHE (generated sphere/torus or a PLY file) and lights it
// with a point light that orbits the model. The camera is the shared arcball
// camera from task 08.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glfwMakeContextCurrent() + gladLoadGLLoader() + depth test
//   3. compile & link shaders
//   4. upload G and the vertex normals to a VBO, V to an EBO
//   5. render loop: clear -> glDrawElements -> glfwSwapBuffers -> glfwPollEvents
//   6. cleanup + glfwTerminate()
//
// Usage:  task09 [sphere | torus | path/to/model.ply ...] [--check]
//         --check prints the statistics of every listed mesh and exits without
//         opening a window. While running, drop a .ply onto the window to load it.

#include "che.hpp"
#include "mesh_io.hpp"
#include "primitives.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 750;

// The point light circles the model above its equator, so the highlight sweeps
// across the surface and the difference between the shading methods shows.
constexpr float kLightRadius = 2.0f;
constexpr float kLightHeight = 1.2f;
constexpr float kLightSpeed = 0.6f; // rad/s
constexpr float kLightMarkerRadius = 0.05f;

camera gCamera(glm::vec3(0.0f), 4.0f, 1.5f, 0.5f);

bool gPaused = false;
bool gWireframe = false;
bool gResetRequested = false;
bool gTitleDirty = true;
std::string gDroppedPath; // set by the drop callback, consumed by the render loop

const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vNormal;

void main() {
    vNormal = uNormalMatrix * aNormal;
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)";

// Normals mapped from [-1, 1] to [0, 1]: a quick check that they are smooth and
// point outwards before any light depends on them.
const char* kFragmentShaderSource = R"(#version 330 core
in vec3 vNormal;

out vec4 FragColor;

void main() {
    FragColor = vec4(0.5 * normalize(vNormal) + 0.5, 1.0);
}
)";

// The light marker is drawn in the light's colour, without shading.
const char* kMarkerVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}
)";

const char* kMarkerFragmentShaderSource = R"(#version 330 core
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
    case GLFW_KEY_W:
        gWireframe = !gWireframe;
        break;
    case GLFW_KEY_R:
        gResetRequested = true;
        break;
    default:
        break;
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

// "sphere" and "torus" are generated; anything else is read as a PLY path.
CHE loadMesh(const std::string& name) {
    if (name == "sphere") {
        return make_sphere(1.0f, 32, 16);
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

void updateTitle(GLFWwindow* window, const std::string& meshName, const CHE& mesh) {
    char title[256];
    std::snprintf(title, sizeof(title),
                  "Task 09 - Illumination | %s  V=%d F=%d | light %s | camera %s",
                  meshName.c_str(), mesh.n_vertices(), mesh.n_triangles(),
                  gPaused ? "paused" : "orbiting", gCamera.animated() ? "animated" : "mouse");
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
    glfwWindowHint(GLFW_SAMPLES, 4);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 09 - Illumination", nullptr, nullptr);
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

    // 3. Shaders.
    const GLuint program = createShaderProgram(kVertexShaderSource, kFragmentShaderSource);
    const GLuint markerProgram =
        createShaderProgram(kMarkerVertexShaderSource, kMarkerFragmentShaderSource);
    if (program == 0 || markerProgram == 0) {
        glDeleteProgram(program);
        glDeleteProgram(markerProgram);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    const GLint modelLocation = glGetUniformLocation(program, "uModel");
    const GLint viewLocation = glGetUniformLocation(program, "uView");
    const GLint projectionLocation = glGetUniformLocation(program, "uProjection");
    const GLint normalMatrixLocation = glGetUniformLocation(program, "uNormalMatrix");
    const GLint markerModelLocation = glGetUniformLocation(markerProgram, "uModel");
    const GLint markerViewLocation = glGetUniformLocation(markerProgram, "uView");
    const GLint markerProjectionLocation = glGetUniformLocation(markerProgram, "uProjection");
    const GLint markerColorLocation = glGetUniformLocation(markerProgram, "uColor");

    // 4. Geometry: the model and a small sphere that marks where the light is.
    GpuMesh gpu;
    gpu.create();
    gpu.upload(mesh);
    std::string meshName = meshNames.front();

    GpuMesh marker;
    marker.create();
    marker.upload(make_sphere(kLightMarkerRadius, 16, 8));

    const glm::vec3 lightColor(1.0f);

    // 5. Render loop.
    float lightAngle = 0.0f;
    double lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;

        if (!gDroppedPath.empty()) {
            try {
                mesh = loadMesh(gDroppedPath);
                printStats(gDroppedPath, mesh);
                meshName = gDroppedPath;
                gpu.upload(mesh);
                gTitleDirty = true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "could not load '%s': %s\n", gDroppedPath.c_str(),
                             e.what());
            }
            gDroppedPath.clear();
        }

        if (gResetRequested) {
            gResetRequested = false;
            lightAngle = 0.0f;
            gCamera.reset();
        }
        if (!gPaused) {
            lightAngle += deltaTime * kLightSpeed;
            gCamera.update(deltaTime);
        }
        if (gTitleDirty) {
            gTitleDirty = false;
            updateTitle(window, meshName, mesh);
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            glfwWaitEvents(); // minimized: no valid aspect ratio or pixels to draw
            lastFrame = glfwGetTime();
            continue;
        }
        glViewport(0, 0, width, height);

        const glm::vec3 lightPos(kLightRadius * std::cos(lightAngle), kLightHeight,
                                 kLightRadius * std::sin(lightAngle));
        const glm::mat4 view = gCamera.view();
        const glm::mat4 projection = gCamera.projection(width, height);
        const glm::mat4 model(1.0f);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

        glUseProgram(program);
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        gpu.draw();

        const glm::mat4 markerModel = glm::translate(glm::mat4(1.0f), lightPos);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUseProgram(markerProgram);
        glUniformMatrix4fv(markerModelLocation, 1, GL_FALSE, glm::value_ptr(markerModel));
        glUniformMatrix4fv(markerViewLocation, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(markerProjectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(markerColorLocation, 1, glm::value_ptr(lightColor));
        marker.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    gpu.destroy();
    marker.destroy();
    glDeleteProgram(program);
    glDeleteProgram(markerProgram);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
