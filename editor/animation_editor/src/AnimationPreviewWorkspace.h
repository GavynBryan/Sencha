#pragma once

#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "data/DataDocument.h"

#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>

#include <string>
#include <vector>

struct RuntimeAssets;

// Owns preview selections and their leases. Browsing/selecting preview content
// is transient and never edits the asset being inspected.
class AnimationPreviewWorkspace
{
public:
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets);
    void RefreshBrowser();
    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    bool SelectClip(const std::string& path);
    bool SelectMaterial(const std::string& path);
    void Frame(double wallSeconds);
    bool OpenRequestSchema(const std::string& path);

    // Opens a rig under the scenario saved beside it, or a new one-participant
    // scenario when there is none. Keeps the rig and its dependencies resident
    // while it is open.
    bool OpenRig(const std::string& path);
    // Writes the working scenario to its sidecar. Explicit: nothing else does.
    bool SaveScenario();
    // Discards the working scenario for the saved one.
    bool ReloadScenario();
    [[nodiscard]] const DataAssetCache& DataCache() const;
    void SelectDocument(std::size_t index);
    void CancelAuthoringEdit();
    void ValidateDocument(DataDocument& document);
    bool SaveDocument(DataDocument& document);
    bool ReloadDocument(DataDocument& document);

    std::vector<std::string> RequestSchemaPaths;
    std::vector<std::string> RigPaths;
    std::vector<std::unique_ptr<DataDocument>> Documents;
    std::size_t ActiveDocument = 0;
    std::string DocumentError;

    AnimationClipPreviewSession Session;
    // The rig under its scenario. Separate from content audition: sampling a
    // clip never advances or alters the simulation.
    AnimationPreviewSession Simulation;
    std::string RigPath;
    std::string ScenarioFile;
    std::string ScenarioError;
    std::vector<AnimDiagnostic> ScenarioLoadProblems;
    AnimationPreviewScene Scene;
    std::vector<std::string> MeshPaths;
    std::vector<std::string> SkeletonPaths;
    std::vector<std::string> ClipPaths;
    std::vector<std::string> MaterialPaths;
    std::string MeshPath;
    std::string ClipPath;
    std::string MaterialPath;
    std::string Error;

private:
    bool SetSkeletonContent(SkeletonHandle skeleton);
    RuntimeAssets& Assets;
    AssetLease RigLease;
    AssetLease MeshLease;
    AssetLease SkeletonLease;
    AssetLease ClipLease;
    AssetLease MaterialLease;
    AssetLease DefaultMaterialLease;
    MaterialHandle DefaultMaterial;
};
