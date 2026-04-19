#include <debug/TimingPanel.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
    float Ms(double seconds)
    {
        return static_cast<float>(seconds * 1000.0);
    }

    struct WindowStats
    {
        double Avg = 0.0;
        double Min = 0.0;
        double Max = 0.0;
        double StdDev = 0.0;
    };

    WindowStats RawStats(const TimingHistory& history, double seconds)
    {
        WindowStats stats{};
        double covered = 0.0;
        double sum = 0.0;
        double sumSq = 0.0;
        std::size_t count = 0;
        for (std::size_t i = history.Size(); i > 0; --i)
        {
            const auto& sample = history.GetChronological(i - 1);
            if (count == 0)
            {
                stats.Min = sample.RawDtSeconds;
                stats.Max = sample.RawDtSeconds;
            }
            stats.Min = std::min(stats.Min, sample.RawDtSeconds);
            stats.Max = std::max(stats.Max, sample.RawDtSeconds);
            sum += sample.RawDtSeconds;
            sumSq += sample.RawDtSeconds * sample.RawDtSeconds;
            covered += sample.RawDtSeconds;
            ++count;
            if (covered >= seconds)
                break;
        }

        stats.Avg = count > 0 ? sum / static_cast<double>(count) : 0.0;
        const double variance = count > 0
            ? std::max(0.0, sumSq / static_cast<double>(count) - stats.Avg * stats.Avg)
            : 0.0;
        stats.StdDev = std::sqrt(variance);
        return stats;
    }

    const char* LifecycleName(int state)
    {
        switch (state)
        {
        case 0: return "Running";
        case 1: return "Resizing";
        case 2: return "SwapchainInvalid";
        case 3: return "RebuildingSwapchain";
        case 4: return "RecoveringPresentation";
        case 5: return "Minimized";
        case 6: return "Suspended";
        default: return "Unknown";
        }
    }

    const char* DiscontinuityName(int reason)
    {
        switch (reason)
        {
        case 0: return "None";
        case 1: return "Resize";
        case 2: return "SwapchainInvalidated";
        case 3: return "SwapchainRecreated";
        case 4: return "Minimized";
        case 5: return "Restored";
        case 6: return "Suspended";
        case 7: return "DebugPause";
        case 8: return "Teleport";
        case 9: return "ZoneLoad";
        case 10: return "RegistryReset";
        default: return "Unknown";
        }
    }
}

TimingPanel::TimingPanel(TimingHistory& history)
    : History(history)
{
}

void TimingPanel::Draw()
{
    FillSeries();

    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Timing"))
    {
        ImGui::End();
        return;
    }

    const TimingFrameSample* latest = History.Latest();
    if (latest == nullptr)
    {
        ImGui::TextUnformatted("Waiting for timing samples.");
        ImGui::End();
        return;
    }

    const WindowStats oneSecond = RawStats(History, 1.0);
    const WindowStats fiveSecond = RawStats(History, 5.0);
    const double fps = latest->RawDtSeconds > 0.0 ? 1.0 / latest->RawDtSeconds : 0.0;

    ImGui::Text("Raw DT: %.3f ms  1s avg/min/max/std %.3f / %.3f / %.3f / %.3f",
                Ms(latest->RawDtSeconds),
                Ms(oneSecond.Avg), Ms(oneSecond.Min), Ms(oneSecond.Max), Ms(oneSecond.StdDev));
    ImGui::Text("Raw DT 5s avg/min/max/std %.3f / %.3f / %.3f / %.3f",
                Ms(fiveSecond.Avg), Ms(fiveSecond.Min), Ms(fiveSecond.Max), Ms(fiveSecond.StdDev));
    ImGui::Text("Engine DT: %.3f ms  Presentation DT: %.3f ms  Accumulator: %.3f ms  Alpha: %.3f",
                Ms(latest->EngineDtSeconds),
                Ms(latest->PresentationDtSeconds),
                Ms(latest->FixedAccumulatorSeconds),
                latest->InterpolationAlpha);
    ImGui::Text("Fixed ticks: %u  FPS: %.1f  Present mode: %d  Images: %u",
                latest->FixedTicks,
                fps,
                latest->PresentMode,
                latest->SwapchainImageCount);
    ImGui::Text("Acquire: %.3f ms  Submit: %.3f ms  Present: %.3f ms  Render record: %.3f ms",
                Ms(latest->AcquireSeconds),
                Ms(latest->SubmitSeconds),
                Ms(latest->PresentSeconds),
                Ms(latest->RenderRecordSeconds));
    ImGui::Text("Lifecycle: %s  Discontinuity: %s  Render result: %d",
                LifecycleName(latest->LifecycleState),
                DiscontinuityName(latest->TemporalDiscontinuityReason),
                latest->RenderResult);
    ImGui::Text("Presented: %s  Lifecycle-only: %s  Swapchain generation: %llu  recreates: %llu  image: %u",
                latest->Presented ? "yes" : "no",
                latest->LifecycleOnly ? "yes" : "no",
                static_cast<unsigned long long>(latest->SwapchainGeneration),
                static_cast<unsigned long long>(latest->SwapchainRecreateCount),
                latest->SwapchainImageIndex);

    if (latest->SwapchainRecreated)
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "Swapchain recreated this frame");
    if (latest->PresentationDtSuppressed)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Presentation DT suppression active");

    ImGui::Separator();
    const int count = static_cast<int>(std::min<std::size_t>(History.Size(), RawDt.size()));
    ImGui::PlotLines("Raw DT", RawDt.data(), count, 0, nullptr, 0.0f, 40.0f, ImVec2(0, 72));
    ImGui::PlotLines("Engine DT", EngineDt.data(), count, 0, nullptr, 0.0f, 40.0f, ImVec2(0, 72));
    ImGui::PlotLines("Presentation DT", PresentationDt.data(), count, 0, nullptr, 0.0f, 40.0f, ImVec2(0, 72));
    ImGui::PlotLines("Accumulator", Accumulator.data(), count, 0, nullptr, 0.0f, 40.0f, ImVec2(0, 72));
    ImGui::PlotLines("Alpha", Alpha.data(), count, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 72));
    ImGui::PlotHistogram("Fixed Ticks", FixedTicks.data(), count, 0, nullptr, 0.0f, 5.0f, ImVec2(0, 64));
    ImGui::PlotHistogram("Swapchain Events", SwapchainEvents.data(), count, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 28));
    ImGui::PlotHistogram("Suppressed Frames", SuppressionEvents.data(), count, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 28));
    ImGui::PlotHistogram("Lifecycle-only Frames", LifecycleEvents.data(), count, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 28));
    ImGui::PlotLines("Acquire", Acquire.data(), count, 0, nullptr, 0.0f, 20.0f, ImVec2(0, 56));
    ImGui::PlotLines("Present", Present.data(), count, 0, nullptr, 0.0f, 20.0f, ImVec2(0, 56));

    ImGui::End();
}

void TimingPanel::FillSeries()
{
    RawDt.fill(0.0f);
    EngineDt.fill(0.0f);
    PresentationDt.fill(0.0f);
    Accumulator.fill(0.0f);
    Alpha.fill(0.0f);
    FixedTicks.fill(0.0f);
    Acquire.fill(0.0f);
    Present.fill(0.0f);
    SwapchainEvents.fill(0.0f);
    SuppressionEvents.fill(0.0f);
    LifecycleEvents.fill(0.0f);

    const std::size_t count = std::min<std::size_t>(History.Size(), RawDt.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& sample = History.GetChronological(History.Size() - count + i);
        RawDt[i] = Ms(sample.RawDtSeconds);
        EngineDt[i] = Ms(sample.EngineDtSeconds);
        PresentationDt[i] = Ms(sample.PresentationDtSeconds);
        Accumulator[i] = Ms(sample.FixedAccumulatorSeconds);
        Alpha[i] = static_cast<float>(sample.InterpolationAlpha);
        FixedTicks[i] = static_cast<float>(sample.FixedTicks);
        Acquire[i] = Ms(sample.AcquireSeconds);
        Present[i] = Ms(sample.PresentSeconds);
        SwapchainEvents[i] = sample.SwapchainRecreated ? 1.0f : 0.0f;
        SuppressionEvents[i] = sample.PresentationDtSuppressed ? 1.0f : 0.0f;
        LifecycleEvents[i] = sample.LifecycleOnly ? 1.0f : 0.0f;
    }
}
