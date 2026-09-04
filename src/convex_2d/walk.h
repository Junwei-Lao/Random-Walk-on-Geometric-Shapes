#ifndef CONVEX_2D_WALK_H
#define CONVEX_2D_WALK_H

// The three 2D walk mechanics described in the documentation:
//   - HARD (section 1.5/1.6): the default walk. Neighbors outside the shape
//     are excluded; the walker never leaves. Stops once 50% of interior
//     lattice points have been visited.
//   - SOFT (section 1.8): the walker may step outside. Every candidate
//     direction is weighted by exp(-d(q)^distancePower / temperature),
//     where d(q) is the lattice-step distance from q back to the shape.
//   - DRAG (section 1.9): defined exclusively for SQUARE. The square is
//     wrapped into a discrete torus (no boundary effects at all), and an
//     additive dragging force biases the walk toward north and east.
//
// A previous revision tangled all three behind a single compile-time
// `#define isCrossBoundary` macro: the HARD and SOFT code paths were dead
// (unreachable), and the DRAG path ran unconditionally for every shape (with
// an undocumented "reflect over center" fallback for non-square shapes).
// This version makes all three real, correct, and selectable at runtime.

#include <cstddef>
#include <random>
#include <vector>

#include "cover.h"
#include "shapes.h"

enum class WalkMode2D
{
    HARD,
    SOFT,
    DRAG
};

struct WalkParams2D
{
    WalkMode2D mode = WalkMode2D::HARD;

    // SOFT mode (doc eq. 4): w_i = exp(-d_i^distancePower / temperature).
    // The doc uses squared distance with a single fixed temperature.
    double temperature = 1.0;
    double distancePower = 2.0;

    // DRAG mode (doc eq. 7/8): force applied to north and east, f in [0, 10000].
    double dragForce = 0.0;

    // Disk-covering analysis (doc section 1.10), layered on top of any mode.
    bool enableCover = false;
    double coverRho = 0.01; // R(n) = coverRho * index
};

struct WalkResult2D
{
    int steps = 0;
    double coverageFraction = 0.0; // valid only if params.enableCover
};

struct WalkContext2D
{
    std::mt19937_64 rng;
};

// Runs one random walk on `shape` at size index `index` until 50% of the
// shape's interior lattice points have been visited (doc section 1.6).
// DRAG mode requires shape == ShapeType2D::SQUARE (doc section 1.9).
WalkResult2D walk(int index, ShapeType2D shape, const WalkParams2D &params,
                   WalkContext2D &ctx, std::vector<LatticePoint2D> *outPath = nullptr);

#endif // CONVEX_2D_WALK_H
