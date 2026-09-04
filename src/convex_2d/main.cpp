// CLI entry point for the 2D analytic-shape random walk engine.
//
// Every run parameter (shape, index range, walk mode, covering, temperature,
// drag force, ...) is a command-line flag -- see the root Makefile's
// `run-2d` target for the canonical way to invoke this binary. Run with
// --list-shapes to print all valid --shape values.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "../common/cli_args.h"
#include "cover.h"
#include "shapes.h"
#include "walk.h"

namespace fs = std::filesystem;

namespace
{

bool parseMode(const std::string &s, WalkMode2D &out)
{
    if (s == "hard")
    {
        out = WalkMode2D::HARD;
        return true;
    }
    if (s == "soft")
    {
        out = WalkMode2D::SOFT;
        return true;
    }
    if (s == "drag")
    {
        out = WalkMode2D::DRAG;
        return true;
    }
    return false;
}

std::string modeName(WalkMode2D m)
{
    switch (m)
    {
    case WalkMode2D::HARD:
        return "hard";
    case WalkMode2D::SOFT:
        return "soft";
    case WalkMode2D::DRAG:
        return "drag";
    }
    return "?";
}

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
        "  --list-shapes           Print all valid --shape values and exit\n"
        "  --shape=NAME            Shape to simulate (default SQUARE)\n"
        "  --mode=hard|soft|drag   Walk mechanics (default hard; drag requires SQUARE)\n"
        "  --index-min=N           Smallest size index (default 10)\n"
        "  --index-max=N           Largest size index, inclusive (default 100)\n"
        "  --index-step=N          Step between indices (default 10)\n"
        "  --runs=N                Independent runs per index (default 1000)\n"
        "  --temperature=T         SOFT mode temperature (default 1.0)\n"
        "  --distance-power=P      SOFT mode distance exponent (default 2.0)\n"
        "  --drag-force=F          DRAG mode force on north+east, 0..10000 (default 0)\n"
        "  --cover=0|1             Enable disk-covering analysis (default 0)\n"
        "  --rho=R                 Covering radius R(n) = rho * n (default 0.01)\n"
        "  --threads=N             Worker threads (default: hardware concurrency)\n"
        "  --data-dir=DIR          CSV output directory (default data)\n"
        "  --path-dir=DIR          Sample walker path directory (default visualization/path_visual)\n"
        "  --outdir=DIR            Overrides both --data-dir and --path-dir at once\n"
        "  --seed=N                Deterministic RNG seed (default: random)\n"
        "  --record-path=0|1       Dump one sample walker path per index (default 1)\n";
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

    ShapeType2D shape;
    std::string shapeStr = args.getStr("shape", "SQUARE");
    if (!shapeFromName(shapeStr, shape))
    {
        std::cerr << "Unknown shape '" << shapeStr << "'. Run with --list-shapes to see valid names.\n";
        return 1;
    }

    WalkParams2D params;
    std::string modeStr = args.getStr("mode", "hard");
    if (!parseMode(modeStr, params.mode))
    {
        std::cerr << "Unknown mode '" << modeStr << "'. Valid: hard, soft, drag.\n";
        return 1;
    }
    if (params.mode == WalkMode2D::DRAG && shape != ShapeType2D::SQUARE)
    {
        std::cerr << "MODE=drag is defined only for SHAPE=SQUARE (doc section 1.9).\n";
        return 1;
    }

    params.temperature = args.getDouble("temperature", 1.0);
    params.distancePower = args.getDouble("distance-power", 2.0);
    params.dragForce = args.getDouble("drag-force", 0.0);
    params.enableCover = args.getBool("cover", false);
    params.coverRho = args.getDouble("rho", 0.01);

    const int indexMin = args.getInt("index-min", 10);
    const int indexMax = args.getInt("index-max", 100);
    const int indexStep = std::max(1, args.getInt("index-step", 10));
    const unsigned numRuns = static_cast<unsigned>(std::max(1, args.getInt("runs", 1000)));
    const bool recordPath = args.getBool("record-path", true);

    // CSV results default into data/, sample walker paths into
    // visualization/path_visual/ -- matching this repo's existing (empty)
    // scaffolding for those two output kinds. --outdir overrides both at
    // once, for callers who just want everything in one place.
    std::string dataDir = args.getStr("data-dir", "data");
    std::string pathDir = args.getStr("path-dir", "visualization/path_visual");
    if (args.has("outdir"))
    {
        dataDir = args.getStr("outdir", dataDir);
        pathDir = args.getStr("outdir", pathDir);
    }

    unsigned workerCount = static_cast<unsigned>(args.getInt("threads", 0));
    if (workerCount == 0)
        workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0)
        workerCount = 4;
    workerCount = std::min<unsigned>(workerCount, std::max(1u, numRuns));

    fs::create_directories(dataDir);
    fs::create_directories(pathDir);

    std::random_device rd;
    std::vector<WalkContext2D> contexts(workerCount);
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
    WalkContext2D pickCtx;
    pickCtx.rng.seed(rd());

    std::cout << "Shape=" << shapeStr << " Mode=" << modeName(params.mode)
              << " Cover=" << (params.enableCover ? "on" : "off")
              << " Runs=" << numRuns << " Threads=" << workerCount << "\n";

    // Cover on/off changes what a run measures, so it must be part of the
    // run's identity -- otherwise re-running the same shape/mode/index with
    // a different --cover value would be mistaken for already-completed
    // work by summaryRowExists() and silently skipped.
    const std::string runTag = shapeStr + "_" + modeName(params.mode) + (params.enableCover ? "_cover" : "");
    const std::string summaryFile = dataDir + "/summary_" + runTag + ".csv";

    for (int index = indexMin; index <= indexMax; index += indexStep)
    {
        if (summaryRowExists(summaryFile, index))
        {
            std::cout << "Skipping existing index " << index << "\n";
            continue;
        }

        std::vector<double> stepResults(numRuns, 0.0);
        std::vector<double> coverResults(numRuns, 0.0);

        std::uniform_int_distribution<unsigned> runDist(0, numRuns - 1);
        unsigned targetRun = recordPath ? runDist(pickCtx.rng) : numRuns; // numRuns == "never matches"

        std::vector<LatticePoint2D> chosenPath;
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
                WalkContext2D &localCtx = contexts[t];
                for (std::size_t runIndex = start; runIndex < end; ++runIndex)
                {
                    WalkResult2D res;
                    if (runIndex == targetRun)
                    {
                        std::vector<LatticePoint2D> localPath;
                        res = walk(index, shape, params, localCtx, &localPath);
                        if (!localPath.empty())
                        {
                            std::lock_guard<std::mutex> lock(chosenPathMutex);
                            if (chosenPath.empty())
                                chosenPath = std::move(localPath);
                        }
                    }
                    else
                    {
                        res = walk(index, shape, params, localCtx, nullptr);
                    }
                    stepResults[runIndex] = static_cast<double>(res.steps);
                    coverResults[runIndex] = res.coverageFraction;
                }
            });
        }
        for (auto &th : threads)
            th.join();

        RunStats stepStats = computeStats(stepResults);
        RunStats coverStats = params.enableCover ? computeStats(coverResults) : RunStats{};

        std::ofstream distFile(dataDir + "/distribution_" + runTag + "_" + std::to_string(index) + ".csv");
        distFile << "Steps" << (params.enableCover ? ",Coverage\n" : "\n");
        for (unsigned i = 0; i < numRuns; ++i)
        {
            distFile << stepResults[i];
            if (params.enableCover)
                distFile << "," << coverResults[i];
            distFile << "\n";
        }

        bool needsHeader = !fs::exists(summaryFile);
        std::ofstream summary(summaryFile, std::ios::app);
        if (needsHeader)
            summary << "Index,Runs,MeanSteps,StdDevSteps" << (params.enableCover ? ",MeanCoverage,StdDevCoverage\n" : "\n");
        summary << index << "," << numRuns << "," << stepStats.mean << "," << stepStats.stddev;
        if (params.enableCover)
            summary << "," << coverStats.mean << "," << coverStats.stddev;
        summary << "\n";

        if (recordPath && !chosenPath.empty())
        {
            std::ofstream pathFile(pathDir + "/path_" + runTag + "_" + std::to_string(index) + ".txt");
            for (const auto &pt : chosenPath)
                pathFile << pt.x << " " << pt.y << "\n";
        }

        std::cout << "index=" << index << " meanSteps=" << stepStats.mean
                   << " stddevSteps=" << stepStats.stddev;
        if (params.enableCover)
            std::cout << " meanCoverage=" << coverStats.mean;
        std::cout << "\n";
        std::cout.flush();
    }

    return 0;
}
