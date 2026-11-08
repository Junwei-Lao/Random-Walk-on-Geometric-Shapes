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

bool parseMode(const std::string &s, WalkMode3D &out)
{
    if (s == "hard")
    {
        out = WalkMode3D::HARD;
        return true;
    }
    if (s == "soft")
    {
        out = WalkMode3D::SOFT;
        return true;
    }
    return false;
}

void printUsage(const char *prog)
{
    std::cout <<
        "Usage: " << prog << " [flags]\n\n"
        "  --list-shapes    Print all valid --shape values and exit\n"
        "  --shape=NAME     Shape to simulate (default SPHERE)\n"
        "  --mode=hard|soft Walk mechanics (default hard)\n"
        "  --index-min=N    Smallest size index (default 10)\n"
        "  --index-max=N    Largest size index, inclusive (default 70)\n"
        "  --index-step=N   Step between indices (default 10)\n"
        "  --runs=N         Independent runs per index (default 1000)\n"
        "  --temperature=T  SOFT mode temperature (default 1.0)\n"
        "  --temperatures=T1,T2,.. SOFT mode temperature sweep; overrides --temperature.\n"
        "                   Every (index, temperature) pair is one grid point.\n"
        "  --distance-power=P SOFT mode distance exponent (default 2.0)\n"
        "  --threads=N      Worker threads (default: hardware concurrency)\n"
        "  --outdir=DIR     Output directory for all CSVs and paths\n"
        "                   (default: <repo_root>/data, resolved from this binary's location)\n"
        "  --seed=N         Deterministic RNG seed (default: random)\n"
        "  --record-path=0|1 Dump one sample walker path per index (default 1)\n"
        "  --record-distribution=0|1 Write the per-run distribution CSV per index (default 1)\n";
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

    WalkParams3D params;
    std::string modeStr = args.getStr("mode", "hard");
    if (!parseMode(modeStr, params.mode))
    {
        std::cerr << "Unknown mode '" << modeStr << "'. Valid: hard, soft.\n";
        return 1;
    }

    params.temperature = args.getDouble("temperature", 1.0);
    // SOFT mode sweeps T_mix(n, T) over every (index, temperature) pair.
    // HARD mode ignores temperature, so it runs a single "temperature".
    const bool sweepTemperature = (params.mode == WalkMode3D::SOFT);
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

    const int indexMin = args.getInt("index-min", 10);
    const int indexMax = args.getInt("index-max", 70);
    const int indexStep = std::max(1, args.getInt("index-step", 10));
    const unsigned numRuns = static_cast<unsigned>(std::max(1, args.getInt("runs", 1000)));
    const bool recordPath = args.getBool("record-path", true);
    const bool recordDistribution = args.getBool("record-distribution", true);

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

    std::cout << "Shape=" << shapeStr << " Mode=" << modeStr << " Runs=" << numRuns << " Threads=" << workerCount;
    if (sweepTemperature)
    {
        std::cout << " Temperatures=";
        for (std::size_t i = 0; i < temperatures.size(); ++i)
            std::cout << (i ? "," : "") << temperatures[i];
    }
    std::cout << "\n";

    // HARD keeps the original summary_<SHAPE>.csv / path_<SHAPE>_<INDEX>.txt
    // names so existing 3D results stay resumable; SOFT gets its own tag.
    const std::string runTag = shapeStr + (params.mode == WalkMode3D::SOFT ? "_soft" : "");
    const std::string summaryFile = outdir + "/summary_" + runTag + ".csv";
    const std::string summaryHeader = std::string("Index") + (sweepTemperature ? ",Temperature" : "")
                                      + ",Runs,MeanSteps,StdDevSteps";
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
        const std::string pointTag = runTag + (sweepTemperature ? "_T" + formatNumber(temperature) : "");
        std::vector<double> rowKeys;
        if (sweepTemperature)
            rowKeys.push_back(temperature);

        for (int index = indexMin; index <= indexMax; index += indexStep)
        {
            if (summaryRowExists(summaryFile, index, rowKeys))
            {
                std::cout << "Skipping existing index " << index;
                if (sweepTemperature)
                    std::cout << " T=" << temperature;
                std::cout << "\n";
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
                            steps = walk(index, shape, params, localCtx, &localPath);
                            std::lock_guard<std::mutex> lock(chosenPathMutex);
                            chosenPath = std::move(localPath);
                        }
                        else
                        {
                            steps = walk(index, shape, params, localCtx, nullptr);
                        }
                        stepResults[runIndex] = static_cast<double>(steps);
                    }
                });
            }
            for (auto &th : threads)
                th.join();

            RunStats stepStats = computeStats(stepResults);

            if (recordDistribution)
            {
                std::ofstream distFile(outdir + "/distribution_" + pointTag + "_" + std::to_string(index) + ".csv");
                distFile << "Steps\n";
                for (unsigned i = 0; i < numRuns; ++i)
                    distFile << stepResults[i] << "\n";
            }

            bool needsHeader = !fs::exists(summaryFile);
            std::ofstream summary(summaryFile, std::ios::app);
            if (needsHeader)
                summary << summaryHeader << "\n";
            summary << index;
            for (double key : rowKeys)
                summary << "," << key;
            summary << "," << numRuns << "," << stepStats.mean << "," << stepStats.stddev << "\n";

            if (recordPath && !chosenPath.empty())
            {
                std::ofstream pathFile(outdir + "/path_" + pointTag + "_" + std::to_string(index) + ".txt");
                for (const auto &pt : chosenPath)
                    pathFile << pt.x << " " << pt.y << " " << pt.z << "\n";
            }

            std::cout << "index=" << index;
            if (sweepTemperature)
                std::cout << " T=" << temperature;
            std::cout << " meanSteps=" << stepStats.mean << " stddevSteps=" << stepStats.stddev << "\n";
            std::cout.flush();
        }
    }

    return 0;
}
