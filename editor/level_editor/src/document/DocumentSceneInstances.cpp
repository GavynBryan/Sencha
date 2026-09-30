// Placing, restoring, removing and breaking scene instances: the document's
// authoring API over its instance records. The projection those records expand
// into is SceneInstanceProjection's; what is here is the record keeping and the
// dirty state, which belong to the document.

#include "EditorDocument.h"

#include "scene_source/SceneSourcePaths.h"

#include <algorithm>
#include <string>
#include <utility>

void EditorDocument::MintMissingInstanceIds()
{
    if (Projection.MintMissingIds())
        MarkDirty();
}

std::string EditorDocument::SourceAssetPath() const
{
    if (FilePath.empty())
        return {};
    return MakeSceneSourcePath(Projection.ContentRoots(), FilePath);
}

SceneInstanceId EditorDocument::PlaceSceneInstance(std::string source,
                                                   const Transform3f& placement,
                                                   PersistentEntityId parent,
                                                   std::string* error)
{
    SceneSourceCache& sources = Projection.Sources();
    if (sources.Find(source) == nullptr)
    {
        if (error != nullptr)
            *error = sources.LastError();
        return SceneInstanceId{};
    }

    const SceneInstanceId id{ Scene.MintPersistentId().Value };
    SceneInstanceRecord record;
    record.Id = id;
    record.Parent = parent;
    record.Source = std::move(source);
    record.Placement = placement;
    Retained_.Instances.push_back(std::move(record));

    // Minting an id for every path the source contributes is the authoring act
    // that makes the placement real, and it re-projects on its way out.
    if (Projection.MintMissingIds())
        MarkDirty();

    // By id, never by position: the mint's rebuild harvests, and the record's
    // survival is the placement's success.
    if (FindSceneInstance(id) == nullptr)
    {
        if (error != nullptr)
            *error = "the placement did not survive projection";
        return SceneInstanceId{};
    }
    return id;
}

const SceneInstanceRecord* EditorDocument::FindSceneInstance(SceneInstanceId id) const
{
    for (const SceneInstanceRecord& record : Retained_.Instances)
        if (record.Id == id)
            return &record;
    return nullptr;
}

bool EditorDocument::RestoreSceneInstance(SceneInstanceRecord record)
{
    if (FindSceneInstance(record.Id) != nullptr)
        return false;
    Retained_.Instances.push_back(std::move(record));
    MarkDirty();
    Projection.Rebuild();
    return true;
}

bool EditorDocument::RemoveSceneInstance(SceneInstanceId id, SceneInstanceRecord* removed)
{
    // Harvest first so the captured record carries every unsaved edit; that is
    // what makes an undo of the removal restore the placement as it was.
    Projection.Harvest();
    const auto found = std::find_if(Retained_.Instances.begin(),
                                    Retained_.Instances.end(),
                                    [&](const SceneInstanceRecord& record)
                                    { return record.Id == id; });
    if (found == Retained_.Instances.end())
        return false;
    if (removed != nullptr)
        *removed = *found;
    Retained_.Instances.erase(found);
    MarkDirty();
    Projection.Rebuild();
    return true;
}

bool EditorDocument::BreakSceneInstance(SceneInstanceId id, SceneInstanceRecord* broken)
{
    // The record must say everything the live entities do, because undo will
    // rebuild the placement from it.
    Projection.Harvest();
    const auto found = std::find_if(Retained_.Instances.begin(),
                                    Retained_.Instances.end(),
                                    [&](const SceneInstanceRecord& record)
                                    { return record.Id == id; });
    if (found == Retained_.Instances.end())
        return false;
    if (broken != nullptr)
        *broken = *found;

    // The live entities stay exactly as they are; they simply stop being a
    // projection. Everything the placement contributed -- nested content
    // included -- becomes plain local entities of this document.
    Projection.Forget(id);
    Retained_.Instances.erase(found);
    MarkDirty();
    return true;
}

std::string EditorDocument::SceneInstanceSourceOf(EntityId entity) const
{
    const SceneInstanceId owner = Projection.OwnerOf(entity);
    if (!owner.IsValid())
        return {};
    const SceneInstanceRecord* record = FindSceneInstance(owner);
    return record != nullptr ? record->Source : std::string{};
}
