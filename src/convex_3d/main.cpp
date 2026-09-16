// CLI entry point for the 3D analytic-shape random walk engine.
// See the root Makefile's `run-3d` target for the canonical invocation.
// Run with --list-shapes to print all valid --shape values.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "../common/cli_args.h"
#include "shapes.h"
#include "walk.h"

namespace fs = std::filesystem;

namespace
{

bool summaryRowExists(const std::string &filename, int index)
{
    std::ifstream in(filename);
    if (!in)
        return false;

    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line))
    {
        std::stringstream ss(line);
        std::string cell;
        std::getline(ss, cell, ',');
        if (cell.empty())
            continue;
        try
        {
            if (std::stoi(cell) == index)
                return true;
        }
        catch (...)
        {
            continue;
        }
    }
    return false;
}

void printUsage(const char *prog)
{
    std::cout <<
        "Usage: " << prog << " [flags]\n\n"
        "  --list-shapes    Print all valid --shape values and exit\n"
        "  --shape=NAME     Shape to simulate (default SPHERE)\n"
        "  --index-min=N    Smallest size index (default 10)\n"
        "  --index-max=N    Largest size index, inclusive (default 70)\n"
        "  --index-step=N   Step between indices (default 10)\n"
        "  --runs=N         Independent runs per index (default 1000)\n"
        "  --threads=N      Worker threads (default: hardware concurrency)\n"
        "  --outdir=DIR     Output directory for all CSVs and paths\n"
        "                   (default: <repo_root>/data, resolved from this binary's location)\n"
        "  --seed=N         Deterministic RNG seed (default: random)\n"
        "  --record-path=0|1 Dump one sample walker path per index (default 1)\n";
}

} // namespace

int main(int argc, char *argv[])
{
    CliArgs args(argc, argv);

    if (args.has("list-shapes"))
    {
        for (const auto &[type, name] : kAllShapes3D)
            std::cout << name << "\n";
        return 0;
    }
    if (args.has("help") || args.has("h"))
    {
        printUsage(argv[0]);
        return 0;
    }

    ShapeType3D shape;
    std::string shapeStr = args.getStr("shape", "SPHERE");
    if (!shapeFromName(shapeStr, shape))
    {
        std::cerr << "Unknown shape '" << shapeStr << "'. Run with --list-shapes to see valid names.\n";
        return 1;
    }

    const int indexMin = args.getInt("index-min", 10);
    const int indexMax = args.getInt("index-max", 70);
    const int indexStep = std::max(1, args.getInt("index-step", 10));
    const unsigned numRuns = static_cast<unsigned>(std::max(1, args.getInt("runs", 1000)));
    const bool recordPath = args.getBool("record-path", true);

    // Everything (summary/distribution CSVs and sample walker paths) is
    // saved under one directory, defaulting to data/ at the repo root.
    const std::string outdir = args.getStr("outdir", defaultDataDir(argv[0]));

    unsigned workerCount = static_cast<unsigned>(args.getInt("threads", 0));
    if (workerCount == 0)
        workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0)
        workerCount = 4;
    workerCount = std::min<unsigned>(workerCount, std::max(1u, numRuns));

    fs::create_directories(outdir);

    std::random_device rd;
    std::vector<WalkContext3D> contexts(workerCount);
    for (unsigned t = 0; t < workerCount; ++t)
    {
        if (args.has("seed"))
        {
            std::uint64_t base = static_cast<std::uint64_t>(args.getInt("seed", 0));
            contexts[t].rng.seed(base + t);
        }
        else
        {
            std::seed_seq seq{rd(), rd(), rd(), rd(), static_cast<unsigned>(t)};
            contexts[t].rng.seed(seq);
        }
    }
    WalkContext3D pickCtx;
    pickCtx.rng.seed(rd());

    std::cout << "Shape=" << shapeStr << " Runs=" << numRuns << " Threads=" << workerCount << "\n";

    const std::string summaryFile = outdir + "/summary_" + shapeStr + ".csv";

    for (int index = indexMin; index <= indexMax; index += indexStep)
    {
        if (summaryRowExists(summaryFile, index))
        {
            std::cout << "Skipping existing index " << index << "\n";
            continue;
        }

        std::vector<double> stepResults(numRuns, 0.0);

        std::uniform_int_distribution<unsigned> runDist(0, numRuns - 1);
        unsigned targetRun = recordPath ? runDist(pickCtx.rng) : numRuns;

        std::vector<LatticePoint3D> chosenPath;
        std::mutex chosenPathMutex;

        std::vector<std::thread> threads;
        threads.reserve(workerCount);
        std::size_t runsPerThread = (numRuns + workerCount - 1) / workerCount;

        for (unsigned t = 0; t < workerCount; ++t)
        {
            std::size_t start = t * runsPerThread;
            std::size_t end = std::min<std::size_t>(start + runsPerThread, numRuns);
            if (start >= end)
                continue;

            threads.emplace_back([&, t, start, end]()
            {
                WalkContext3D &localCtx = contexts[t];
                for (std::size_t runIndex = start; runIndex < end; ++runIndex)
                {
                    int steps;
                    if (runIndex == targetRun)
                    {
                        std::vector<LatticePoint3D> localPath;
                        steps = walk(index, shape, localCtx, &localPath);
                        if (!localPath.empty())
                        {
                            std::lock_guard<std::mutex> lock(chosenPathMutex);
                            if (chosenPath.empty())
                                chosenPath = std::move(localPath);
                        }
                    }
                    else
                    {
                        steps = walk(index, shape, localCtx, nullptr);
                    }
                    stepResults[runIndex] = static_cast<double>(steps);
                }
            });
        }
        for (auto &th : threads)
            th.join();

        RunStats stepStats = computeStats(stepResults);

        std::ofstream distFile(outdir + "/distribution_" + shapeStr + "_" + std::to_string(index) + ".csv");
        distFile << "Steps\n";
        for (unsigned i = 0; i < numRuns; ++i)
            distFile << stepResults[i] << "\n";

        bool needsHeader = !fs::exists(summaryFile);
        std::ofstream summary(summaryFile, std::ios::app);
        if (needsHeader)
            summary << "Index,Runs,MeanSteps,StdDevSteps\n";
        summary << index << "," << numRuns << "," << stepStats.mean << "," << stepStats.stddev << "\n";

        if (recordPath && !chosenPath.empty())
        {
            std::ofstream pathFile(outdir + "/path_" + shapeStr + "_" + std::to_string(index) + ".txt");
            for (const auto &pt : chosenPath)
                pathFile << pt.x << " " << pt.y << " " << pt.z << "\n";
        }

        std::cout << "index=" << index << " meanSteps=" << stepStats.mean
                   << " stddevSteps=" << stepStats.stddev << "\n";
        std::cout.flush();
    }

    return 0;
}
