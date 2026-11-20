#include "samplers.hpp"

#include <algorithm>
#include <random>

namespace {

constexpr std::uint32_t kSeed = 2026; // every pseudo-random cloud is reproducible

constexpr double k2Pow32 = 4294967296.0;

// Three consecutive outputs of a std generator per point, mapped to [0, 1)
// by std::uniform_real_distribution.
template <typename Engine>
std::vector<glm::dvec3> pseudo_random(int n) {
    Engine engine(kSeed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::vector<glm::dvec3> points(n);
    for (glm::dvec3& p : points) {
        p.x = uniform(engine);
        p.y = uniform(engine);
        p.z = uniform(engine);
    }
    return points;
}

// RANDU (IBM, 1960s): x_{k+1} = 65539 x_k mod 2^31. Since 65539 = 2^16 + 3,
// x_{k+2} = (2^16 + 3)^2 x_k = (6 * 2^16 + 9) x_k = 6 x_{k+1} - 9 x_k (mod 2^31),
// so every three consecutive outputs satisfy 9x - 6y + z = integer: all the
// points of the cube lie on the 15 planes 9x - 6y + z = k.
std::vector<glm::dvec3> randu(int n) {
    std::uint32_t x = kSeed | 1u; // RANDU needs an odd seed
    const auto next = [&x] {
        x = static_cast<std::uint32_t>((65539ull * x) & 0x7fffffffu);
        return x / 2147483648.0;
    };
    std::vector<glm::dvec3> points(n);
    for (glm::dvec3& p : points) {
        p.x = next();
        p.y = next();
        p.z = next();
    }
    return points;
}

std::vector<glm::dvec3> halton(int n, std::uint32_t b0, std::uint32_t b1, std::uint32_t b2) {
    std::vector<glm::dvec3> points(n);
    for (int i = 0; i < n; ++i) {
        const auto index = static_cast<std::uint32_t>(i + 1);
        points[i] = {radical_inverse(index, b0), radical_inverse(index, b1),
                     radical_inverse(index, b2)};
    }
    return points;
}

// One random permutation of the digits per base, from a fixed seed.
std::vector<std::uint32_t> digit_permutation(std::uint32_t base, std::mt19937& engine) {
    std::vector<std::uint32_t> perm(base);
    for (std::uint32_t d = 0; d < base; ++d) {
        perm[d] = d;
    }
    std::shuffle(perm.begin(), perm.end(), engine);
    return perm;
}

std::vector<glm::dvec3> halton_scrambled(int n, std::uint32_t b0, std::uint32_t b1,
                                         std::uint32_t b2) {
    std::mt19937 engine(kSeed);
    const std::vector<std::uint32_t> p0 = digit_permutation(b0, engine);
    const std::vector<std::uint32_t> p1 = digit_permutation(b1, engine);
    const std::vector<std::uint32_t> p2 = digit_permutation(b2, engine);
    std::vector<glm::dvec3> points(n);
    for (int i = 0; i < n; ++i) {
        const auto index = static_cast<std::uint32_t>(i + 1);
        points[i] = {scrambled_radical_inverse(index, b0, p0),
                     scrambled_radical_inverse(index, b1, p1),
                     scrambled_radical_inverse(index, b2, p2)};
    }
    return points;
}

} // namespace

const char* name_of(Generator generator) {
    switch (generator) {
    case Generator::Mt19937: return "std::mt19937";
    case Generator::MinstdRand: return "std::minstd_rand";
    case Generator::Randu: return "RANDU";
    case Generator::Halton: return "Halton (2, 3, 5)";
    case Generator::HaltonHigh: return "Halton (29, 31, 37)";
    case Generator::HaltonScrambled: return "Scrambled Halton (29, 31, 37)";
    case Generator::Sobol: return "Sobol (Joe-Kuo)";
    }
    return "?";
}

const char* file_name_of(Generator generator) {
    switch (generator) {
    case Generator::Mt19937: return "mt19937";
    case Generator::MinstdRand: return "minstd_rand";
    case Generator::Randu: return "randu";
    case Generator::Halton: return "halton";
    case Generator::HaltonHigh: return "halton_high";
    case Generator::HaltonScrambled: return "halton_scrambled";
    case Generator::Sobol: return "sobol";
    }
    return "unknown";
}

std::vector<glm::dvec3> generate(Generator generator, int n) {
    switch (generator) {
    case Generator::Mt19937: return pseudo_random<std::mt19937>(n);
    case Generator::MinstdRand: return pseudo_random<std::minstd_rand>(n);
    case Generator::Randu: return randu(n);
    case Generator::Halton: return halton(n, 2, 3, 5);
    case Generator::HaltonHigh: return halton(n, 29, 31, 37);
    case Generator::HaltonScrambled: return halton_scrambled(n, 29, 31, 37);
    case Generator::Sobol: return sobol_3d(n);
    }
    return {};
}

double radical_inverse(std::uint32_t i, std::uint32_t base) {
    const double inv_base = 1.0 / base;
    double scale = inv_base; // b^-(k+1) for digit k
    double result = 0.0;
    while (i > 0) {
        result += (i % base) * scale;
        i /= base;
        scale *= inv_base;
    }
    return result;
}

double scrambled_radical_inverse(std::uint32_t i, std::uint32_t base,
                                 const std::vector<std::uint32_t>& perm) {
    const double inv_base = 1.0 / base;
    double scale = inv_base;
    double result = 0.0;
    while (i > 0) {
        result += perm[i % base] * scale;
        i /= base;
        scale *= inv_base;
    }
    // The digits above the last one of i are zeros, each also replaced by
    // perm[0]: sum_{k > K} perm[0] b^-k = perm[0] b^-K / (b - 1), where scale is
    // now b^-(K+1), so the tail is perm[0] * scale * b / (b - 1).
    result += perm[0] * scale * base / (base - 1.0);
    return std::min(result, 1.0 - 1e-12); // all digits b-1 would reach exactly 1
}

SobolDimension SobolDimension::first() {
    // m_k = 1 for every k: v_k = 2^-(k+1), the van der Corput sequence in base 2.
    SobolDimension dimension;
    for (int k = 0; k < 32; ++k) {
        dimension.v_[k] = 1u << (31 - k);
    }
    return dimension;
}

// Bratley and Fox's recurrence on the m_k of a primitive polynomial
//   x^s + a_1 x^(s-1) + ... + a_(s-1) x + 1   (a's bits: a_1 is the highest),
//   m_k = 2 a_1 m_(k-1) ^ 4 a_2 m_(k-2) ^ ... ^ 2^(s-1) a_(s-1) m_(k-s+1)
//         ^ 2^s m_(k-s) ^ m_(k-s),
// then v_k = m_k / 2^k, stored as v_k * 2^32 = m_k << (32 - k).
SobolDimension::SobolDimension(int s, std::uint32_t a, const std::vector<std::uint32_t>& m) {
    std::uint32_t mk[33] = {};
    for (int k = 1; k <= s; ++k) {
        mk[k] = m[k - 1];
    }
    for (int k = s + 1; k <= 32; ++k) {
        std::uint32_t value = mk[k - s] ^ (mk[k - s] << s);
        for (int j = 1; j < s; ++j) {
            const std::uint32_t a_j = (a >> (s - 1 - j)) & 1u;
            if (a_j != 0) {
                value ^= mk[k - j] << j;
            }
        }
        mk[k] = value;
    }
    for (int k = 1; k <= 32; ++k) {
        v_[k - 1] = mk[k] << (32 - k);
    }
}

std::vector<glm::dvec3> sobol_3d(int n) {
    // Joe and Kuo (2008), new-joe-kuo-6.21201, the first two rows after
    // dimension 1:   d  s  a  m_i
    //                2  1  0  1
    //                3  2  1  1 3
    const SobolDimension dims[3] = {SobolDimension::first(), SobolDimension(1, 0, {1}),
                                    SobolDimension(2, 1, {1, 3})};

    // Gray code (Antonov and Saleev): point i differs from point i - 1 by one
    // XOR with the direction number of the lowest zero bit of i - 1, so each
    // point costs one XOR per dimension.
    std::uint32_t x[3] = {0, 0, 0}; // point 0
    std::vector<glm::dvec3> points(n);
    for (int i = 1; i <= n; ++i) {
        int c = 0;
        for (std::uint32_t value = static_cast<std::uint32_t>(i - 1); value & 1u; value >>= 1) {
            ++c;
        }
        for (int d = 0; d < 3; ++d) {
            x[d] ^= dims[d].direction(c);
        }
        points[i - 1] = {x[0] / k2Pow32, x[1] / k2Pow32, x[2] / k2Pow32};
    }
    return points;
}
