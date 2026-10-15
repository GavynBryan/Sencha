#include "DraftPreview.h"

#include "brush/BrushEvaluation.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/EntityChangeSet.h"
#include "operations/LevelTarget.h"

#include <assets/runtime/RuntimeAssets.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/registry/Registry.h>

namespace
{
    // The changes of `draft` whose `side` exists, staged in a document of their
    // own, each entity where `placedBy` holds it in the world: a staged child
    // has no parent unless its parent changed too.
    std::unique_ptr<DocumentStage> Stage(const LevelDraft& draft, ChangeSetSide side, const EditorDocument* placedBy,
                                         RuntimeAssets* assets, LoggingProvider& logging)
    {
        // What it adds or changes, as it leaves them; or what it deletes, as it finds them.
        EntityChangeSet kept;
        const auto shown = [side](const auto& change) {
            return side == ChangeSetSide::After ? change.After.has_value() : change.Before && !change.After;
        };
        for (const EntityChange& change : draft.Changes.Changes)
            if (shown(change))
                kept.Changes.push_back(change);
        for (const InstanceChange& change : draft.Changes.Instances)
            if (shown(change))
                kept.Instances.push_back(change);
        if (kept.Empty())
            return nullptr;

        auto document = std::make_unique<EditorDocument>(logging);
        document->SetContentRoots(draft.Document->ContentRoots());
        if (assets != nullptr)
            document->SetAssetEnvironment(*assets);
        ApplyEntityChangeSet(*document, kept, side);
        if (placedBy != nullptr)
        {
            const auto* from = placedBy->GetRegistry().Components.TryGetResource<PersistentEntityIndex>();
            const auto* to = document->GetRegistry().Components.TryGetResource<PersistentEntityIndex>();
            EditorScene& scene = document->GetScene();
            for (const EntityChange& change : kept.Changes)
            {
                const EntityId source = from != nullptr ? from->TryResolve(change.Id) : EntityId{};
                const Transform3f* world = source.IsValid() ? placedBy->GetScene().TryGetWorldTransform(source) : nullptr;
                const EntityId staged = to != nullptr ? to->TryResolve(change.Id) : EntityId{};
                if (world != nullptr && staged.IsValid())
                    scene.SetWorldTransform(staged, *world);
            }
        }
        return std::make_unique<DocumentStage>(std::move(document), assets, logging);
    }
}

DraftPreview::DraftPreview(RuntimeAssets* assets, LoggingProvider& logging)
    : Assets(assets)
    , Logging(logging)
{
}

DraftPreview::~DraftPreview() = default;

void DraftPreview::Update(const LevelDraft* draft, const EditorDocument* base, const BrushEvaluationPolicy& policy)
{
    if (draft == nullptr)
    {
        AddedStage.reset();
        RemovedStage.reset();
        ShownInstance = 0;
        return;
    }
    if (draft->Document->Instance() != ShownInstance || draft->Document->Revision() != ShownRevision)
    {
        draft->Document->GetScene().RefreshDerivedTransforms();
        AddedStage = Stage(*draft, ChangeSetSide::After, draft->Document.get(), Assets, Logging);
        RemovedStage = Stage(*draft, ChangeSetSide::Before, base, Assets, Logging);
        ShownInstance = draft->Document->Instance();
        ShownRevision = draft->Document->Revision();
    }
    for (DocumentStage* stage : { AddedStage.get(), RemovedStage.get() })
        if (stage != nullptr)
        {
            stage->SetEvaluationPolicy(policy);
            stage->Build();
        }
}
