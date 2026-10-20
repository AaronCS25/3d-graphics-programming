// Project 1 — Interactive 3D Scene Viewer
//
// The tasks of the course in one real-time viewer: OBJ/MTL scenes with
// textures (tasks 11, 12), Blinn-Phong lighting with a shadow-casting sun
// (tasks 09, 10), procedural animation on a timeline (task 05), the arcball
// camera (task 08), and the geometry algorithms on the half-edge structure:
// Fast Marching distances from picked seeds (task 06), QEM simplification
// (task 04) and normal recomputation, all driven from a Dear ImGui panel.
//
//   1. glfwInit() + window hints (OpenGL 3.3 core) + glfwCreateWindow()
//   2. glad, our input callbacks, then Dear ImGui chained in front of them
//   3. App::create(): shaders, shadow map, first preset
//   4. loop: input -> timeline and animation -> panel -> both passes -> panel on top
//   5. cleanup
//
// Usage:  project01 [path/to/scene.obj | model.ply] [--check]
//         --check builds every preset without a window and exercises the
//         geometry pipeline. While running, drop an .obj or .ply on the window.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "app.hpp"
#include "panel.hpp"
#include "presets.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kWindowWidth = 1440;
constexpr int kWindowHeight = 900;
constexpr int kPanelWidth = 380; // window units

App gApp;
std::string gDroppedPath; // set by the drop callback, consumed by the loop

// ImGui gets every event first; when the cursor is over the panel or a widget
// is active, the scene does not react.
bool panelWantsMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool panelWantsKeyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT) {
        gApp.cam.end_drag();
        return;
    }
    if (action != GLFW_PRESS || panelWantsMouse()) {
        return;
    }
    // In the coordinates of the scene view, right of the panel.
    double x = 0.0, y = 0.0;
    int width = 0, height = 0;
    glfwGetCursorPos(window, &x, &y);
    glfwGetWindowSize(window, &width, &height);
    x -= gApp.panel_width;
    width -= gApp.panel_width;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gApp.cam.begin_drag(x, y, width, height);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        gApp.pick(x, y, width, height, (mods & GLFW_MOD_SHIFT) != 0);
    }
}

void cursorPosCallback(GLFWwindow* /*window*/, double x, double y) {
    gApp.cam.drag(x - gApp.panel_width, y);
}

void scrollCallback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    if (!panelWantsMouse()) {
        gApp.cam.zoom(yoffset);
    }
}

void windowSizeCallback(GLFWwindow* /*window*/, int /*width*/, int /*height*/) {
    gApp.cam.end_drag(); // the old drag was mapped to different window dimensions
}

void focusCallback(GLFWwindow* /*window*/, int focused) {
    if (!focused) {
        gApp.cam.end_drag(); // do not keep dragging after switching applications
    }
}

void dropCallback(GLFWwindow* /*window*/, int count, const char** paths) {
    if (count > 0) {
        gDroppedPath = paths[0];
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS || panelWantsKeyboard()) {
        return;
    }
    RenderSettings& s = gApp.settings;
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, true);
        break;
    case GLFW_KEY_SPACE:
        gApp.timeline.playing = !gApp.timeline.playing;
        break;
    case GLFW_KEY_R:
        gApp.reset();
        break;
    case GLFW_KEY_C:
        gApp.cam.set_animated(!gApp.cam.animated());
        break;
    case GLFW_KEY_V:
        s.view = static_cast<ViewMode>((static_cast<int>(s.view) + 1) % kViewModeCount);
        break;
    case GLFW_KEY_N:
        if (const Object* object = gApp.selection()) {
            gApp.set_normals(
                static_cast<NormalSource>((static_cast<int>(object->normals) + 1) % 3));
        }
        break;
    case GLFW_KEY_X:
        gApp.simplify_selected();
        break;
    case GLFW_KEY_Z:
        gApp.restore_selected();
        break;
    case GLFW_KEY_1:
        s.diffuse_maps = !s.diffuse_maps;
        break;
    case GLFW_KEY_2:
        s.specular_maps = !s.specular_maps;
        break;
    case GLFW_KEY_3:
        s.normal_maps = !s.normal_maps;
        break;
    case GLFW_KEY_S:
        s.shadows = !s.shadows;
        break;
    case GLFW_KEY_W:
        s.wireframe = !s.wireframe;
        break;
    case GLFW_KEY_DELETE:
    case GLFW_KEY_BACKSPACE:
        gApp.clear_seeds();
        break;
    case GLFW_KEY_P:
        gApp.load_preset((gApp.preset + 1) % kPresetCount);
        break;
    case GLFW_KEY_F1:
        gApp.show_panel = !gApp.show_panel;
        break;
    default:
        break;
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string startFile;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            return run_checks();
        }
        startFile = argv[i];
    }

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
        glfwCreateWindow(kWindowWidth, kWindowHeight, "Project 1 - Scene Viewer", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    // 2. Context, function pointers, callbacks. ImGui installs its own
    //    callbacks and calls ours after its own, so ours go first.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetWindowSizeCallback(window, windowSizeCallback);
    glfwSetWindowFocusCallback(window, focusCallback);
    glfwSetDropCallback(window, dropCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the layout is set in code
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // 3. Shaders, shadow map and the first scene.
    if (!gApp.create()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!startFile.empty()) {
        gApp.load_file(startFile);
    }

    // 4. Render loop.
    double lastFrame = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const double now = glfwGetTime();
        const float deltaTime = static_cast<float>(now - lastFrame);
        lastFrame = now;
        gApp.performance.record(1000.0f * deltaTime);

        if (!gDroppedPath.empty()) {
            gApp.load_file(gDroppedPath);
            gDroppedPath.clear();
            lastFrame = glfwGetTime(); // loading is not animation time
        }

        int width = 0;
        int height = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        if (width <= 0 || height <= 0) {
            glfwWaitEvents(); // minimized: no valid aspect ratio or pixels to draw
            lastFrame = glfwGetTime();
            continue;
        }

        gApp.update(deltaTime);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        gApp.panel_width = gApp.show_panel ? std::min(kPanelWidth, windowWidth / 2) : 0;
        if (gApp.show_panel) {
            draw_panel(gApp, gApp.panel_width, windowHeight);
        }
        ImGui::Render();

        gApp.render(width, height, windowWidth);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 5. Cleanup.
    gApp.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
