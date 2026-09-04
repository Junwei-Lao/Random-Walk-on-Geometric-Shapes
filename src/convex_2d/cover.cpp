#include "cover.h"

#include <cmath>

DiskCover2D::DiskCover2D(double radius, ShapeType2D shape, int index)
    : r_(radius), r2_(radius * radius), shape_(shape), index_(index)
{
}

void DiskCover2D::coverPoint(int vx, int vy)
{
    const int xmin = static_cast<int>(std::floor(vx - r_));
    const int xmax = static_cast<int>(std::ceil(vx + r_));
    const int ymin = static_cast<int>(std::floor(vy - r_));
    const int ymax = static_cast<int>(std::ceil(vy + r_));

    for (int x = xmin; x <= xmax; ++x)
    {
        for (int y = ymin; y <= ymax; ++y)
        {
            const double dx = static_cast<double>(x) - vx;
            const double dy = static_cast<double>(y) - vy;
            if (dx * dx + dy * dy > r2_)
                continue;
            if (!inBoundary(x, y, index_, shape_))
                continue;
            covered_.insert(LatticePoint2D(x, y));
        }
    }
}

double DiskCover2D::coverageFraction(long long totalShapePoints) const
{
    if (totalShapePoints <= 0)
        return 0.0;
    return static_cast<double>(covered_.size()) / static_cast<double>(totalShapePoints);
}
