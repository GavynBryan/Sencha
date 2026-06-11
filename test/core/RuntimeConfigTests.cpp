#include <gtest/gtest.h>

#include <core/config/RuntimeConfig.h>
#include <core/json/JsonParser.h>

#include <optional>
#include <string_view>

namespace
{
    std::optional<EngineRuntimeConfig> Parse(std::string_view json,
                                             RuntimeConfigError* error = nullptr)
    {
        auto root = JsonParse(json);
        if (!root)
        {
            ADD_FAILURE() << "test JSON failed to parse: " << json;
            return std::nullopt;
        }
        return DeserializeRuntimeConfig(*root, error);
    }
}

TEST(RuntimeConfig, EmptyObjectYieldsDefaults)
{
    auto config = Parse("{}");
    ASSERT_TRUE(config.has_value());
    EXPECT_DOUBLE_EQ(config->FixedTickRate, 60.0);
    EXPECT_DOUBLE_EQ(config->TargetFps, 0.0);
    EXPECT_DOUBLE_EQ(config->ResizeSettleSeconds, 0.10);
    EXPECT_DOUBLE_EQ(config->AsyncCommitBudgetMs, 2.0);
    EXPECT_EQ(config->JobWorkerCount, -1);
    EXPECT_EQ(config->AsyncTaskThreadCount, 1);
    EXPECT_FALSE(config->ZoneParallelPropagation);
    EXPECT_FALSE(config->ExitOnEscape);
    EXPECT_FALSE(config->TogglePauseOnF1);
}

TEST(RuntimeConfig, ReadsCamelCaseFields)
{
    auto config = Parse(R"({
        "fixedTickRate": 120.0,
        "asyncCommitBudgetMs": 0.0,
        "jobWorkerCount": 4,
        "asyncTaskThreadCount": 3,
        "zoneParallelPropagation": true
    })");
    ASSERT_TRUE(config.has_value());
    EXPECT_DOUBLE_EQ(config->FixedTickRate, 120.0);
    EXPECT_DOUBLE_EQ(config->AsyncCommitBudgetMs, 0.0);
    EXPECT_EQ(config->JobWorkerCount, 4);
    EXPECT_EQ(config->AsyncTaskThreadCount, 3);
    EXPECT_TRUE(config->ZoneParallelPropagation);
}

TEST(RuntimeConfig, ReadsSnakeCaseFields)
{
    auto config = Parse(R"({
        "job_worker_count": 0,
        "async_task_thread_count": 2,
        "zone_parallel_propagation": true,
        "async_commit_budget_ms": 5.5
    })");
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->JobWorkerCount, 0);
    EXPECT_EQ(config->AsyncTaskThreadCount, 2);
    EXPECT_TRUE(config->ZoneParallelPropagation);
    EXPECT_DOUBLE_EQ(config->AsyncCommitBudgetMs, 5.5);
}

TEST(RuntimeConfig, RejectsNonObjectRoot)
{
    auto root = JsonParse("[1, 2, 3]");
    ASSERT_TRUE(root.has_value());
    RuntimeConfigError error;
    EXPECT_FALSE(DeserializeRuntimeConfig(*root, &error).has_value());
    EXPECT_FALSE(error.Message.empty());
}

TEST(RuntimeConfig, RejectsNonPositiveTickRate)
{
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"fixedTickRate": 0.0})", &error).has_value());
    EXPECT_NE(error.Message.find("fixedTickRate"), std::string::npos);
}

TEST(RuntimeConfig, RejectsNegativeCommitBudget)
{
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"asyncCommitBudgetMs": -1.0})", &error).has_value());
    EXPECT_NE(error.Message.find("asyncCommitBudgetMs"), std::string::npos);
}

TEST(RuntimeConfig, RejectsJobWorkerCountBelowAuto)
{
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"jobWorkerCount": -2})", &error).has_value());
    EXPECT_NE(error.Message.find("jobWorkerCount"), std::string::npos);
}

TEST(RuntimeConfig, RejectsFractionalIntegerFields)
{
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"jobWorkerCount": 1.5})", &error).has_value());
    EXPECT_NE(error.Message.find("integer"), std::string::npos);
}

TEST(RuntimeConfig, RejectsZeroAsyncTaskThreads)
{
    // The engine never pumps async work inline; zero task threads would
    // strand every zone load, so the floor is 1.
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"asyncTaskThreadCount": 0})", &error).has_value());
    EXPECT_NE(error.Message.find("asyncTaskThreadCount"), std::string::npos);
}

TEST(RuntimeConfig, RejectsWrongFieldTypes)
{
    EXPECT_FALSE(Parse(R"({"fixedTickRate": "fast"})").has_value());
    EXPECT_FALSE(Parse(R"({"exitOnEscape": 1})").has_value());
    EXPECT_FALSE(Parse(R"({"zoneParallelPropagation": "yes"})").has_value());
}
