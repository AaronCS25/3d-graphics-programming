// The path tracer, written once for both versions. render_cpu.cpp calls
// trace_sample() from std::threads, render_gpu.cu from one CUDA thread per
// pixel: the light transport, the random numbers and so the image are the same,
// only the way the pixels are shared out changes.
//
// Every surface is diffuse (Lambert). A path from the camera:
//
//   hit = closest_hit(ray)                      no hit: background or sky
//   L += T * albedo * sun * cos * visible       shadow ray towards the sun
//   ray = cosine-weighted direction around n    diffuse bounce
//   T *= albedo
//
// The shadow ray is next event estimation: the sun is a direction, so a
// bounced ray would never find it by chance; it is asked for directly at every
// hit. The bounces add what OpenGL fakes with its ambient term: light from the
// sky and from the other surfaces, and the darkening where little of either
// arrives (corners, the ground under the spheres).
#pragma once

#include "scene_data.hpp"

// ---- random numbers ---------------------------------------------------------

// PCG hash (Jarzynski & Olano, 2020): a good 32-bit hash with a few integer
// operations, the same on the CPU and the GPU.
HD inline unsigned pcg_hash(unsigned v) {
    const unsigned state = v * 747796405u + 2891336453u;
    const unsigned word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Each (pixel, sample) gets its own stream, seeded from both. No state is
// shared between threads, and the CPU and the GPU draw the same numbers for
// the same path.
struct Rng {
    unsigned state;

    HD Rng(unsigned pixel, unsigned sample, unsigned seed)
        : state(pcg_hash(pixel ^ pcg_hash(sample ^ pcg_hash(seed)))) {}

    HD float next() { // uniform in [0, 1)
        state = pcg_hash(state);
        return (state >> 8) * (1.0f / 16777216.0f);
    }
};

// ---- intersection -----------------------------------------------------------

struct Hit {
    float t;
    int triangle; // -1 if nothing was hit
    float u, v;   // barycentric coordinates of the hit in that triangle
};

// Möller-Trumbore (task 13), also returning u and v for the shading.
HD inline bool hit_triangle(Vec3 origin, Vec3 direction, const Triangle& tri, float& t,
                            float& u, float& v) {
    const Vec3 p = cross(direction, tri.e2);
    const float det = dot(tri.e1, p);
    if (fabsf(det) < 1e-12f) {
        return false;
    }
    const float inv_det = 1.0f / det;
    const Vec3 s = origin - tri.a;
    u = dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vec3 q = cross(s, tri.e1);
    v = dot(direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    t = dot(tri.e2, q) * inv_det;
    return t > 0.0f;
}

// Slab test (task 13): whether the ray enters the box before t_max.
HD inline bool hit_box(Vec3 origin, Vec3 inv_direction, const Object& object, float t_max) {
    const Vec3 t0 = (object.box_min - origin) * inv_direction;
    const Vec3 t1 = (object.box_max - origin) * inv_direction;
    const float enter = fmaxf(fmaxf(fminf(t0.x, t1.x), fminf(t0.y, t1.y)),
                              fmaxf(fminf(t0.z, t1.z), 0.0f));
    const float exit = fminf(fminf(fmaxf(t0.x, t1.x), fmaxf(t0.y, t1.y)),
                             fminf(fmaxf(t0.z, t1.z), t_max));
    return enter <= exit;
}

HD inline Vec3 inverse(Vec3 d) {
    return {1.0f / d.x, 1.0f / d.y, 1.0f / d.z};
}

// The nearest hit along the ray. No BVH: every object, and every triangle of
// the objects whose box the ray enters before the closest hit so far.
HD inline Hit closest_hit(const SceneView& scene, Vec3 origin, Vec3 direction) {
    const Vec3 inv_direction = inverse(direction);
    Hit hit{INFINITY, -1, 0.0f, 0.0f};
    for (int i = 0; i < scene.object_count; ++i) {
        const Object& object = scene.objects[i];
        if (!hit_box(origin, inv_direction, object, hit.t)) {
            continue;
        }
        for (int k = object.first; k < object.first + object.count; ++k) {
            float t = 0.0f, u = 0.0f, v = 0.0f;
            if (hit_triangle(origin, direction, scene.triangles[k], t, u, v) && t < hit.t) {
                hit = {t, k, u, v};
            }
        }
    }
    return hit;
}

// The shadow ray: any hit will do, so it stops at the first one. The sun is
// infinitely far, so there is no t_max.
HD inline bool occluded(const SceneView& scene, Vec3 origin, Vec3 direction) {
    const Vec3 inv_direction = inverse(direction);
    for (int i = 0; i < scene.object_count; ++i) {
        const Object& object = scene.objects[i];
        if (!hit_box(origin, inv_direction, object, INFINITY)) {
            continue;
        }
        for (int k = object.first; k < object.first + object.count; ++k) {
            float t = 0.0f, u = 0.0f, v = 0.0f;
            if (hit_triangle(origin, direction, scene.triangles[k], t, u, v)) {
                return true;
            }
        }
    }
    return false;
}

// ---- shading ----------------------------------------------------------------

HD inline Vec3 texel(const SceneView& scene, const TextureInfo& texture, int x, int y) {
    x %= texture.width;
    y %= texture.height;
    x += x < 0 ? texture.width : 0; // repeat wrapping, as task 11
    y += y < 0 ? texture.height : 0;
    const unsigned char* p = scene.texels + texture.offset + 3 * (y * texture.width + x);
    return vec3(p[0], p[1], p[2]) * (1.0f / 255.0f);
}

// Bilinear filtering between the four texels around (u, v). There are no
// mipmaps: the many samples per pixel average what a far, tilted floor would
// alias.
HD inline Vec3 sample_texture(const SceneView& scene, const TextureInfo& texture, float u,
                              float v) {
    const float fx = u * texture.width - 0.5f;
    const float fy = v * texture.height - 0.5f;
    const float x0 = floorf(fx);
    const float y0 = floorf(fy);
    const float ax = fx - x0;
    const float ay = fy - y0;
    const int x = static_cast<int>(x0);
    const int y = static_cast<int>(y0);
    const Vec3 bottom = texel(scene, texture, x, y) * (1.0f - ax) + texel(scene, texture, x + 1, y) * ax;
    const Vec3 top =
        texel(scene, texture, x, y + 1) * (1.0f - ax) + texel(scene, texture, x + 1, y + 1) * ax;
    return bottom * (1.0f - ay) + top * ay;
}

// A direction around n with probability proportional to cos(theta), the
// Lambert term: with this choice the estimator of a diffuse bounce is simply
// the albedo (BRDF albedo / pi, times cos, divided by the pdf cos / pi).
HD inline Vec3 cosine_direction(Vec3 n, float r1, float r2) {
    // An orthonormal basis around n without branches on the axis (Duff et al. 2017).
    const float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    const Vec3 tangent = vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    const Vec3 bitangent = vec3(b, sign + n.y * n.y * a, -n.y);

    const float radius = sqrtf(r1);
    const float phi = 6.28318530718f * r2;
    const float z = sqrtf(fmaxf(0.0f, 1.0f - r1));
    return normalize(tangent * (radius * cosf(phi)) + bitangent * (radius * sinf(phi)) + n * z);
}

// The primary ray of task 13, through a random point of the pixel instead of
// its centre: averaging the samples also antialiases the edges.
HD inline Vec3 primary_direction(const RenderSettings& settings, float px, float py) {
    const float sx = px / settings.width * 2.0f - 1.0f;
    const float sy = py / settings.height * 2.0f - 1.0f;
    const float* m = settings.inv_proj_view;
    float q[4];
    for (int row = 0; row < 4; ++row) {
        q[row] = m[row] * sx + m[4 + row] * sy + m[8 + row] + m[12 + row]; // M * (sx, sy, 1, 1)
    }
    const Vec3 p = vec3(q[0] / q[3], q[1] / q[3], q[2] / q[3]);
    return normalize(p - settings.camera_pos);
}

// How far a new ray starts from the surface it leaves, so that rounding does
// not make it hit that same surface again ("shadow acne"). The gallery is
// 16 units wide.
constexpr float kRayOffset = 1e-3f;

// One path through pixel (x, y): its radiance, in the same units as OpenGL's
// colours (no tone mapping, no gamma, as task 12).
HD inline Vec3 trace_sample(const SceneView& scene, const RenderSettings& settings, int x, int y,
                            int sample) {
    Rng rng(static_cast<unsigned>(y * settings.width + x), static_cast<unsigned>(sample),
            settings.seed);
    Vec3 origin = settings.camera_pos;
    Vec3 direction = primary_direction(settings, x + rng.next(), y + rng.next());
    Vec3 radiance = vec3(0.0f);
    Vec3 throughput = vec3(1.0f);

    for (int bounce = 0;; ++bounce) {
        const Hit hit = closest_hit(scene, origin, direction);
        if (hit.triangle < 0) {
            radiance += throughput * (bounce == 0 ? settings.background : settings.sky_color);
            break;
        }

        const Triangle& tri = scene.triangles[hit.triangle];
        const Shading& shading = scene.shading[hit.triangle];
        const float w = 1.0f - hit.u - hit.v;

        // The geometric normal faces the side the ray came from: a scene file
        // does not promise which way its walls face. The interpolated normal
        // (smooth spheres) is turned to the same side.
        Vec3 geometric = normalize(cross(tri.e1, tri.e2));
        if (dot(geometric, direction) > 0.0f) {
            geometric = -geometric;
        }
        Vec3 normal = normalize(shading.n0 * w + shading.n1 * hit.u + shading.n2 * hit.v);
        if (dot(normal, geometric) < 0.0f) {
            normal = -normal;
        }

        const Diffuse& material = scene.materials[shading.material];
        Vec3 albedo = material.albedo;
        if (material.texture >= 0) {
            const float u = shading.uv[0] * w + shading.uv[2] * hit.u + shading.uv[4] * hit.v;
            const float v = shading.uv[1] * w + shading.uv[3] * hit.u + shading.uv[5] * hit.v;
            albedo = sample_texture(scene, scene.textures[material.texture], u, v);
        }

        const Vec3 point = origin + direction * hit.t + geometric * kRayOffset;

        // Direct light: Lambert of task 09 times the sun's visibility, the
        // shadow-map test of task 10 replaced by a ray.
        const float cos_sun = dot(normal, settings.sun_dir);
        if (cos_sun > 0.0f && dot(geometric, settings.sun_dir) > 0.0f &&
            !occluded(scene, point, settings.sun_dir)) {
            radiance += throughput * albedo * settings.sun_color * cos_sun;
        }

        if (bounce == settings.max_bounces) {
            break;
        }
        throughput *= albedo;
        origin = point;
        direction = cosine_direction(normal, rng.next(), rng.next());
        if (dot(direction, geometric) <= 0.0f) {
            break; // the smooth normal tilted the ray below the actual surface
        }
    }
    return radiance;
}

// The average of all the paths of pixel (x, y).
HD inline Vec3 trace_pixel(const SceneView& scene, const RenderSettings& settings, int x, int y) {
    Vec3 sum = vec3(0.0f);
    for (int s = 0; s < settings.samples; ++s) {
        sum += trace_sample(scene, settings, x, y, s);
    }
    return sum * (1.0f / settings.samples);
}
