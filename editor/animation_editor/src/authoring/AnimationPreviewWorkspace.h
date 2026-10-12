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
#include "prompts/PromptQueue.h"
#include "workspaces/WorkspaceFacets.h"

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

struct RuntimeAssets;

[[nodiscard]] std::span<const std::string_view> AnimationDocumentSubtypes();

// The document dial's keys for the documents that are not data documents.
inline constexpr std::string_view kEventsDialKey = "events:";
inline constexpr std::string_view kScenarioDialKey = "scenario:";

// Composes the animation editor's parts and runs the operations that span them.
class AnimationPreviewWorkspace final
{
public:
    // `sources` and `store` are the application's journal and data documents.
    // An empty `authoringRoot` makes a workspace that creates no assets.
    AnimationPreviewWorkspace(RuntimeAssets& assets, DocumentSourceSet& sources, DataDocumentStore& store,
                              std::function<void(World&)> vocabulary = {}, std::filesystem::path authoringRoot = {});

    AnimationPreviewWorkspace(const AnimationPreviewWorkspace&) = delete;
    AnimationPreviewWorkspace& operator=(const AnimationPreviewWorkspace&) = delete;
    AnimationPreviewWorkspace(AnimationPreviewWorkspace&&) = delete;
    AnimationPreviewWorkspace& operator=(AnimationPreviewWorkspace&&) = delete;

    void RefreshBrowser();
    // Shows the clip in the viewport; an empty path shows the bind pose.
    bool AuditionClip(const std::string& path);
    // Moves both clocks; the viewport is extracted separately and never moves them.
    void Advance(double wallSeconds);
    void ExtractViewport();

    // Opens the rig's scenario, shows it in the viewport and brings waiting
    // edits to it; the open scenario's changes are refused, saved or discarded
    // first as `disposition` says.
    bool OpenRig(const std::string& path, DirtyDisposition disposition = DirtyDisposition::Refuse);
    // Refuses before writing anything if any of the rig's files already exists.
    bool CreateRig(const AnimationRigRecipe& recipe, std::string& error);
    bool MigrateClipPlayers(std::string& error);
    // The open scenario's file version, taken as one step.
    bool RevertScenario();
    // Runs in its own session; the working simulation is untouched.
    [[nodiscard]] std::vector<AnimationScenarioRun> RunScenarioBatch(bool againstOpenRig);
    [[nodiscard]] const DataAssetCache& DataCache() const;
    // Includes working clip events not yet saved.
    [[nodiscard]] const AnimationClipCache& Clips() const;
    [[nodiscard]] const SkeletonCache& Skeletons() const;

    // The open rig's scenario and every open clip's events on the document
    // dial, and the dial's key for any document of the workspace.
    void AppendDialDocuments(WorkspaceDocumentFacet& facet);
    [[nodiscard]] std::string DialKeyOf(const DocumentRef& document) const;

    // Declared before the vocabulary and every session that captures it.
    AnimationContentTags Tags;
    DocumentSourceSet& Sources;
    DataDocumentSet Documents;
    AnimationClipEventsSet ClipEvents;
    AnimationContentLists Content;
    AnimationAuditionSelection Audition;
    AnimationViewportExtraction Viewport;
    std::string DocumentError;

    AnimationRigScenario Rig;
    // The game module's hook, then Tags.
    std::function<void(World&)> Vocabulary;
    AnimationTakeComparison Takes;
    AnimationLabRun Lab;
    AnimationClipPlayerScan ClipPlayers;

private:
    void DataDocumentChanged(DataDocument& document, bool residentChanged);
    RuntimeAssets& Assets;
    std::filesystem::path AuthoringRoot;
};

// Opens `path` as the rig, asking first when the open scenario has unsaved
// changes; `opened` hears that it did. False when it was refused at once.
bool OpenRigAsking(AnimationPreviewWorkspace& workspace, PromptQueue::Asker& asker, const std::string& path,
                   std::function<void()> opened);
