#pragma once

#include "authoring/AnimationAuditionSelection.h"
#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationClipEventsSet.h"
#include "authoring/AnimationClipPlayerMigration.h"
#include "authoring/AnimationContentLists.h"
#include "authoring/AnimationContentTags.h"
#include "authoring/AnimationRigScenario.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationScenarioBatch.h"
#include "authoring/AnimationSessionLab.h"
#include "authoring/AnimationViewportExtraction.h"
#include "data/DataDocumentSet.h"
#include "documents/DocumentSourceSet.h"

#include <anim/Skeleton.h>
#include <core/assets/AssetLease.h>

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

struct RuntimeAssets;

[[nodiscard]] std::span<const std::string_view> AnimationDocumentSubtypes();

// Composes the animation editor's parts and runs the operations that span them.
class AnimationPreviewWorkspace final
{
public:
    // An empty `authoringRoot` makes a workspace that creates no assets.
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary = {},
                                       std::filesystem::path authoringRoot = {});
    void RefreshBrowser();
    // Shows the clip in the viewport; an empty path shows the bind pose.
    bool AuditionClip(const std::string& path);
    // Moves both clocks; the viewport is extracted separately and never moves them.
    void Advance(double wallSeconds);
    void ExtractViewport();
    [[nodiscard]] std::string PreviewStatusOf(const DataDocument& document) const;
    [[nodiscard]] std::string PreviewStatusOf(const AnimationClipEventsDocument& document) const;
    // Leaves a message in DocumentError when the save did not happen.
    bool SaveDocument(const DocumentRef& document);

    // Opens the rig's scenario, shows it in the viewport and brings waiting edits to it.
    bool OpenRig(const std::string& path);
    // Refuses before writing anything if any of the rig's files already exists.
    bool CreateRig(const AnimationRigRecipe& recipe, std::string& error);
    void ScanClipPlayers();
    bool MigrateClipPlayers(std::string& error);

    // Replay runs the working scenario from tick 0 to take A's last tick and
    // compares poses, so any edit since the recording shows as a residual.
    bool RecordTakeA();
    bool ReplayAgainstTakeA();
    void ClearTakeA();
    // Always from tick 0, so the result reflects the current edits.
    bool RunLab();
    bool ReloadScenario();
    // Runs in its own session; the working simulation is untouched.
    void RunScenarioBatch(bool againstOpenRig);
    [[nodiscard]] const DataAssetCache& DataCache() const;
    // Includes working clip events not yet saved.
    [[nodiscard]] const AnimationClipCache& Clips() const;
    [[nodiscard]] const SkeletonCache& Skeletons() const;

    // Declared before the vocabulary and every session that captures it.
    AnimationContentTags Tags;
    // Declared before the document sets, which register with it.
    DocumentSourceSet Sources;
    DataDocumentSet Documents;
    AnimationClipEventsSet ClipEvents;
    AnimationContentLists Content;
    AnimationAuditionSelection Audition;
    AnimationViewportExtraction Viewport;
    std::optional<AnimationPoseTake> TakeA;
    AnimationPoseComparison Comparison;
    std::string DocumentError;

    AnimationRigScenario Rig;
    // The game module's hook, then Tags.
    std::function<void(World&)> Vocabulary;
    std::unique_ptr<AnimationSessionLab> Lab;
    std::vector<AnimationClipPlayerUse> ClipPlayerUses;
    std::vector<std::string> ClipPlayerProblems;
    AnimationLabSettings LabSettings;
    std::vector<AnimationLabInjection> LabInjections;
    AnimTick LabTick = 300;
    std::vector<AnimationScenarioRun> ScenarioRuns;

private:
    void DataDocumentChanged(DataDocument& document, bool residentChanged);
    // Refuses before writing anything if any document already exists.
    bool WriteNewDocuments(const std::vector<AnimationNewDocument>& documents, std::string& error);
    bool WriteFile(const AnimationNewDocument& document, int indent, std::string& error);
    RuntimeAssets& Assets;
    std::filesystem::path AuthoringRoot;
};
