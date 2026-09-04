// Dumps the interior lattice points of a 3D shape to a text file, for
// visualization/debugging. Previously a fixed shape+index were hard-coded
// in the source; now both are CLI flags (see the root Makefile's
// `mask-3d` target).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "../src/common/cli_args.h"
#include "../src/convex_3d/shapes.h"

int main(int argc, char *argv[])
{
    CliArgs args(argc, argv);

    if (args.has("list-shapes"))
    {
        for (const auto &[type, name] : kAllShapes3D)
            std::cout << name << "\n";
        return 0;
    }

    ShapeType3D shape;
    std::string shapeStr = args.getStr("shape", "SPHERE");
    if (!shapeFromName(shapeStr, shape))
    {
        std::cerr << "Unknown shape '" << shapeStr << "'. Run with --list-shapes to see valid names.\n";
        return 1;
    }

    int index = args.getInt("index", 20);
    bool shellOnly = args.getBool("shell", false);
    std::string outdir = args.getStr("outdir", shellOnly ? "visualization/shell_visual" : "visualization");
    std::filesystem::create_directories(outdir);

    int xMin, xMax, yMin, yMax, zMin, zMax;
    boundingBox(index, shape, xMin, xMax, yMin, yMax, zMin, zMax);

    const int sx = xMax - xMin;
    const int sy = yMax - yMin;
    const int sz = zMax - zMin;

    std::vector<char> mask(static_cast<std::size_t>(sx) * sy * sz, 0);
    auto at = [&](int i, int j, int k) -> char & { return mask[(static_cast<std::size_t>(i) * sy + j) * sz + k]; };

    for (int i = 0; i < sx; ++i)
        for (int j = 0; j < sy; ++j)
            for (int k = 0; k < sz; ++k)
                at(i, j, k) = inBoundary(xMin + i, yMin + j, zMin + k, index, shape) ? 1 : 0;

    std::ostringstream fname;
    fname << outdir << "/mask_" << shapeStr << "_" << index << (shellOnly ? "_shell" : "") << ".txt";
    std::ofstream out(fname.str());
    if (!out)
    {
        std::cerr << "Failed to open output file: " << fname.str() << "\n";
        return 1;
    }

    const int dx[6] = {1, -1, 0, 0, 0, 0};
    const int dy[6] = {0, 0, 1, -1, 0, 0};
    const int dz[6] = {0, 0, 0, 0, 1, -1};

    int count = 0;
    for (int i = 0; i < sx; ++i)
    {
        for (int j = 0; j < sy; ++j)
        {
            for (int k = 0; k < sz; ++k)
            {
                if (!at(i, j, k))
                    continue;

                if (shellOnly)
                {
                    bool onShell = false;
                    for (int d = 0; d < 6; ++d)
                    {
                        int ni = i + dx[d], nj = j + dy[d], nk = k + dz[d];
                        if (ni < 0 || ni >= sx || nj < 0 || nj >= sy || nk < 0 || nk >= sz || !at(ni, nj, nk))
                        {
                            onShell = true;
                            break;
                        }
                    }
                    if (!onShell)
                        continue;
                }

                out << (xMin + i) << " " << (yMin + j) << " " << (zMin + k) << "\n";
                ++count;
            }
        }
    }

    std::cout << "Wrote " << count << " points to " << fname.str() << "\n";
    return 0;
}
