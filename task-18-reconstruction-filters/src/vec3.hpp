// A small float vector that both compilers understand. The path tracer is
// written once and compiled twice: by the host compiler for the CPU version and
// by nvcc for the GPU version. glm does not fit that: its headers make nvcc warn
// about their constructors. On the host HD expands to nothing; under nvcc every
// function becomes __host__ __device__.
#pragma once

#include <math.h>

#ifdef __CUDACC__
#define HD __host__ __device__
#else
#define HD
#endif

struct Vec3 {
    float x, y, z;
};

HD inline Vec3 vec3(float x, float y, float z) {
    return {x, y, z};
}
HD inline Vec3 vec3(float s) {
    return {s, s, s};
}
HD inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
HD inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
HD inline Vec3 operator-(Vec3 a) {
    return {-a.x, -a.y, -a.z};
}
HD inline Vec3 operator*(Vec3 a, Vec3 b) { // component-wise, for colours
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
HD inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
HD inline Vec3 operator*(float s, Vec3 a) {
    return a * s;
}
HD inline Vec3& operator+=(Vec3& a, Vec3 b) {
    a = a + b;
    return a;
}
HD inline Vec3& operator*=(Vec3& a, Vec3 b) {
    a = a * b;
    return a;
}
HD inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
HD inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
HD inline Vec3 normalize(Vec3 v) {
    return v * (1.0f / sqrtf(dot(v, v)));
}
