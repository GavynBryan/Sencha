#pragma once

#include "authoring/AnimationNavigation.h"
#include "authoring/AnimationPreviewSession.h"

#include <core/assets/AssetLease.h>

#include <functional>
#include <string>
#include <vector>

struct AnimRigData;
struct RuntimeAssets;
struct SkeletonData;
class World;

// The rig under simulation, its saved scenario, and what the author has navigated to in it.
class AnimationRigScenario
{
public:
    AnimationRigScenario(RuntimeAssets& assets, std::function<void(World&)> vocabulary);

    AnimationRigScenario(const AnimationRigScenario&) = delete;
    AnimationRigScenario& operator=(const AnimationRigScenario&) = delete;
    AnimationRigScenario(AnimationRigScenario&&) = delete;
    AnimationRigScenario& operator=(AnimationRigScenario&&) = delete;

    // Uses the scenario saved beside the rig, or a new one-participant scenario;
    // the rig and its dependencies stay resident while it is open.
    bool Open(const std::string& path);
    bool Save();
    [[nodiscard]] const AnimRigData* Data() const;
    [[nodiscard]] const SkeletonData* Skeleton() const;

    AnimationPreviewSession Simulation;
    AnimationNavigation Navigation;
    std::string Path;
    std::string ScenarioFile;
    std::string Error;
    std::vector<AnimDiagnostic> LoadProblems;

private:
    RuntimeAssets& Assets;
    AssetLease Lease;
};
