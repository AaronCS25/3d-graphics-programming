// Task 02 — Sierpinski Triangle
//
// Two ways to draw the same fractal, toggled with SPACE:
//   * chaos game  — the point-wise version from the statement: jump halfway to
//                   a randomly chosen vertex, plot the point, repeat
//   * subdivision — split the triangle into its three corner copies, n levels
//
// Same skeleton as task 01: context -> shaders -> VBO/VAO -> render loop.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;

constexpr int kMaxPoints = 200000;
constexpr int kPointsPerFrame = 2000;
constexpr float kPointSize = 3.0f;

// The first few iterates may still sit off the attractor; drop them so the
// gaps of the fractal stay clean.
constexpr int kWarmupIterations = 20;

constexpr int kMaxDepth = 8;
constexpr int kInitialDepth = 5;

struct Vec2 {
    float x;
    float y;
};

// Equilateral once the aspect correction below is applied.
constexpr Vec2 kVertices[3] = {{-0.9f, -0.78f}, {0.9f, -0.78f}, {0.0f, 0.78f}};

// One color per corner, laid out as the columns of a mat3 so the vertex shader
// can blend them with a single multiply.
constexpr float kVertexColors[9] = {
    0.98f, 0.28f, 0.42f, // left  — rose
    0.24f, 0.78f, 0.95f, // right — cyan
    0.99f, 0.81f, 0.29f, // apex  — amber
};

// Shading is positional: every fragment blends the three corner colors by the
// barycentric weights of its own position, so a point knows its color without
// carrying one. uToBarycentric turns an offset from uOrigin into the weights of
// the other two corners; the weight of uOrigin is whatever is left over.
const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos;

uniform vec2 uScale;
uniform vec2 uOrigin;
uniform mat2 uToBarycentric;
uniform mat3 uVertexColors;

out vec3 vColor;

void main() {
    vec2 w = uToBarycentric * (aPos - uOrigin);
    vColor = uVertexColors * vec3(1.0 - w.x - w.y, w.x, w.y);
    gl_Position = vec4(aPos * uScale, 0.0, 1.0);
}
)";

// Carves a disc out of the square point sprite: the statement asks for the
// samples to be drawn as small circles.
const char* kPointFragmentShaderSource = R"(#version 330 core
in vec3 vColor;

out vec4 FragColor;

void main() {
    if (length(gl_PointCoord - vec2(0.5)) > 0.5) {
        discard;
    }
    FragColor = vec4(vColor, 1.0);
}
)";

const char* kSolidFragmentShaderSource = R"(#version 330 core
in vec3 vColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

enum class Mode { ChaosGame, Subdivision };

Mode gMode = Mode::ChaosGame;
int gDepth = kInitialDepth;
bool gSubdivisionDirty = true;
bool gRestartRequested = false;

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
        gMode = gMode == Mode::ChaosGame ? Mode::Subdivision : Mode::ChaosGame;
        break;
    case GLFW_KEY_R:
        gRestartRequested = true;
        break;
    case GLFW_KEY_UP:
        if (gDepth < kMaxDepth) {
            ++gDepth;
            gSubdivisionDirty = true;
        }
        break;
    case GLFW_KEY_DOWN:
        if (gDepth > 0) {
            --gDepth;
            gSubdivisionDirty = true;
        }
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

// Inverse of the 2x2 matrix whose columns are v1-v0 and v2-v0, column-major as
// GLSL expects it.
std::array<float, 4> barycentricMatrix() {
    const float e1x = kVertices[1].x - kVertices[0].x;
    const float e1y = kVertices[1].y - kVertices[0].y;
    const float e2x = kVertices[2].x - kVertices[0].x;
    const float e2y = kVertices[2].y - kVertices[0].y;
    const float det = e1x * e2y - e2x * e1y;
    return {e2y / det, -e1y / det, -e2x / det, e1x / det};
}

// A linked program plus the only uniform that changes at runtime; the color
// basis never moves, so it is uploaded once here.
struct Program {
    GLuint id = 0;
    GLint scaleLocation = -1;
};

Program createProgram(const char* vertexSource, const char* fragmentSource) {
    Program program;
    program.id = createShaderProgram(vertexSource, fragmentSource);
    if (program.id == 0) {
        return program;
    }
    program.scaleLocation = glGetUniformLocation(program.id, "uScale");

    const std::array<float, 4> toBarycentric = barycentricMatrix();
    glUseProgram(program.id);
    glUniform2f(glGetUniformLocation(program.id, "uOrigin"), kVertices[0].x, kVertices[0].y);
    glUniformMatrix2fv(glGetUniformLocation(program.id, "uToBarycentric"), 1, GL_FALSE,
                       toBarycentric.data());
    glUniformMatrix3fv(glGetUniformLocation(program.id, "uVertexColors"), 1, GL_FALSE,
                       kVertexColors);
    return program;
}

// Keeps the triangle equilateral instead of stretching with the window.
Vec2 aspectScale(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {1.0f, 1.0f};
    }
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    return aspect >= 1.0f ? Vec2{1.0f / aspect, 1.0f} : Vec2{1.0f, aspect};
}

Vec2 midpoint(const Vec2& a, const Vec2& b) {
    return {0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
}

// Uniform sample over the triangle: the square root skews the barycentric
// weights so area, not parameter space, is sampled evenly.
Vec2 randomPointInside(std::mt19937& rng) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float s = std::sqrt(unit(rng));
    const float t = unit(rng);
    const float w0 = 1.0f - s;
    const float w1 = s * (1.0f - t);
    const float w2 = s * t;
    return {w0 * kVertices[0].x + w1 * kVertices[1].x + w2 * kVertices[2].x,
            w0 * kVertices[0].y + w1 * kVertices[1].y + w2 * kVertices[2].y};
}

Vec2 warmStart(std::mt19937& rng, std::uniform_int_distribution<int>& vertexPicker) {
    Vec2 p = randomPointInside(rng);
    for (int i = 0; i < kWarmupIterations; ++i) {
        p = midpoint(p, kVertices[vertexPicker(rng)]);
    }
    return p;
}

// Emits the three corner copies and drops the middle one, which is the hole.
void subdivide(const Vec2& a, const Vec2& b, const Vec2& c, int depth, std::vector<Vec2>& out) {
    if (depth == 0) {
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
        return;
    }
    const Vec2 ab = midpoint(a, b);
    const Vec2 bc = midpoint(b, c);
    const Vec2 ca = midpoint(c, a);
    subdivide(a, ab, ca, depth - 1, out);
    subdivide(ab, b, bc, depth - 1, out);
    subdivide(ca, bc, c, depth - 1, out);
}

void updateTitle(GLFWwindow* window, int pointCount, int triangleCount) {
    char title[128];
    if (gMode == Mode::ChaosGame) {
        std::snprintf(title, sizeof(title), "Task 02 - Sierpinski | chaos game | %d points",
                      pointCount);
    } else {
        std::snprintf(title, sizeof(title),
                      "Task 02 - Sierpinski | subdivision | depth %d | %d triangles", gDepth,
                      triangleCount);
    }
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
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 02 - Sierpinski", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Make the context current and load the GL function pointers.
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // 3. Shaders: one vertex stage, two fragment stages (round dots / flat fill).
    const Program pointProgram = createProgram(kVertexShaderSource, kPointFragmentShaderSource);
    const Program solidProgram = createProgram(kVertexShaderSource, kSolidFragmentShaderSource);
    if (pointProgram.id == 0 || solidProgram.id == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // 4. Two buffers: the chaos game grows into a preallocated one, the
    //    subdivision rebuilds its own whenever the depth changes.
    GLuint pointVao = 0;
    GLuint pointVbo = 0;
    glGenVertexArrays(1, &pointVao);
    glGenBuffers(1, &pointVbo);
    glBindVertexArray(pointVao);
    glBindBuffer(GL_ARRAY_BUFFER, pointVbo);
    glBufferData(GL_ARRAY_BUFFER, kMaxPoints * sizeof(Vec2), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), nullptr);
    glEnableVertexAttribArray(0);

    GLuint meshVao = 0;
    GLuint meshVbo = 0;
    glGenVertexArrays(1, &meshVao);
    glGenBuffers(1, &meshVbo);
    glBindVertexArray(meshVao);
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vec2), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> vertexPicker(0, 2);

    Vec2 current = warmStart(rng, vertexPicker);

    int pointCount = 0;
    int meshVertexCount = 0;
    std::vector<Vec2> batch;
    batch.reserve(kPointsPerFrame);
    std::vector<Vec2> mesh;

    glPointSize(kPointSize);
    updateTitle(window, pointCount, 0);

    // 5. Render loop.
    Mode lastMode = gMode;

    while (!glfwWindowShouldClose(window)) {
        bool titleDirty = gMode != lastMode;
        lastMode = gMode;

        if (gRestartRequested) {
            gRestartRequested = false;
            pointCount = 0;
            current = warmStart(rng, vertexPicker);
            titleDirty = true;
        }

        if (gMode == Mode::ChaosGame && pointCount < kMaxPoints) {
            const int wanted = std::min(kPointsPerFrame, kMaxPoints - pointCount);
            batch.clear();
            for (int i = 0; i < wanted; ++i) {
                current = midpoint(current, kVertices[vertexPicker(rng)]);
                batch.push_back(current);
            }
            glBindBuffer(GL_ARRAY_BUFFER, pointVbo);
            glBufferSubData(GL_ARRAY_BUFFER, pointCount * sizeof(Vec2), batch.size() * sizeof(Vec2),
                            batch.data());
            pointCount += wanted;
            titleDirty = true;
        }

        if (gMode == Mode::Subdivision && gSubdivisionDirty) {
            gSubdivisionDirty = false;
            mesh.clear();
            subdivide(kVertices[0], kVertices[1], kVertices[2], gDepth, mesh);
            meshVertexCount = static_cast<int>(mesh.size());
            glBindBuffer(GL_ARRAY_BUFFER, meshVbo);
            glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(Vec2), mesh.data(), GL_STATIC_DRAW);
            titleDirty = true;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        const Vec2 scale = aspectScale(framebufferWidth, framebufferHeight);

        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (gMode == Mode::ChaosGame) {
            glUseProgram(pointProgram.id);
            glUniform2f(pointProgram.scaleLocation, scale.x, scale.y);
            glBindVertexArray(pointVao);
            glDrawArrays(GL_POINTS, 0, pointCount);
        } else {
            glUseProgram(solidProgram.id);
            glUniform2f(solidProgram.scaleLocation, scale.x, scale.y);
            glBindVertexArray(meshVao);
            glDrawArrays(GL_TRIANGLES, 0, meshVertexCount);
        }

        if (titleDirty) {
            updateTitle(window, pointCount, meshVertexCount / 3);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 6. Cleanup.
    glDeleteVertexArrays(1, &pointVao);
    glDeleteVertexArrays(1, &meshVao);
    glDeleteBuffers(1, &pointVbo);
    glDeleteBuffers(1, &meshVbo);
    glDeleteProgram(pointProgram.id);
    glDeleteProgram(solidProgram.id);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
