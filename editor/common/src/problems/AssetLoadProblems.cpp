#include "problems/AssetLoadProblems.h"

#include "problems/ProblemList.h"

#include <assets/runtime/AssetSystem.h>

namespace
{
constexpr std::string_view kProblemSource = "assets";
} // namespace

void AssetLoadProblems::Update(const AssetSystem& assets)
{
    if (assets.LoadFailuresVersion() == Published)
        return;
    Published = assets.LoadFailuresVersion();
    std::vector<Problem> problems;
    for (const AssetSystem::LoadFailure& failure : assets.LoadFailures())
        problems.push_back({ .Severity = ProblemSeverity::Error, .Message = failure.Reason, .Location = failure.Path });
    Problems.Publish(kProblemSource, std::move(problems));
}
