#include "scene_source/SceneComposition.h"

#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    // Paths key the per-instance lookups below; hashing the elements directly
    // keeps the hot loops free of ToString() string builds.
    struct PathHash
    {
        [[nodiscard]] std::size_t operator()(const SceneElementPath& path) const
        {
            std::size_t seed = path.Elements.size();
            for (const std::uint64_t element : path.Elements)
                seed ^= std::hash<std::uint64_t>{}(element)
                     + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    bool Fail(std::string* error, std::string message)
    {
        if (error != nullptr)
            *error = std::move(message);
        return false;
    }

    // The instance root's transform component, in exactly the shape
    // TypeSchema<LocalTransform> serializes ("Transform": { local: ... }), so
    // downstream consumers load it like any authored transform. The inner
    // shape is the format's own.
    [[nodiscard]] Json5Value PlacementComponents(const Transform3f& placement)
    {
        Json5Value transform = Json5Value::MakeObject();
        transform.Members.emplace_back("local", TransformToJson5(placement));
        Json5Value components = Json5Value::MakeObject();
        components.Members.emplace_back("Transform", std::move(transform));
        return components;
    }

    // Objects merge member-by-member; anything else replaces atomically. The
    // patch is sparse on purpose: what it does not mention stays the source's.
    void MergeInto(Json5Value& target, const Json5Value& patch)
    {
        if (!target.IsObject() || !patch.IsObject())
        {
            target = patch;
            return;
        }
        for (const Json5Value::Member& member : patch.Members)
        {
            if (Json5Value* existing = target.FindMutable(member.first))
                MergeInto(*existing, member.second);
            else
                target.Members.push_back(member);
        }
    }

    class Resolver
    {
    public:
        Resolver(ISceneSourceLookup& sources, std::string* error)
            : Sources(sources)
            , Error(error)
        {
        }

        [[nodiscard]] bool Resolve(const SceneSourceDocument& document,
                                   SceneCompositionResult& out)
        {
            for (const SceneSourceEntity& entity : document.Entities)
            {
                ResolvedSceneEntity resolved;
                resolved.Id = entity.Id;
                resolved.Parent = entity.Parent;
                resolved.Path.Elements = { entity.Id.Value };
                resolved.Hidden = entity.Hidden;
                resolved.Locked = entity.Locked;
                resolved.Components = entity.Components;
                resolved.SourceComponents = entity.Components;
                resolved.SourceBrushMeshes = &document.BrushMeshes;
                out.Entities.push_back(std::move(resolved));
            }
            for (const SceneInstanceRecord& instance : document.Instances)
                if (!Expand(instance, out))
                    return false;
            return true;
        }

    private:
        ISceneSourceLookup& Sources;
        std::string* Error = nullptr;
        std::vector<std::string> Stack; // sources in flight, for cycle detection

        [[nodiscard]] bool Expand(const SceneInstanceRecord& instance,
                                  SceneCompositionResult& out)
        {
            for (const std::string& inFlight : Stack)
                if (inFlight == instance.Source)
                    return Fail(Error, "source cycle through '" + instance.Source + "'");

            const SceneSourceDocument* source = Sources.Find(instance.Source);
            if (source == nullptr)
                return Fail(Error, "instance " + SceneIdText(instance.Id.Value)
                    + ": source '" + instance.Source + "' did not resolve");

            SceneCompositionResult inner;
            Stack.push_back(instance.Source);
            const bool resolved = Resolve(*source, inner);
            Stack.pop_back();
            if (!resolved)
                return false;
            out.MissingIds.insert(out.MissingIds.end(),
                                  inner.MissingIds.begin(), inner.MissingIds.end());
            out.DanglingOverrides.insert(out.DanglingOverrides.end(),
                                         inner.DanglingOverrides.begin(),
                                         inner.DanglingOverrides.end());

            // The placement's recorded identities, by inner path.
            std::unordered_map<SceneElementPath, PersistentEntityId, PathHash>
                mintedByPath;
            for (const auto& [path, minted] : instance.EntityIds)
                mintedByPath.emplace(path, minted);

            // Which inner entities each override targets, tracked so a target
            // that matches nothing can be reported rather than silently doing
            // nothing forever.
            const auto danglingText = [&](const SceneElementPath& path)
            {
                return SceneIdText(instance.Id.Value) + ": " + path.ToString();
            };

            // Apply overrides over the inner result, matched on inner paths.
            // One index serves every override family; the loops were each
            // O(overrides x entities) as linear scans.
            std::unordered_map<SceneElementPath, std::size_t, PathHash> innerByPath;
            innerByPath.reserve(inner.Entities.size());
            for (std::size_t entityIndex = 0; entityIndex < inner.Entities.size();
                 ++entityIndex)
                innerByPath.emplace(inner.Entities[entityIndex].Path, entityIndex);
            const auto innerAt =
                [&](const SceneElementPath& path) -> ResolvedSceneEntity*
            {
                const auto found = innerByPath.find(path);
                return found != innerByPath.end() ? &inner.Entities[found->second]
                                                  : nullptr;
            };

            std::unordered_set<std::uint64_t> suppressedIds;
            for (const SceneElementPath& path : instance.Suppressed)
            {
                if (const ResolvedSceneEntity* entity = innerAt(path))
                    suppressedIds.insert(entity->Id.Value);
                else
                    out.DanglingOverrides.push_back(danglingText(path));
            }

            const auto applyByPath =
                [&](const std::vector<std::pair<SceneElementPath, Json5Value>>& group,
                    auto&& apply)
            {
                for (const auto& [path, value] : group)
                {
                    if (ResolvedSceneEntity* entity = innerAt(path))
                        apply(*entity, value);
                    else
                        out.DanglingOverrides.push_back(danglingText(path));
                }
            };
            // Everything this placement inherited, before it overrides any of
            // it. Applied outward, so a nested instance's own overrides count
            // as part of the source the outer placement overrides.
            for (ResolvedSceneEntity& entity : inner.Entities)
                entity.SourceComponents = entity.Components;

            applyByPath(instance.Patches,
                [](ResolvedSceneEntity& entity, const Json5Value& patch)
                { MergeInto(entity.Components, patch); });
            applyByPath(instance.AddedComponents,
                [](ResolvedSceneEntity& entity, const Json5Value& added)
                {
                    for (const Json5Value::Member& member : added.Members)
                    {
                        if (Json5Value* existing = entity.Components.FindMutable(member.first))
                            *existing = member.second;
                        else
                            entity.Components.Members.push_back(member);
                    }
                });
            for (const auto& [path, names] : instance.RemovedComponents)
            {
                if (ResolvedSceneEntity* entity = innerAt(path))
                {
                    for (const std::string& name : names)
                        std::erase_if(entity->Components.Members,
                            [&](const Json5Value::Member& member)
                            { return member.first == name; });
                }
                else
                {
                    out.DanglingOverrides.push_back(danglingText(path));
                }
            }

            // The synthetic instance root: the placement as an ordinary entity
            // whose identity is the instance id. Editing the placement is
            // editing this entity's transform; source roots hang from it.
            ResolvedSceneEntity root;
            root.Id = PersistentEntityId{ instance.Id.Value };
            root.Parent = instance.Parent;
            root.Path.Elements = { instance.Id.Value };
            root.Instance = instance.Id;
            root.IsInstanceRoot = true;
            root.Components = PlacementComponents(instance.Placement);
            if (!instance.Name.empty())
            {
                Json5Value name = Json5Value::MakeObject();
                name.Members.emplace_back("value", Json5Value(instance.Name));
                root.Components.Members.emplace_back("name", std::move(name));
            }
            out.Entities.push_back(std::move(root));

            // Remap and emit the expanded content, in two phases so the
            // outcome cannot depend on emission order: first the complete
            // dropped set -- suppressed entities plus every path the placement
            // has no minted id for -- then the survivors, where an entity with
            // any dropped ancestor is out too, because a child must never
            // outlive its branch.
            std::unordered_map<std::uint64_t, const ResolvedSceneEntity*> innerById;
            for (const ResolvedSceneEntity& entity : inner.Entities)
                innerById.emplace(entity.Id.Value, &entity);

            std::unordered_set<std::uint64_t> droppedIds = suppressedIds;
            for (const ResolvedSceneEntity& entity : inner.Entities)
            {
                if (!mintedByPath.contains(entity.Path))
                {
                    out.MissingIds.emplace_back(instance.Id, entity.Path);
                    droppedIds.insert(entity.Id.Value);
                }
            }

            // Drops only originate at seeds, so one bounded walk up the parent
            // chain against the complete seed set is already transitive.
            const auto isDropped = [&](const ResolvedSceneEntity& entity)
            {
                std::uint64_t current = entity.Id.Value;
                for (std::size_t steps = 0; steps <= inner.Entities.size(); ++steps)
                {
                    if (droppedIds.contains(current))
                        return true;
                    const auto found = innerById.find(current);
                    if (found == innerById.end() || !found->second->Parent.IsValid())
                        return false;
                    current = found->second->Parent.Value;
                }
                return false;
            };

            for (ResolvedSceneEntity& entity : inner.Entities)
            {
                if (isDropped(entity))
                    continue;

                // The drop phase guarantees a minted id for every survivor and
                // its parent; a miss here is a resolver defect, degraded to a
                // dangling diagnostic rather than a throw on document open.
                const auto minted = mintedByPath.find(entity.Path);
                const auto parentOf =
                    [&](const ResolvedSceneEntity& child)
                        -> const PersistentEntityId*
                {
                    if (!child.Parent.IsValid())
                        return nullptr;
                    const auto found = innerById.find(child.Parent.Value);
                    if (found == innerById.end())
                        return nullptr;
                    const auto parentMinted =
                        mintedByPath.find(found->second->Path);
                    return parentMinted != mintedByPath.end()
                        ? &parentMinted->second
                        : nullptr;
                };
                const PersistentEntityId* parentId = parentOf(entity);
                assert(minted != mintedByPath.end());
                if (minted == mintedByPath.end()
                    || (entity.Parent.IsValid() && parentId == nullptr))
                {
                    out.DanglingOverrides.push_back(danglingText(entity.Path));
                    continue;
                }

                // The trees dominate the expansion's cost, and this scratch
                // dies with the call; hand them over instead of copying. The
                // small fields stay put -- descendants still resolve their
                // parent's path through innerById.
                Json5Value components =
                    std::exchange(entity.Components, Json5Value{});
                Json5Value sourceComponents =
                    std::exchange(entity.SourceComponents, Json5Value{});
                ResolvedSceneEntity emitted = entity;
                emitted.Components = std::move(components);
                emitted.SourceComponents = std::move(sourceComponents);
                emitted.Id = minted->second;
                emitted.Parent = parentId != nullptr
                    ? *parentId
                    : PersistentEntityId{ instance.Id.Value };

                SceneElementPath outerPath;
                outerPath.Elements.reserve(1 + entity.Path.Elements.size());
                outerPath.Elements.push_back(instance.Id.Value);
                outerPath.Elements.insert(outerPath.Elements.end(),
                                          entity.Path.Elements.begin(),
                                          entity.Path.Elements.end());
                emitted.Path = std::move(outerPath);
                emitted.Instance = entity.Instance.IsValid() ? entity.Instance
                                                             : instance.Id;
                emitted.IsAdded = false; // an inner add is the inner record's
                out.Entities.push_back(std::move(emitted));
            }

            // Entities this placement adds inside the instance (D4). Their ids
            // are recorded in the document like a local's; an empty parent path
            // hangs them from the instance root.
            for (const SceneAddedEntity& added : instance.AddedEntities)
            {
                ResolvedSceneEntity emitted;
                emitted.Id = added.Id;
                if (added.ParentPath.IsEmpty())
                {
                    emitted.Parent = PersistentEntityId{ instance.Id.Value };
                }
                else
                {
                    const auto minted = mintedByPath.find(added.ParentPath);
                    if (minted == mintedByPath.end())
                    {
                        out.DanglingOverrides.push_back(
                            danglingText(added.ParentPath));
                        continue;
                    }
                    emitted.Parent = minted->second;
                }
                emitted.Path.Elements = { instance.Id.Value, added.Id.Value };
                emitted.Instance = instance.Id;
                emitted.IsAdded = true;
                emitted.Components = added.Components;
                out.Entities.push_back(std::move(emitted));
            }
            return true;
        }
    };
} // namespace

std::optional<SceneCompositionResult> ResolveSceneComposition(
    const SceneSourceDocument& root,
    ISceneSourceLookup& sources,
    std::string* error)
{
    SceneCompositionResult result;
    Resolver resolver(sources, error);
    if (!resolver.Resolve(root, result))
        return std::nullopt;
    return result;
}
