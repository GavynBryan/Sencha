#pragma once

#include "CookGraph.h"

#include <assets/cook/CookControl.h>

#include <memory>
#include <string>
#include <string_view>

// The resolved policy for one document cook: what the caller selected, not what
// the document contains. Built once from the cook options. Execution reads step
// selection from the resolved graph here, never from a captured field being
// empty.
struct DocumentCookRequest
{
    CookProfile Profile;
    ResolvedCookGraph Graph;
    bool ForceRebuild = false;
    std::string OutputNamespace;
    std::shared_ptr<CookControl> Control;
    double CellSize = 16.0;

    // True when the resolved graph runs `stepId` (a CookStepIds or
    // DocumentCookStepIds value).
    [[nodiscard]] bool Selects(std::string_view stepId) const
    {
        for (const std::string& step : Graph.OrderedSteps)
            if (step == stepId)
                return true;
        return false;
    }

    [[nodiscard]] CookOutputDisposition Disposition(std::string_view family) const
    {
        return CookProfileOutputDisposition(Profile, family);
    }
};
