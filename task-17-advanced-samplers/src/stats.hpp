// Numbers to back what the eye sees in the viewer.
#pragma once

#include <glm/glm.hpp>

#include <vector>

// L2-star discrepancy (Warnock's closed form): the root mean square, over every
// box [0, a) anchored at the origin, of the difference between the fraction of
// points inside the box and its volume. 0 is perfectly even. For n independent
// uniform points its expected square is (2^-d - 3^-d) / n. O(n^2).
double l2_star_discrepancy(const std::vector<glm::dvec3>& points);

// The expected L2-star discrepancy of n independent uniform points in 3D.
double random_l2_star_discrepancy(int n);

// The cube cut into kx * ky * kz equal cells: how many are empty and how many
// points the fullest one holds. With n = kx ky kz points a perfectly stratified
// set puts one point in every cell; independent points leave about 1/e = 36.8%
// empty. Which grids a sequence is built to fill depends on its bases: Sobol
// targets 2^a x 2^b x 2^c grids, Halton (2, 3, 5) targets 2^a x 3^b x 5^c.
struct Occupancy {
    int cells = 0;
    int empty = 0;
    int max_count = 0;
};
Occupancy occupancy(const std::vector<glm::dvec3>& points, int kx, int ky, int kz);
