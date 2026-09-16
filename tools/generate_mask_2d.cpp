// Dumps the interior lattice points of a 2D shape to a text file, for
// visualization/debugging. Previously a fixed shape+index were hard-coded
// in the source; now both are CLI flags (see the root Makefile's
// `mask-2d` target).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../src/common/cli_args.h"
#include "../src/convex_2d/shapes.h"

int main(int argc, char *argv[])
{
    CliArgs args(argc, argv);

    if (args.has("list-shapes"))
    {
        for (const auto &[type, name] : kAllShapes2D)
            std::cout << name << "\n";
        return 0;
    }

    ShapeType2D shape;
    std::string shapeStr = args.getStr("shape", "SQUARE");
    if (!shapeFromName(shapeStr, shape))
    {
        std::cerr << "Unknown shape '" << shapeStr << "'. Run with --list-shapes to see valid names.\n";
        return 1;
    }

    int index = args.getInt("index", 50);
    bool shellOnly = args.getBool("shell", false);
    std::string outdir = args.getStr("outdir", defaultDataDir(argv[0]));
    std::filesystem::create_directories(outdir);

    bool **mask = nullptr;
    int xLen, yLen;
    getPointsInShape(index, shape, &mask, &xLen, &yLen);

    std::ostringstream fname;
    fname << outdir << "/mask_" << shapeStr << "_" << index << (shellOnly ? "_shell" : "") << ".txt";
    std::ofstream out(fname.str());
    if (!out)
    {
        std::cerr << "Failed to open output file: " << fname.str() << "\n";
        freeMask2D(mask, xLen);
        return 1;
    }

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    int count = 0;
    for (int x = 0; x < xLen; ++x)
    {
        for (int y = 0; y < yLen; ++y)
        {
            if (!mask[x][y])
                continue;

            if (shellOnly)
            {
                bool onShell = false;
                for (int d = 0; d < 4; ++d)
                {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (nx < 0 || nx >= xLen || ny < 0 || ny >= yLen || !mask[nx][ny])
                    {
                        onShell = true;
                        break;
                    }
                }
                if (!onShell)
                    continue;
            }

            out << x << " " << y << "\n";
            ++count;
        }
    }

    freeMask2D(mask, xLen);
    std::cout << "Wrote " << count << " points to " << fname.str() << "\n";
    return 0;
}
