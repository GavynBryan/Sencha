#pragma once

// Metric recording for the evidence generators.
//
// A bench emits a schema-1 document that scripts/bench_streaming_compare.py
// reads: milliseconds get a drift-normalized tolerance, counts must match
// exactly, and the build field keeps a debug run from being compared against an
// optimized one. That contract belongs in one place, because a second bench
// that formatted its own output would be compared by the same script against
// rules it had drifted from.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Bench
{
using Clock = std::chrono::steady_clock;

// One recorded measurement. Unit is carried so the compare script can apply a
// tolerance appropriate to the quantity: milliseconds drift between runs, counts
// must match exactly.
struct Metric
{
    std::string Name;
    std::string Unit;
    double Value = 0.0;
};

inline double MillisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

inline double Median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// Nearest-rank, on an already sorted range. Percentiles of a handful of frames
// are coarse by nature; the rank is what the evidence records.
inline double Percentile(std::vector<double>& samples, double fraction)
{
    std::sort(samples.begin(), samples.end());
    std::size_t rank = static_cast<std::size_t>(fraction * static_cast<double>(samples.size()));
    if (rank >= samples.size())
        rank = samples.size() - 1;
    return samples[rank];
}

inline int RepsFromEnvironment(const char* variable, int fallback)
{
    const char* raw = std::getenv(variable);
    if (raw == nullptr || raw[0] == '\0')
        return fallback;
    const int parsed = std::atoi(raw);
    return parsed > 0 ? parsed : fallback;
}

// Debug and optimized numbers describe different programs. The compare script
// refuses to mix them, and this is what it keys on.
inline const char* BuildConfiguration()
{
#ifdef NDEBUG
    return "release-codegen";
#else
    return "debug-asserts";
#endif
}

class Recorder
{
public:
    void Record(std::string name, std::string unit, double value)
    {
        Metrics_.push_back(Metric{ std::move(name), std::move(unit), value });
    }

    void Clear() { Metrics_.clear(); }

    [[nodiscard]] const std::vector<Metric>& Metrics() const { return Metrics_; }

    [[nodiscard]] bool WriteJson(const std::filesystem::path& path) const
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open())
            return false;
        out << "{\n";
        out << "  \"schema\": 1,\n";
        out << "  \"build\": \"" << BuildConfiguration() << "\",\n";
        out << "  \"metrics\": [\n";
        for (std::size_t index = 0; index < Metrics_.size(); ++index)
        {
            const Metric& metric = Metrics_[index];
            out << "    { \"name\": \"" << metric.Name << "\", \"unit\": \""
                << metric.Unit << "\", \"value\": " << metric.Value << " }"
                << (index + 1 < Metrics_.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    }

    [[nodiscard]] bool WriteCsv(const std::filesystem::path& path) const
    {
        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open())
            return false;
        out << "name,unit,value\n";
        for (const Metric& metric : Metrics_)
            out << metric.Name << ',' << metric.Unit << ',' << metric.Value << '\n';
        return true;
    }

private:
    std::vector<Metric> Metrics_;
};
}  // namespace Bench
