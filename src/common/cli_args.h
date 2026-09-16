#ifndef COMMON_CLI_ARGS_H
#define COMMON_CLI_ARGS_H

// Minimal flag-style argv parser shared by every CLI entry point in this
// project. Accepts `--key value` and `--key=value`; a `--key` with no value
// (followed by another flag or end of argv) is treated as a boolean flag.
//
// This exists so that every simulation binary (2D analytic, 3D analytic, 2D
// convex-hull, 3D convex-hull) is driven entirely by command-line flags, and
// the Makefile is the single place that lists which shape/mode/index range
// to run -- nothing is hard-coded inside the .cpp files.

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

class CliArgs
{
public:
    CliArgs(int argc, char *argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string tok = argv[i];
            if (tok.rfind("--", 0) != 0)
            {
                continue; // ignore stray positional args
            }
            tok = tok.substr(2);

            auto eq = tok.find('=');
            if (eq != std::string::npos)
            {
                values_[tok.substr(0, eq)] = tok.substr(eq + 1);
                continue;
            }

            bool nextIsValue = (i + 1 < argc) && std::string(argv[i + 1]).rfind("--", 0) != 0;
            if (nextIsValue)
            {
                values_[tok] = argv[++i];
            }
            else
            {
                values_[tok] = "1";
            }
        }
    }

    bool has(const std::string &key) const { return values_.count(key) != 0; }

    std::string getStr(const std::string &key, const std::string &def) const
    {
        auto it = values_.find(key);
        return it == values_.end() ? def : it->second;
    }

    int getInt(const std::string &key, int def) const
    {
        auto it = values_.find(key);
        return it == values_.end() ? def : std::atoi(it->second.c_str());
    }

    double getDouble(const std::string &key, double def) const
    {
        auto it = values_.find(key);
        return it == values_.end() ? def : std::atof(it->second.c_str());
    }

    bool getBool(const std::string &key, bool def) const
    {
        auto it = values_.find(key);
        if (it == values_.end())
            return def;
        const std::string &v = it->second;
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }

private:
    std::unordered_map<std::string, std::string> values_;
};

// Every binary is built into <repo_root>/bin/ (see the root Makefile), so
// this resolves the default data/ output directory relative to the running
// executable's own location -- correct no matter what the caller's current
// working directory happens to be. A plain "data" default would instead
// resolve against the caller's cwd, so `cd bin && ./walk2d` would silently
// write into bin/data/ instead of the repo's own data/.
inline std::string defaultDataDir(const char *argv0)
{
    std::error_code ec;
    std::filesystem::path exePath = std::filesystem::canonical(argv0, ec);
    if (!ec)
        return (exePath.parent_path().parent_path() / "data").string();
    return "data"; // fallback: relative to cwd
}

struct RunStats
{
    double mean = 0.0;
    double stddev = 0.0; // population stddev: sqrt(mean squared deviation)
};

inline RunStats computeStats(const std::vector<double> &values)
{
    RunStats s;
    if (values.empty())
        return s;

    double sum = 0.0;
    for (double v : values)
        sum += v;
    s.mean = sum / static_cast<double>(values.size());

    double sq = 0.0;
    for (double v : values)
    {
        double d = v - s.mean;
        sq += d * d;
    }
    s.stddev = std::sqrt(sq / static_cast<double>(values.size()));
    return s;
}

#endif // COMMON_CLI_ARGS_H
