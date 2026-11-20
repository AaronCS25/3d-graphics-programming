# Task 17 — Advanced Samplers

> Code and delve into the details of Halton and Sobol sequence generators to
> generate a 3D point cloud inside a 1x1x1 box.
>
> Visually verify their spatial distribution (render the point cloud in your
> viewer), check for unwanted patterns, and compare their output vs the
> std::random generators.
>
> Submit a link showing the rendered point clouds, and update your repository.

**Status:** ✅ done

## Result

![overview](docs/overview.png)

Seven generators, each drawn in the viewer: 4096 points in 3D (left) and the
first 1024 seen from the front, an orthographic projection on the x-y plane
(right). Colour is the index of the point, from dark blue (first) to yellow
(last).

| Generator | 3D, n = 4096 | Front (x, y), n = 1024 |
|-----------|--------------|------------------------|
| `std::mt19937` | ![](docs/mt19937_3d.png) | ![](docs/mt19937_front.png) |
| `std::minstd_rand` | ![](docs/minstd_rand_3d.png) | ![](docs/minstd_rand_front.png) |
| RANDU | ![](docs/randu_3d.png) | ![](docs/randu_front.png) |
| Halton (2, 3, 5) | ![](docs/halton_3d.png) | ![](docs/halton_front.png) |
| Halton (29, 31, 37) | ![](docs/halton_high_3d.png) | ![](docs/halton_high_front.png) |
| Scrambled Halton (29, 31, 37) | ![](docs/halton_scrambled_3d.png) | ![](docs/halton_scrambled_front.png) |
| Sobol (Joe-Kuo) | ![](docs/sobol_3d.png) | ![](docs/sobol_front.png) |

What the eye sees in the projections:

- **Pseudo-random** (`std::mt19937`, `std::minstd_rand`): clumps and empty
  patches. That is what independent points look like. Nothing forbids two
  points from landing next to each other.
- **Halton (2, 3, 5) and Sobol**: no clumps and no large holes. Every point
  lands where the earlier ones left room, and that is the whole idea of a
  low-discrepancy sequence.
- **Halton (29, 31, 37)**: diagonal stripes, a pattern nobody wants in a
  sampler (below).
- **Scrambled Halton (29, 31, 37)**: the stripes are gone, and the points are
  still more even than random ones.

## Unwanted patterns

### RANDU: every point on 15 planes

RANDU (IBM, 1960s) is `x_{k+1} = 65539 x_k mod 2^31`. Since `65539 = 2^16 + 3`,
squaring gives `x_{k+2} = 6 x_{k+1} − 9 x_k (mod 2^31)`. Any three consecutive
outputs therefore satisfy `9x − 6y + z ∈ ℤ`, and inside the cube that means one
of 15 parallel planes. From an ordinary viewpoint (table above) the cloud looks
fine. Looking *along* the planes (`V` until "along RANDU's planes"), they become
lines:

| RANDU, along its planes | `std::mt19937`, same view |
|-------------------------|---------------------------|
| ![](docs/randu_planes.png) | ![](docs/mt19937_planes.png) |

Every linear congruential generator has this flaw (Marsaglia, 1968). The
question is how many planes it uses. `std::minstd_rand` (multiplier 48271) has
so many, and so close together, that no view shows them. `std::mt19937` is not
an LCG at all.

### Halton with large bases: correlated dimensions

The Halton coordinate in base `b` is the radical inverse of the index: its
digits in base `b` mirrored around the radix point. For `i < b` it is just
`i / b`. With bases 29 and 31, the first hundreds of indices give
`x ≈ i/29` and `y ≈ i/31`, two almost equal ramps. The points then run along
lines until the digits wrap, which produces the stripes. They fade only after
about 29 · 31 ≈ 900 points. A path tracer that uses Halton dimensions 10 and 11
(bases 29 and 31) for a bounce direction shows this as structured noise.

**The fix is scrambling.** Each base gets a random permutation of its digits
`0 … b−1`, applied to every digit of the radical inverse. That includes the
infinitely many leading zeros of the index, which add `perm[0] · b^−K / (b−1)`.
The two ramps stop being parallel. The sequence keeps its stratification by
`b^k`, because a permutation of the digits only renames which interval gets
which point.

## Numbers

`task17 --stats` (the table below is its output):

| Generator | D* n = 512 | D* n = 4096 | 16³ grid, n = 4096: empty / fullest | 8×9×5 grid, n = 360: empty / fullest |
|---|---|---|---|---|
| expected for random points | 0.01311 | 0.00463 | 1507 (36.8%) | 132 (36.8%) |
| std::mt19937 | 0.01148 | 0.00422 | 1501 (36.6%) / 6 | 136 (37.8%) / 5 |
| std::minstd_rand | 0.01020 | 0.00633 | 1458 (35.6%) / 6 | 138 (38.3%) / 5 |
| RANDU | 0.01530 | 0.00412 | 1822 (44.5%) / 7 | 128 (35.6%) / 5 |
| Halton (2, 3, 5) | 0.00280 | 0.00052 | 986 (24.1%) / 4 | **0 (0.0%) / 1** |
| Halton (29, 31, 37) | 0.01460 | 0.00194 | 1300 (31.7%) / 4 | 187 (51.9%) / 4 |
| Scrambled Halton (29, 31, 37) | 0.00567 | 0.00107 | 1429 (34.9%) / 5 | 125 (34.7%) / 4 |
| Sobol (Joe-Kuo) | 0.00233 | 0.00035 | **1 (0.0%) / 2** | 82 (22.8%) / 3 |

- **D\*** is the L2-star discrepancy. For every box `[0, a)` anchored at the
  origin, it compares the fraction of points inside the box with the box's
  volume, and returns the root mean square of that difference over all boxes.
  0 means perfectly even. Warnock's formula computes it exactly in `O(n²)`
  (`stats.cpp`). For independent points its expected square is
  `(2⁻³ − 3⁻³)/n`, so it falls as `1/√n`. Sobol at 4096 points is 13 times
  below that, and Halton (2, 3, 5) 9 times. Quasi-random error falls roughly
  as `(log n)³ / n`.
- **The grids** cut the cube into as many cells as there are points. Perfect
  stratification puts one point in every cell. Independent points leave
  `(1 − 1/n)ⁿ ≈ 1/e = 36.8%` of the cells empty, and the three std-style
  generators match that.
- **Each sequence fills the grid of its bases.** Halton (2, 3, 5) puts exactly
  one point in each of the 8 × 9 × 5 = 2³ · 3² · 5 cells, but leaves 24% of
  the 16³ cells empty: a power-of-two grid does not suit bases 3 and 5. Sobol
  is built in base 2. It fills the 16³ grid but not the 8 × 9 × 5 one.
- **Sobol's one empty cell** comes from skipping index 0. Its guarantee is for
  the block of indices `0 … 2^m − 1`. The points here are `1 … 4096`, so the
  origin's cell lost its point (0, 0, 0) and the cell of point 4096 holds two.
  Halton has no such hole, because point 360 lands in the origin's cell (360 is
  `2³ · 3² · 5`, so all three of its low digits are 0).
- **RANDU's discrepancy looks normal** (0.00412), and its planes do not show in
  it. The 16³ grid catches them: 44.5% of the cells are empty, against 36.8%,
  because the cells between two planes never get a point. A single number does
  not prove that a generator is good.
- **Halton (29, 31, 37) is worse than random** on the 8 × 9 × 5 grid: the
  stripes leave half the cells empty. Scrambling brings it back to the level
  of random points on that grid, while its discrepancy stays below a quarter
  of the random one (0.00107 against 0.00463).
- The pseudo-random rows use one seed each, so the small differences among
  those three are not meaningful by themselves.

## Approach — `src/samplers.cpp`

### Pseudo-random: the std generators

`std::mt19937` (Mersenne Twister, period 2¹⁹⁹³⁷ − 1) and `std::minstd_rand`
(Park-Miller LCG, multiplier 48271, modulus 2³¹ − 1) both go through
`std::uniform_real_distribution<double>`. Each point takes three consecutive
outputs as x, y, z, which is how a renderer consumes them and what exposes
correlations between consecutive outputs such as RANDU's. The seed is fixed
(2026), so the clouds are reproducible.

### Halton

`radical_inverse(i, b)`: the digits of `i` in base `b`, mirrored:

```
i = d0 + d1 b + d2 b² + …   ->   φ_b(i) = d0/b + d1/b² + d2/b³ + …
```

In base 2, 1, 2, 3, 4, … give 0.5, 0.25, 0.75, 0.125, …: each new point splits
the largest gap left. Halton uses a different prime base per dimension,
`(φ₂(i), φ₃(i), φ₅(i))`, so the dimensions do not repeat each other. The
sequence starts at `i = 1`, because `i = 0` is the corner (0, 0, 0).

### Sobol

Every dimension is base 2, but each one scrambles the bits with its own matrix,
built from a primitive polynomial over GF(2):

1. **Direction numbers.** For a polynomial
   `x^s + a₁ x^(s−1) + … + a_(s−1) x + 1` and initial odd `m₁ … m_s`, the
   recurrence of Bratley and Fox gives every `m_k`, which is
   `2 a₁ m_(k−1) ⊕ 4 a₂ m_(k−2) ⊕ … ⊕ 2^s m_(k−s) ⊕ m_(k−s)`. The direction
   number is `v_k = m_k / 2^k`, stored as 32-bit fractions.
   Dimension 1 is the van der Corput sequence, `v_k = 2^−k`. Dimensions 2 and 3
   are the first two rows of Joe and Kuo's table (`new-joe-kuo-6.21201`):
   `(s = 1, a = 0, m = 1)` and `(s = 2, a = 1, m = 1, 3)`.
2. **Points by Gray code** (Antonov and Saleev). Point `i` is the XOR of the
   direction numbers of the bits of `gray(i)`. Consecutive Gray codes differ in
   one bit, the lowest zero bit of `i − 1`, so each new point is the previous
   one XOR one direction number: one operation per coordinate.

The first seven points match SciPy's unscrambled Sobol (`scipy.stats.qmc.Sobol`)
exactly: (0.5, 0.5, 0.5), (0.75, 0.25, 0.25), (0.25, 0.75, 0.75), …

### The viewer — `src/main.cpp`

Points are `GL_POINTS` whose size the vertex shader sets (`gl_PointSize`). The
fragment shader discards the corners of each point so it is round, and colours
it with viridis by index. The cube's 12 edges are drawn as lines. The camera is
the shared arcball (`common/camera.hpp`). The four fixed views are orthographic
`lookAt`s: along z, x and y, and along a direction inside RANDU's planes with
their normal `(9, −6, 1)` pointing up.

`--save` renders into an offscreen framebuffer of 800 × 800 and writes the
images of this README. `S` saves what the window shows.

## Controls

| Key / mouse | Action |
|-------------|--------|
| `1` … `7` | Generator: mt19937 · minstd_rand · RANDU · Halton · Halton (29, 31, 37) · scrambled · Sobol |
| `↑` / `↓` | Twice / half the points (64 … 1,048,576) |
| `V` | View: arcball · front · side · top · along RANDU's planes |
| Left drag / scroll | Arcball rotation · zoom (back to the arcball view) |
| `C` | Colour by index ↔ plain white |
| `[` / `]` | Smaller / larger points |
| `S` | Save the window to `docs/<generator>_<n>_screenshot.png` |
| `R` | Reset the camera |
| `ESC` | Quit |

The title bar shows the generator, the number of points and their L2-star
discrepancy next to the value expected for random points.

## Run

```sh
cmake --build --preset <preset> --target task17
./build/<preset>/task-17-advanced-samplers/task17            # viewer
./build/<preset>/task-17-advanced-samplers/task17 --stats    # the table above
./build/<preset>/task-17-advanced-samplers/task17 --save     # the images above
```

## Layout

```
src/
├── main.cpp            viewer, views, offscreen --save, --stats
├── samplers.hpp/.cpp   std generators, RANDU, Halton, scrambled Halton, Sobol
├── stats.hpp/.cpp      L2-star discrepancy (Warnock), grid occupancy
└── stb_image_write.cpp the one translation unit that compiles stb_image_write
```

## Notes / what I learned

- "Random" and "uniform" are different goals. Independent points are uniform
  only on average, and they clump. A low-discrepancy sequence gives up
  independence in exchange for evenness, which is what a Monte Carlo
  estimate needs: the error falls almost as `1/n` instead of `1/√n`.
- Low-discrepancy sequences are built for a base. Halton stratifies by the
  powers of its primes and Sobol by powers of 2. Each fills its own grids
  perfectly and the other's only partly (about a quarter of the cells empty,
  against 37% for random points), so sample counts should be powers of 2 for
  Sobol.
- Large Halton bases are correlated for small `n`, so the dimensions of a path
  tracer far down the list are where Halton breaks. Scrambling costs one table
  lookup per digit.
- An unwanted pattern can hide from one view and from one statistic. RANDU
  looked fine from the default camera and in its discrepancy. Only the right
  view (along its planes) and the right test (empty cells) show it.
- Points that fall exactly on a cell border (`1/3` is a Halton coordinate and a
  border of a 9-cell axis) can round into the wrong cell in `double`. The
  occupancy test adds `1e-9` before taking the floor.
