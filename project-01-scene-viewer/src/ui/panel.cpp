#include "panel.hpp"

#include "presets.hpp"

#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <set>

namespace {

void scene_section(App& app) {
    if (!ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const char* current = app.preset >= 0 ? kPresets[app.preset].name : app.world.name.c_str();
    if (ImGui::BeginCombo("Preset", current)) {
        for (int i = 0; i < kPresetCount; ++i) {
            if (ImGui::Selectable(kPresets[i].name, i == app.preset)) {
                app.load_preset(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Drop an .obj or .ply on the window to load it.");

    if (ImGui::BeginListBox("Objects",
                            ImVec2(-FLT_MIN, 6 * ImGui::GetTextLineHeightWithSpacing()))) {
        for (size_t i = 0; i < app.world.objects.size(); ++i) {
            const Object& object = app.world.objects[i];
            char label[96];
            std::snprintf(label, sizeof(label), "%s  (%d tris)##%zu", object.name.c_str(),
                          object.gpu.triangles, i);
            if (ImGui::Selectable(label, app.selected == static_cast<int>(i))) {
                app.selected = static_cast<int>(i);
            }
        }
        ImGui::EndListBox();
    }
}

void timeline_section(App& app) {
    if (!ImGui::CollapsingHeader("Timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    Timeline& timeline = app.timeline;
    if (ImGui::Button(timeline.playing ? "Pause" : "Play", ImVec2(70, 0))) {
        timeline.playing = !timeline.playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(70, 0))) {
        app.reset();
    }
    ImGui::SameLine();
    bool orbit = app.cam.animated();
    if (ImGui::Checkbox("Camera orbit", &orbit)) {
        app.cam.set_animated(orbit);
    }
    ImGui::SliderFloat("Time", &timeline.time, 0.0f, Timeline::kLength, "%.1f s");
    ImGui::SliderFloat("Speed", &timeline.speed, 0.0f, 4.0f, "%.2fx");
}

void light_section(App& app) {
    if (!ImGui::CollapsingHeader("Light")) {
        return;
    }
    Sun& sun = app.world.sun;
    ImGui::Checkbox("Animated sun", &sun.animated);
    ImGui::SliderFloat("Azimuth", &sun.azimuth, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat(sun.animated ? "Mean elevation" : "Elevation", &sun.elevation, 5.0f, 85.0f,
                       "%.0f deg");
    ImGui::ColorEdit3("Colour", glm::value_ptr(sun.colour));
    ImGui::SliderFloat("Ambient", &sun.ambient, 0.0f, 1.0f);
    ImGui::Separator();
    ImGui::Checkbox("Shadows", &app.settings.shadows);
    ImGui::SameLine();
    ImGui::Checkbox("PCF 3x3", &app.settings.pcf);
    ImGui::SliderFloat("Bias (texels)", &app.settings.bias_scale, 0.0f, 5.0f);
}

void view_section(App& app) {
    if (!ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    RenderSettings& s = app.settings;
    int mode = static_cast<int>(s.view);
    if (ImGui::Combo("Colour mode", &mode, kViewModeNames, kViewModeCount)) {
        s.view = static_cast<ViewMode>(mode);
    }
    ImGui::Checkbox("map_Kd", &s.diffuse_maps);
    ImGui::SameLine();
    ImGui::Checkbox("map_Ks", &s.specular_maps);
    ImGui::SameLine();
    ImGui::Checkbox("bump", &s.normal_maps);
    ImGui::RadioButton("jet", &s.colormap, 0);
    ImGui::SameLine();
    ImGui::RadioButton("plasma", &s.colormap, 1);
    ImGui::SameLine();
    ImGui::Checkbox("Isolines", &s.isolines);
    ImGui::SliderFloat("Isoline count", &s.isoline_count, 5.0f, 60.0f, "%.0f");
    ImGui::Checkbox("Wireframe", &s.wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Seeds", &s.show_seeds);
    ImGui::SameLine();
    ImGui::Checkbox("Sun", &s.show_sun);
}

void material_editor(App& app, const Object& object) {
    std::set<int> used;
    for (const Submesh& submesh : object.gpu.submeshes) {
        if (submesh.material >= 0) {
            used.insert(submesh.material);
        }
    }
    if (used.empty() || !ImGui::TreeNode("Materials")) {
        return;
    }
    for (const int id : used) {
        Material& m = app.world.materials[static_cast<size_t>(id)];
        ImGui::PushID(id);
        if (ImGui::TreeNode(m.name.c_str())) {
            ImGui::ColorEdit3("Kd", glm::value_ptr(m.diffuse));
            ImGui::ColorEdit3("Ks", glm::value_ptr(m.specular));
            ImGui::SliderFloat("Ns", &m.shininess, 1.0f, 1000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("d", &m.opacity, 0.0f, 1.0f);
            if (!m.diffuse_map.empty()) {
                ImGui::TextDisabled("map_Kd replaces Kd while the maps are on");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void selection_section(App& app) {
    if (!ImGui::CollapsingHeader("Selected object", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    Object* object = app.selection();
    if (object == nullptr) {
        ImGui::TextDisabled("Right click an object, or choose it in the list.");
        return;
    }
    const CHE& che = object->che;
    ImGui::Text("%s", object->name.c_str());
    ImGui::Text("CHE: V %d  E %d  F %d  (chi %d, %s)", che.n_vertices(), che.n_edges(),
                che.n_triangles(), che.n_vertices() - che.n_edges() + che.n_triangles(),
                che.is_closed() ? "closed" : "open");

    material_editor(app, *object);

    ImGui::SeparatorText("Normals");
    int normals = static_cast<int>(object->normals);
    bool changed = ImGui::RadioButton("Loaded", &normals, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Recomputed", &normals, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Flat", &normals, 2);
    if (changed) {
        app.set_normals(static_cast<NormalSource>(normals));
    }

    ImGui::SeparatorText("Simplification (QEM)");
    float percent = 100.0f * app.simplify_fraction;
    if (ImGui::SliderFloat("Keep", &percent, 1.0f, 100.0f, "%.0f %% of triangles")) {
        app.simplify_fraction = percent / 100.0f;
    }
    if (ImGui::Button("Simplify", ImVec2(90, 0))) {
        app.simplify_selected();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!object->simplified);
    if (ImGui::Button("Restore", ImVec2(90, 0))) {
        app.restore_selected();
    }
    ImGui::EndDisabled();
    if (object->simplified && app.simplified_once) {
        const SimplifyResult& r = app.last_simplify;
        ImGui::Text("%d -> %d triangles, %.1f ms", r.triangles_before, che.n_triangles(),
                    r.milliseconds);
        ImGui::Text("Hausdorff error: mean %.4f  max %.4f", r.deviation.mean, r.deviation.max);
        ImGui::TextDisabled("Rejected: %d pinch, %d flip", r.report.rejected_link,
                            r.report.rejected_flip);
    }

    ImGui::SeparatorText("Fast Marching");
    ImGui::Text("%zu seeds, t_max %.3f, %.2f ms", object->seeds.size(),
                object->distances.max_finite, app.performance.fmm_ms);
    if (ImGui::Button("Clear seeds")) {
        app.clear_seeds();
    }
    ImGui::TextDisabled("Right click: seed   Shift + right click: add a seed");
}

void performance_section(App& app) {
    if (!ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const Performance& p = app.performance;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.0f FPS  %.2f ms/frame", io.Framerate, 1000.0f / io.Framerate);
    const float worst = *std::max_element(p.frame_ms.begin(), p.frame_ms.end());
    ImGui::PlotLines("##frames", p.frame_ms.data(), static_cast<int>(p.frame_ms.size()), p.next,
                     "frame time (ms)", 0.0f, std::max(20.0f, worst), ImVec2(-FLT_MIN, 60));
    ImGui::Text("%d draw calls, %d triangles", p.stats.draw_calls, p.stats.triangles);
    ImGui::Text("%zu objects, %zu materials", app.world.objects.size(), app.world.materials.size());
}

void help_section() {
    if (!ImGui::CollapsingHeader("Controls")) {
        return;
    }
    ImGui::TextUnformatted("Left drag       orbit (arcball)\n"
                           "Scroll          zoom\n"
                           "Right click     select + seed\n"
                           "Shift + right   add a seed\n"
                           "SPACE           play / pause\n"
                           "R               reset time and camera\n"
                           "C               camera orbit on / off\n"
                           "V               next colour mode\n"
                           "N               next normals of the selection\n"
                           "X               simplify the selection\n"
                           "Z               restore the selection\n"
                           "1 / 2 / 3       map_Kd / map_Ks / bump\n"
                           "S               shadows\n"
                           "W               wireframe\n"
                           "DELETE          clear seeds\n"
                           "P               next preset\n"
                           "F1              hide this panel\n"
                           "ESC             quit");
}

} // namespace

void draw_panel(App& app, int width, int height) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
    ImGui::Begin("Scene Viewer", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    if (!app.message.empty()) {
        ImGui::TextWrapped("%s", app.message.c_str());
    }
    scene_section(app);
    timeline_section(app);
    light_section(app);
    view_section(app);
    selection_section(app);
    performance_section(app);
    help_section();
    ImGui::End();
}
