#include "shapes.h"

#include <array>
#include <cmath>
#include <queue>
#include <stdexcept>

const double PI = std::acos(-1);

const std::vector<std::pair<ShapeType2D, std::string>> kAllShapes2D = {
    {ShapeType2D::CIRCLE, "CIRCLE"},
    {ShapeType2D::RECTANGLE_3_1, "RECTANGLE_3_1"},
    {ShapeType2D::RECTANGLE_4_1, "RECTANGLE_4_1"},
    {ShapeType2D::SQUARE, "SQUARE"},
    {ShapeType2D::TRIANGLE, "TRIANGLE"},
    {ShapeType2D::SHARP_TRIANGLE, "SHARP_TRIANGLE"},
    {ShapeType2D::FLAT_TRIANGLE, "FLAT_TRIANGLE"},
    {ShapeType2D::HEXAGON, "HEXAGON"},
    {ShapeType2D::POLYGON5, "POLYGON5"},
    {ShapeType2D::POLYGON8, "POLYGON8"},
    {ShapeType2D::POLYGON9, "POLYGON9"},
    {ShapeType2D::POLYGON10, "POLYGON10"},
    {ShapeType2D::POLYGON12, "POLYGON12"},
    {ShapeType2D::POLYGON15, "POLYGON15"},
    {ShapeType2D::ROTATED_SQUARE, "ROTATED_SQUARE"},
    {ShapeType2D::STRETCHED_ROTATED_SQUARE, "STRETCHED_ROTATED_SQUARE"},
    {ShapeType2D::BOWTIE, "BOWTIE"},
    {ShapeType2D::DOUBLE_BOWTIE, "DOUBLE_BOWTIE"},
    {ShapeType2D::SLOTTED_RECT_30x10, "SLOTTED_RECT_30x10"},
    {ShapeType2D::V_NOTCH_RECT, "V_NOTCH_RECT"},
    {ShapeType2D::SNOWFLAKE, "SNOWFLAKE"},
};

std::string shapeName(ShapeType2D shape)
{
    for (const auto &[type, name] : kAllShapes2D)
        if (type == shape)
            return name;
    throw std::invalid_argument("Unknown ShapeType2D");
}

bool shapeFromName(const std::string &name, ShapeType2D &out)
{
    for (const auto &[type, shapeNameStr] : kAllShapes2D)
        if (shapeNameStr == name)
        {
            out = type;
            return true;
        }
    return false;
}

namespace
{

constexpr double MARGIN = 20.0;

// SNOWFLAKE's area-normalization constant: with a = sqrt(PI / kSnowflakeShapeFactor)
// * index, the fractal's true (rectangle-union) area equals PI * index^2 only if
// kSnowflakeShapeFactor equals the union area of the a=1 fractal itself. That was
// previously hard-coded as 140.0, an apparent estimate that was never actually
// checked against the geometry as coded (hubThk=1, hubArm=2, connectorLen=1.6,
// depth=3, scaleLen=0.55, scaleThk=0.50, endArm0=1.9, endThk0=0.55, all below) --
// the real value, computed exactly in rational arithmetic by summing the union of
// all 239 rectangles at a=1, is 4075589/80000 = 50.9448625. Using 140 made every
// SNOWFLAKE run simulate a shape with only ~36% of its intended area (confirmed
// empirically: the walker's own point count matched this ratio at every index
// from 10 to 100, a stable multiplicative error rather than a discretization one).
constexpr double kSnowflakeShapeFactor = 50.9448625;

double regularPolygonCircumradius(int index, int N)
{
    return index * std::sqrt((2.0 * PI) / (N * std::sin(2.0 * PI / N)));
}

bool inRegularPolygon(int x, int y, int index, int N)
{
    double R = regularPolygonCircumradius(index, N);
    double a = R * std::cos(PI / N); // apothem

    double cx = R + MARGIN;
    double cy = R + MARGIN;
    double dx = x - cx;
    double dy = y - cy;

    double r = std::sqrt(dx * dx + dy * dy);
    if (r > R)
        return false; // quick reject

    double theta = std::atan2(dy, dx);
    double sector = 2.0 * PI / N;
    theta = std::fmod(theta + PI, sector);
    if (theta < 0)
        theta += sector;
    theta -= sector / 2.0;

    return r * std::cos(theta) <= a;
}

double slottedRect30x10_a(int index)
{
    // Outer: 60a x 10a, slot: thickness 0.5a, length 59a starting at x = 1a.
    constexpr double WU = 60.0;
    constexpr double HU = 10.0;
    constexpr double slotLenU = 59.0;
    constexpr double slotThkU = 0.5;

    const double outerAreaU = WU * HU;
    const double slotAreaU = slotLenU * slotThkU;
    const double areaU = outerAreaU - slotAreaU;
    return static_cast<double>(index) * std::sqrt(PI / areaU);
}

void slottedRect30x10Dims(int index, double &W, double &H,
                           double &sx0, double &sx1, double &sy0, double &sy1)
{
    constexpr double WU = 60.0;
    constexpr double HU = 10.0;
    constexpr double slotLeftGapU = 1.0;
    constexpr double slotLenU = 59.0;
    constexpr double slotThkU = 0.5;

    const double a = slottedRect30x10_a(index);
    W = WU * a;
    H = HU * a;
    sx0 = slotLeftGapU * a;
    sx1 = (slotLeftGapU + slotLenU) * a;
    const double t = slotThkU * a;
    const double cy = H / 2.0;
    sy0 = cy - t / 2.0;
    sy1 = cy + t / 2.0;
}

struct BBox2D
{
    double xLen;
    double yLen;
};

BBox2D computeBBox(ShapeType2D shape, int index)
{
    switch (shape)
    {
    case ShapeType2D::CIRCLE:
        return {2.0 * index, 2.0 * index};

    case ShapeType2D::RECTANGLE_3_1:
    {
        double h = std::sqrt(PI / 3.0) * index;
        return {3.0 * h, h};
    }
    case ShapeType2D::RECTANGLE_4_1:
    {
        double h = std::sqrt(PI / 4.0) * index;
        return {4.0 * h, h};
    }
    case ShapeType2D::SQUARE:
    {
        double s = std::sqrt(PI) * index;
        return {s, s};
    }
    case ShapeType2D::TRIANGLE:
    {
        double s = std::sqrt(4.0 * PI / std::sqrt(3.0)) * index;
        return {s, std::sqrt(3.0) * s / 2.0};
    }
    case ShapeType2D::SHARP_TRIANGLE:
    {
        double s = std::sqrt(4.0 * PI / std::tan(75.0 * PI / 180.0)) * index;
        return {s, std::tan(75.0 * PI / 180.0) * s / 2.0};
    }
    case ShapeType2D::FLAT_TRIANGLE:
    {
        double s = std::sqrt(4.0 * PI / std::tan(40.0 * PI / 180.0)) * index;
        return {s, std::tan(40.0 * PI / 180.0) * s / 2.0};
    }
    case ShapeType2D::SLOTTED_RECT_30x10:
    {
        double W, H, sx0, sx1, sy0, sy1;
        slottedRect30x10Dims(index, W, H, sx0, sx1, sy0, sy1);
        return {W, H};
    }
    case ShapeType2D::HEXAGON:
    {
        double R = regularPolygonCircumradius(index, 6);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON5:
    {
        double R = regularPolygonCircumradius(index, 5);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON8:
    {
        double R = regularPolygonCircumradius(index, 8);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON9:
    {
        double R = regularPolygonCircumradius(index, 9);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON10:
    {
        double R = regularPolygonCircumradius(index, 10);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON12:
    {
        double R = regularPolygonCircumradius(index, 12);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::POLYGON15:
    {
        double R = regularPolygonCircumradius(index, 15);
        return {2.0 * R + 2.0 * MARGIN, 2.0 * R + 2.0 * MARGIN};
    }
    case ShapeType2D::ROTATED_SQUARE:
    {
        // inBoundary's rotation is centered at (0, s), not (0, 0), so the
        // shape's actual y-range is [s*(1 - cos45), s*(1 + cos45)] -- not
        // [0, s*sqrt(2)]. Since the rasterization loop always starts at
        // y = 0, yLen must reach the true upper bound s*(1 + cos45) or the
        // top of the square gets silently cut off (this previously
        // truncated ~8% of the shape's area).
        double s = std::sqrt(PI) * index;
        double yMax = s * (1.0 + std::cos(45.0 * PI / 180.0));
        return {s * std::sqrt(2.0), yMax};
    }
    case ShapeType2D::STRETCHED_ROTATED_SQUARE:
    {
        double h = std::sqrt(2.0 * PI / 3.0) * index;
        return {3.0 * h, h};
    }
    case ShapeType2D::BOWTIE:
    {
        double w = std::sqrt(PI / 45.0) * index;
        double h = w + 2.0 * (4.0 * w);
        double l = 5.0 * w + 2.0 * (4.0 * w);
        return {l, h};
    }
    case ShapeType2D::DOUBLE_BOWTIE:
    {
        double a = std::sqrt(PI / 89.0) * index;
        double Long = 13.0 * a;
        return {Long, Long};
    }
    case ShapeType2D::V_NOTCH_RECT:
    {
        double a = static_cast<double>(index) * std::sqrt(PI / 14.0);
        return {4.0 * a, 6.0 * a};
    }
    case ShapeType2D::SNOWFLAKE:
    {
        double a = std::sqrt(PI / kSnowflakeShapeFactor) * index;
        return {80.0 * a, 80.0 * a};
    }
    }
    throw std::invalid_argument("Unknown ShapeType2D in computeBBox");
}

} // namespace

bool inBoundary(int x, int y, int index, ShapeType2D shape)
{
    double c;

    switch (shape)
    {
    case ShapeType2D::CIRCLE:
        return ((x - index) * (x - index) + (y - index) * (y - index)) < index * index;

    case ShapeType2D::RECTANGLE_3_1:
        c = std::sqrt(PI / 3.0);
        return (x >= 0 && x <= 3 * c * index && y >= 0 && y <= c * index);

    case ShapeType2D::RECTANGLE_4_1:
        c = std::sqrt(PI / 4.0);
        return (x >= 0 && x <= 4 * c * index && y >= 0 && y <= c * index);

    case ShapeType2D::SQUARE:
        c = std::sqrt(PI);
        return (x >= 0 && x <= c * index && y >= 0 && y <= c * index);

    case ShapeType2D::TRIANGLE:
        c = std::sqrt(4.0 * PI / std::sqrt(3.0));
        if (x >= 0 && x < c * index / 2.0)
            return (y >= 0 && y <= std::sqrt(3.0) * x);
        if (x >= c * index / 2.0 && x <= c * index)
            return (y >= 0 && y <= -std::sqrt(3.0) * x + std::sqrt(3.0) * c * index);
        return false;

    case ShapeType2D::SHARP_TRIANGLE:
        c = std::sqrt(4.0 * PI / std::tan(75.0 * PI / 180.0));
        if (x >= 0 && x < c * index / 2.0)
            return (y >= 0 && y <= std::tan(75.0 * PI / 180.0) * x);
        if (x >= c * index / 2.0 && x <= c * index)
            return (y >= 0 && y <= c * index / std::tan(15.0 * PI / 180.0) - std::tan(75.0 * PI / 180.0) * x);
        return false;

    case ShapeType2D::FLAT_TRIANGLE:
        c = std::sqrt(4.0 * PI / std::tan(40.0 * PI / 180.0));
        if (x >= 0 && x < c * index / 2.0)
            return (y >= 0 && y <= std::tan(40.0 * PI / 180.0) * x);
        if (x >= c * index / 2.0 && x <= c * index)
            return (y >= 0 && y <= c * index * std::tan(40.0 * PI / 180.0) - std::tan(40.0 * PI / 180.0) * x);
        return false;

    case ShapeType2D::SLOTTED_RECT_30x10:
    {
        double W, H, sx0, sx1, sy0, sy1;
        slottedRect30x10Dims(index, W, H, sx0, sx1, sy0, sy1);
        if (!(x >= 0 && x <= W && y >= 0 && y <= H))
            return false;
        const bool inSlot = (x >= sx0 && x <= sx1 && y >= sy0 && y <= sy1);
        return !inSlot;
    }

    case ShapeType2D::HEXAGON:
        return inRegularPolygon(x, y, index, 6);
    case ShapeType2D::POLYGON5:
        return inRegularPolygon(x, y, index, 5);
    case ShapeType2D::POLYGON8:
        return inRegularPolygon(x, y, index, 8);
    case ShapeType2D::POLYGON9:
        return inRegularPolygon(x, y, index, 9);
    case ShapeType2D::POLYGON10:
        return inRegularPolygon(x, y, index, 10);
    case ShapeType2D::POLYGON12:
        return inRegularPolygon(x, y, index, 12);
    case ShapeType2D::POLYGON15:
        return inRegularPolygon(x, y, index, 15);

    case ShapeType2D::ROTATED_SQUARE:
    {
        // 45 degree rotated square; rotation center at (0, side).
        c = std::sqrt(PI);
        double side = c * index;
        double rx = x * std::cos(-45.0 * PI / 180.0) - (y - side) * std::sin(-45.0 * PI / 180.0);
        double ry = x * std::sin(-45.0 * PI / 180.0) + (y - side) * std::cos(-45.0 * PI / 180.0) + side;
        return (rx >= 0 && rx <= side && ry >= 0 && ry <= side);
    }

    case ShapeType2D::STRETCHED_ROTATED_SQUARE:
    {
        // Rotated square stretched to a 3:1 horizontal:vertical extent ratio,
        // implemented as two triangles for efficiency.
        c = std::sqrt(2.0 * PI / 3.0);
        double ylength = c * index;
        double xlength = 3.0 * ylength;
        if (x >= 0 && x <= xlength / 2.0)
            return (y >= (ylength / 2.0 - x / 3.0)) && (y <= (ylength / 2.0 + x / 3.0));
        if (x > xlength / 2.0 && x <= xlength)
            return (y >= (ylength / 2.0) - ((xlength - x) / 3.0)) && (y <= (ylength / 2.0) + ((xlength - x) / 3.0));
        return false;
    }

    case ShapeType2D::BOWTIE:
    {
        double width = std::sqrt(PI / 45.0) * index;
        double length = 5.0 * width;
        double hyposide = 4.0 * width;
        double Height = width + 2.0 * hyposide;
        double Long = length + 2.0 * hyposide;
        if (x >= 0 && x <= hyposide)
            return (y >= x && y <= Height - x);
        if (x > hyposide && x <= hyposide + length)
            return (y >= hyposide && y <= hyposide + width);
        if (x > hyposide + length && x <= Long)
            return (y >= Long - x && y <= x + Height - Long);
        return false;
    }

    case ShapeType2D::V_NOTCH_RECT:
    {
        // Area = 4a*6a - (1/2)*4a*5a = 14a^2 = PI*index^2
        const double a = static_cast<double>(index) * std::sqrt(PI / 14.0);
        const double W = 4.0 * a;
        const double H = 6.0 * a;
        if (x < 0 || x > W || y < 0 || y > H)
            return false;

        const double xApex = 2.0 * a;
        if (x <= xApex)
            return (y <= 6.0 * a - 2.5 * x);
        return (y <= 2.5 * x - 4.0 * a);
    }

    case ShapeType2D::DOUBLE_BOWTIE:
    {
        // Union of a horizontal bowtie (shifted up by 2a) and a vertical
        // bowtie (axes swapped, shifted right by 2a). Bounding box: 13a x 13a.
        const double a = std::sqrt(PI / 89.0) * index;
        const double w = a;
        const double h = 4.0 * a;
        const double L = 5.0 * a;
        const double Height = w + 2.0 * h;
        const double Long = L + 2.0 * h;

        auto inBowtieH = [&](double px, double py) -> bool
        {
            if (px < 0.0 || px > Long || py < 0.0 || py > Height)
                return false;
            if (px <= h)
                return (py >= px && py <= Height - px);
            if (px <= h + L)
                return (py >= h && py <= h + w);
            return (py >= (Long - px) && py <= (px + Height - Long));
        };

        const bool inHB = inBowtieH(static_cast<double>(x), static_cast<double>(y) - 2.0 * a);
        const bool inVB = inBowtieH(static_cast<double>(y), static_cast<double>(x) - 2.0 * a);
        return inHB || inVB;
    }

    case ShapeType2D::SNOWFLAKE:
    {
        // Two hub crosses joined by a connector bar; each hub's three outer
        // endpoints grow a recursive "+"-fractal that only branches away
        // from its parent cross center (never folding back over its own
        // branch).
        const double a = std::sqrt(PI / kSnowflakeShapeFactor) * index;

        const double hubThk = 1.0 * a;
        const double hubArm = 2.0 * a;
        const double connectorLen = 1.6 * a;

        const int depth = 3;
        const double scaleLen = 0.55;
        const double scaleThk = 0.50;

        const double endArm0 = 1.9 * a;
        const double endThk0 = 0.55 * a;

        const double W = 80.0 * a;
        const double cy = 40.0 * a;
        const double cxL = 40.0 * a - (connectorLen / 2.0 + hubArm);
        const double cxR = 40.0 * a + (connectorLen / 2.0 + hubArm);

        const double X = static_cast<double>(x);
        const double Y = static_cast<double>(y);
        if (X < 0.0 || X > W || Y < 0.0 || Y > W)
            return false;

        auto inRect = [&](double px, double py, double x0, double x1, double y0, double y1) -> bool
        {
            return (px >= x0 && px <= x1 && py >= y0 && py <= y1);
        };

        auto inCrossFull = [&](double px, double py, double ccx, double ccy, double arm, double thk) -> bool
        {
            bool ok = inRect(px, py, ccx - arm, ccx + arm, ccy - thk / 2.0, ccy + thk / 2.0);
            ok = ok || inRect(px, py, ccx - thk / 2.0, ccx + thk / 2.0, ccy - arm, ccy + arm);
            return ok;
        };

        auto inCrossOutward = [&](double px, double py, double ccx, double ccy, double arm, double thk,
                                   double px0, double py0) -> bool
        {
            const double d0 = (ccx - px0) * (ccx - px0) + (ccy - py0) * (ccy - py0);
            bool ok = false;

            auto outwardEndpoint = [&](double ex, double ey) -> bool
            {
                const double d1 = (ex - px0) * (ex - px0) + (ey - py0) * (ey - py0);
                return d1 > d0 + 1e-9;
            };

            if (outwardEndpoint(ccx + arm, ccy))
                ok = ok || inRect(px, py, ccx, ccx + arm, ccy - thk / 2.0, ccy + thk / 2.0);
            if (outwardEndpoint(ccx - arm, ccy))
                ok = ok || inRect(px, py, ccx - arm, ccx, ccy - thk / 2.0, ccy + thk / 2.0);
            if (outwardEndpoint(ccx, ccy + arm))
                ok = ok || inRect(px, py, ccx - thk / 2.0, ccx + thk / 2.0, ccy, ccy + arm);
            if (outwardEndpoint(ccx, ccy - arm))
                ok = ok || inRect(px, py, ccx - thk / 2.0, ccx + thk / 2.0, ccy - arm, ccy);

            return ok;
        };

        auto inCrossFractalOutward = [&](auto &&self, double px, double py, double ccx, double ccy,
                                          double arm, double thk, double parentx, double parenty, int d) -> bool
        {
            if (arm <= 0.0 || thk <= 0.0)
                return false;

            bool ok = inCrossOutward(px, py, ccx, ccy, arm, thk, parentx, parenty);
            if (d <= 1)
                return ok;

            const double d0 = (ccx - parentx) * (ccx - parentx) + (ccy - parenty) * (ccy - parenty);

            auto tryRecurse = [&](double nx, double ny)
            {
                const double d1 = (nx - parentx) * (nx - parentx) + (ny - parenty) * (ny - parenty);
                if (d1 > d0 + 1e-9)
                {
                    const double nextArm = arm * scaleLen;
                    const double nextThk = thk * scaleThk;
                    ok = ok || self(self, px, py, nx, ny, nextArm, nextThk, ccx, ccy, d - 1);
                }
            };

            tryRecurse(ccx + arm, ccy);
            tryRecurse(ccx - arm, ccy);
            tryRecurse(ccx, ccy + arm);
            tryRecurse(ccx, ccy - arm);

            return ok;
        };

        bool inside = false;
        inside = inside || inRect(X, Y, cxL + hubArm, cxR - hubArm, cy - hubThk / 2.0, cy + hubThk / 2.0);
        inside = inside || inCrossFull(X, Y, cxL, cy, hubArm, hubThk);
        inside = inside || inCrossFull(X, Y, cxR, cy, hubArm, hubThk);

        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxL - hubArm, cy, endArm0, endThk0, cxL, cy, depth);
        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxL, cy + hubArm, endArm0, endThk0, cxL, cy, depth);
        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxL, cy - hubArm, endArm0, endThk0, cxL, cy, depth);
        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxR + hubArm, cy, endArm0, endThk0, cxR, cy, depth);
        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxR, cy + hubArm, endArm0, endThk0, cxR, cy, depth);
        inside = inside || inCrossFractalOutward(inCrossFractalOutward, X, Y, cxR, cy - hubArm, endArm0, endThk0, cxR, cy, depth);

        return inside;
    }
    }

    throw std::invalid_argument("Unknown ShapeType2D in inBoundary");
}

int getPointsInShape(int index, ShapeType2D shape, bool ***mask, int *xLenOut, int *yLenOut)
{
    BBox2D bbox = computeBBox(shape, index);

    const int xLen = static_cast<int>(std::ceil(bbox.xLen)) + 1;
    const int yLen = static_cast<int>(std::ceil(bbox.yLen)) + 1;

    if (mask)
    {
        if (xLenOut)
            *xLenOut = xLen;
        if (yLenOut)
            *yLenOut = yLen;
        if (*mask != nullptr)
            throw std::invalid_argument("getPointsInShape requires *mask == nullptr");

        *mask = new bool *[xLen];
        for (int i = 0; i < xLen; ++i)
        {
            (*mask)[i] = new bool[yLen];
            for (int j = 0; j < yLen; ++j)
                (*mask)[i][j] = false;
        }
    }

    int points = 0;
    for (int i = 0; i < xLen; ++i)
    {
        for (int j = 0; j < yLen; ++j)
        {
            if (inBoundary(i, j, index, shape))
            {
                ++points;
                if (mask)
                    (*mask)[i][j] = true;
            }
        }
    }
    return points;
}

void freeMask2D(bool **mask, int xLen)
{
    if (!mask)
        return;
    for (int i = 0; i < xLen; ++i)
        delete[] mask[i];
    delete[] mask;
}

void initialize(LatticePoint2D &start, ShapeType2D shape, int index, std::mt19937_64 &rng)
{
    BBox2D bbox = computeBBox(shape, index);
    std::uniform_int_distribution<> distX(0, static_cast<int>(std::ceil(bbox.xLen)));
    std::uniform_int_distribution<> distY(0, static_cast<int>(std::ceil(bbox.yLen)));

    do
    {
        start.x = distX(rng);
        start.y = distY(rng);
    } while (!inBoundary(start.x, start.y, index, shape));
}

int distanceToShape(LatticePoint2D from, int index, ShapeType2D shape)
{
    if (inBoundary(from.x, from.y, index, shape))
        return 0;

    static const std::array<LatticePoint2D, 4> dirs{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};

    std::queue<std::pair<LatticePoint2D, int>> bfsQueue;
    PointSet2D visited;
    visited.insert(from);
    bfsQueue.push({from, 0});

    while (!bfsQueue.empty())
    {
        auto [cur, dist] = bfsQueue.front();
        bfsQueue.pop();
        for (const auto &d : dirs)
        {
            LatticePoint2D next(cur.x + d.x, cur.y + d.y);
            if (inBoundary(next.x, next.y, index, shape))
                return dist + 1;
            if (visited.insert(next).second)
                bfsQueue.push({next, dist + 1});
        }
    }

    throw std::runtime_error("BFS exhausted all options without finding the shape boundary");
}

int squareLatticeModulus(int index)
{
    double s = std::sqrt(PI) * index;
    return static_cast<int>(std::floor(s)) + 1;
}

namespace
{

void keepLargestComponent(bool **mask, int xLen, int yLen, int &keptCount)
{
    static const std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

    std::vector<std::vector<bool>> seen(xLen, std::vector<bool>(yLen, false));
    std::vector<LatticePoint2D> best;
    std::vector<LatticePoint2D> current;

    for (int i = 0; i < xLen; ++i)
    {
        for (int j = 0; j < yLen; ++j)
        {
            if (!mask[i][j] || seen[i][j])
                continue;

            current.clear();
            std::queue<std::pair<int, int>> q;
            q.push({i, j});
            seen[i][j] = true;
            while (!q.empty())
            {
                auto [x, y] = q.front();
                q.pop();
                current.emplace_back(x, y);
                for (const auto &d : dirs)
                {
                    int nx = x + d.first, ny = y + d.second;
                    if (nx >= 0 && nx < xLen && ny >= 0 && ny < yLen && mask[nx][ny] && !seen[nx][ny])
                    {
                        seen[nx][ny] = true;
                        q.push({nx, ny});
                    }
                }
            }

            if (current.size() > best.size())
                best = current;
        }
    }

    for (int i = 0; i < xLen; ++i)
        for (int j = 0; j < yLen; ++j)
            mask[i][j] = false;
    for (const auto &p : best)
        mask[p.x][p.y] = true;

    keptCount = static_cast<int>(best.size());
}

} // namespace

ConnectedShape2D buildConnectedShape(int index, ShapeType2D shape)
{
    ConnectedShape2D cs;
    getPointsInShape(index, shape, &cs.mask, &cs.xLen, &cs.yLen);
    keepLargestComponent(cs.mask, cs.xLen, cs.yLen, cs.pointCount);
    return cs;
}

void freeConnectedShape(ConnectedShape2D &cs)
{
    freeMask2D(cs.mask, cs.xLen);
    cs.mask = nullptr;
    cs.pointCount = 0;
}

LatticePoint2D sampleUniform(const ConnectedShape2D &cs, std::mt19937_64 &rng)
{
    if (cs.pointCount <= 0)
        throw std::runtime_error("sampleUniform: connected shape has no points");

    std::uniform_int_distribution<int> pick(0, cs.pointCount - 1);
    int target = pick(rng);
    int seen = 0;
    for (int i = 0; i < cs.xLen; ++i)
        for (int j = 0; j < cs.yLen; ++j)
            if (cs.mask[i][j])
            {
                if (seen == target)
                    return LatticePoint2D(i, j);
                ++seen;
            }

    throw std::runtime_error("sampleUniform: point count mismatch");
}
