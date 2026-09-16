// QEM simplification, implemented as methods of the CHE itself.
//
// Garland & Heckbert, "Surface Simplification Using Quadric Error Metrics",
// SIGGRAPH 1997.
//
// Every face contributes the quadric K = p p^T of its plane p = [a b c d], with
// the plane normalized so that a^2+b^2+c^2 = 1. Summed over the faces meeting at
// a vertex, v^T Q v is the total squared distance from v to those planes. The
// cost of collapsing an edge is that quantity evaluated at the position the
// collapse would produce, and the mesh is reduced by always taking the cheapest
// edge left.

#include "che.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace {

using Quadric = glm::mat4;

// K = p p^T for the plane through `point` with unit normal `n`.
Quadric plane_quadric(const glm::vec3& n, const glm::vec3& point) {
    const glm::vec4 p(n, -glm::dot(n, point));
    return glm::outerProduct(p, p);
}

float quadric_error(const Quadric& q, const glm::vec3& v) {
    const glm::vec4 h(v, 1.0f);
    return glm::dot(h, q * h);
}

// The position minimizing v^T Q v, from dE/dv = 0. Replacing the last row of Q
// with [0 0 0 1] and solving against [0 0 0 1]^T is exactly that system.
// Returns false when Q is singular, which happens on flat or symmetric
// neighbourhoods where the minimum is not a single point.
bool optimal_position(const Quadric& q, glm::vec3& out) {
    Quadric a = q;
    a[0][3] = 0.0f;
    a[1][3] = 0.0f;
    a[2][3] = 0.0f;
    a[3][3] = 1.0f;

    if (std::abs(glm::determinant(a)) < 1e-10f) {
        return false;
    }
    out = glm::vec3(glm::inverse(a) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    return true;
}

// A pending collapse. The endpoints and their versions are recorded so a stale
// entry can be recognised when it surfaces, instead of being hunted down in the
// heap every time the mesh around it changes.
struct Candidate {
    float cost = 0.0f;
    int he = 0;
    int u = 0;
    int w = 0;
    int u_version = 0;
    int w_version = 0;
    glm::vec3 position{0.0f};

    bool operator<(const Candidate& other) const {
        return cost > other.cost;
    }
};

// Squared distance from p to the triangle abc (Ericson, Real-Time Collision
// Detection): project onto the plane, then clamp into the triangle by Voronoi
// region so edges and corners are handled too.
float distance2_to_triangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b,
                            const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;

    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return glm::dot(ap, ap);
    }

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return glm::dot(bp, bp);
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return glm::dot(cp, cp);
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const glm::vec3 q = a + ab * (d1 / (d1 - d3));
        return glm::dot(p - q, p - q);
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const glm::vec3 q = a + ac * (d2 / (d2 - d6));
        return glm::dot(p - q, p - q);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const glm::vec3 q = b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        return glm::dot(p - q, p - q);
    }

    const float denom = 1.0f / (va + vb + vc);
    const glm::vec3 q = a + ab * (vb * denom) + ac * (vc * denom);
    return glm::dot(p - q, p - q);
}

} // namespace

bool CHE::link_condition(int he) const {
    const int twin = O(he);
    if (twin == NIL) {
        return false;
    }

    // The only vertices the two endpoints may share are the two opposite the
    // edge. Any other shared neighbour means the collapse would fuse two parts
    // of the surface that only touch at that vertex.
    const int opposite_here = V(prev(he));
    const int opposite_there = V(prev(twin));

    const std::vector<int> ring_u = link(he);
    const std::vector<int> ring_w = link(next(he));

    int shared = 0;
    for (const int a : ring_u) {
        for (const int b : ring_w) {
            if (a != b) {
                continue;
            }
            if (a != opposite_here && a != opposite_there) {
                return false;
            }
            ++shared;
        }
    }
    return shared == 2;
}

bool CHE::would_flip(int he, const glm::vec3& target) const {
    const int twin = O(he);
    const int dying_here = trig(he);
    const int dying_there = twin == NIL ? -1 : trig(twin);

    const int u = V(he);
    const int w = V(next(he));

    const auto survives = [&](int h) {
        const int t = trig(h);
        if (t == dying_here || t == dying_there) {
            return true;
        }
        const glm::vec3& a = G(V(3 * t));
        const glm::vec3& b = G(V(3 * t + 1));
        const glm::vec3& c = G(V(3 * t + 2));
        const glm::vec3 before = glm::cross(b - a, c - a);

        // Same triangle with u and w replaced by the collapse target.
        const auto moved = [&](int v) { return v == u || v == w ? target : G(v); };
        const glm::vec3 a2 = moved(V(3 * t));
        const glm::vec3 b2 = moved(V(3 * t + 1));
        const glm::vec3 c2 = moved(V(3 * t + 2));
        const glm::vec3 after = glm::cross(b2 - a2, c2 - a2);

        return glm::dot(before, after) > 0.0f;
    };

    for (const int h : star(he)) {
        if (!survives(h)) {
            return true;
        }
    }
    for (const int h : star(next(he))) {
        if (!survives(h)) {
            return true;
        }
    }
    return false;
}

void CHE::compact(const std::vector<bool>& dead_triangle) {
    // A vertex survives if some surviving triangle still uses it. Deriving this
    // from V rather than trusting a per-vertex mark keeps every index valid
    // even on non-manifold input, where a star may not cover a whole fan.
    std::vector<int> remap(m_G.size(), NIL);
    std::vector<glm::vec3> geometry;
    geometry.reserve(m_G.size());

    std::vector<int> vertices;
    vertices.reserve(m_V.size());
    for (int t = 0; t < n_triangles(); ++t) {
        if (dead_triangle[static_cast<size_t>(t)]) {
            continue;
        }
        for (int i = 0; i < 3; ++i) {
            int& target = remap[static_cast<size_t>(V(3 * t + i))];
            if (target == NIL) {
                target = static_cast<int>(geometry.size());
                geometry.push_back(G(V(3 * t + i)));
            }
            vertices.push_back(target);
        }
    }

    m_G = std::move(geometry);
    m_V = std::move(vertices);
    build_opposites();
}

CHE::SimplifyReport CHE::simplify(int target_triangles) {
    SimplifyReport report;
    if (n_triangles() <= 4 || target_triangles >= n_triangles()) {
        return report;
    }

    // Step 1 of the paper: one quadric per vertex, summed over incident faces.
    std::vector<Quadric> quadric(m_G.size(), Quadric(0.0f));
    for (int t = 0; t < n_triangles(); ++t) {
        const Quadric k = plane_quadric(triangle_normal(t), G(V(3 * t)));
        for (int i = 0; i < 3; ++i) {
            quadric[static_cast<size_t>(V(3 * t + i))] += k;
        }
    }

    std::vector<bool> dead_triangle(static_cast<size_t>(n_triangles()), false);
    std::vector<int> version(m_G.size(), 0);

    // Cheapest position for collapsing the edge at `he`, with its cost.
    const auto evaluate = [&](int he, glm::vec3& position) {
        const int u = V(he);
        const int w = V(next(he));
        const Quadric q = quadric[static_cast<size_t>(u)] + quadric[static_cast<size_t>(w)];

        if (!optimal_position(q, position)) {
            // Singular quadric: fall back to the best of the two endpoints and
            // their midpoint, as the paper suggests.
            const glm::vec3 options[3] = {G(u), G(w), 0.5f * (G(u) + G(w))};
            position = options[0];
            float best = quadric_error(q, options[0]);
            for (int i = 1; i < 3; ++i) {
                const float candidate = quadric_error(q, options[i]);
                if (candidate < best) {
                    best = candidate;
                    position = options[i];
                }
            }
            return best;
        }
        return quadric_error(q, position);
    };

    // Every edge enters the queue from both of its half-edges. The duplicate is
    // harmless — whichever surfaces second is rejected as stale — and it saves
    // having to pick a canonical direction when re-pricing a fan.
    std::priority_queue<Candidate> queue;
    const auto push = [&](int he) {
        Candidate candidate;
        candidate.he = he;
        candidate.u = V(he);
        candidate.w = V(next(he));
        candidate.u_version = version[static_cast<size_t>(candidate.u)];
        candidate.w_version = version[static_cast<size_t>(candidate.w)];
        candidate.cost = evaluate(he, candidate.position);
        queue.push(candidate);
    };

    for (int he = 0; he < n_half_edges(); ++he) {
        push(he);
    }

    // A closed triangulated surface bottoms out at the tetrahedron; going
    // further produces two coincident triangles, which is not a surface.
    const int floor_triangles = std::max(target_triangles, 4);

    int alive = n_triangles();
    while (alive > floor_triangles && !queue.empty()) {
        const Candidate candidate = queue.top();
        queue.pop();

        const int he = candidate.he;
        if (dead_triangle[static_cast<size_t>(trig(he))]) {
            continue;
        }
        // Stale entry: the edge or one of its endpoints has moved on.
        if (V(he) != candidate.u || V(next(he)) != candidate.w ||
            version[static_cast<size_t>(candidate.u)] != candidate.u_version ||
            version[static_cast<size_t>(candidate.w)] != candidate.w_version) {
            continue;
        }
        if (!link_condition(he)) {
            ++report.rejected_link;
            continue;
        }
        if (would_flip(he, candidate.position)) {
            ++report.rejected_flip;
            continue;
        }

        const int twin = O(he);
        const int u = V(he);
        const int w = V(next(he));

        // Everything the repair needs, read before the mesh is touched.
        const std::vector<int> star_w = star(next(he));
        const int a = next(he);
        const int b = prev(he);
        const int a_twin = next(twin);
        const int b_twin = prev(twin);
        const int outer_a = O(a);
        const int outer_b = O(b);
        const int outer_a_twin = O(a_twin);
        const int outer_b_twin = O(b_twin);

        // Collapse: u absorbs w and moves to the optimal position.
        m_G[static_cast<size_t>(u)] = candidate.position;
        quadric[static_cast<size_t>(u)] += quadric[static_cast<size_t>(w)];
        for (const int h : star_w) {
            m_V[static_cast<size_t>(h)] = u;
        }

        dead_triangle[static_cast<size_t>(trig(he))] = true;
        dead_triangle[static_cast<size_t>(trig(twin))] = true;

        // The two faces that survived on either side of each dying triangle now
        // face each other directly.
        const auto reconnect = [&](int left, int right) {
            if (left != NIL) {
                m_O[static_cast<size_t>(left)] = right;
            }
            if (right != NIL) {
                m_O[static_cast<size_t>(right)] = left;
            }
        };
        reconnect(outer_a, outer_b);
        reconnect(outer_a_twin, outer_b_twin);

        ++version[static_cast<size_t>(u)];
        ++version[static_cast<size_t>(w)];
        ++report.collapses;
        report.total_cost += candidate.cost;
        report.max_cost = std::max(report.max_cost, static_cast<double>(candidate.cost));
        alive -= 2;

        // Re-price every edge now touching u. Any surviving half-edge of the two
        // old stars that still starts at u is a valid seed into the new fan.
        int seed = NIL;
        for (const int h : {outer_a, outer_b, outer_a_twin, outer_b_twin}) {
            if (h != NIL && !dead_triangle[static_cast<size_t>(trig(h))] && V(h) == u) {
                seed = h;
                break;
            }
            if (h != NIL && !dead_triangle[static_cast<size_t>(trig(h))] && V(next(h)) == u) {
                seed = next(h);
                break;
            }
        }
        // Only the edges meeting u change price: u moved and absorbed w's quadric.
        if (seed != NIL) {
            for (const int h : star(seed)) {
                push(h);
            }
        }
    }

    compact(dead_triangle);
    return report;
}

CHE::Deviation CHE::deviation_from(const CHE& reference, int max_samples) const {
    // One direction alone lies: simplifying a cube down to a tetrahedron leaves
    // every surviving vertex exactly on the original surface, so the forward
    // distance is zero while the shape is badly wrong. The reverse catches it.
    Deviation deviation;
    double sum = 0.0;
    double sum_squares = 0.0;
    int count = 0;

    const auto accumulate = [&](const CHE& points, const CHE& surface) {
        const int stride =
            std::max(1, (points.n_vertices() + max_samples - 1) / std::max(max_samples, 1));
        for (int v = 0; v < points.n_vertices(); v += stride) {
            float best = std::numeric_limits<float>::max();
            for (int t = 0; t < surface.n_triangles(); ++t) {
                best =
                    std::min(best, distance2_to_triangle(points.G(v), surface.G(surface.V(3 * t)),
                                                         surface.G(surface.V(3 * t + 1)),
                                                         surface.G(surface.V(3 * t + 2))));
            }
            const float distance = std::sqrt(best);
            sum += distance;
            sum_squares += static_cast<double>(distance) * distance;
            deviation.max = std::max(deviation.max, distance);
            ++count;
        }
    };

    if (n_triangles() > 0 && reference.n_triangles() > 0) {
        accumulate(*this, reference);
        accumulate(reference, *this);
    }
    if (count == 0) {
        return deviation;
    }

    deviation.mean = static_cast<float>(sum / count);
    deviation.rms = static_cast<float>(std::sqrt(sum_squares / count));
    return deviation;
}
