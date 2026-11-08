#include "cover.h"

#include <cmath>

DiskCover2D::DiskCover2D(double radius, ShapeType2D shape, int index, bool recordCenters)
    : r_(radius), r2_(radius * radius), shape_(shape), index_(index), recordCenters_(recordCenters)
{
}

bool DiskCover2D::tryPlaceDisk(int vx, int vy, std::mt19937_64 &rng)
{
    const LatticePoint2D v(vx, vy);
    if (covered_.count(v))
        return false;

    // Uniform point in the disk of radius R around v: the sqrt on the
    // radial draw makes the density uniform in area, not in radius.
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const double rho = r_ * std::sqrt(unit(rng));
    const double theta = 2.0 * PI * unit(rng);
    const double cx = vx + rho * std::cos(theta);
    const double cy = vy + rho * std::sin(theta);

    const int xmin = static_cast<int>(std::floor(cx - r_));
    const int xmax = static_cast<int>(std::ceil(cx + r_));
    const int ymin = static_cast<int>(std::floor(cy - r_));
    const int ymax = static_cast<int>(std::ceil(cy + r_));

    for (int x = xmin; x <= xmax; ++x)
    {
        for (int y = ymin; y <= ymax; ++y)
        {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            if (dx * dx + dy * dy > r2_)
                continue;
            if (!inBoundary(x, y, index_, shape_))
                continue;
            covered_.insert(LatticePoint2D(x, y));
        }
    }

    // |c - v| <= R guarantees v is covered, but rounding in cos/sin can put
    // it a hair outside when rho lands at R; insert it explicitly.
    covered_.insert(v);

    ++diskCount_;
    if (recordCenters_)
        centers_.push_back({cx, cy});
    return true;
}
