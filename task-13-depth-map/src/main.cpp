// Task 13 — Depth Map by Ray Casting
//
// The image is not rasterized: for every pixel a primary ray leaves the camera,
// is tested against every triangle of the scene, and the distance to the
// nearest hit becomes a grey level. OpenGL only shows the finished image.
//
//   1. read the OBJ (obj_loader of task 12) and place it in the world
//   2. build the ray caster's scene: world-space triangles + a box per object
//   3. glfwInit() + window + gladLoadGLLoader(), a texture and a full-screen triangle
//   4. render loop: when the camera moves, cast the rays again (depth_map),
//      upload the grey levels to the texture, draw it
//   5. cleanup + glfwTerminate()
//
// Usage:  task13 [gallery | path/to/scene.obj] [--save]
//         --save renders the image without opening a window and writes it to
//         docs/depth.png.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <camera.hpp>

#include "depth_map.hpp"
#include "obj_loader.hpp"
#include "ray_scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace {

constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 800;

// While the mouse drags the camera, the rays are cast for one pixel in
// kPreviewStep x kPreviewStep so the image follows the mouse.
constexpr int kPreviewStep = 4;

// Same placement as task 12: longest side to this size, centred on the y axis,
// standing on y = 0.
constexpr float kSceneSize = 16.0f;

struct Preset {
    const char* name;
    const char* file; // relative to SCENE_DIR
    float yaw;        // degrees about y, applied to the whole scene
    glm::vec3 target;
    float orbit_radius;
    float orbit_height;
};

// Task 12's gallery and its camera. Its Sibenik cathedral is left out: 75 k
// triangles in a single object take minutes without a BVH (see the README).
const Preset kPresets[] = {
    {"gallery", "gallery/gallery.obj", 0.0f, {0.0f, 1.2f, 0.0f}, 12.0f, 4.5f},
};

camera gCamera;
camera gStartCamera;
bool gUseBoxes = true;
bool gDirty = true;
bool gSaveRequested = false;

const char* const kVertexSource = R"(#version 330 core
out vec2 vUv;

// One triangle that covers the screen, its corners made from gl_VertexID.
void main() {
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragmentSource = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uDepth;
out vec4 FragColor;

void main() {
    FragColor = vec4(vec3(texture(uDepth, vUv).r), 1.0);
}
)";

const Preset* presetOf(const std::string& name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
}

// The model matrix of task 12 and the camera that frames the result: the
// preset's, or for any other file an orbit at about twice the scene's radius.
glm::mat4 placeScene(const Scene& scene, const Preset* preset) {
    const glm::vec3 extent = scene.bounds_max - scene.bounds_min;
    const float scale = kSceneSize / std::max({extent.x, extent.y, extent.z, 1e-6f});
    const glm::vec3 anchor(0.5f * (scene.bounds_min.x + scene.bounds_max.x), scene.bounds_min.y,
                           0.5f * (scene.bounds_min.z + scene.bounds_max.z));
    const float yaw = preset != nullptr ? preset->yaw : 0.0f;

    if (preset != nullptr) {
        gStartCamera = camera(preset->target, preset->orbit_radius, preset->orbit_height, 0.0f);
    } else {
        const float radius = 0.5f * glm::length(extent) * scale;
        gStartCamera = camera(glm::vec3(0.0f, 0.5f * extent.y * scale, 0.0f), 1.7f * radius,
                              0.7f * radius, 0.0f);
    }
    gCamera = gStartCamera;

    return glm::rotate(glm::mat4(1.0f), glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
           glm::translate(glm::mat4(1.0f), -anchor);
}

bool savePng(const DepthMap& map, const std::string& path) {
    const std::vector<unsigned char> pixels = to_grayscale(map);
    stbi_flip_vertically_on_write(1); // the map's row 0 is the bottom, a PNG's is the top
    return stbi_write_png(path.c_str(), map.width, map.height, 1, pixels.data(), map.width) != 0;
}

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
    gDirty = true;
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
    } else if (action == GLFW_RELEASE) {
        gCamera.end_drag();
        gDirty = true; // the last preview becomes a full-resolution image
    }
}

void cursorPosCallback(GLFWwindow* /*window*/, double x, double y) {
    if (gCamera.dragging()) {
        gCamera.drag(x, y);
        gDirty = true;
    }
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    gCamera.zoom(yoffset);
    gDirty = true;
}

void focusCallback(GLFWwindow* /*window*/, int focused) {
    if (!focused && gCamera.dragging()) {
        gCamera.end_drag();
        gDirty = true;
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
    case GLFW_KEY_B:
        gUseBoxes = !gUseBoxes;
        gDirty = true;
        break;
    case GLFW_KEY_R:
        gCamera = gStartCamera;
        gDirty = true;
        break;
    case GLFW_KEY_S:
        gSaveRequested = true;
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

void updateTitle(GLFWwindow* window, const std::string& name, const RayScene& scene,
                 const DepthMap& map, double milliseconds) {
    char title[256];
    std::snprintf(title, sizeof(title),
                  "Task 13 - Depth Map | %s  %d tris | %dx%d in %.0f ms | depth %.2f - %.2f | "
                  "boxes %s [B]",
                  name.c_str(), scene.triangle_count(), map.width, map.height, milliseconds,
                  map.nearest, map.farthest, gUseBoxes ? "on" : "off");
    glfwSetWindowTitle(window, title);
}

} // namespace

int main(int argc, char** argv) {
    std::string name = kPresets[0].name;
    bool saveOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--save") == 0) {
            saveOnly = true;
        } else {
            name = argv[i];
        }
    }

    const Preset* preset = presetOf(name);
    const std::string path = preset != nullptr ? std::string(SCENE_DIR) + preset->file : name;
    Scene obj;
    try {
        obj = load_obj(path);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    const RayScene scene(obj, placeScene(obj, preset));
    const std::string output = std::string(DOCS_DIR) + "depth.png";

    if (saveOnly) {
        const auto start = std::chrono::steady_clock::now();
        const DepthMap map = render_depth(scene, gCamera, kWindowWidth, kWindowHeight, gUseBoxes);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        std::printf("%s: %d triangles, %dx%d rays in %.2f s, depth %.2f - %.2f\n", name.c_str(),
                    scene.triangle_count(), map.width, map.height, elapsed.count(), map.nearest,
                    map.farthest);
        if (!savePng(map, output)) {
            std::fprintf(stderr, "Could not write %s\n", output.c_str());
            return 1;
        }
        std::printf("wrote %s\n", output.c_str());
        return 0;
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

    GLFWwindow* window =
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Task 13 - Depth Map", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetWindowFocusCallback(window, focusCallback);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize glad\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const GLuint program = createShaderProgram(kVertexSource, kFragmentSource);
    if (program == 0) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // The core profile draws nothing without a VAO bound, even with no attributes.
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // one byte per pixel: rows are not 4-byte aligned

    DepthMap map;
    while (!glfwWindowShouldClose(window)) {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) {
            glfwWaitEvents(); // minimized: no pixels to cast rays for
            continue;
        }

        if (gDirty) {
            gDirty = false;
            const int step = gCamera.dragging() ? kPreviewStep : 1;
            const auto start = std::chrono::steady_clock::now();
            map = render_depth(scene, gCamera, width / step, height / step, gUseBoxes);
            const std::chrono::duration<double, std::milli> elapsed =
                std::chrono::steady_clock::now() - start;

            const std::vector<unsigned char> pixels = to_grayscale(map);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, map.width, map.height, 0, GL_RED,
                         GL_UNSIGNED_BYTE, pixels.data());
            updateTitle(window, name, scene, map, elapsed.count());
        }

        if (gSaveRequested) {
            gSaveRequested = false;
            if (savePng(map, output)) {
                std::printf("wrote %s (%dx%d)\n", output.c_str(), map.width, map.height);
            } else {
                std::fprintf(stderr, "Could not write %s\n", output.c_str());
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        // Nothing changes until an event arrives, so wait for it instead of
        // spinning; a drag still produces one event per mouse move.
        glfwWaitEvents();
    }

    glDeleteTextures(1, &texture);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
