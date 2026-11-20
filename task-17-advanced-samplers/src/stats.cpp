#include "stats.hpp"

#include <algorithm>
#include <cmath>

// Warnock (1972), in dimension d = 3:
//   D^2 = 3^-d - (2^(1-d) / n) sum_i prod_k (1 - x_ik^2)
//         + (1 / n^2) sum_i sum_j prod_k (1 - max(x_ik, x_jk))
double l2_star_discrepancy(const std::vector<glm::dvec3>& points) {
    const double n = static_cast<double>(points.size());
    double single = 0.0;
    for (const glm::dvec3& p : points) {
        single += (1.0 - p.x * p.x) * (1.0 - p.y * p.y) * (1.0 - p.z * p.z);
    }
    // The double sum is symmetric: the diagonal once, every pair twice.
    double pairs = 0.0;
    for (size_t i = 0; i < points.size(); ++i) {
        const glm::dvec3& a = points[i];
        pairs += (1.0 - a.x) * (1.0 - a.y) * (1.0 - a.z);
        for (size_t j = i + 1; j < points.size(); ++j) {
            const glm::dvec3& b = points[j];
            pairs += 2.0 * (1.0 - std::max(a.x, b.x)) * (1.0 - std::max(a.y, b.y)) *
                     (1.0 - std::max(a.z, b.z));
        }
    }
    const double squared = 1.0 / 27.0 - 0.25 * single / n + pairs / (n * n); // 2^(1-3) = 1/4
    return std::sqrt(std::max(squared, 0.0));
}

double random_l2_star_discrepancy(int n) {
    return std::sqrt((1.0 / 8.0 - 1.0 / 27.0) / n);
}

Occupancy occupancy(const std::vector<glm::dvec3>& points, int kx, int ky, int kz) {
    std::vector<int> counts(static_cast<size_t>(kx) * ky * kz, 0);
    // Quasi-random points fall exactly on cell borders (1/3 = 3/9 is a Halton
    // coordinate and a border of a 9-cell axis). In double, 9 * (1/3) can round
    // below 3 and send the point to the cell below; the 1e-9 undoes that.
    const auto cell = [](double v, int k) {
        return std::clamp(static_cast<int>(v * k + 1e-9), 0, k - 1);
    };
    for (const glm::dvec3& p : points) {
        ++counts[(static_cast<size_t>(cell(p.z, kz)) * ky + cell(p.y, ky)) * kx + cell(p.x, kx)];
    }
    Occupancy result;
    result.cells = static_cast<int>(counts.size());
    for (const int count : counts) {
        result.empty += count == 0 ? 1 : 0;
        result.max_count = std::max(result.max_count, count);
    }
    return result;
}
