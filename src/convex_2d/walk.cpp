#include "walk.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{

// North/East/South/West, matching the doc's naming for the DRAG force set.
const std::array<LatticePoint2D, 4> kDirs{{{0, 1}, {1, 0}, {0, -1}, {-1, 0}}};
enum DirIndex
{
    NORTH = 0,
    EAST = 1,
    SOUTH = 2,
    WEST = 3
};

LatticePoint2D getNextPositionHard(LatticePoint2D pos, int index, ShapeType2D shape, std::mt19937_64 &rng)
{
    std::vector<int> allowed;
    for (int d = 0; d < 4; ++d)
    {
        LatticePoint2D next(pos.x + kDirs[d].x, pos.y + kDirs[d].y);
        if (inBoundary(next.x, next.y, index, shape))
            allowed.push_back(d);
    }

    if (allowed.empty())
    {
        std::cerr << "Warning: no valid moves from (" << pos.x << ", " << pos.y << "). Staying in place.\n";
        return pos;
    }

    std::uniform_int_distribution<std::size_t> pick(0, allowed.size() - 1);
    int d = allowed[pick(rng)];
    return LatticePoint2D(pos.x + kDirs[d].x, pos.y + kDirs[d].y);
}

// Doc eq. 4/5: w_i = exp(-d_i^p / T). d(q) is defined for every candidate q
// regardless of whether the walker itself is currently inside or outside;
// this makes the walk reduce to a uniform choice deep in the interior (all
// d_i == 0) exactly as Remark 2 describes, without needing to special-case
// "am I inside right now".
LatticePoint2D getNextPositionSoft(LatticePoint2D pos, int index, ShapeType2D shape,
                                    const WalkParams2D &params, std::mt19937_64 &rng)
{
    std::array<double, 4> weights{};
    for (int d = 0; d < 4; ++d)
    {
        LatticePoint2D next(pos.x + kDirs[d].x, pos.y + kDirs[d].y);
        int dist = distanceToShape(next, index, shape);
        weights[d] = std::exp(-std::pow(static_cast<double>(dist), params.distancePower) / params.temperature);
    }

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int d = dist(rng);
    return LatticePoint2D(pos.x + kDirs[d].x, pos.y + kDirs[d].y);
}

// Doc eq. 6/7/8: toroidal wrap-around plus an additive dragging force toward
// north and east. Defined exclusively for the square (doc section 1.9).
LatticePoint2D getNextPositionDrag(LatticePoint2D pos, int modulus, double dragForce, std::mt19937_64 &rng)
{
    std::array<double, 4> weights{1.0, 1.0, 1.0, 1.0};
    weights[NORTH] += dragForce;
    weights[EAST] += dragForce;

    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    int d = dist(rng);

    int nx = ((pos.x + kDirs[d].x) % modulus + modulus) % modulus;
    int ny = ((pos.y + kDirs[d].y) % modulus + modulus) % modulus;
    return LatticePoint2D(nx, ny);
}

} // namespace

WalkResult2D walk(int index, ShapeType2D shape, const WalkParams2D &params,
                   WalkContext2D &ctx, std::vector<LatticePoint2D> *outPath)
{
    if (params.mode == WalkMode2D::DRAG && shape != ShapeType2D::SQUARE)
        throw std::invalid_argument("DRAG mode is defined only for SQUARE (doc section 1.9)");

    // Seed and target off the largest connected component, not the raw
    // inBoundary count: a hard-boundary walk can never reach points outside
    // its own connected component, so a target based on the full (possibly
    // disconnected) point count would be unreachable if the walker happens
    // to start in a smaller stray component. See buildConnectedShape.
    ConnectedShape2D cs = buildConnectedShape(index, shape);
    const int shapePoints = cs.pointCount;
    const int target = shapePoints / 2; // doc section 1.6: 2D stops at 50%
    const int modulus = (params.mode == WalkMode2D::DRAG) ? squareLatticeModulus(index) : 0;

    LatticePoint2D pos = sampleUniform(cs, ctx.rng);
    freeConnectedShape(cs);

    DiskCover2D cover(params.coverRho * index, shape, index);

    PointSet2D visited;
    visited.reserve(static_cast<std::size_t>(shapePoints) / 2 + 1);

    std::vector<LatticePoint2D> path;
    path.reserve(static_cast<std::size_t>(std::max(target + 5, 1000)));
    path.push_back(pos);

    if (inBoundary(pos.x, pos.y, index, shape))
    {
        visited.insert(pos);
        if (params.enableCover)
            cover.coverPoint(pos.x, pos.y);
    }

    int steps = 0;
    while (static_cast<int>(visited.size()) < target)
    {
        switch (params.mode)
        {
        case WalkMode2D::HARD:
            pos = getNextPositionHard(pos, index, shape, ctx.rng);
            break;
        case WalkMode2D::SOFT:
            pos = getNextPositionSoft(pos, index, shape, params, ctx.rng);
            break;
        case WalkMode2D::DRAG:
            pos = getNextPositionDrag(pos, modulus, params.dragForce, ctx.rng);
            break;
        }

        path.push_back(pos);
        ++steps;

        if (inBoundary(pos.x, pos.y, index, shape))
        {
            if (visited.insert(pos).second && params.enableCover)
                cover.coverPoint(pos.x, pos.y);
        }
    }

    WalkResult2D result;
    result.steps = steps;
    if (params.enableCover)
        result.coverageFraction = cover.coverageFraction(shapePoints);

    if (outPath)
        *outPath = std::move(path);

    return result;
}
