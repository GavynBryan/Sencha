#pragma once

#include "DocumentCook.h"

#include <assets/cook/CookControl.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Graph-aware progress and cancellation for one document cook. Initialized from
// the resolved step order; only this operation touches the shared CookControl
// counters. A null control is the headless path and every call is inert.
//
// Cancelled() reports the shared cancellation flag and, when set, stamps the
// cook result so callers propagate one consistent cancellation outcome.
class CookStepProgress
{
public:
    CookStepProgress(std::shared_ptr<CookControl> control,
                     const std::vector<std::string>& orderedSteps)
        : Control_(std::move(control)), Steps_(orderedSteps)
    {
        if (Control_ == nullptr)
            return;
        Control_->TotalSteps.store(static_cast<std::uint32_t>(Steps_.size()));
        Control_->CompletedSteps.store(0);
        Control_->CurrentStep.store(0);
    }

    void Begin(std::string_view step)
    {
        if (Control_ == nullptr)
            return;
        const auto found = std::find(Steps_.begin(), Steps_.end(), step);
        if (found != Steps_.end())
            Control_->CurrentStep.store(
                static_cast<std::uint32_t>(std::distance(Steps_.begin(), found)));
    }

    void Complete()
    {
        if (Control_ != nullptr)
            Control_->CompletedSteps.fetch_add(1);
    }

    // Reports whether cancellation was requested. On a request it records the
    // cancelled outcome on `result` so the caller returns a consistent result.
    [[nodiscard]] bool Cancelled(DocumentCookResult& result) const
    {
        if (Control_ == nullptr || !Control_->IsCancellationRequested())
            return false;
        result.Cancelled = true;
        result.Error = "CookDocument: cancelled";
        return true;
    }

    void Finish()
    {
        if (Control_ != nullptr)
            Control_->CompletedSteps.store(Control_->TotalSteps.load());
    }

private:
    std::shared_ptr<CookControl> Control_;
    const std::vector<std::string>& Steps_;
};
