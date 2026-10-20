// The state of the viewer and every action the panel, the keyboard and the
// mouse can trigger. main.cpp owns the window; the panel only reads and
// writes this.

#pragma once

#include "object.hpp"
#include "renderer.hpp"
#include "world.hpp"

#include <camera.hpp>

#include <array>
#include <string>

struct Timeline {
    static constexpr float kLength = 120.0f; // seconds, then it starts over
    float time = 0.0f;
    bool playing = true;
    float speed = 1.0f;
};

struct Performance {
    std::array<float, 240> frame_ms{}; // ring buffer for the plot
    int next = 0;
    RenderStats stats;
    double fmm_ms = 0.0;

    void record(float milliseconds) {
        frame_ms[static_cast<size_t>(next)] = milliseconds;
        next = (next + 1) % static_cast<int>(frame_ms.size());
    }
};

struct App {
    World world;
    Renderer renderer;
    RenderSettings settings;
    camera cam;
    Timeline timeline;
    Performance performance;

    int preset = 0; // -1 while a dropped file is shown
    int selected = -1;
    float simplify_fraction = 0.25f;
    bool simplified_once = false;
    SimplifyResult last_simplify;
    std::string message; // result of the last action, shown in the panel
    bool show_panel = true;
    int panel_width = 0; // window units taken by the panel on the left

    bool create();
    void destroy();

    void load_preset(int index);
    void load_file(const std::string& path);
    void reset();

    // Advances the timeline (when playing), the camera and the animation.
    void update(float seconds);
    glm::vec3 light_direction() const;
    // Framebuffer size; the scene is drawn right of the panel.
    void render(int framebuffer_width, int framebuffer_height, int window_width);

    // Cursor and size of the scene view, in window units. Selects the object
    // under the cursor and puts a seed at the picked vertex, replacing the
    // object's seeds or, with `add`, next to them.
    void pick(double x, double y, int width, int height, bool add);
    void clear_seeds();
    void set_normals(NormalSource source);
    void simplify_selected();
    void restore_selected();

    Object* selection();

  private:
    void enter(World next);
};

// --check: builds every preset without a window and exercises the geometry
// pipeline (welding, Fast Marching, simplification, picking). Returns the
// number of failures.
int run_checks();
