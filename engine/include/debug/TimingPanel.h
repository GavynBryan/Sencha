#pragma once

#include <debug/IDebugPanel.h>
#include <time/TimingHistory.h>

#include <array>

class VulkanSwapchainService;

class TimingPanel : public IDebugPanel
{
public:
    explicit TimingPanel(TimingHistory& history);

    void Draw() override;

private:
    TimingHistory& History;
    std::array<float, 512> RawDt{};
    std::array<float, 512> EngineDt{};
    std::array<float, 512> PresentationDt{};
    std::array<float, 512> Accumulator{};
    std::array<float, 512> Alpha{};
    std::array<float, 512> FixedTicks{};
    std::array<float, 512> Acquire{};
    std::array<float, 512> Present{};
    std::array<float, 512> SwapchainEvents{};
    std::array<float, 512> SuppressionEvents{};
    std::array<float, 512> LifecycleEvents{};

    void FillSeries();
};
