#include "fast_marching.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

enum class State { Far, Trial, Alive };

// One half-edge leaving each vertex. L1 has no vertex -> half-edge relation,
// only half-edge -> vertex (V), so it is built once in O(n): the first
// half-edge found with V[he] == v is as good as any other for star().
std::vector<int> half_edge_of_vertex(const CHE& mesh) {
    std::vector<int> he_of(static_cast<size_t>(mesh.n_vertices()), CHE::NIL);
    for (int he = 0; he < mesh.n_half_edges(); ++he) {
        int& slot = he_of[static_cast<size_t>(mesh.V(he))];
        if (slot == CHE::NIL) {
            slot = he;
        }
    }
    return he_of;
}

// Fallback update — Dijkstra along the edges, eq. (16) of Weber et al.:
//     t(v) = min(t(a) + |v - a|, t(b) + |v - b|)
// It measures distance along the mesh edges, not along the surface, so it
// overestimates whenever the front crosses the triangle diagonally. Used when
// only one of a, b is ALIVE (one point does not define a front) or when the
// planar solution below fails its validity checks.
float update_along_edges(const CHE& mesh, const std::vector<float>& t,
                         const std::vector<State>& state, int v, int a, int b) {
    float best = kInf;
    if (state[static_cast<size_t>(a)] == State::Alive) {
        best = std::min(best, t[static_cast<size_t>(a)] + glm::distance(mesh.G(v), mesh.G(a)));
    }
    if (state[static_cast<size_t>(b)] == State::Alive) {
        best = std::min(best, t[static_cast<size_t>(b)] + glm::distance(mesh.G(v), mesh.G(b)));
    }
    return best;
}

// Local update: the tentative arrival time at `v` given the triangle (v, a, b).
// Weber et al. 2008, section 3.1, planar wavefront. With v at the origin and
// x1 = a - v, x2 = b - v, the front is a plane n.x + p = 0 travelling at unit
// speed, so it reaches x at time n.x + p and reaches v at time p. Demanding it
// passes a and b at their known times t1, t2 gives X^T n + p 1 = t, and |n| = 1
// turns that into a quadratic in p (eq. 10):
//     (1^T Q 1) p^2 - 2 (1^T Q t) p + (t^T Q t - 1) = 0,   Q = (X^T X)^-1.
// The larger root is the front arriving after passing a and b; the smaller one
// is the same plane travelling backwards.
float update_from_triangle(const CHE& mesh, const std::vector<float>& t,
                           const std::vector<State>& state, int v, int a, int b) {
    const bool a_alive = state[static_cast<size_t>(a)] == State::Alive;
    const bool b_alive = state[static_cast<size_t>(b)] == State::Alive;
    if (!a_alive || !b_alive) {
        return update_along_edges(mesh, t, state, v, a, b);
    }

    const glm::vec3 x1 = mesh.G(a) - mesh.G(v);
    const glm::vec3 x2 = mesh.G(b) - mesh.G(v);
    const float t1 = t[static_cast<size_t>(a)];
    const float t2 = t[static_cast<size_t>(b)];

    // X^T X is 2x2 symmetric: its entries are the dot products of the two edges.
    // Q is its inverse; every Q-form below is written times det to avoid
    // dividing early, which changes no sign test and only scales the quadratic.
    const float e11 = glm::dot(x1, x1);
    const float e12 = glm::dot(x1, x2);
    const float e22 = glm::dot(x2, x2);
    const float det = e11 * e22 - e12 * e12; // > 0 unless the triangle is degenerate
    if (det <= 0.0f) {
        return update_along_edges(mesh, t, state, v, a, b);
    }

    const float one_Q_one = e11 - 2.0f * e12 + e22;            // det * 1^T Q 1 = |x1 - x2|^2
    const float one_Q_t = (e22 - e12) * t1 + (e11 - e12) * t2; // det * 1^T Q t
    const float t_Q_t = e22 * t1 * t1 - 2.0f * e12 * t1 * t2 + e11 * t2 * t2; // det * t^T Q t

    // one_Q_one p^2 - 2 one_Q_t p + (t_Q_t - det) = 0, larger root.
    const float discriminant = one_Q_t * one_Q_t - one_Q_one * (t_Q_t - det);
    if (discriminant < 0.0f) {
        return update_along_edges(mesh, t, state, v, a, b);
    }
    const float p = (one_Q_t + std::sqrt(discriminant)) / one_Q_one;

    // Consistency: the front reaches v after the vertices it came through.
    if (p <= std::max(t1, t2)) {
        return update_along_edges(mesh, t, state, v, a, b);
    }

    // Monotonicity, eq. (15): Q (t - p 1) < 0 componentwise, i.e. the front
    // direction falls inside the angle of the triangle at v. When it does not,
    // the true characteristic runs outside this triangle (typically an obtuse
    // angle at v) and extrapolating through it would be wrong.
    const float q1 = e22 * (t1 - p) - e12 * (t2 - p);
    const float q2 = e11 * (t2 - p) - e12 * (t1 - p);
    if (q1 >= 0.0f || q2 >= 0.0f) {
        return update_along_edges(mesh, t, state, v, a, b);
    }

    return p;
}

} // namespace

DistanceMap fast_marching(const CHE& mesh, int source) {
    const size_t n = static_cast<size_t>(mesh.n_vertices());
    const std::vector<int> he_of = half_edge_of_vertex(mesh);

    DistanceMap result;
    result.t.assign(n, kInf);
    std::vector<State> state(n, State::Far);

    // Min-heap of (t, vertex). std::priority_queue is a max-heap, so std::greater
    // flips it. There is no decrease-key: a vertex whose t improves is pushed
    // again, and stale entries are recognised on pop because the vertex is
    // already ALIVE by then.
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;

    result.t[static_cast<size_t>(source)] = 0.0f;
    state[static_cast<size_t>(source)] = State::Trial;
    heap.emplace(0.0f, source);

    while (!heap.empty()) {
        const int u = heap.top().second;
        heap.pop();
        if (state[static_cast<size_t>(u)] == State::Alive) {
            continue; // stale entry: u was already fixed with a smaller t
        }
        state[static_cast<size_t>(u)] = State::Alive;
        result.max_finite = std::max(result.max_finite, result.t[static_cast<size_t>(u)]);

        // The front just reached u, so every neighbour that is not yet fixed may
        // now be reached sooner. link() gives the vertices around u, including
        // the far end of a boundary edge when the fan is open.
        for (const int v : mesh.link(he_of[static_cast<size_t>(u)])) {
            if (state[static_cast<size_t>(v)] == State::Alive) {
                continue;
            }

            // Re-solve v from every triangle around it: half-edge h leaves v, so
            // the triangle is (v, V[next h], V[prev h]).
            float t_new = kInf;
            for (const int h : mesh.star(he_of[static_cast<size_t>(v)])) {
                t_new = std::min(t_new,
                                 update_from_triangle(mesh, result.t, state, v,
                                                      mesh.V(CHE::next(h)), mesh.V(CHE::prev(h))));
            }

            if (t_new < result.t[static_cast<size_t>(v)]) {
                result.t[static_cast<size_t>(v)] = t_new;
                state[static_cast<size_t>(v)] = State::Trial;
                heap.emplace(t_new, v);
            }
        }
    }

    return result;
}
