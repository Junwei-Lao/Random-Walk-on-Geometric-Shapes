#ifndef CONVEX_HULL_2D_H
#define CONVEX_HULL_2D_H

// Mirrors src/convex_3d_convexhull's approach (build ONE base convex hull,
// reuse it for every target area by uniform scaling, rasterize integer grid
// points via CGAL's point-in-polygon test) applied to 2D. Requires CGAL.
//
// Where convex_2d/shapes.cpp defines each shape by a closed-form inBoundary
// formula, this engine instead defines each shape by an explicit vertex set
// and lets CGAL::convex_hull_2 + a point-in-polygon test do the rasterizing
// -- the same generic, formula-free approach the existing 3D convex-hull
// engine already used for OCTAHEDRON/CUBE/RECTANGULAR_PRISM_1_2_3/PYRAMID.
// Only the 15 polygonal (vertex-defined) 2D convex shapes are covered here;
// CIRCLE has no finite vertex set and stays exclusively in convex_2d, just
// as SPHERE stays exclusively in convex_3d.

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>

#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

extern const double PI;

enum class ShapeType2DHull
{
    RECTANGLE_3_1,
    RECTANGLE_4_1,
    SQUARE,
    TRIANGLE,
    SHARP_TRIANGLE,
    FLAT_TRIANGLE,
    HEXAGON,
    POLYGON5,
    POLYGON8,
    POLYGON9,
    POLYGON10,
    POLYGON12,
    POLYGON15,
    ROTATED_SQUARE,
    STRETCHED_ROTATED_SQUARE
};

extern const std::vector<std::pair<ShapeType2DHull, std::string>> kAllShapes2DHull;

std::string shapeName(ShapeType2DHull shape);
bool shapeFromName(const std::string &name, ShapeType2DHull &out);

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point2 = Kernel::Point_2;
using Polygon2 = CGAL::Polygon_2<Kernel>;

struct GridPoint2D
{
    int x{};
    int y{};

    bool operator==(const GridPoint2D &other) const { return x == other.x && y == other.y; }
};

struct GridPoint2DHash
{
    std::size_t operator()(const GridPoint2D &p) const noexcept
    {
        std::uint64_t packed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.x)) << 32) |
                                static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.y));
        return std::hash<std::uint64_t>{}(packed);
    }
};

class ConvexHullDomain2D
{
public:
    // Builds the base hull for `shape` at unit scale (see shape vertex
    // tables in the .cpp). Throws std::runtime_error on failure.
    void generateBaseHull(ShapeType2DHull shape);

    // Uniformly scales the base hull so its area matches `targetArea`.
    ConvexHullDomain2D scaledCopyForArea(double targetArea) const;

    // Rasterizes all integer lattice points inside/on the (possibly scaled)
    // hull polygon. Must be called before points()/contains().
    void buildIntegerGridPoints();

    bool contains(const GridPoint2D &p) const { return insideSet_.count(p) != 0; }
    const std::vector<GridPoint2D> &points() const { return insidePoints_; }
    std::size_t gridPointCount() const { return insidePoints_.size(); }

    double baseArea() const { return baseArea_; }
    double actualArea() const { return actualArea_; }

private:
    Polygon2 polygon_;
    double baseArea_ = 0.0;
    double actualArea_ = 0.0;

    std::vector<GridPoint2D> insidePoints_;
    std::unordered_set<GridPoint2D, GridPoint2DHash> insideSet_;
};

struct Stats2D
{
    double mean = 0.0;
    double sampleStddev = 0.0;
};

class RandomWalkSimulator2D
{
public:
    explicit RandomWalkSimulator2D(std::uint64_t seed) : rng_(seed) {}

    // Random-walks on `domain` (hard boundary, doc section 1.5/1.6) until
    // `coverageFraction` of its grid points have been visited; returns step count.
    std::uint64_t runOneTrial(const ConvexHullDomain2D &domain, double coverageFraction);

    Stats2D runManyTrials(const ConvexHullDomain2D &domain, double coverageFraction, int trials);

private:
    std::mt19937_64 rng_;
};

#endif // CONVEX_HULL_2D_H
