#include <core/config/RuntimeConfig.h>

#include <cmath>

namespace
{
    const JsonValue* FindEither(const JsonValue& root, const char* a, const char* b)
    {
        if (const JsonValue* value = root.Find(a))
            return value;
        return root.Find(b);
    }

    bool ReadBoolEither(const JsonValue& root,
                        const char* a,
                        const char* b,
                        bool& out,
                        std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsBool())
        {
            error = std::string("runtime config: '") + a + "' must be a boolean";
            return false;
        }
        out = value->AsBool();
        return true;
    }

    bool ReadDoubleEither(const JsonValue& root,
                          const char* a,
                          const char* b,
                          double& out,
                          std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("runtime config: '") + a + "' must be a number";
            return false;
        }
        out = value->AsNumber();
        return true;
    }

    bool ReadIntEither(const JsonValue& root,
                       const char* a,
                       const char* b,
                       int& out,
                       std::string& error)
    {
        const JsonValue* value = FindEither(root, a, b);
        if (!value)
            return true;
        if (!value->IsNumber())
        {
            error = std::string("runtime config: '") + a + "' must be a number";
            return false;
        }
        const double number = value->AsNumber();
        if (!std::isfinite(number) || number != std::floor(number))
        {
            error = std::string("runtime config: '") + a + "' must be an integer";
            return false;
        }
        out = static_cast<int>(number);
        return true;
    }
}

std::optional<EngineRuntimeConfig> DeserializeRuntimeConfig(
    const JsonValue& root,
    RuntimeConfigError* error)
{
    if (!root.IsObject())
    {
        if (error) error->Message = "runtime config: root must be a JSON object";
        return std::nullopt;
    }

    EngineRuntimeConfig config;
    std::string sectionError;

    if (!ReadDoubleEither(root, "fixedTickRate", "fixed_tick_rate",
            config.FixedTickRate, sectionError)
        || !ReadDoubleEither(root, "targetFps", "target_fps",
            config.TargetFps, sectionError)
        || !ReadDoubleEither(root, "resizeSettleSeconds", "resize_settle_seconds",
            config.ResizeSettleSeconds, sectionError)
        || !ReadDoubleEither(root, "asyncCommitBudgetMs", "async_commit_budget_ms",
            config.AsyncCommitBudgetMs, sectionError)
        || !ReadIntEither(root, "jobWorkerCount", "job_worker_count",
            config.JobWorkerCount, sectionError)
        || !ReadIntEither(root, "asyncTaskThreadCount", "async_task_thread_count",
            config.AsyncTaskThreadCount, sectionError)
        || !ReadBoolEither(root, "zoneParallelPropagation", "zone_parallel_propagation",
            config.ZoneParallelPropagation, sectionError)
        || !ReadBoolEither(root, "exitOnEscape", "exit_on_escape",
            config.ExitOnEscape, sectionError)
        || !ReadBoolEither(root, "togglePauseOnF1", "toggle_pause_on_f1",
            config.TogglePauseOnF1, sectionError))
    {
        if (error) error->Message = sectionError;
        return std::nullopt;
    }

    if (!std::isfinite(config.FixedTickRate) || config.FixedTickRate <= 0.0)
    {
        if (error) error->Message = "runtime config: 'fixedTickRate' must be greater than zero";
        return std::nullopt;
    }

    if (!std::isfinite(config.AsyncCommitBudgetMs) || config.AsyncCommitBudgetMs < 0.0)
    {
        if (error) error->Message = "runtime config: 'asyncCommitBudgetMs' must be zero (unbudgeted) or positive";
        return std::nullopt;
    }

    if (config.JobWorkerCount < -1)
    {
        if (error) error->Message = "runtime config: 'jobWorkerCount' must be -1 (auto), 0 (single-threaded), or positive";
        return std::nullopt;
    }

    if (config.AsyncTaskThreadCount < 1)
    {
        if (error) error->Message = "runtime config: 'asyncTaskThreadCount' must be at least 1";
        return std::nullopt;
    }

    return config;
}
