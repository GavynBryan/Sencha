#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <profiling/RenderCapture.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>

#include <optional>
#include <string>

TEST(RenderProfileMode, ParsesTheLadderAndRejectsUnknownNames)
{
    RenderProfileMode mode = RenderProfileMode::Off;
    EXPECT_TRUE(ParseRenderProfileMode("counters", mode));
    EXPECT_EQ(mode, RenderProfileMode::Counters);
    EXPECT_TRUE(ParseRenderProfileMode("gpu", mode));
    EXPECT_EQ(mode, RenderProfileMode::Gpu);
    EXPECT_TRUE(ParseRenderProfileMode("capture", mode));
    EXPECT_EQ(mode, RenderProfileMode::Capture);
    EXPECT_TRUE(ParseRenderProfileMode("off", mode));
    EXPECT_EQ(mode, RenderProfileMode::Off);

    mode = RenderProfileMode::Gpu;
    EXPECT_FALSE(ParseRenderProfileMode("Counters", mode));
    EXPECT_FALSE(ParseRenderProfileMode("", mode));
    EXPECT_EQ(mode, RenderProfileMode::Gpu);

    EXPECT_STREQ(ToString(RenderProfileMode::Capture), "capture");
}

TEST(RenderStatsHistory, RingRetainsChronologyAndVersionCountsWrites)
{
    RenderStatsHistory history(3);
    EXPECT_EQ(history.GetVersion(), 0u);
    EXPECT_EQ(history.Latest(), nullptr);

    for (std::uint64_t frame = 1; frame <= 5; ++frame)
    {
        RenderStats stats;
        stats.FrameIndex = frame;
        history.Push(stats);
    }

    EXPECT_EQ(history.GetVersion(), 5u);
    EXPECT_EQ(history.Size(), 3u);
    ASSERT_NE(history.Latest(), nullptr);
    EXPECT_EQ(history.Latest()->FrameIndex, 5u);
    EXPECT_EQ(history.GetChronological(0).FrameIndex, 3u);
    EXPECT_EQ(history.GetChronological(2).FrameIndex, 5u);
}

#ifdef SENCHA_ENABLE_RENDER_PROFILING
namespace
{
    RenderCapture::FrameRecord MakeRecord(std::uint64_t frame)
    {
        RenderCapture::FrameRecord record;
        record.Stats.FrameIndex = frame;
        record.Stats.DrawCalls = static_cast<std::uint32_t>(frame * 10);
        record.Stats.PointShadowFacesRendered = static_cast<std::uint32_t>(frame * 2);
        record.Stats.PointShadowCubesHeld = static_cast<std::uint32_t>(frame);
        record.Timing.RawDtSeconds = 0.016;
        record.Timing.GpuScopes[0] = GpuScopeSpan{ .Milliseconds = 1.5f, .Valid = true };
        return record;
    }
}

TEST(RenderCapture, RecordsOnlyWhileArmedAndStopsAtTheFrameLimit)
{
    RenderCapture capture;
    const RenderCapture::FrameRecord record = MakeRecord(1);

    // Appends before Start must not write: the version counter is the
    // Off-path proof.
    capture.Append(record.Timing, record.Stats);
    EXPECT_EQ(capture.GetVersion(), 0u);
    EXPECT_EQ(capture.Size(), 0u);

    capture.Start(2);
    EXPECT_TRUE(capture.IsRecording());
    capture.Append(record.Timing, record.Stats);
    capture.Append(record.Timing, record.Stats);
    EXPECT_FALSE(capture.IsRecording());
    capture.Append(record.Timing, record.Stats);
    EXPECT_EQ(capture.Size(), 2u);
    EXPECT_EQ(capture.GetVersion(), 2u);

    capture.Start(0);
    EXPECT_EQ(capture.Size(), 0u);
    capture.Append(record.Timing, record.Stats);
    EXPECT_TRUE(capture.IsRecording());
    capture.Stop();
    EXPECT_FALSE(capture.IsRecording());
    EXPECT_EQ(capture.Size(), 1u);
}

TEST(RenderCapture, JsonEnvelopeCarriesSchemaCvarsAndUnitKeyedFrames)
{
    RenderCapture capture;
    capture.Start(0);
    for (std::uint64_t frame = 1; frame <= 3; ++frame)
    {
        const RenderCapture::FrameRecord record = MakeRecord(frame);
        capture.Append(record.Timing, record.Stats);
    }

    const std::string json = capture.SerializeJson(
        { { "render.profile.mode", "capture" } });

    JsonParseError error;
    const std::optional<JsonValue> parsed = JsonParse(json, &error);
    ASSERT_TRUE(parsed.has_value()) << error.Message;
    const JsonValue& root = *parsed;
    ASSERT_NE(root.Find("schema_version"), nullptr);
    EXPECT_EQ(root.Find("schema_version")->AsNumber(), 2.0);
    EXPECT_EQ(root.Find("frame_count")->AsNumber(), 3.0);
    ASSERT_NE(root.Find("cvars"), nullptr);
    ASSERT_NE(root.Find("cvars")->Find("render.profile.mode"), nullptr);

    const JsonValue* frames = root.Find("frames");
    ASSERT_NE(frames, nullptr);
    ASSERT_EQ(frames->AsArray().size(), 3u);
    const JsonValue& first = frames->AsArray().front();
    ASSERT_NE(first.Find("draw_calls_count"), nullptr);
    EXPECT_EQ(first.Find("draw_calls_count")->AsNumber(), 10.0);
    ASSERT_NE(first.Find("point_shadow_faces_rendered_count"), nullptr);
    EXPECT_EQ(first.Find("point_shadow_faces_rendered_count")->AsNumber(), 2.0);
    ASSERT_NE(first.Find("point_shadow_cubes_held_count"), nullptr);
    EXPECT_EQ(first.Find("point_shadow_cubes_held_count")->AsNumber(), 1.0);
    ASSERT_NE(first.Find("raw_dt_ms"), nullptr);
    EXPECT_NEAR(first.Find("raw_dt_ms")->AsNumber(), 16.0, 1.0e-6);
    // Scope 0 was collected; the frame carries its span in milliseconds.
    ASSERT_NE(first.Find("Phase_Offscreen_gpu_ms"), nullptr);
    EXPECT_NEAR(first.Find("Phase_Offscreen_gpu_ms")->AsNumber(), 1.5, 1.0e-6);
    // Uncollected scopes read -1, never a fake zero duration.
    ASSERT_NE(first.Find("Forward_Opaque_gpu_ms"), nullptr);
    EXPECT_EQ(first.Find("Forward_Opaque_gpu_ms")->AsNumber(), -1.0);
}

TEST(RenderCapture, CsvHasOneHeaderAndOneRowPerFrame)
{
    RenderCapture capture;
    capture.Start(0);
    const RenderCapture::FrameRecord record = MakeRecord(7);
    capture.Append(record.Timing, record.Stats);
    capture.Append(record.Timing, record.Stats);

    const std::string csv = capture.SerializeCsv();
    std::size_t lines = 0;
    for (char c : csv)
        lines += c == '\n' ? 1u : 0u;
    EXPECT_EQ(lines, 3u);
    EXPECT_EQ(csv.find("frame_index_count"), 0u);
    EXPECT_NE(csv.find("shadow_tile_bytes"), std::string::npos);
}
#endif
