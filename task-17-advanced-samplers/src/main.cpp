// Task 17 — Advanced Samplers: Halton and Sobol vs std::random
//
// Point clouds in the unit cube from seven generators (samplers.hpp), drawn as
// GL_POINTS inside the cube's wireframe, with the shared arcball camera and
// orthographic views along the axes, where patterns show up best. Each point
// is coloured by its index, so the order in which a sequence fills the cube is
// visible too.
//
//   1. glfwInit() + window + gladLoadGLLoader(), shaders, cube wireframe
//   2. generate the points of the current generator, upload them to a VBO
//   3. render loop: points + cube with the current view
//   4. cleanup + glfwTerminate()
//
// Usage:  task17            the viewer
//         task17 --stats    discrepancy and stratification tables, no window
//         task17 --save     writes the images of the README to docs/

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

#include "samplers.hpp"
#include "stats.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWindowSize = 900;
constexpr int kSaveSize = 800;
constexpr int kMinPoints = 64;
constexpr int kMaxPoints = 1 << 20;
// Warnock's formula is O(n^2): above this the title leaves it out.
constexpr int kMaxDiscrepancyPoints = 16384;

const glm::vec3 kCentre(0.5f);

// RANDU's points lie on the planes 9x - 6y + z = k (samplers.cpp). Seen along
// a direction inside those planes, each plane becomes a line.
const glm::vec3 kRanduNormal = glm::normalize(glm::vec3(9.0f, -6.0f, 1.0f));

enum class View { Arcball, Front, Side, Top, Planes };
constexpr int kViewCount = 5;

const char* nameOf(View view) {
    switch (view) {
    case View::Arcball: return "arcball";
    case View::Front: return "front (x, y)";
    case View::Side: return "side (z, y)";
    case View::Top: return "top (x, z)";
    case View::Planes: return "along RANDU's planes";
    }
    return "?";
}

struct State {
    Generator generator = Generator::Sobol;
    int count = 4096;
    View view = View::Arcball;
    bool colourByIndex = true;
    float pointSize = 3.0f; // in window units
    bool dirty = true;      // the points must be generated again
    bool titleDirty = true;
    bool saveRequested = false;
};

State gState;
camera gCamera;

camera startCamera() {
    return camera(kCentre, 2.2f, 1.1f, 0.0f);
}

const char* const kPointVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aOrder; // index / count, in [0, 1)
uniform mat4 uViewProj;
uniform float uPointSize;
out float vOrder;
void main() {
    vOrder = aOrder;
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
)";

// Viridis (a polynomial fit by Matt Zucker), from dark blue for the first
// points to yellow for the last. Round points: the corners of the square
// sprite are discarded.
const char* const kPointFragment = R"(#version 330 core
in float vOrder;
uniform bool uColourByIndex;
out vec4 FragColor;
vec3 viridis(float t) {
    const vec3 c0 = vec3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
    const vec3 c1 = vec3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
    const vec3 c2 = vec3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
    const vec3 c3 = vec3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
    const vec3 c4 = vec3(6.228269936347081, 14.17993336680509, 56.69055260068105);
    const vec3 c5 = vec3(4.776384997670288, -13.74514537774601, -65.35303263337234);
    const vec3 c6 = vec3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}
void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    if (dot(d, d) > 1.0) {
        discard;
    }
    // Start at 0.2: the darkest end of viridis vanishes on the background.
    vec3 colour = uColourByIndex ? viridis(mix(0.2, 1.0, vOrder)) : vec3(0.92);
    FragColor = vec4(colour, 1.0);
}
)";

const char* const kLineVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* const kLineFragment = R"(#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.45, 0.47, 0.52, 1.0);
}
)";

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

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
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

// Everything that draws: the two programs, the point cloud and the cube.
struct Renderer {
    GLuint pointProgram = 0;
    GLuint lineProgram = 0;
    GLuint pointVao = 0, pointVbo = 0;
    GLuint cubeVao = 0, cubeVbo = 0;
    int pointCount = 0;

    bool create() {
        pointProgram = createProgram(kPointVertex, kPointFragment);
        lineProgram = createProgram(kLineVertex, kLineFragment);
        if (pointProgram == 0 || lineProgram == 0) {
            return false;
        }

        glGenVertexArrays(1, &pointVao);
        glGenBuffers(1, &pointVbo);
        glBindVertexArray(pointVao);
        glBindBuffer(GL_ARRAY_BUFFER, pointVbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // The 12 edges of the unit cube: each pair of corners that differ in one axis.
        std::vector<float> edges;
        for (int a = 0; a < 8; ++a) {
            for (int axis = 0; axis < 3; ++axis) {
                const int b = a | (1 << axis);
                if (b == a) {
                    continue;
                }
                for (const int corner : {a, b}) {
                    edges.push_back(static_cast<float>(corner & 1));
                    edges.push_back(static_cast<float>((corner >> 1) & 1));
                    edges.push_back(static_cast<float>((corner >> 2) & 1));
                }
            }
        }
        glGenVertexArrays(1, &cubeVao);
        glGenBuffers(1, &cubeVbo);
        glBindVertexArray(cubeVao);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(edges.size() * sizeof(float)),
                     edges.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        glEnable(GL_PROGRAM_POINT_SIZE); // the vertex shader sets gl_PointSize
        glEnable(GL_DEPTH_TEST);
        return true;
    }

    void upload(const std::vector<glm::dvec3>& points) {
        std::vector<float> data;
        data.reserve(points.size() * 4);
        for (size_t i = 0; i < points.size(); ++i) {
            data.push_back(static_cast<float>(points[i].x));
            data.push_back(static_cast<float>(points[i].y));
            data.push_back(static_cast<float>(points[i].z));
            data.push_back(static_cast<float>(i) / static_cast<float>(points.size()));
        }
        glBindBuffer(GL_ARRAY_BUFFER, pointVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                     data.data(), GL_STATIC_DRAW);
        pointCount = static_cast<int>(points.size());
    }

    void draw(const glm::mat4& viewProj, float pointSize, bool colourByIndex) const {
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(lineProgram);
        glUniformMatrix4fv(glGetUniformLocation(lineProgram, "uViewProj"), 1, GL_FALSE,
                           glm::value_ptr(viewProj));
        glBindVertexArray(cubeVao);
        glDrawArrays(GL_LINES, 0, 24);

        glUseProgram(pointProgram);
        glUniformMatrix4fv(glGetUniformLocation(pointProgram, "uViewProj"), 1, GL_FALSE,
                           glm::value_ptr(viewProj));
        glUniform1f(glGetUniformLocation(pointProgram, "uPointSize"), pointSize);
        glUniform1i(glGetUniformLocation(pointProgram, "uColourByIndex"), colourByIndex);
        glBindVertexArray(pointVao);
        glDrawArrays(GL_POINTS, 0, pointCount);
    }

    void destroy() {
        glDeleteBuffers(1, &pointVbo);
        glDeleteBuffers(1, &cubeVbo);
        glDeleteVertexArrays(1, &pointVao);
        glDeleteVertexArrays(1, &cubeVao);
        glDeleteProgram(pointProgram);
        glDeleteProgram(lineProgram);
    }
};

// The orthographic views look at the centre of the cube from a fixed
// direction, so the cloud is seen as its projection on one plane.
glm::mat4 viewProjection(View view, const camera& cam, int width, int height) {
    if (view == View::Arcball) {
        return cam.projection(width, height) * cam.view();
    }
    glm::vec3 direction(0.0f, 0.0f, 1.0f); // from the centre towards the eye
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    float half = 0.72f; // half the height of what is shown
    switch (view) {
    case View::Side:
        direction = glm::vec3(1.0f, 0.0f, 0.0f);
        break;
    case View::Top:
        direction = glm::vec3(0.0f, 1.0f, 0.0f);
        up = glm::vec3(0.0f, 0.0f, -1.0f);
        break;
    case View::Planes:
        // A direction inside the planes (perpendicular to their normal), with
        // the normal pointing up: the planes become horizontal lines.
        direction = glm::normalize(glm::cross(kRanduNormal, glm::vec3(0.0f, 1.0f, 0.0f)));
        up = kRanduNormal;
        half = 0.95f;
        break;
    default:
        break;
    }
    const float aspect = height > 0 ? static_cast<float>(width) / height : 1.0f;
    return glm::ortho(-half * aspect, half * aspect, -half, half, 0.1f, 10.0f) *
           glm::lookAt(kCentre + 3.0f * direction, kCentre, up);
}

void updateTitle(GLFWwindow* window, const std::vector<glm::dvec3>& points) {
    char discrepancy[96] = "D* n/a (n too large)";
    const int n = static_cast<int>(points.size());
    if (n <= kMaxDiscrepancyPoints) {
        std::snprintf(discrepancy, sizeof(discrepancy), "D* %.5f (random ~%.5f)",
                      l2_star_discrepancy(points), random_l2_star_discrepancy(n));
    }
    char title[256];
    std::snprintf(title, sizeof(title), "Task 17 - Samplers | [%d] %s | n = %d [up/down] | %s | %s [V]",
                  static_cast<int>(gState.generator) + 1, name_of(gState.generator), n,
                  discrepancy, nameOf(gState.view));
    glfwSetWindowTitle(window, title);
}

std::string savePixels(int width, int height, const std::string& name) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1); // OpenGL's row 0 is the bottom
    const std::string path = std::string(DOCS_DIR) + name + ".png";
    if (stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) == 0) {
        std::fprintf(stderr, "Could not write %s\n", path.c_str());
        return {};
    }
    return path;
}

// --save: every generator from the arcball view and from the front, rendered
// into an offscreen framebuffer of a fixed size, plus the view along RANDU's
// planes for RANDU and for std::mt19937.
int saveImages(Renderer& renderer) {
    GLuint fbo = 0, colour = 0, depth = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &colour);
    glBindRenderbuffer(GL_RENDERBUFFER, colour);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kSaveSize, kSaveSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kSaveSize, kSaveSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "Offscreen framebuffer incomplete\n");
        return 1;
    }
    glViewport(0, 0, kSaveSize, kSaveSize);

    const camera cam = startCamera();
    const auto shoot = [&](Generator generator, int n, View view, const std::string& name) {
        renderer.upload(generate(generator, n));
        // Smaller points in 3D, where the cloud overlaps itself; larger ones in
        // the projections, which show fewer points.
        const float size = view == View::Arcball ? 3.0f : 5.0f;
        renderer.draw(viewProjection(view, cam, kSaveSize, kSaveSize), size, true);
        const std::string path = savePixels(kSaveSize, kSaveSize, name);
        if (!path.empty()) {
            std::printf("wrote %s\n", path.c_str());
        }
    };
    for (int g = 0; g < kGeneratorCount; ++g) {
        const Generator generator = static_cast<Generator>(g);
        const std::string file = file_name_of(generator);
        shoot(generator, 4096, View::Arcball, file + "_3d");
        shoot(generator, 1024, View::Front, file + "_front");
    }
    shoot(Generator::Randu, 4096, View::Planes, "randu_planes");
    shoot(Generator::Mt19937, 4096, View::Planes, "mt19937_planes");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &colour);
    glDeleteRenderbuffers(1, &depth);
    glDeleteFramebuffers(1, &fbo);
    return 0;
}

// --stats: the tables of the README, in Markdown.
void printStats() {
    std::printf("| Generator | D* n = 512 | D* n = 4096 | 16^3 grid, n = 4096: empty / fullest | "
                "8x9x5 grid, n = 360: empty / fullest |\n");
    std::printf("|---|---|---|---|---|\n");
    std::printf("| expected for random points | %.5f | %.5f | 1507 (36.8%%) | 132 (36.8%%) |\n",
                random_l2_star_discrepancy(512), random_l2_star_discrepancy(4096));
    for (int g = 0; g < kGeneratorCount; ++g) {
        const Generator generator = static_cast<Generator>(g);
        const std::vector<glm::dvec3> p4096 = generate(generator, 4096);
        const std::vector<glm::dvec3> p512(p4096.begin(), p4096.begin() + 512);
        const std::vector<glm::dvec3> p360(p4096.begin(), p4096.begin() + 360);
        const Occupancy cube = occupancy(p4096, 16, 16, 16);
        const Occupancy halton = occupancy(p360, 8, 9, 5);
        std::printf("| %s | %.5f | %.5f | %d (%.1f%%) / %d | %d (%.1f%%) / %d |\n",
                    name_of(generator), l2_star_discrepancy(p512), l2_star_discrepancy(p4096),
                    cube.empty, 100.0 * cube.empty / cube.cells, cube.max_count, halton.empty,
                    100.0 * halton.empty / halton.cells, halton.max_count);
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    if (key >= GLFW_KEY_1 && key < GLFW_KEY_1 + kGeneratorCount) {
        gState.generator = static_cast<Generator>(key - GLFW_KEY_1);
        gState.dirty = true;
        return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_UP:
        gState.count = std::min(gState.count * 2, kMaxPoints);
        gState.dirty = true;
        break;
    case GLFW_KEY_DOWN:
        gState.count = std::max(gState.count / 2, kMinPoints);
        gState.dirty = true;
        break;
    case GLFW_KEY_V:
        gState.view = static_cast<View>((static_cast<int>(gState.view) + 1) % kViewCount);
        gState.titleDirty = true;
        break;
    case GLFW_KEY_C:
        gState.colourByIndex = !gState.colourByIndex;
        break;
    case GLFW_KEY_LEFT_BRACKET:
        gState.pointSize = std::max(gState.pointSize - 1.0f, 1.0f);
        break;
    case GLFW_KEY_RIGHT_BRACKET:
        gState.pointSize = std::min(gState.pointSize + 1.0f, 12.0f);
        break;
    case GLFW_KEY_R:
        gCamera = startCamera();
        gState.view = View::Arcball;
        gState.titleDirty = true;
        break;
    case GLFW_KEY_S:
        gState.saveRequested = true;
        break;
    default:
        break;
    }
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
        if (gState.view != View::Arcball) { // dragging goes back to the free camera
            gState.view = View::Arcball;
            gState.titleDirty = true;
        }
    } else if (action == GLFW_RELEASE) {
        gCamera.end_drag();
    }
}

void cursorPosCallback(GLFWwindow* /*window*/, double x, double y) {
    gCamera.drag(x, y);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    gCamera.zoom(yoffset);
}

void focusCallback(GLFWwindow* /*window*/, int focused) {
    if (!focused) {
        gCamera.end_drag();
    }
}

} // namespace

int main(int argc, char** argv) {
    bool save = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stats") == 0) {
            printStats();
            return 0;
        }
        if (std::strcmp(argv[i], "--save") == 0) {
            save = true;
        }
    }

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
    if (save) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // only the offscreen framebuffer is used
    }
    GLFWwindow* window =
        glfwCreateWindow(kWindowSize, kWindowSize, "Task 17 - Samplers", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize glad\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    Renderer renderer;
    if (!renderer.create()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int status = 0;
    if (save) {
        status = saveImages(renderer);
    } else {
        glfwSetKeyCallback(window, keyCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetWindowFocusCallback(window, focusCallback);
        gCamera = startCamera();

        std::vector<glm::dvec3> points;
        while (!glfwWindowShouldClose(window)) {
            if (gState.dirty) {
                gState.dirty = false;
                points = generate(gState.generator, gState.count);
                renderer.upload(points);
                gState.titleDirty = true;
            }
            if (gState.titleDirty) {
                gState.titleDirty = false;
                updateTitle(window, points);
            }

            int width = 0, height = 0, windowWidth = 0, windowHeight = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glfwGetWindowSize(window, &windowWidth, &windowHeight);
            if (width > 0 && height > 0) {
                // Point sizes are in pixels of the framebuffer, twice the window
                // units on a Retina screen.
                const float scale = windowWidth > 0 ? static_cast<float>(width) / windowWidth : 1.0f;
                glViewport(0, 0, width, height);
                renderer.draw(viewProjection(gState.view, gCamera, width, height),
                              gState.pointSize * scale, gState.colourByIndex);
                if (gState.saveRequested) {
                    gState.saveRequested = false;
                    const std::string name = std::string(file_name_of(gState.generator)) + "_" +
                                             std::to_string(gState.count) + "_screenshot";
                    const std::string path = savePixels(width, height, name);
                    if (!path.empty()) {
                        std::printf("wrote %s\n", path.c_str());
                    }
                }
            }
            glfwSwapBuffers(window);
            glfwWaitEvents(); // nothing moves on its own
        }
    }

    renderer.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
