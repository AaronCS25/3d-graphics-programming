#include <camera.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool same_rotation(glm::quat a, glm::quat b) {
    return std::abs(glm::dot(a, b)) > 1.0f - 1e-5f;
}

void check_frame(const camera& cam, glm::vec3 target = glm::vec3(0)) {
    const glm::mat4 view = cam.view();
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            check(std::isfinite(view[column][row]), "finite view matrix");
        }
    }
    check(std::abs(glm::length(cam.orientation()) - 1.0f) < 1e-5f, "unit quaternion");
    check(glm::length(glm::vec3(view * glm::vec4(cam.position(), 1))) < 2e-4f,
          "camera position maps to the origin");
    const glm::vec3 center(view * glm::vec4(target, 1));
    check(std::abs(center.x) < 2e-4f && std::abs(center.y) < 2e-4f,
          "target remains centered after orbit and roll");
    check(std::abs(center.z + cam.distance()) < 2e-4f, "target is in front of the camera");
}
} // namespace

int main() {
    camera cam;
    const glm::quat initial = cam.orientation();
    const float initial_distance = cam.distance();
    check_frame(cam);

    // A central drag to x=1/2 on the sphere gives a 60-degree arcball rotation.
    // This checks the quaternion convention and camera/scene inverse, not just motion.
    cam.begin_drag(480, 360, 960, 720);
    cam.drag(660, 360);
    const glm::quat expected = initial * glm::angleAxis(-glm::pi<float>() / 3, glm::vec3(0, 1, 0));
    check(same_rotation(cam.orientation(), expected), "arcball angle and direction");
    check_frame(cam);
    check(std::abs(cam.distance() - initial_distance) < 1e-6f, "drag preserves distance");
    cam.drag(480, 360);
    check(same_rotation(cam.orientation(), initial), "return to press position cancels rotation");

    camera direct, sampled, scaled;
    direct.begin_drag(400, 300, 960, 720);
    direct.drag(700, 500);
    sampled.begin_drag(400, 300, 960, 720);
    for (int i = 0; i <= 100; ++i) {
        sampled.drag(400 + 3 * i, 300 + 2 * i);
    }
    check(same_rotation(direct.orientation(), sampled.orientation()), "event-rate independence");
    scaled.begin_drag(800, 600, 1920, 1440);
    scaled.drag(1400, 1000);
    check(same_rotation(direct.orientation(), scaled.orientation()), "DPI/size independence");

    cam.end_drag();
    cam.drag(0, 0);
    check(same_rotation(cam.orientation(), initial), "released mouse cannot rotate");
    cam.begin_drag(0, 0, 0, 0);
    check(!cam.dragging(), "zero-size window cannot start a drag");
    cam.begin_drag(-240, 360, 960, 720);
    cam.drag(1200, 360); // opposite rim points: quaternion is -identity, not NaN
    check_frame(cam);
    for (int i = 0; i < 2000; ++i) {
        cam.begin_drag(380, 280, 960, 720);
        cam.drag(600, 420);
        cam.end_drag();
    }
    check_frame(cam);

    cam.reset();
    cam.zoom(2);
    check(cam.distance() < initial_distance, "positive scroll zooms in");
    cam.zoom(-2);
    check(std::abs(cam.distance() - initial_distance) < 1e-5f, "opposite scroll restores distance");
    cam.zoom(10000);
    check(std::abs(cam.distance() - 0.5f) < 1e-5f, "near zoom limit");
    cam.zoom(-10000);
    check(std::abs(cam.distance() - 50.0f) < 1e-5f, "far zoom limit");
    check_frame(cam);

    cam.reset();
    cam.set_animated(true);
    cam.update(4.5f);
    check(glm::distance(cam.position(), glm::vec3(10, 4.7f, 0)) < 1e-4f,
          "animated path matches task 07 at a quarter revolution");
    cam.set_animated(false);
    const glm::quat stopped = cam.orientation();
    cam.update(1.0f);
    check(same_rotation(cam.orientation(), stopped), "manual mode ignores animation time");
    cam.begin_drag(400, 300, 960, 720);
    cam.drag(650, 480);
    const glm::quat manual = cam.orientation();
    cam.set_animated(true);
    check(!cam.dragging() && same_rotation(cam.orientation(), manual), "switch keeps current pose");
    cam.update(1e-4f);
    check(same_rotation(cam.orientation(), manual), "animation resumes without a snap");
    for (int i = 0; i < 60; ++i) {
        cam.update(1.0f / 60);
        check_frame(cam);
    }
    cam.zoom(1);
    check(!cam.animated(), "scroll takes manual control");
    cam.set_animated(true);
    cam.begin_drag(480, 360, 960, 720);
    check(!cam.animated(), "drag takes manual control");

    camera shifted(glm::vec3(2, -1, 3), 4, 2, 0.5f);
    shifted.begin_drag(480, 360, 960, 720);
    shifted.drag(720, 520);
    check_frame(shifted, glm::vec3(2, -1, 3));
    const glm::mat4 square = cam.projection(720, 720);
    const glm::mat4 wide = cam.projection(1440, 720);
    check(std::abs(wide[0][0] * 2 - square[0][0]) < 1e-6f, "projection follows aspect ratio");
    check(std::isfinite(cam.projection(0, 0)[0][0]), "minimized projection stays finite");
    std::puts("Camera checks passed: arcball, frame, zoom, DPI, mode transition, task 07 orbit.");
}
