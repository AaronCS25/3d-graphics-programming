// Shared orbit camera. GLM only: callers forward input and upload the matrices.
// Arcball follows Shoemake's sphere mapping and quaternion composition.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

class camera {
public:
    explicit camera(glm::vec3 target = glm::vec3(0.0f), float orbit_radius = 10.0f,
                    float orbit_height = 3.5f, float height_swing = 1.2f)
        : target_(target), orbit_radius_(std::max(orbit_radius, 0.01f)),
          orbit_height_(orbit_height), height_swing_(height_swing) {
        reset();
    }

    glm::vec3 position() const {
        return target_ + orientation_ * glm::vec3(0.0f, 0.0f, distance_);
    }

    glm::mat4 view() const {
        // orientation_ maps camera axes to world axes. Invert it for the view.
        // Rotating the entire frame also rotates up, so arcball can roll freely.
        return glm::mat4_cast(glm::conjugate(orientation_)) *
               glm::translate(glm::mat4(1.0f), -position());
    }

    glm::mat4 projection(int width, int height) const {
        const float aspect = width > 0 && height > 0 ? static_cast<float>(width) / height : 1.0f;
        return glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }

    float distance() const {
        return distance_;
    }

    bool animated() const {
        return animated_;
    }

    bool dragging() const {
        return dragging_;
    }

    glm::quat orientation() const {
        return orientation_;
    }

    void reset() {
        phase_ = 0.0f;
        zoom_scale_ = 1.0f;
        const glm::vec3 offset = orbit_offset();
        distance_ = std::clamp(glm::length(offset), kMinDistance, kMaxDistance);
        orientation_ = orbit_orientation(offset);
        blend_time_ = kBlendDuration;
        end_drag();
    }

    void set_animated(bool enabled) {
        if (enabled == animated_) {
            return;
        }
        end_drag();
        animated_ = enabled;
        if (enabled) {
            // Keep the manual zoom and blend from the current pose, not a reset.
            zoom_scale_ = distance_ / glm::length(orbit_offset());
            blend_orientation_ = orientation_;
            blend_distance_ = distance_;
            blend_time_ = 0.0f;
        }
    }

    void update(float seconds) {
        if (!animated_ || !std::isfinite(seconds) || seconds <= 0.0f) {
            return;
        }
        phase_ = std::fmod(phase_ + glm::two_pi<float>() * seconds / 18.0f,
                          glm::two_pi<float>());
        const glm::vec3 offset = orbit_offset();
        const glm::quat desired = orbit_orientation(offset);
        const float desired_distance =
            std::clamp(glm::length(offset) * zoom_scale_, kMinDistance, kMaxDistance);
        blend_time_ = std::min(blend_time_ + seconds, kBlendDuration);
        const float t = glm::smoothstep(0.0f, kBlendDuration, blend_time_);
        orientation_ = glm::normalize(glm::slerp(blend_orientation_, desired, t));
        distance_ = glm::mix(blend_distance_, desired_distance, t);
    }

    // Coordinates and dimensions must use the same units (GLFW window units,
    // not framebuffer pixels). Anchor the whole drag to avoid event-rate drift.
    void begin_drag(double x, double y, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        set_animated(false);
        drag_width_ = width;
        drag_height_ = height;
        drag_start_ = on_sphere(x, y);
        drag_orientation_ = orientation_;
        dragging_ = true;
    }

    void drag(double x, double y) {
        if (!dragging_) {
            return;
        }
        const glm::vec3 to = on_sphere(x, y);
        const glm::vec3 axis = glm::cross(drag_start_, to);
        const glm::quat delta(glm::dot(drag_start_, to), axis.x, axis.y, axis.z);
        // This is a virtual scene rotation in camera coordinates. Apply its
        // inverse to the camera so the scene follows the mouse, including roll.
        orientation_ = glm::normalize(drag_orientation_ * glm::conjugate(delta));
    }

    void end_drag() {
        dragging_ = false;
    }

    void zoom(double steps) {
        if (!std::isfinite(steps) || steps == 0.0) {
            return;
        }
        set_animated(false);
        const double factor = std::exp(std::clamp(-0.12 * steps, -50.0, 50.0));
        const double clamped = std::clamp(distance_ * factor, static_cast<double>(kMinDistance),
                                           static_cast<double>(kMaxDistance));
        distance_ = static_cast<float>(clamped);
    }

private:
    static constexpr float kMinDistance = 0.5f;
    static constexpr float kMaxDistance = 50.0f;
    static constexpr float kBlendDuration = 0.6f;

    glm::vec3 orbit_offset() const {
        return {orbit_radius_ * std::sin(phase_),
                orbit_height_ + height_swing_ * std::sin(phase_),
                orbit_radius_ * std::cos(phase_)};
    }

    static glm::quat orbit_orientation(const glm::vec3& offset) {
        const glm::mat4 view = glm::lookAt(offset, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        return glm::normalize(glm::conjugate(glm::quat_cast(glm::mat3(view))));
    }

    glm::vec3 on_sphere(double x, double y) const {
        const double size = std::min(drag_width_, drag_height_);
        glm::vec3 p(static_cast<float>((2.0 * x - drag_width_) / size),
                    static_cast<float>((drag_height_ - 2.0 * y) / size), 0.0f);
        const float r2 = glm::dot(p, p);
        if (r2 <= 1.0f) {
            p.z = std::sqrt(1.0f - r2);
        } else {
            p = glm::normalize(p); // outside the sphere: continue along its rim
        }
        return p;
    }

    glm::vec3 target_;
    float orbit_radius_;
    float orbit_height_;
    float height_swing_;
    glm::quat orientation_{1, 0, 0, 0}; // camera-to-world, always unit length
    float distance_ = 1.0f;
    bool animated_ = false;
    float phase_ = 0.0f;
    float zoom_scale_ = 1.0f;
    glm::quat blend_orientation_{1, 0, 0, 0};
    float blend_distance_ = 1.0f;
    float blend_time_ = kBlendDuration;
    bool dragging_ = false;
    int drag_width_ = 1;
    int drag_height_ = 1;
    glm::vec3 drag_start_{0, 0, 1};
    glm::quat drag_orientation_{1, 0, 0, 0};
};
