#include "EntityBranches.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <world/identity/PersistentIdComponent.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
    // "Brush 2" joins the "Brush" family; a bespoke "North Door" starts its own.
    std::string_view NameFamily(std::string_view name)
    {
        const std::size_t space = name.find_last_of(' ');
        if (space != std::string_view::npos && space + 1 < name.size()
            && name.find_first_not_of("0123456789", space + 1) == std::string_view::npos)
            return name.substr(0, space);
        return name;
    }

    // Every named record is renamed, not only roots, or two live "Door Handle"s
    // would result. Unnamed sources stay unnamed.
    void ClaimNames(const EditorScene& scene, std::vector<EntitySnapshot>& snapshots)
    {
        std::unordered_set<std::string> claimed;
        for (EntitySnapshot& snapshot : snapshots)
        {
            JsonValue* name = snapshot.Components.Find("name");
            JsonValue* value = name != nullptr ? name->Find("value") : nullptr;
            if (value == nullptr || !value->IsString() || value->AsString().empty())
                continue;
            std::string fresh = scene.NextEntityName(NameFamily(value->AsString()), &claimed);
            claimed.insert(fresh);
            *value = JsonValue(std::move(fresh));
        }
    }
}

std::vector<EntityId> CollectBranches(const EditorScene& scene, std::span<const EntityId> roots)
{
    std::vector<EntityId> members;
    for (const EntityId root : roots)
    {
        const bool ridesAlong = std::ranges::any_of(roots, [&](EntityId other) {
            return other != root && scene.IsAncestorOf(other, root);
        });
        if (ridesAlong || std::ranges::find(members, root) != members.end())
            continue;
        std::vector<EntityId> subtree;
        scene.CollectSubtree(root, subtree);
        for (const EntityId member : subtree)
            if (std::ranges::find(members, member) == members.end())
                members.push_back(member);
    }
    return members;
}

BranchCopy CaptureBranchCopy(const EditorDocument& document, std::span<const EntityId> sources,
                             DuplicateBranchPolicy branch, const SnapshotRemap& remap)
{
    const EditorScene& scene = document.GetScene();
    std::vector<EntityId> expanded;
    if (branch == DuplicateBranchPolicy::Subtree)
        expanded = CollectBranches(scene, sources);
    else
        for (const EntityId source : sources)
            if (std::ranges::find(expanded, source) == expanded.end())
                expanded.push_back(source);

    BranchCopy copy;
    const World& world = scene.GetRegistry().Components;
    for (const EntityId member : expanded)
    {
        copy.Snapshots.push_back(document.CaptureEntity(member));
        const auto* id = world.TryGet<PersistentIdComponent>(member);
        copy.SourceIds.push_back(id != nullptr ? id->Id : PersistentEntityId{});
        std::size_t passed = SIZE_MAX;
        for (std::size_t index = 0; index < sources.size(); ++index)
            if (sources[index] == member)
                passed = index;
        copy.RootOf.push_back(passed);
    }
    if (remap)
        remap(copy.Snapshots);
    ClaimNames(scene, copy.Snapshots);
    return copy;
}

std::vector<EntityId> RestoreBranchCopy(EditorDocument& document, const BranchCopy& copy,
                                        std::span<const Transform3f> placements, bool asInstance)
{
    EditorScene& scene = document.GetScene();
    std::vector<EntityId> created;
    created.reserve(copy.Snapshots.size());
    for (std::size_t i = 0; i < copy.Snapshots.size(); ++i)
    {
        const EntityId entity = document.RestoreEntity(copy.Snapshots[i], !asInstance);
        // Restoring resolves the parent against live identity, which for a
        // copied branch is the original; a root whose parent is outside the
        // branch keeps it, so the copy lands beside its source.
        for (std::size_t j = 0; j < i; ++j)
            if (copy.SourceIds[j].IsValid() && copy.Snapshots[i].ParentId == copy.SourceIds[j])
            {
                (void)scene.SetParent(entity, created[j]);
                break;
            }
        if (copy.RootOf[i] != SIZE_MAX && copy.RootOf[i] < placements.size())
            scene.SetWorldTransform(entity, placements[copy.RootOf[i]]);
        created.push_back(entity);
    }
    return created;
}
