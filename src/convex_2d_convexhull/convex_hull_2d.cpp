#include "convex_hull_2d.h"

#include <CGAL/convex_hull_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

const double PI = std::acos(-1);

const std::vector<std::pair<ShapeType2DHull, std::string>> kAllShapes2DHull = {
    {ShapeType2DHull::RECTANGLE_3_1, "RECTANGLE_3_1"},
    {ShapeType2DHull::RECTANGLE_4_1, "RECTANGLE_4_1"},
    {ShapeType2DHull::SQUARE, "SQUARE"},
    {ShapeType2DHull::TRIANGLE, "TRIANGLE"},
    {ShapeType2DHull::SHARP_TRIANGLE, "SHARP_TRIANGLE"},
    {ShapeType2DHull::FLAT_TRIANGLE, "FLAT_TRIANGLE"},
    {ShapeType2DHull::HEXAGON, "HEXAGON"},
    {ShapeType2DHull::POLYGON5, "POLYGON5"},
    {ShapeType2DHull::POLYGON8, "POLYGON8"},
    {ShapeType2DHull::POLYGON9, "POLYGON9"},
    {ShapeType2DHull::POLYGON10, "POLYGON10"},
    {ShapeType2DHull::POLYGON12, "POLYGON12"},
    {ShapeType2DHull::POLYGON15, "POLYGON15"},
    {ShapeType2DHull::ROTATED_SQUARE, "ROTATED_SQUARE"},
    {ShapeType2DHull::STRETCHED_ROTATED_SQUARE, "STRETCHED_ROTATED_SQUARE"},
};

std::string shapeName(ShapeType2DHull shape)
{
    for (const auto &[type, name] : kAllShapes2DHull)
        if (type == shape)
            return name;
    throw std::invalid_argument("Unknown ShapeType2DHull");
}

bool shapeFromName(const std::string &name, ShapeType2DHull &out)
{
    for (const auto &[type, shapeNameStr] : kAllShapes2DHull)
        if (shapeNameStr == name)
        {
            out = type;
            return true;
        }
    return false;
}

namespace
{

std::vector<Point2> regularPolygonVertices(int N)
{
    std::vector<Point2> pts;
    pts.reserve(N);
    for (int k = 0; k < N; ++k)
    {
        double theta = 2.0 * PI * k / N;
        pts.emplace_back(std::cos(theta), std::sin(theta));
    }
    return pts;
}

std::vector<Point2> baseVertices(ShapeType2DHull shape)
{
    switch (shape)
    {
    case ShapeType2DHull::SQUARE:
        return {Point2(0, 0), Point2(1, 0), Point2(1, 1), Point2(0, 1)};
    case ShapeType2DHull::RECTANGLE_3_1:
        return {Point2(0, 0), Point2(3, 0), Point2(3, 1), Point2(0, 1)};
    case ShapeType2DHull::RECTANGLE_4_1:
        return {Point2(0, 0), Point2(4, 0), Point2(4, 1), Point2(0, 1)};
    case ShapeType2DHull::TRIANGLE:
        return {Point2(0, 0), Point2(1, 0), Point2(0.5, std::sqrt(3.0) / 2.0)};
    case ShapeType2DHull::SHARP_TRIANGLE:
        return {Point2(0, 0), Point2(1, 0), Point2(0.5, 0.5 * std::tan(75.0 * PI / 180.0))};
    case ShapeType2DHull::FLAT_TRIANGLE:
        return {Point2(0, 0), Point2(1, 0), Point2(0.5, 0.5 * std::tan(40.0 * PI / 180.0))};
    case ShapeType2DHull::HEXAGON:
        return regularPolygonVertices(6);
    case ShapeType2DHull::POLYGON5:
        return regularPolygonVertices(5);
    case ShapeType2DHull::POLYGON8:
        return regularPolygonVertices(8);
    case ShapeType2DHull::POLYGON9:
        return regularPolygonVertices(9);
    case ShapeType2DHull::POLYGON10:
        return regularPolygonVertices(10);
    case ShapeType2DHull::POLYGON12:
        return regularPolygonVertices(12);
    case ShapeType2DHull::POLYGON15:
        return regularPolygonVertices(15);
    case ShapeType2DHull::ROTATED_SQUARE:
        return {Point2(1, 0), Point2(0, 1), Point2(-1, 0), Point2(0, -1)};
    case ShapeType2DHull::STRETCHED_ROTATED_SQUARE:
        return {Point2(3, 0), Point2(0, 1), Point2(-3, 0), Point2(0, -1)};
    }
    throw std::invalid_argument("Unknown ShapeType2DHull in baseVertices");
}

} // namespace

void ConvexHullDomain2D::generateBaseHull(ShapeType2DHull shape)
{
    std::vector<Point2> pts = baseVertices(shape);

    Polygon2 hull;
    CGAL::convex_hull_2(pts.begin(), pts.end(), std::back_inserter(hull));

    if (hull.size() < 3)
        throw std::runtime_error("Convex hull degenerated to fewer than 3 vertices");

    double area = std::abs(CGAL::to_double(hull.area()));
    if (!(area > 0.0))
        throw std::runtime_error("Base hull has non-positive area");

    polygon_ = std::move(hull);
    baseArea_ = area;
    actualArea_ = area;
}

ConvexHullDomain2D ConvexHullDomain2D::scaledCopyForArea(double targetArea) const
{
    if (polygon_.size() == 0)
        throw std::runtime_error("Base hull is empty. Call generateBaseHull first.");
    if (targetArea <= 0.0)
        throw std::invalid_argument("targetArea must be positive");

    double scale = std::sqrt(targetArea / baseArea_);

    ConvexHullDomain2D out;
    std::vector<Point2> scaledPts;
    scaledPts.reserve(polygon_.size());
    for (auto it = polygon_.vertices_begin(); it != polygon_.vertices_end(); ++it)
        scaledPts.emplace_back(it->x() * scale, it->y() * scale);

    out.polygon_ = Polygon2(scaledPts.begin(), scaledPts.end());
    out.baseArea_ = baseArea_;
    out.actualArea_ = std::abs(CGAL::to_double(out.polygon_.area()));
    return out;
}

void ConvexHullDomain2D::buildIntegerGridPoints()
{
    insidePoints_.clear();
    insideSet_.clear();

    if (polygon_.size() == 0)
        throw std::runtime_error("Polygon is empty.");

    auto bbox = polygon_.bbox();
    int xmin = static_cast<int>(std::ceil(bbox.xmin()));
    int xmax = static_cast<int>(std::floor(bbox.xmax()));
    int ymin = static_cast<int>(std::ceil(bbox.ymin()));
    int ymax = static_cast<int>(std::floor(bbox.ymax()));

    for (int x = xmin; x <= xmax; ++x)
    {
        for (int y = ymin; y <= ymax; ++y)
        {
            Point2 q(x, y);
            CGAL::Bounded_side side = polygon_.bounded_side(q);
            if (side == CGAL::ON_BOUNDED_SIDE || side == CGAL::ON_BOUNDARY)
            {
                GridPoint2D p{x, y};
                insidePoints_.push_back(p);
                insideSet_.insert(p);
            }
        }
    }

    if (insidePoints_.empty())
        throw std::runtime_error("No integer grid points found inside the hull.");
}

std::uint64_t RandomWalkSimulator2D::runOneTrial(const ConvexHullDomain2D &domain, double coverageFraction)
{
    const auto &pts = domain.points();
    const std::size_t total = pts.size();
    if (total == 0)
        throw std::runtime_error("Domain has no integer points.");
    if (!(coverageFraction > 0.0 && coverageFraction <= 1.0))
        throw std::invalid_argument("coverageFraction must be in (0, 1]");

    const std::size_t targetUnique =
        static_cast<std::size_t>(std::ceil(coverageFraction * static_cast<double>(total)));

    std::uniform_int_distribution<std::size_t> startDist(0, total - 1);
    static const std::array<GridPoint2D, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

    GridPoint2D current = pts[startDist(rng_)];
    std::unordered_set<GridPoint2D, GridPoint2DHash> visited;
    visited.reserve(targetUnique * 2 + 16);
    visited.insert(current);

    // Doc section 1.5: choose uniformly among the *valid* neighbors, not
    // uniformly among all 4 directions with a "stay in place" self-loop
    // whenever the chosen direction happens to be invalid. The latter (what
    // an earlier revision of the 3D hull engine did) biases mixing time
    // upward near the boundary and does not match the analytic engines.
    std::array<GridPoint2D, 4> candidates;
    std::uint64_t steps = 0;
    while (visited.size() < targetUnique)
    {
        int numValid = 0;
        for (const auto &d : dirs)
        {
            GridPoint2D next{current.x + d.x, current.y + d.y};
            if (domain.contains(next))
                candidates[numValid++] = next;
        }
        if (numValid > 0)
        {
            std::uniform_int_distribution<int> pick(0, numValid - 1);
            current = candidates[pick(rng_)];
        }
        visited.insert(current);
        ++steps;
    }
    return steps;
}

Stats2D RandomWalkSimulator2D::runManyTrials(const ConvexHullDomain2D &domain, double coverageFraction, int trials)
{
    if (trials < 2)
        throw std::invalid_argument("trials must be at least 2");

    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(trials));
    for (int i = 0; i < trials; ++i)
        values.push_back(static_cast<double>(runOneTrial(domain, coverageFraction)));

    double mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    double accum = 0.0;
    for (double v : values)
    {
        double d = v - mean;
        accum += d * d;
    }
    double variance = accum / static_cast<double>(values.size() - 1);
    return Stats2D{mean, std::sqrt(variance)};
}
