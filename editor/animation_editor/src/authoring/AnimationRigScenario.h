#pragma once

#include "authoring/AnimationNavigation.h"
#include "authoring/AnimationPreviewSession.h"

#include "documents/SnapshotDocument.h"

#include <core/assets/AssetLease.h>

#include <functional>
#include <string>
#include <vector>

struct AnimRigData;
struct RuntimeAssets;
struct SkeletonData;
class World;

// The rig under simulation, its scenario as a document of the journal, and
// what the author has navigated to in it.
class AnimationRigScenario
{
public:
    AnimationRigScenario(RuntimeAssets& assets, DocumentSourceSet& sources, std::function<void(World&)> vocabulary);

    AnimationRigScenario(const AnimationRigScenario&) = delete;
    AnimationRigScenario& operator=(const AnimationRigScenario&) = delete;
    AnimationRigScenario(AnimationRigScenario&&) = delete;
    AnimationRigScenario& operator=(AnimationRigScenario&&) = delete;

    // Uses the scenario saved beside the rig, or a new one-participant scenario;
    // the rig and its dependencies stay resident while it is open. The open
    // scenario's changes were settled by whoever opens another.
    bool Open(const std::string& path);
    [[nodiscard]] const AnimRigData* Data() const;
    [[nodiscard]] const SkeletonData* Skeleton() const;
    // The recorded tick being inspected, or the newest while live; null before
    // the first tick.
    [[nodiscard]] const AnimationPreviewTickRecord* Inspected() const;

    AnimationPreviewSession Simulation;
    // The scenario file beside the rig, every edit to the scenario a step.
    SnapshotDocument Scenario;
    AnimationNavigation Navigation;
    std::string Path;
    std::string Error;
    std::vector<AnimDiagnostic> LoadProblems;

private:
    RuntimeAssets& Assets;
    AssetLease Lease;
};
