// Computes each 2D shape's exact continuous perimeter and its isoperimetric
// ratio P / sqrt(A), for evaluating shapes against the isoperimetric
// inequality (P^2 >= 4*PI*A, i.e. P/sqrt(A) >= 2*sqrt(PI), with equality
// only for the circle).
//
// Every ShapeType2D shape is normalized so its *exact* continuous area is
// PI * index^2 (src/convex_2d/shapes.cpp; see that file's and this
// project's fix log for how SNOWFLAKE's area constant was corrected to
// actually satisfy this). Since perimeter also scales linearly with index
// for every shape here, P/sqrt(A) is a scale-invariant constant per shape
// -- it does not depend on --index at all, only on which shape it is. That
// makes it a good complement to the walk engines' measured mixing times:
// this is a pure boundary-geometry "roundness" score, independent of any
// simulation.
//
// Each shape's boundary is a simple polygon except CIRCLE (closed form,
// P = 2*PI*r) and SNOWFLAKE (a union of ~239 axis-aligned rectangles from a
// recursive fractal, not a simple polygon at all -- its perimeter is a
// precomputed constant, kSnowflakeUnitPerimeter, derived once via an exact
// coordinate-compression grid over the union; see shapes.h). Every other
// shape's vertices are ported directly from visualization/path_visual/
// path_visualizer.py's boundary generators (already cross-checked there
// against the real C++ rasterization), since perimeter only needs vertex
// positions, not the winding order path_visualizer.py needs for plotting.
//
// Run with --list-shapes to print all valid --shape values, or just run
// with no flags to get one CSV row per shape (the common case).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "../src/common/cli_args.h"
#include "../src/convex_2d/shapes.h"

namespace fs = std::filesystem;

namespace
{

using Point = std::pair<double, double>;

double polygonPerimeter(const std::vector<Point> &verts)
{
    double p = 0.0;
    for (std::size_t i = 0; i < verts.size(); ++i)
    {
        const Point &a = verts[i];
        const Point &b = verts[(i + 1) % verts.size()];
        p += std::hypot(b.first - a.first, b.second - a.second);
    }
    return p;
}

std::vector<Point> squareVertices(int index)
{
    double s = std::sqrt(PI) * index;
    return {{0, 0}, {s, 0}, {s, s}, {0, s}};
}

std::vector<Point> rectangleVertices(int index, int ratio)
{
    double c = std::sqrt(PI / ratio);
    double w = ratio * c * index;
    double h = c * index;
    return {{0, 0}, {w, 0}, {w, h}, {0, h}};
}

// TRIANGLE (base_angle=60, equilateral) / SHARP_TRIANGLE (75) / FLAT_TRIANGLE
// (40): base along y=0 from x=0 to x=c*index, apex at x=c*index/2, where
// c = sqrt(4*PI / tan(base_angle)). Matches shapes.cpp's isoceles-triangle
// inBoundary formulas exactly.
std::vector<Point> isoscelesTriangleVertices(int index, double baseAngleDeg)
{
    double baseAngle = baseAngleDeg * PI / 180.0;
    double c = std::sqrt(4.0 * PI / std::tan(baseAngle)) * index;
    double apexY = std::tan(baseAngle) * c / 2.0;
    return {{0.0, 0.0}, {c, 0.0}, {c / 2.0, apexY}};
}

// HEXAGON / POLYGON5/8/9/10/12/15: matches inRegularPolygon's circumradius
// R = index * sqrt(2*PI / (N * sin(2*PI/N))). The vertex phase below (not
// simply (2k+1)*sector/2) matters for *plotting* orientation -- see
// path_visualizer.py's regular_polygon() for the full derivation of why
// that formula is only correct for odd N -- but perimeter is a sum of edge
// lengths and doesn't care about the polygon's rotation, so any consistent
// vertex ordering gives the same total; this keeps the same phase anyway
// for a single source of truth between the two files.
std::vector<Point> regularPolygonVertices(int index, int N, double margin = 20.0)
{
    double twoPiOverN = 2.0 * PI / N;
    double denom = N * std::sin(twoPiOverN);
    double R = index * std::sqrt((2.0 * PI) / denom);
    double cx = R + margin;
    double cy = R + margin;

    double phase = std::fmod(-PI, twoPiOverN);
    if (phase < 0)
        phase += twoPiOverN;

    std::vector<Point> verts;
    verts.reserve(N);
    for (int k = 0; k < N; ++k)
    {
        double t = phase + k * twoPiOverN;
        verts.push_back({cx + R * std::cos(t), cy + R * std::sin(t)});
    }
    return verts;
}

// STRETCHED_ROTATED_SQUARE: a diamond with horizontal:vertical extent ratio
// 3:1, implemented in shapes.cpp as two triangles.
std::vector<Point> stretchedRotatedSquareVertices(int index)
{
    double ylength = std::sqrt(2.0 * PI / 3.0) * index;
    double xlength = 3.0 * ylength;
    return {{0, ylength / 2.0}, {xlength / 2.0, 0}, {xlength, ylength / 2.0}, {xlength / 2.0, ylength}};
}

// BOWTIE: two wide triangular ends joined by a thin rectangular waist of
// thickness `width`.
std::vector<Point> bowtieVertices(int index)
{
    double width = std::sqrt(PI / 45.0) * index;
    double length = 5.0 * width;
    double hyposide = 4.0 * width;
    double height = width + 2.0 * hyposide;
    double totalLen = length + 2.0 * hyposide;
    return {
        {0, 0}, {hyposide, hyposide}, {hyposide + length, hyposide}, {totalLen, 0},
        {totalLen, height}, {hyposide + length, hyposide + width}, {hyposide, hyposide + width}, {0, height},
    };
}

// DOUBLE_BOWTIE: the union of a horizontal bowtie and a vertical bowtie
// sharing a 13a x 13a bounding box.
std::vector<Point> doubleBowtieVertices(int index)
{
    double a = std::sqrt(PI / 89.0) * index;
    auto k = [a](double n) { return n * a; };
    double x0 = k(0), x2 = k(2), x4 = k(4), x6 = k(6), x7 = k(7), x9 = k(9), x11 = k(11), x13 = k(13);
    double y0 = x0, y2 = x2, y4 = x4, y6 = x6, y7 = x7, y9 = x9, y11 = x11, y13 = x13;
    return {
        {x2, y13}, {x11, y13}, {x7, y9}, {x7, y7}, {x9, y7}, {x13, y11},
        {x13, y2}, {x9, y6}, {x7, y6}, {x7, y4}, {x11, y0}, {x2, y0},
        {x6, y4}, {x6, y6}, {x4, y6}, {x0, y2}, {x0, y11}, {x4, y7},
        {x6, y7}, {x6, y9},
    };
}

// SLOTTED_RECT_30x10: a 60a x 10a outer rectangle with a slot cut in from
// the right edge, starting 1a from the left (the shape's outer width
// constant is 60, not 30 -- inherited naming from the original enum/doc).
std::vector<Point> slottedRect30x10Vertices(int index)
{
    double a = std::sqrt(PI / 570.5) * index;
    double W = 60 * a, H = 10 * a;
    double slotX0 = 1 * a;
    double slotH = 0.5 * a;
    double slotY0 = 5 * a - slotH / 2.0;
    double slotY1 = 5 * a + slotH / 2.0;
    return {{0, 0}, {W, 0}, {W, slotY0}, {slotX0, slotY0}, {slotX0, slotY1}, {W, slotY1}, {W, H}, {0, H}};
}

// V_NOTCH_RECT: a 4a x 6a rectangle whose entire top edge is replaced by a
// V notch reaching down to apex (2a, a).
std::vector<Point> vNotchRectVertices(int index)
{
    double a = index * std::sqrt(PI / 14.0);
    double W = 4 * a, H = 6 * a;
    return {{0, 0}, {W, 0}, {W, H}, {2 * a, a}, {0, H}};
}

double perimeterOf(ShapeType2D shape, int index)
{
    switch (shape)
    {
    case ShapeType2D::CIRCLE:
        return 2.0 * PI * index; // radius == index

    case ShapeType2D::SQUARE:
    case ShapeType2D::ROTATED_SQUARE: // rotation is an isometry: same perimeter as SQUARE
        return polygonPerimeter(squareVertices(index));

    case ShapeType2D::RECTANGLE_3_1:
        return polygonPerimeter(rectangleVertices(index, 3));
    case ShapeType2D::RECTANGLE_4_1:
        return polygonPerimeter(rectangleVertices(index, 4));

    case ShapeType2D::TRIANGLE:
        return polygonPerimeter(isoscelesTriangleVertices(index, 60.0));
    case ShapeType2D::SHARP_TRIANGLE:
        return polygonPerimeter(isoscelesTriangleVertices(index, 75.0));
    case ShapeType2D::FLAT_TRIANGLE:
        return polygonPerimeter(isoscelesTriangleVertices(index, 40.0));

    case ShapeType2D::HEXAGON:
        return polygonPerimeter(regularPolygonVertices(index, 6));
    case ShapeType2D::POLYGON5:
        return polygonPerimeter(regularPolygonVertices(index, 5));
    case ShapeType2D::POLYGON8:
        return polygonPerimeter(regularPolygonVertices(index, 8));
    case ShapeType2D::POLYGON9:
        return polygonPerimeter(regularPolygonVertices(index, 9));
    case ShapeType2D::POLYGON10:
        return polygonPerimeter(regularPolygonVertices(index, 10));
    case ShapeType2D::POLYGON12:
        return polygonPerimeter(regularPolygonVertices(index, 12));
    case ShapeType2D::POLYGON15:
        return polygonPerimeter(regularPolygonVertices(index, 15));

    case ShapeType2D::STRETCHED_ROTATED_SQUARE:
        return polygonPerimeter(stretchedRotatedSquareVertices(index));

    case ShapeType2D::BOWTIE:
        return polygonPerimeter(bowtieVertices(index));
    case ShapeType2D::DOUBLE_BOWTIE:
        return polygonPerimeter(doubleBowtieVertices(index));
    case ShapeType2D::SLOTTED_RECT_30x10:
        return polygonPerimeter(slottedRect30x10Vertices(index));
    case ShapeType2D::V_NOTCH_RECT:
        return polygonPerimeter(vNotchRectVertices(index));

    case ShapeType2D::SNOWFLAKE:
    {
        double a = std::sqrt(PI / kSnowflakeShapeFactor) * index;
        return kSnowflakeUnitPerimeter * a;
    }
    }
    throw std::invalid_argument("Unknown ShapeType2D in perimeterOf");
}

void printUsage(const char *prog)
{
    std::cout <<
        "Usage: " << prog << " [flags]\n\n"
        "  --list-shapes   Print all valid --shape values and exit\n"
        "  --shape=NAME|ALL  Shape to compute, or ALL for every 2D shape (default ALL)\n"
        "  --index=N       Size index (default 100). The isoperimetric ratio P/sqrt(A)\n"
        "                  is scale-invariant -- this only affects the reported\n"
        "                  absolute Area/Perimeter columns, not Ratio\n"
        "  --outdir=DIR    Output directory (default: <repo_root>/data, resolved\n"
        "                  from this binary's location)\n";
}

} // namespace

int main(int argc, char *argv[])
{
    CliArgs args(argc, argv);

    if (args.has("list-shapes"))
    {
        for (const auto &[type, name] : kAllShapes2D)
            std::cout << name << "\n";
        return 0;
    }
    if (args.has("help") || args.has("h"))
    {
        printUsage(argv[0]);
        return 0;
    }

    std::string shapeArg = args.getStr("shape", "ALL");
    int index = args.getInt("index", 100);
    std::string outdir = args.getStr("outdir", defaultDataDir(argv[0]));
    fs::create_directories(outdir);

    std::vector<std::pair<ShapeType2D, std::string>> targets;
    if (shapeArg == "ALL")
    {
        targets = kAllShapes2D;
    }
    else
    {
        ShapeType2D shape;
        if (!shapeFromName(shapeArg, shape))
        {
            std::cerr << "Unknown shape '" << shapeArg << "'. Run with --list-shapes to see valid names.\n";
            return 1;
        }
        targets.push_back({shape, shapeArg});
    }

    const std::string outFile = outdir + "/isoperimetric_2d.csv";
    std::ofstream out(outFile);
    if (!out)
    {
        std::cerr << "Failed to open output file: " << outFile << "\n";
        return 1;
    }

    const double circleRatio = 2.0 * std::sqrt(PI); // isoperimetric minimum, attained only by the circle

    out << "Shape,Index,Area,Perimeter,Ratio,RatioOverCircleMinimum\n";
    for (const auto &[shape, name] : targets)
    {
        double area = PI * static_cast<double>(index) * static_cast<double>(index);
        double perimeter = perimeterOf(shape, index);
        double ratio = perimeter / std::sqrt(area);

        out << name << "," << index << "," << area << "," << perimeter << ","
            << ratio << "," << (ratio / circleRatio) << "\n";
        std::cout << name << ": Perimeter=" << perimeter << " Ratio=" << ratio
                   << " (circle minimum=" << circleRatio << ")\n";
    }

    std::cout << "Wrote " << targets.size() << " row(s) to " << outFile << "\n";
    return 0;
}
