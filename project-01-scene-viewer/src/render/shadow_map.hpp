// The target of the depth pass (task 10): a framebuffer whose only attachment
// is a depth texture, sampled afterwards to decide what the sun cannot see.

#pragma once

#include <glad/glad.h>

struct ShadowMap {
    GLuint fbo = 0;
    GLuint depth = 0;
    int size = 0;

    bool create(int resolution);
    void destroy();
};
