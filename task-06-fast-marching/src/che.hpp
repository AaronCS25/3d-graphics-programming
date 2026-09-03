// Compact Half-Edge (CHE), level L1.
//
// Lage, Lewiner, Lopes, Velho — "CHE: A scalable topological data structure for
// triangular meshes", SIBGRAPI 2005.
//
// Triangle t owns the three consecutive half-edges 3t, 3t+1, 3t+2, so the
// parent triangle of a half-edge is he/3 and no face array has to be stored.
// Every relation is an integer index; the structure holds no pointers.
//
//   G[v]   geometry array — position of vertex v
//   V[he]  vertex array   — vertex at the origin of half-edge he
//   O[he]  opposite array — half-edge glued to he, NIL on a boundary   (L1)
//
// L0 is G and V alone (a triangle soup); L1 adds O, which is what turns the
// soup into a surface with adjacency.

#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

class CHE {
  public:
    static constexpr int NIL = -1;

    CHE() = default;
    CHE(std::vector<glm::vec3> geometry, std::vector<int> vertex_array);

    int n_vertices() const {
        return static_cast<int>(m_G.size());
    }
    int n_half_edges() const {
        return static_cast<int>(m_V.size());
    }
    int n_triangles() const {
        return n_half_edges() / 3;
    }

    int n_edges() const;

    // Array accessors, named after the paper so the code reads like it.
    const glm::vec3& G(int v) const {
        return m_G[static_cast<size_t>(v)];
    }
    int V(int he) const {
        return m_V[static_cast<size_t>(he)];
    }
    int O(int he) const {
        return m_O[static_cast<size_t>(he)];
    }

    // Half-edge arithmetic: no lookup, just the index algebra of the paper.
    static int trig(int he) {
        return he / 3;
    }
    static int next(int he) {
        return 3 * (he / 3) + (he + 1) % 3;
    }
    static int prev(int he) {
        return 3 * (he / 3) + (he + 2) % 3;
    }

    bool is_boundary(int he) const {
        return O(he) == NIL;
    }
    bool is_closed() const;

    // Half-edges leaving V(he), walking the star counter-clockwise. On a
    // boundary the walk is restarted backwards so the whole fan is still
    // reported. Takes a half-edge rather than a vertex on purpose: L1 has no
    // vertex-to-half-edge map, and every caller already holds one.
    std::vector<int> star(int he) const;
    int valence(int he) const {
        return static_cast<int>(star(he).size());
    }

    // The vertices opposite to V(he) across its incident triangles.
    std::vector<int> link(int he) const;

    // Area-weighted vertex normals, which fall out of the star.
    std::vector<glm::vec3> vertex_normals() const;

    glm::vec3 triangle_normal(int t) const;

    // Checks the invariants that make the structure a valid L1 CHE. Returns an
    // empty string when the mesh is sound, otherwise the first violation found.
    std::string validate() const;

    // Raw index buffer for glDrawElements — V is already exactly that.
    const std::vector<int>& indices() const {
        return m_V;
    }
    const std::vector<glm::vec3>& positions() const {
        return m_G;
    }

  private:
    void build_opposites();

    std::vector<glm::vec3> m_G;
    std::vector<int> m_V;
    std::vector<int> m_O;
};

