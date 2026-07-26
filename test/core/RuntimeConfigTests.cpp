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
    EXPECT_FALSE(config->ExitOnEscape);
    EXPECT_FALSE(config->TogglePauseOnF1);
}

TEST(RuntimeConfig, ReadsCamelCaseFields)
{
    auto config = Parse(R"({
        "fixedTickRate": 120.0,
        "asyncCommitBudgetMs": 0.0,
        "jobWorkerCount": 4,
        "asyncTaskThreadCount": 3
    })");
    ASSERT_TRUE(config.has_value());
    EXPECT_DOUBLE_EQ(config->FixedTickRate, 120.0);
    EXPECT_DOUBLE_EQ(config->AsyncCommitBudgetMs, 0.0);
    EXPECT_EQ(config->JobWorkerCount, 4);
    EXPECT_EQ(config->AsyncTaskThreadCount, 3);
}

TEST(RuntimeConfig, ReadsSnakeCaseFields)
{
    auto config = Parse(R"({
        "job_worker_count": 0,
        "async_task_thread_count": 2,
        "async_commit_budget_ms": 5.5
    })");
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->JobWorkerCount, 0);
    EXPECT_EQ(config->AsyncTaskThreadCount, 2);
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
}

TEST(RuntimeConfig, StreamingFieldsParseAndDefault)
{
    const auto defaults = Parse(R"({})");
    ASSERT_TRUE(defaults.has_value());
    EXPECT_EQ(defaults->StreamingHopCount, 1);
    EXPECT_DOUBLE_EQ(defaults->StreamingLingerSeconds, 3.0);
    EXPECT_EQ(defaults->StreamingResidentZoneCap, 8);

    const auto parsed = Parse(R"({
        "streaming_hop_count": 2,
        "streaming_linger_seconds": 0.5,
        "streaming_resident_zone_cap": 4
    })");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->StreamingHopCount, 2);
    EXPECT_DOUBLE_EQ(parsed->StreamingLingerSeconds, 0.5);
    EXPECT_EQ(parsed->StreamingResidentZoneCap, 4);
}

TEST(RuntimeConfig, StreamingFieldsRejectInvalid)
{
    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"streaming_hop_count": -1})", &error).has_value());
    EXPECT_NE(error.Message.find("streamingHopCount"), std::string::npos);

    EXPECT_FALSE(Parse(R"({"streaming_linger_seconds": -0.1})", &error).has_value());
    EXPECT_NE(error.Message.find("streamingLingerSeconds"), std::string::npos);

    // Non-finite literals never survive the JSON parser; the isfinite clause
    // guards programmatic construction only.
    EXPECT_FALSE(Parse(R"({"streaming_resident_zone_cap": 0})", &error).has_value());
    EXPECT_NE(error.Message.find("streamingResidentZoneCap"), std::string::npos);
}

TEST(RuntimeConfig, StreamingPreloadFieldsParseAndValidate)
{
    const auto defaults = Parse(R"({})");
    ASSERT_TRUE(defaults.has_value());
    EXPECT_TRUE(defaults->StreamingNeighborVisible);
    EXPECT_TRUE(defaults->StreamingNeighborPhysics);
    EXPECT_DOUBLE_EQ(defaults->StreamingRadius, 0.0);

    const auto parsed = Parse(R"({
        "streaming_neighbor_visible": false,
        "streaming_neighbor_physics": false,
        "streaming_radius": 64.0
    })");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->StreamingNeighborVisible);
    EXPECT_FALSE(parsed->StreamingNeighborPhysics);
    EXPECT_DOUBLE_EQ(parsed->StreamingRadius, 64.0);

    RuntimeConfigError error;
    EXPECT_FALSE(Parse(R"({"streaming_radius": -1.0})", &error).has_value());
    EXPECT_NE(error.Message.find("streamingRadius"), std::string::npos);
}
