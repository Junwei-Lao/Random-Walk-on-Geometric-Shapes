// CLI entry point for the 2D analytic-shape random walk engine.
//
// Every run parameter (shape, index range, walk mode, covering, temperature,
// drag force, ...) is a command-line flag -- see the root Makefile's
// `run-2d` target for the canonical way to invoke this binary. Run with
// --list-shapes to print all valid --shape values.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
        "  --temperatures=T1,T2,.. SOFT mode temperature sweep; overrides --temperature.\n"
        "                          Every (index, temperature) pair is one grid point.\n"
        "  --distance-power=P      SOFT mode distance exponent (default 2.0)\n"
        "  --drag-force=F          DRAG mode force on north+east, 0..10000 (default 0)\n"
        "  --cover=0|1             Enable disk-covering analysis (default 0)\n"
        "  --rho=R                 Covering radius R(n) = rho * n (default 0.01)\n"
        "  --rhos=R1,R2,..         Covering-radius sweep; overrides --rho (needs --cover=1).\n"
        "                          Every (index[, temperature], rho) is one grid point.\n"
        "  --threads=N             Worker threads (default: hardware concurrency)\n"
        "  --outdir=DIR            Output directory for all CSVs and paths\n"
        "                          (default: <repo_root>/data, resolved from this binary's location)\n"
        "  --seed=N                Deterministic RNG seed (default: random)\n"
        "  --record-path=0|1       Dump one sample walker path per index (default 1)\n"
        "  --record-cover=0|1      With --cover=1, dump that same sample run's covering\n"
        "                          radius and disk centers per index (default 1)\n"
        "  --record-distribution=0|1 Write the per-run distribution CSV per index (default 1)\n";
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
        std::cerr << "MODE=drag is defined only for SHAPE=SQUARE.\n";
        return 1;
    }

    params.temperature = args.getDouble("temperature", 1.0);
    // SOFT mode sweeps T_mix(n, T) over every (index, temperature) pair.
    // Other modes ignore temperature, so they run a single "temperature".
    const bool sweepTemperature = (params.mode == WalkMode2D::SOFT);
    std::vector<double> temperatures{params.temperature};
    if (sweepTemperature && args.has("temperatures")
        && !parseDoubleList(args.getStr("temperatures", ""), temperatures))
    {
        std::cerr << "Invalid --temperatures list '" << args.getStr("temperatures", "")
                  << "'. Expected comma-separated numbers, e.g. 0.25,0.5,1,2.\n";
        return 1;
    }
    for (double t : temperatures)
    {
        if (sweepTemperature && !(t > 0.0))
        {
            std::cerr << "Temperatures must be > 0 (got " << t << ").\n";
            return 1;
        }
    }
    params.distancePower = args.getDouble("distance-power", 2.0);
    params.dragForce = args.getDouble("drag-force", 0.0);
    params.enableCover = args.getBool("cover", false);
    params.coverRho = args.getDouble("rho", 0.1);
    // With covering on, every rho is its own grid point, just like the
    // temperatures above. Without covering, rho is meaningless.
    const bool sweepRho = params.enableCover;
    std::vector<double> rhos{params.coverRho};
    if (sweepRho && args.has("rhos") && !parseDoubleList(args.getStr("rhos", ""), rhos))
    {
        std::cerr << "Invalid --rhos list '" << args.getStr("rhos", "")
                  << "'. Expected comma-separated numbers, e.g. 0.05,0.1,0.2.\n";
        return 1;
    }
    for (double r : rhos)
    {
        if (sweepRho && !(r >= 0.0))
        {
            std::cerr << "Rhos must be >= 0 (got " << r << ").\n";
            return 1;
        }
    }

    const int indexMin = args.getInt("index-min", 10);
    const int indexMax = args.getInt("index-max", 100);
    const int indexStep = std::max(1, args.getInt("index-step", 10));
    const unsigned numRuns = static_cast<unsigned>(std::max(1, args.getInt("runs", 1000)));
    const bool recordPath = args.getBool("record-path", true);
    const bool recordDistribution = args.getBool("record-distribution", true);
    const bool recordCover = params.enableCover && args.getBool("record-cover", true);
    const bool recordSample = recordPath || recordCover;

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
              << " Runs=" << numRuns << " Threads=" << workerCount;
    if (sweepTemperature)
    {
        std::cout << " Temperatures=";
        for (std::size_t i = 0; i < temperatures.size(); ++i)
            std::cout << (i ? "," : "") << temperatures[i];
    }
    if (sweepRho)
    {
        std::cout << " Rhos=";
        for (std::size_t i = 0; i < rhos.size(); ++i)
            std::cout << (i ? "," : "") << rhos[i];
    }
    std::cout << "\n";

    // Cover on/off changes what a run measures, so it must be part of the
    // run's identity -- otherwise re-running the same shape/mode/index with
    // a different --cover value would be mistaken for already-completed
    // work by summaryRowExists() and silently skipped.
    const std::string runTag = shapeStr + "_" + modeName(params.mode) + (params.enableCover ? "_cover" : "");
    const std::string summaryFile = outdir + "/summary_" + runTag + ".csv";
    const std::string summaryHeader = std::string("Index") + (sweepTemperature ? ",Temperature" : "")
                                      + (sweepRho ? ",Rho" : "") + ",Runs,MeanSteps,StdDevSteps"
                                      + (params.enableCover ? ",MeanDisks,StdDevDisks" : "");
    if (!summaryHeaderMatches(summaryFile, summaryHeader))
    {
        std::cerr << "Existing " << summaryFile << " has a different column layout than this run\n"
                  << "(expected header: " << summaryHeader << ").\n"
                  << "Move it aside or use a different --outdir.\n";
        return 1;
    }

    for (double temperature : temperatures)
    {
        params.temperature = temperature;
        for (double rho : rhos)
        {
            params.coverRho = rho;
            // Swept parameters are part of each grid point's file names so
            // every point gets its own distribution/path/cover file; the
            // summary holds the whole grid, keyed by the same values.
            const std::string pointTag = runTag
                                         + (sweepTemperature ? "_T" + formatNumber(temperature) : "")
                                         + (sweepRho ? "_R" + formatNumber(rho) : "");
            std::vector<double> rowKeys;
            if (sweepTemperature)
                rowKeys.push_back(temperature);
            if (sweepRho)
                rowKeys.push_back(rho);

            for (int index = indexMin; index <= indexMax; index += indexStep)
            {
                if (summaryRowExists(summaryFile, index, rowKeys))
                {
                    std::cout << "Skipping existing index " << index;
                    if (sweepTemperature)
                        std::cout << " T=" << temperature;
                    if (sweepRho)
                        std::cout << " rho=" << rho;
                    std::cout << "\n";
                    continue;
                }

                std::vector<double> stepResults(numRuns, 0.0);
                std::vector<double> diskResults(numRuns, 0.0);

                std::uniform_int_distribution<unsigned> runDist(0, numRuns - 1);
                unsigned targetRun = recordSample ? runDist(pickCtx.rng) : numRuns; // numRuns == "never matches"

                std::vector<LatticePoint2D> chosenPath;
                std::vector<DiskCenter2D> chosenCover;
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
                                std::vector<DiskCenter2D> localCover;
                                res = walk(index, shape, params, localCtx, &localPath, &localCover);
                                std::lock_guard<std::mutex> lock(chosenPathMutex);
                                chosenPath = std::move(localPath);
                                chosenCover = std::move(localCover);
                            }
                            else
                            {
                                res = walk(index, shape, params, localCtx, nullptr, nullptr);
                            }
                            stepResults[runIndex] = static_cast<double>(res.steps);
                            diskResults[runIndex] = static_cast<double>(res.diskCount);
                        }
                    });
                }
                for (auto &th : threads)
                    th.join();

                RunStats stepStats = computeStats(stepResults);
                RunStats diskStats = params.enableCover ? computeStats(diskResults) : RunStats{};

                if (recordDistribution)
                {
                    std::ofstream distFile(outdir + "/distribution_" + pointTag + "_" + std::to_string(index) + ".csv");
                    distFile << "Steps" << (params.enableCover ? ",Disks\n" : "\n");
                    for (unsigned i = 0; i < numRuns; ++i)
                    {
                        distFile << stepResults[i];
                        if (params.enableCover)
                            distFile << "," << diskResults[i];
                        distFile << "\n";
                    }
                }

                bool needsHeader = !fs::exists(summaryFile);
                std::ofstream summary(summaryFile, std::ios::app);
                if (needsHeader)
                    summary << summaryHeader << "\n";
                summary << index;
                for (double key : rowKeys)
                    summary << "," << key;
                summary << "," << numRuns << "," << stepStats.mean << "," << stepStats.stddev;
                if (params.enableCover)
                    summary << "," << diskStats.mean << "," << diskStats.stddev;
                summary << "\n";

                if (recordPath && !chosenPath.empty())
                {
                    std::ofstream pathFile(outdir + "/path_" + pointTag + "_" + std::to_string(index) + ".txt");
                    for (const auto &pt : chosenPath)
                        pathFile << pt.x << " " << pt.y << "\n";
                }

                // First line is the covering radius R(n); every following line
                // is one disk center "x y", in placement order.
                if (recordCover)
                {
                    std::ofstream coverFile(outdir + "/cover_" + pointTag + "_" + std::to_string(index) + ".txt");
                    coverFile << std::setprecision(10);
                    coverFile << params.coverRho * index << "\n";
                    for (const auto &pt : chosenCover)
                        coverFile << pt.x << " " << pt.y << "\n";
                }

                std::cout << "index=" << index;
                if (sweepTemperature)
                    std::cout << " T=" << temperature;
                if (sweepRho)
                    std::cout << " rho=" << rho;
                std::cout << " meanSteps=" << stepStats.mean
                           << " stddevSteps=" << stepStats.stddev;
                if (params.enableCover)
                    std::cout << " meanDisks=" << diskStats.mean;
                std::cout << "\n";
                std::cout.flush();
            }
        }
    }

    return 0;
}
