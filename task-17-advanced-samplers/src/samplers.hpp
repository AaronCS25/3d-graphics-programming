// Point generators for the unit cube [0, 1)^3.
//
// Two families:
//   - pseudo-random: each point is three consecutive outputs of a generator
//     that tries to look independent (std::mt19937, std::minstd_rand, RANDU);
//   - low-discrepancy (quasi-random): each point is a deterministic function of
//     its index, built so that every prefix of the sequence covers the cube as
//     evenly as possible (Halton, Sobol).
//
// Every generator returns the first n points, so asking for more points keeps
// the ones already drawn. The quasi-random sequences start at index 1: index 0
// is the corner (0, 0, 0) for both.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

enum class Generator {
    Mt19937,          // std::mt19937 + std::uniform_real_distribution
    MinstdRand,       // std::minstd_rand (Park-Miller LCG)
    Randu,            // IBM's RANDU, the textbook bad LCG
    Halton,           // bases 2, 3, 5
    HaltonHigh,       // bases 29, 31, 37: correlated for small n
    HaltonScrambled,  // bases 29, 31, 37 with random digit permutations
    Sobol,            // Joe-Kuo direction numbers, dimensions 1-3
};

constexpr int kGeneratorCount = 7;

const char* name_of(Generator generator);       // for the window title and the tables
const char* file_name_of(Generator generator);  // for docs/<file>_*.png

std::vector<glm::dvec3> generate(Generator generator, int n);

// ---- the pieces, exposed for the tests and the README ----------------------

// Radical inverse: the digits of i in base b mirrored around the radix point.
// i = d0 + d1 b + d2 b^2 + ...  ->  0.d0 d1 d2 ... (base b).
double radical_inverse(std::uint32_t i, std::uint32_t base);

// The same with every digit d replaced by perm[d] (perm is a permutation of
// 0 .. base-1), including the infinitely many leading zeros of i.
double scrambled_radical_inverse(std::uint32_t i, std::uint32_t base,
                                 const std::vector<std::uint32_t>& perm);

// One dimension of the Sobol sequence: its 32 direction numbers v_k (as
// fractions times 2^32) and the point of index i, by Gray code.
class SobolDimension {
public:
    // Dimension 1 (van der Corput in base 2), or a dimension given by its
    // primitive polynomial of degree s, its coefficients a and its initial
    // m_1 .. m_s, as listed by Joe and Kuo.
    static SobolDimension first();
    SobolDimension(int s, std::uint32_t a, const std::vector<std::uint32_t>& m);

    std::uint32_t direction(int k) const { return v_[k]; } // k = 0 .. 31

private:
    SobolDimension() = default;
    std::uint32_t v_[32] = {};
};

// The first n points of the 3D Sobol sequence (index 1 .. n), each coordinate
// as a 32-bit fraction.
std::vector<glm::dvec3> sobol_3d(int n);
