#pragma once

#include "../CandidateOperationSupport.h"

#include <spatial/sight/SightTest.h>

#include <limits>

namespace candidate_operation
{
    // The observer half of a sight test as authored: view cone, range, and the
    // heights eyes and targets sit above their points.
    struct SightArguments
    {
        std::uint8_t Slot = 0;
        float HalfAngle = std::numbers::pi_v<float>;
        float Range = std::numeric_limits<float>::infinity();
        float EyeHeight = 1.6f;
        float TargetHeight = 1.0f;
    };

    [[nodiscard]] std::vector<DataFieldSchema> SightFields();
    [[nodiscard]] SightArguments ReadSightArguments(const JsonValue& arguments, std::uint8_t slot);

    // Range and cone first; a ray only when both pass and the budget allows.
    // Nullopt when the budget is spent.
    [[nodiscard]] std::optional<SightResult> TestSightCharged(CandidateRun& run,
                                                              const SightObserver& observer,
                                                              const Vec3d& target,
                                                              EntityId targetEntity,
                                                              EntityId observerEntity);
}
