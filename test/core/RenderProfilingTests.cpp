#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <profiling/CpuScopeTimings.h>
#include <profiling/RenderCapture.h>
#include <profiling/RenderInstrumentation.h>
#include <profiling/RenderStats.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <render/RenderDebugView.h>
#endif

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

#ifdef SENCHA_ENABLE_RENDER_PROFILING
TEST(RenderDebugView, NamesRoundTripAndUnknownNamesPreserveTheSelection)
{
    for (std::uint32_t index = 0; index < kRenderDebugViewCount; ++index)
    {
        const RenderDebugView expected = static_cast<RenderDebugView>(index);
        RenderDebugView parsed = RenderDebugView::None;
        EXPECT_TRUE(ParseRenderDebugView(ToString(expected), parsed));
        EXPECT_EQ(parsed, expected);
        EXPECT_NE(RenderDebugViewLabel(expected), nullptr);
    }

    RenderDebugView view = RenderDebugView::ShadowRaw;
    EXPECT_FALSE(ParseRenderDebugView("Shadow_Raw", view));
    EXPECT_FALSE(ParseRenderDebugView("", view));
    EXPECT_EQ(view, RenderDebugView::ShadowRaw);
}
#endif

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

TEST(CpuScopeTimings, UnmeasuredScopesStayNegativeAndMeasuredOnesAccumulate)
{
    // Not-measured has to be distinguishable from measured-as-free, or a
    // scope that never ran reads as a scope that cost nothing.
    CpuScopeTimings timings;
    for (std::uint32_t index = 0; index < kCpuScopeCount; ++index)
        EXPECT_LT(timings.Get(static_cast<CpuScope>(index)), 0.0f);

    timings.Add(CpuScope::Extraction, 0.0);
    EXPECT_FLOAT_EQ(timings.Get(CpuScope::Extraction), 0.0f);

    // A scope running once per registry or per view reports its frame total.
    timings.Add(CpuScope::Extraction, 1.5);
    timings.Add(CpuScope::Extraction, 2.5);
    EXPECT_FLOAT_EQ(timings.Get(CpuScope::Extraction), 4.0f);
    EXPECT_LT(timings.Get(CpuScope::ForwardRecord), 0.0f);

    timings.ResetFrame();
    EXPECT_LT(timings.Get(CpuScope::Extraction), 0.0f);
}

TEST(CpuScopeTimings, ATimerWithNoSinkRecordsNothing)
{
    // The instrumentation-off path: the bundle hands out a null sink and the
    // timer must not touch anything.
    CpuScopeTimings timings;
    {
        CpuScopeTimer timer(nullptr, CpuScope::LightSelection);
    }
    EXPECT_LT(timings.Get(CpuScope::LightSelection), 0.0f);

    {
        CpuScopeTimer timer(&timings, CpuScope::LightSelection);
    }
    EXPECT_GE(timings.Get(CpuScope::LightSelection), 0.0f);
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
        record.Stats.ScratchUsedBytes = frame * 1024;
        record.Stats.ScratchBytesPerFrame = 1024 * 1024;
        record.Stats.ScratchAllocFailures = static_cast<std::uint32_t>(frame);
        record.Stats.ScratchTagHighWaterBytes[
            static_cast<std::size_t>(ScratchTag::ForwardInstanceData)] = frame * 256;
        record.Stats.ScratchTagFailures[
            static_cast<std::size_t>(ScratchTag::ForwardViewUniforms)] = 1;
        record.Stats.PassesSkipped = static_cast<std::uint32_t>(frame);
        record.Stats.InstancesDropped = static_cast<std::uint32_t>(frame * 3);
        record.Stats.ShadowCastersTested = static_cast<std::uint32_t>(frame * 100);
        record.Stats.ShadowCastersVisible = static_cast<std::uint32_t>(frame * 4);
        record.Timing.RawDtSeconds = 0.016;
        record.Timing.GpuScopes[0] = GpuScopeSpan{ .Milliseconds = 1.5f, .Valid = true };
        record.Timing.CpuScopes.Add(CpuScope::Extraction, 0.25);
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
    // Last moved by the per-consumer scratch columns joining the frame record
    // (v7).
    EXPECT_EQ(root.Find("schema_version")->AsNumber(), 7.0);
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
    // Per-consumer scratch columns: one high-water and one failure count for
    // every tag, so a capture can name which consumer filled the slice.
    for (std::size_t i = 0; i < ScratchTagCounters::kTagCount; ++i)
    {
        const std::string tag(ToString(static_cast<ScratchTag>(i)));
        EXPECT_NE(first.Find("scratch_" + tag + "_high_water_bytes"), nullptr)
            << "missing high-water column for " << tag;
        EXPECT_NE(first.Find("scratch_" + tag + "_failures_count"), nullptr)
            << "missing failure column for " << tag;
    }
    EXPECT_EQ(first.Find("scratch_forward_instance_data_high_water_bytes")->AsNumber(), 256.0);
    EXPECT_EQ(first.Find("scratch_forward_view_uniforms_failures_count")->AsNumber(), 1.0);
    ASSERT_NE(first.Find("raw_dt_ms"), nullptr);
    EXPECT_NEAR(first.Find("raw_dt_ms")->AsNumber(), 16.0, 1.0e-6);
    // Scope 0 was collected; the frame carries its span in milliseconds.
    ASSERT_NE(first.Find("Phase_Offscreen_gpu_ms"), nullptr);
    EXPECT_NEAR(first.Find("Phase_Offscreen_gpu_ms")->AsNumber(), 1.5, 1.0e-6);
    // Uncollected scopes read -1, never a fake zero duration.
    ASSERT_NE(first.Find("Forward_Opaque_gpu_ms"), nullptr);
    EXPECT_EQ(first.Find("Forward_Opaque_gpu_ms")->AsNumber(), -1.0);
    // CPU scopes follow the same rule in their own columns.
    ASSERT_NE(first.Find("Extract_Meshes_cpu_ms"), nullptr);
    EXPECT_NEAR(first.Find("Extract_Meshes_cpu_ms")->AsNumber(), 0.25, 1.0e-6);
    ASSERT_NE(first.Find("Record_ForwardOpaque_cpu_ms"), nullptr);
    EXPECT_EQ(first.Find("Record_ForwardOpaque_cpu_ms")->AsNumber(), -1.0);
}

TEST(RenderCapture, FramesCarryTheWorkDroppedAndTheBudgetItWasDroppedAgainst)
{
    // A frame that dropped its scene must be distinguishable from a frame
    // that was genuinely cheap, or capture percentiles reward failure.
    RenderCapture capture;
    capture.Start(0);
    const RenderCapture::FrameRecord record = MakeRecord(1);
    capture.Append(record.Timing, record.Stats);

    const std::optional<JsonValue> parsed = JsonParse(capture.SerializeJson({}));
    ASSERT_TRUE(parsed.has_value());
    const JsonValue& frame = parsed->Find("frames")->AsArray().front();

    ASSERT_NE(frame.Find("scratch_alloc_failures_count"), nullptr);
    EXPECT_EQ(frame.Find("scratch_alloc_failures_count")->AsNumber(), 1.0);
    ASSERT_NE(frame.Find("passes_skipped_count"), nullptr);
    EXPECT_EQ(frame.Find("passes_skipped_count")->AsNumber(), 1.0);
    ASSERT_NE(frame.Find("instances_dropped_count"), nullptr);
    EXPECT_EQ(frame.Find("instances_dropped_count")->AsNumber(), 3.0);
    ASSERT_NE(frame.Find("scratch_used_bytes"), nullptr);
    EXPECT_EQ(frame.Find("scratch_used_bytes")->AsNumber(), 1024.0);
    ASSERT_NE(frame.Find("scratch_bytes_per_frame"), nullptr);
    EXPECT_EQ(frame.Find("scratch_bytes_per_frame")->AsNumber(), 1048576.0);
    ASSERT_NE(frame.Find("shadow_casters_tested_count"), nullptr);
    EXPECT_EQ(frame.Find("shadow_casters_tested_count")->AsNumber(), 100.0);
    ASSERT_NE(frame.Find("shadow_casters_visible_count"), nullptr);
    EXPECT_EQ(frame.Find("shadow_casters_visible_count")->AsNumber(), 4.0);
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
