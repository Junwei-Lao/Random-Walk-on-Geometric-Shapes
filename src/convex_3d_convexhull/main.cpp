// CLI entry point for the 3D convex-hull random walk engine.
// See the root Makefile's `run-3d-hull` target for the canonical invocation.
// Run with --list-shapes to print all valid --shape values.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "../common/cli_args.h"
#include "convex_hull_3d.h"

namespace fs = std::filesystem;

namespace
{

bool summaryRowExists(const std::string &filename, int index)
{
    std::ifstream in(filename);
    if (!in)
        return false;
    std::string line;
    std::getline(in, line);
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
        "  --list-shapes         Print all valid --shape values and exit\n"
        "  --shape=NAME          Shape to simulate (default CUBE)\n"
        "  --index-min=N         Smallest size index n (volume = (4/3)*PI*n^3); default 10\n"
        "  --index-max=N         Largest size index, inclusive (default 70)\n"
        "  --index-step=N        Step between indices (default 10)\n"
        "  --runs=N              Independent runs per index (default 1000)\n"
        "  --coverage-fraction=F Fraction of grid points to visit (default 0.75, doc 3D)\n"
        "  --threads=N           Worker threads (default: hardware concurrency)\n"
        "  --data-dir=DIR        CSV output directory (default data)\n"
        "  --points-dir=DIR      Interior grid point dump directory (default visualization)\n"
        "  --outdir=DIR          Overrides both --data-dir and --points-dir at once\n"
        "  --seed=N              Deterministic RNG seed (default: random)\n"
        "  --export-points=0|1   Dump the domain's interior grid points per index (default 0)\n";
}

} // namespace

int main(int argc, char *argv[])
{
    CliArgs args(argc, argv);

    if (args.has("list-shapes"))
    {
        for (const auto &[type, name] : kAllShapes3DHull)
            std::cout << name << "\n";
        return 0;
    }
    if (args.has("help") || args.has("h"))
    {
        printUsage(argv[0]);
        return 0;
    }

    ShapeType3DHull shape;
    std::string shapeStr = args.getStr("shape", "CUBE");
    if (!shapeFromName(shapeStr, shape))
    {
        std::cerr << "Unknown shape '" << shapeStr << "'. Run with --list-shapes to see valid names.\n";
        return 1;
    }

    const int indexMin = args.getInt("index-min", 10);
    const int indexMax = args.getInt("index-max", 70);
    const int indexStep = std::max(1, args.getInt("index-step", 10));
    const unsigned numRuns = static_cast<unsigned>(std::max(1, args.getInt("runs", 1000)));
    const double coverageFraction = args.getDouble("coverage-fraction", 0.75);
    const bool exportPoints = args.getBool("export-points", false);

    std::string dataDir = args.getStr("data-dir", "data");
    std::string pointsDir = args.getStr("points-dir", "visualization");
    if (args.has("outdir"))
    {
        dataDir = args.getStr("outdir", dataDir);
        pointsDir = args.getStr("outdir", pointsDir);
    }

    unsigned workerCount = static_cast<unsigned>(args.getInt("threads", 0));
    if (workerCount == 0)
        workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0)
        workerCount = 4;
    workerCount = std::min<unsigned>(workerCount, std::max(1u, numRuns));

    fs::create_directories(dataDir);
    if (exportPoints)
        fs::create_directories(pointsDir);

    ConvexHullDomain3D baseDomain;
    try
    {
        baseDomain.generateBaseHull(shape);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to build base hull for " << shapeStr << ": " << e.what() << "\n";
        return 1;
    }

    std::cout << "Shape=" << shapeStr << " BaseVolume=" << baseDomain.baseVolume()
              << " Runs=" << numRuns << " Threads=" << workerCount
              << " CoverageFraction=" << coverageFraction << "\n";

    const std::string summaryFile = dataDir + "/summary_" + shapeStr + "_hull.csv";
    std::uint64_t seedBase = args.has("seed") ? static_cast<std::uint64_t>(args.getInt("seed", 0))
                                               : std::random_device{}();

    for (int index = indexMin; index <= indexMax; index += indexStep)
    {
        if (summaryRowExists(summaryFile, index))
        {
            std::cout << "Skipping existing index " << index << "\n";
            continue;
        }

        double targetVolume = (4.0 / 3.0) * PI * std::pow(static_cast<double>(index), 3);

        ConvexHullDomain3D domain = baseDomain.scaledCopyForVolume(targetVolume);
        domain.buildIntegerGridPoints();

        if (domain.gridPointCount() < 2)
        {
            std::cerr << "Too few grid points at index " << index << ", skipping.\n";
            continue;
        }

        if (exportPoints)
        {
            std::ofstream pf(pointsDir + "/points_" + shapeStr + "_hull_" + std::to_string(index) + ".txt");
            for (const auto &p : domain.points())
                pf << p.x << " " << p.y << " " << p.z << "\n";
        }

        std::vector<double> stepResults(numRuns, 0.0);
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
                RandomWalkSimulator3D sim(seedBase + 1000ULL * static_cast<std::uint64_t>(index) + t);
                for (std::size_t runIndex = start; runIndex < end; ++runIndex)
                    stepResults[runIndex] = static_cast<double>(sim.runOneTrial(domain, coverageFraction));
            });
        }
        for (auto &th : threads)
            th.join();

        RunStats stats = computeStats(stepResults);

        std::ofstream distFile(dataDir + "/distribution_" + shapeStr + "_hull_" + std::to_string(index) + ".csv");
        distFile << "Steps\n";
        for (double v : stepResults)
            distFile << v << "\n";

        bool needsHeader = !fs::exists(summaryFile);
        std::ofstream summary(summaryFile, std::ios::app);
        if (needsHeader)
            summary << "Index,Runs,TargetVolume,ActualVolume,GridPointCount,CoverageFraction,MeanSteps,StdDevSteps\n";
        summary << index << "," << numRuns << "," << targetVolume << "," << domain.actualVolume() << ","
                << domain.gridPointCount() << "," << coverageFraction << "," << stats.mean << "," << stats.stddev
                << "\n";

        std::cout << "index=" << index << " gridPoints=" << domain.gridPointCount()
                   << " meanSteps=" << stats.mean << " stddevSteps=" << stats.stddev << "\n";
        std::cout.flush();
    }

    return 0;
}
