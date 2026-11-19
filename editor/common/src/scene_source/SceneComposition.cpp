#include "scene_source/SceneComposition.h"

#include "scene_source/Json5Composition.h"

#include <core/composition/CompositionChain.h>

#include <algorithm>
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

    // How an override that matched nothing is reported: the placement that
    // carried it and the inner path it aimed at.
    [[nodiscard]] std::string DanglingText(const SceneInstanceRecord& instance,
                                           const SceneElementPath& path)
    {
        return SceneIdText(instance.Id.Value) + ": " + path.ToString();
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

    void ApplyVersionOnePatch(Json5Value& target, const Json5Value& patch)
    {
        CompositionReport report;
        ApplyCompositionPatch(target, patch, std::string_view{},
                              CompositionPolicy{ .Schema = nullptr, .Null = CompositionNull::IsAValue }, report);
    }

    DataFieldSchema EntitySchema(const ISceneComponentRules* rules, std::initializer_list<const Json5Value*> layers)
    {
        DataFieldSchema entity = MakeDataField(DataFieldKind::Record, {}, {});
        if (rules == nullptr)
            return entity;
        for (const Json5Value* layer : layers)
            for (const Json5Value::Member& member : layer != nullptr && layer->IsObject() ? layer->Members
                                                                                         : std::vector<Json5Value::Member>{})
            {
                const DataFieldSchema* declared = rules->SchemaOf(member.first);
                if (declared == nullptr || FindChild(entity, member.first) != nullptr)
                    continue;
                DataFieldSchema component = *declared;
                component.Key = member.first;
                component.Required = false;
                entity.Children.push_back(std::move(component));
            }
        return entity;
    }

    template <typename Visit>
    void VisitEntityReferences(Json5Value& value, const DataFieldSchema& schema, const Visit& visit)
    {
        switch (schema.Kind)
        {
        case DataFieldKind::Entity:
            if (value.IsString())
                visit(value.Text);
            return;
        case DataFieldKind::Record:
            if (value.IsObject())
                for (Json5Value::Member& member : value.Members)
                    if (const DataFieldSchema* child = FindChild(schema, member.first))
                        VisitEntityReferences(member.second, *child, visit);
            return;
        case DataFieldKind::Array:
            if (value.IsArray() && schema.Children.size() == 1)
                for (Json5Value& element : value.Elements)
                    VisitEntityReferences(element, schema.Children.front(), visit);
            return;
        case DataFieldKind::Optional:
            if (!value.IsNull() && schema.Children.size() == 1)
                VisitEntityReferences(value, schema.Children.front(), visit);
            return;
        default:
            return;
        }
    }

    CompositionDiagnostic AtEntity(CompositionDiagnostic diagnostic, PersistentEntityId entity)
    {
        diagnostic.Path = SceneIdText(entity.Value) + diagnostic.Path.substr(1);
        return diagnostic;
    }

    //-------------------------------------------------------------------------
    // SourceIndex
    //
    // The three lookups every phase of an expansion addresses the source's
    // content through: which inner entity a path names, which identity the
    // placement minted for that path, and which entity an id belongs to.
    //
    // Built once, before any override runs. The override pass rewrites
    // component trees and nothing else -- no entity is added, dropped, or
    // reordered -- so the indices and the pointers both stay valid across it.
    //-------------------------------------------------------------------------
    struct SourceIndex
    {
        std::unordered_map<SceneElementPath, PersistentEntityId, PathHash> MintedByPath;
        std::unordered_map<SceneElementPath, std::size_t, PathHash>        ByPath;
        std::unordered_map<std::uint64_t, const ResolvedSceneEntity*>      ById;

        [[nodiscard]] static SourceIndex Build(const SceneInstanceRecord& instance,
                                               const SceneCompositionResult& inner)
        {
            SourceIndex index;
            for (const auto& [path, minted] : instance.EntityIds)
                index.MintedByPath.emplace(path, minted);

            index.ByPath.reserve(inner.Entities.size());
            index.ById.reserve(inner.Entities.size());
            for (std::size_t i = 0; i < inner.Entities.size(); ++i)
            {
                index.ByPath.emplace(inner.Entities[i].Path, i);
                index.ById.emplace(inner.Entities[i].Id.Value, &inner.Entities[i]);
            }
            return index;
        }

        [[nodiscard]] ResolvedSceneEntity* At(SceneCompositionResult& inner,
                                              const SceneElementPath& path) const
        {
            const auto found = ByPath.find(path);
            return found != ByPath.end() ? &inner.Entities[found->second] : nullptr;
        }
    };

    class Resolver
    {
    public:
        Resolver(ISceneSourceLookup& sources, std::string* error, const ISceneComponentRules* rules)
            : Sources(sources)
            , Error(error)
            , Rules(rules)
        {
        }

        [[nodiscard]] bool ResolveOutermost(const SceneSourceDocument& document, SceneCompositionResult& out)
        {
            (void)Chain.Enter({});
            const bool resolved = Resolve(document, out);
            Chain.Leave();
            return resolved;
        }

    private:
        [[nodiscard]] bool Resolve(const SceneSourceDocument& document,
                                   SceneCompositionResult& out,
                                   std::string_view asset = {})
        {
            std::unordered_set<std::uint64_t> inherited;
            std::unordered_set<std::uint64_t> orphaned;
            if (!document.Extends.empty())
            {
                SceneCompositionResult base;
                if (!ResolveNested(document.Extends, Unresolved(asset, "$.extends", document.Extends), base, out))
                    return false;
                for (ResolvedSceneEntity& entity : base.Entities)
                {
                    entity.IsInherited = true;
                    entity.Instance = {};
                    entity.IsInstanceRoot = false;
                    entity.IsAdded = false;
                    entity.Path.Elements = { entity.Id.Value };
                    entity.SourceComponents = entity.Components;
                    inherited.insert(entity.Id.Value);
                    out.Entities.push_back(std::move(entity));
                }
                if (!CheckLocalsAgainst(document, inherited, orphaned, out))
                    return false;
            }
            for (const SceneSourceEntity& entity : document.Entities)
            {
                ResolvedSceneEntity resolved;
                resolved.Id = entity.Id;
                resolved.Parent = orphaned.contains(entity.Id.Value) ? PersistentEntityId{} : entity.Parent;
                resolved.Path.Elements = { entity.Id.Value };
                resolved.Hidden = entity.Hidden;
                resolved.Locked = entity.Locked;
                resolved.Components = entity.Components;
                resolved.SourceComponents = entity.Components;
                resolved.SourceBrushMeshes = &document.BrushMeshes;
                out.Entities.push_back(std::move(resolved));
            }
            if (!ComposeLayers(document, asset, out))
                return false;
            for (const SceneInstanceRecord& instance : document.Instances)
                if (!Expand(instance, document.FormatVersion, asset, out))
                    return false;
            return true;
        }

        ISceneSourceLookup& Sources;
        std::string* Error = nullptr;
        const ISceneComponentRules* Rules = nullptr;
        CompositionChain Chain;

        [[nodiscard]] bool ResolveNested(const std::string& path, const std::string& unresolved,
                                         SceneCompositionResult& nested, SceneCompositionResult& out)
        {
            const SceneSourceDocument* document = Sources.Find(path);
            if (document == nullptr)
                return Fail(Error, unresolved);
            if (std::optional<std::string> refusal = Chain.Enter(path))
                return Fail(Error, std::move(*refusal));
            const bool resolved = Resolve(*document, nested, path);
            Chain.Leave();
            if (!resolved)
                return false;
            out.MissingIds.insert(out.MissingIds.end(), nested.MissingIds.begin(), nested.MissingIds.end());
            out.DanglingOverrides.insert(out.DanglingOverrides.end(), nested.DanglingOverrides.begin(),
                                         nested.DanglingOverrides.end());
            out.Diagnostics.insert(out.Diagnostics.end(), nested.Diagnostics.begin(), nested.Diagnostics.end());
            NoteSources(path, nested, out);
            return true;
        }

        [[nodiscard]] static std::string Unresolved(std::string_view referrer, std::string_view field,
                                                    std::string_view path)
        {
            return (referrer.empty() ? std::string("the document") : "'" + std::string(referrer) + "'") + ": "
                + std::string(field) + " names '" + std::string(path) + "', which did not resolve";
        }

        static void NoteSources(const std::string& path, const SceneCompositionResult& nested,
                                SceneCompositionResult& out)
        {
            for (const std::string& source : nested.Sources)
                if (std::ranges::find(out.Sources, source) == out.Sources.end())
                    out.Sources.push_back(source);
            if (std::ranges::find(out.Sources, path) == out.Sources.end())
                out.Sources.push_back(path);
        }

        [[nodiscard]] bool CheckLocalsAgainst(const SceneSourceDocument& document,
                                              const std::unordered_set<std::uint64_t>& inherited,
                                              std::unordered_set<std::uint64_t>& orphaned,
                                              SceneCompositionResult& out) const
        {
            std::unordered_set<std::uint64_t> parentable = inherited;
            for (const SceneSourceEntity& entity : document.Entities)
            {
                if (inherited.contains(entity.Id.Value))
                    return Fail(Error, "entity " + SceneIdText(entity.Id.Value) + " is inherited from '"
                        + document.Extends + "'; a local entity cannot reuse its id");
                parentable.insert(entity.Id.Value);
            }
            for (const SceneInstanceRecord& instance : document.Instances)
                parentable.insert(instance.Id.Value);
            for (const SceneSourceEntity& entity : document.Entities)
                if (entity.Parent.IsValid() && !parentable.contains(entity.Parent.Value))
                {
                    out.Diagnostics.push_back({ "dangling_parent", SceneIdText(entity.Id.Value), document.Extends,
                                                "its parent " + SceneIdText(entity.Parent.Value)
                                                    + " is no longer in the base" });
                    orphaned.insert(entity.Id.Value);
                }
            return true;
        }

        [[nodiscard]] static const ResolvedSceneEntity* RootOf(const SceneCompositionResult& out)
        {
            const ResolvedSceneEntity* root = nullptr;
            for (const ResolvedSceneEntity& entity : out.Entities)
            {
                if (entity.Parent.IsValid())
                    continue;
                if (root != nullptr)
                    return nullptr;
                root = &entity;
            }
            return root;
        }

        struct FragmentLayer
        {
            std::uint64_t Target = 0;
            Json5Value Components;
            std::string Asset;
        };

        [[nodiscard]] bool ComposeLayers(const SceneSourceDocument& document, std::string_view asset,
                                         SceneCompositionResult& out)
        {
            if (document.Extends.empty() && document.Includes.empty() && document.InheritedPatches.empty()
                && document.InheritedRemovals.empty())
                return true;
            std::vector<FragmentLayer> fragments;
            for (std::size_t i = 0; i < document.Includes.size(); ++i)
            {
                const SceneFragmentInclude& include = document.Includes[i];
                const std::string field = "$.includes[" + std::to_string(i) + "]";
                SceneCompositionResult fragment;
                if (!ResolveNested(include.Fragment, Unresolved(asset, field, include.Fragment), fragment, out))
                    return false;
                if (fragment.Entities.size() != 1)
                {
                    out.Diagnostics.push_back({ "fragment_shape", field, std::string(asset),
                                                "'" + include.Fragment + "' is not one entity; a fragment is" });
                    continue;
                }
                if (const SceneSourceDocument* source = Sources.Find(include.Fragment);
                    source != nullptr && !source->Extends.empty())
                {
                    out.Diagnostics.push_back({ "include_extends", field, std::string(asset),
                                                "'" + include.Fragment + "' extends '" + source->Extends
                                                    + "'; include a fragment that states its values itself" });
                    continue;
                }
                std::uint64_t target = include.Onto.Value;
                if (!include.Onto.IsValid())
                {
                    const ResolvedSceneEntity* root = RootOf(out);
                    if (root == nullptr)
                    {
                        out.Diagnostics.push_back({ "fragment_target", field, std::string(asset),
                                                    "there is no one root; name the entity it goes onto" });
                        continue;
                    }
                    target = root->Id.Value;
                }
                if (const auto onto = std::ranges::find(out.Entities, PersistentEntityId{ target }, &ResolvedSceneEntity::Id);
                    onto != out.Entities.end() && !onto->IsInherited)
                {
                    out.Diagnostics.push_back({ "fragment_target", field, std::string(asset),
                                                "a fragment goes onto an entity this prefab inherits" });
                    continue;
                }
                fragments.push_back({ target, std::move(fragment.Entities.front().Components), include.Fragment });
            }

            for (ResolvedSceneEntity& entity : out.Entities)
            {
                const auto patch = std::ranges::find(document.InheritedPatches, entity.Id,
                                                     &std::pair<PersistentEntityId, Json5Value>::first);
                const bool patched = patch != document.InheritedPatches.end() && entity.IsInherited;
                const bool included = std::ranges::any_of(
                    fragments, [&](const FragmentLayer& layer) { return layer.Target == entity.Id.Value; });
                if (entity.IsInherited || included)
                    ComposeEntity(entity, patched ? &patch->second : nullptr, fragments, document, asset, out);
            }
            for (const auto& [id, patch] : document.InheritedPatches)
            {
                const auto target = std::ranges::find(out.Entities, id, &ResolvedSceneEntity::Id);
                if (target == out.Entities.end() || !target->IsInherited)
                    out.DanglingOverrides.push_back("$.patch: " + SceneIdText(id.Value));
            }
            for (const FragmentLayer& layer : fragments)
                if (std::ranges::find(out.Entities, PersistentEntityId{ layer.Target }, &ResolvedSceneEntity::Id)
                    == out.Entities.end())
                    out.DanglingOverrides.push_back("$.includes onto " + SceneIdText(layer.Target));

            for (const auto& [id, names] : document.InheritedRemovals)
            {
                const auto target = std::ranges::find(out.Entities, id, &ResolvedSceneEntity::Id);
                if (target == out.Entities.end() || !target->IsInherited)
                {
                    out.DanglingOverrides.push_back("$.remove: " + SceneIdText(id.Value));
                    continue;
                }
                Remove(*target, names, asset, out);
            }
            return true;
        }

        void ComposeEntity(ResolvedSceneEntity& entity, const Json5Value* patch,
                           const std::vector<FragmentLayer>& fragments, const SceneSourceDocument& document,
                           std::string_view asset, SceneCompositionResult& out) const
        {
            const Json5Value nothing = Json5Value::MakeObject();
            const Json5Value own = entity.Components;
            const Json5Value& local = entity.IsInherited ? (patch != nullptr ? *patch : nothing) : own;
            std::vector<CompositionLayerInput<Json5Value>> includes;
            for (const FragmentLayer& layer : fragments)
                if (layer.Target == entity.Id.Value)
                    includes.push_back({ &layer.Components, layer.Asset, nullptr });
            std::initializer_list<const Json5Value*> all{ &own, &local };
            DataFieldSchema schema = EntitySchema(Rules, all);
            for (const CompositionLayerInput<Json5Value>& include : includes)
                for (DataFieldSchema& extra : EntitySchema(Rules, { include.Value }).Children)
                    if (FindChild(schema, extra.Key) == nullptr)
                        schema.Children.push_back(std::move(extra));

            const CompositionLayerInput<Json5Value> base{ &own, document.Extends, &entity.Provenance };
            const CompositionLayerInput<Json5Value>* baseLayer = entity.IsInherited ? &base : nullptr;
            CompositionReport report;
            const CompositionPolicy policy{ .Schema = &schema, .Null = CompositionNull::Removes, .Provenance = true };
            Json5Value composed = ::ComposeLayers<Json5Value>(baseLayer, includes, { &local, asset, nullptr }, policy, report);
            CompositionReport unused;
            entity.SourceComponents = ::ComposeLayers<Json5Value>(baseLayer, includes, { &nothing, asset, nullptr },
                                                                  { .Schema = &schema, .Null = CompositionNull::Removes },
                                                                  unused);
            entity.Components = std::move(composed);
            entity.Provenance = std::move(report.Provenance);
            for (CompositionDiagnostic& diagnostic : report.Diagnostics)
                out.Diagnostics.push_back(AtEntity(std::move(diagnostic), entity.Id));
        }

        void Remove(ResolvedSceneEntity& entity, const std::vector<std::string>& names, std::string_view asset,
                    SceneCompositionResult& out) const
        {
            const auto at = [&](const std::string& name) { return SceneIdText(entity.Id.Value) + "." + name; };
            std::vector<std::string> going;
            for (const std::string& name : names)
                if (Rules != nullptr && !Rules->IsRemovable(name))
                    out.Diagnostics.push_back(
                        { "not_removable", at(name), std::string(asset), "'" + name + "' cannot be removed" });
                else
                    going.push_back(name);
            for (bool kept = Rules != nullptr; kept;)
            {
                kept = false;
                for (const Json5Value::Member& held : entity.Components.Members)
                {
                    if (std::ranges::find(going, held.first) != going.end())
                        continue;
                    for (const std::string& owed : Rules->OwedBy(held.first))
                        if (const auto blocked = std::ranges::find(going, owed); blocked != going.end())
                        {
                            out.Diagnostics.push_back({ "removes_owed", at(owed), std::string(asset),
                                                        "'" + held.first + "' needs '" + owed
                                                            + "'; remove it too, or keep both" });
                            going.erase(blocked);
                            kept = true;
                        }
                }
            }
            for (const std::string& name : going)
            {
                if (std::erase_if(entity.Components.Members,
                                  [&](const Json5Value::Member& member) { return member.first == name; })
                    == 0)
                {
                    out.DanglingOverrides.push_back("$.remove: " + at(name));
                    continue;
                }
                entity.RemovedComponents.push_back(name);
            }
        }

        // One placement: resolve what it points at, override it, and emit the
        // result into the outer scene under the placement's own identities.
        [[nodiscard]] bool Expand(const SceneInstanceRecord& instance, std::uint32_t version, std::string_view asset,
                                  SceneCompositionResult& out)
        {
            SceneCompositionResult inner;
            if (!ResolveNested(instance.Source,
                               Unresolved(asset, "instance " + SceneIdText(instance.Id.Value) + " source", instance.Source),
                               inner, out))
                return false;

            const SourceIndex index = SourceIndex::Build(instance, inner);
            const std::unordered_set<std::uint64_t> suppressed =
                ApplyOverrides(instance, version, index, inner, out);

            EmitInstanceRoot(instance, out);
            const std::size_t firstSource = out.Entities.size();
            EmitSourceEntities(instance, index, suppressed, inner, out);
            RenameMemberReferences(instance, index, firstSource, out);
            EmitAddedEntities(instance, index, out);
            return true;
        }

        void RenameMemberReferences(const SceneInstanceRecord& instance, const SourceIndex& index,
                                    std::size_t firstSource, SceneCompositionResult& out) const
        {
            if (Rules == nullptr)
                return;
            std::unordered_set<std::uint64_t> emitted;
            for (std::size_t i = firstSource; i < out.Entities.size(); ++i)
                emitted.insert(out.Entities[i].Id.Value);
            for (std::size_t i = firstSource; i < out.Entities.size(); ++i)
            {
                ResolvedSceneEntity& entity = out.Entities[i];
                const DataFieldSchema schema = EntitySchema(Rules, { &entity.Components });
                VisitEntityReferences(entity.Components, schema, [&](std::string& text) {
                    const std::optional<PersistentEntityId> named = PersistentEntityIdFromString(text);
                    const auto member = named ? index.ById.find(named->Value) : index.ById.end();
                    if (member == index.ById.end())
                        return;
                    const auto minted = index.MintedByPath.find(member->second->Path);
                    if (minted == index.MintedByPath.end() || !emitted.contains(minted->second.Value))
                    {
                        out.Diagnostics.push_back({ "dangling_member_reference", SceneIdText(entity.Id.Value),
                                                    instance.Source,
                                                    "names " + text + ", which this placement does not keep" });
                        return;
                    }
                    text = PersistentEntityIdToString(minted->second);
                });
            }
        }

        // The placement's edits, applied over the resolved source. Returns the
        // inner ids it suppressed, which the emit phase drops along with their
        // descendants. An override that matches no inner path is reported
        // rather than silently doing nothing forever.
        [[nodiscard]] std::unordered_set<std::uint64_t> ApplyOverrides(
            const SceneInstanceRecord& instance,
            std::uint32_t version,
            const SourceIndex& index,
            SceneCompositionResult& inner,
            SceneCompositionResult& out)
        {
            std::unordered_set<std::uint64_t> suppressed;
            for (const SceneElementPath& path : instance.Suppressed)
            {
                if (const ResolvedSceneEntity* entity = index.At(inner, path))
                    suppressed.insert(entity->Id.Value);
                else
                    out.DanglingOverrides.push_back(DanglingText(instance, path));
            }

            const auto applyByPath =
                [&](const std::vector<std::pair<SceneElementPath, Json5Value>>& group,
                    auto&& apply)
            {
                for (const auto& [path, value] : group)
                {
                    if (ResolvedSceneEntity* entity = index.At(inner, path))
                        apply(*entity, value);
                    else
                        out.DanglingOverrides.push_back(DanglingText(instance, path));
                }
            };

            // Everything this placement inherited, before it overrides any of
            // it. Applied outward, so a nested instance's own overrides count
            // as part of the source the outer placement overrides.
            for (ResolvedSceneEntity& entity : inner.Entities)
                entity.SourceComponents = entity.Components;

            applyByPath(instance.Patches,
                [&](ResolvedSceneEntity& entity, const Json5Value& patch)
                {
                    if (version < 2)
                    {
                        ApplyVersionOnePatch(entity.Components, patch);
                        return;
                    }
                    const DataFieldSchema schema = EntitySchema(Rules, { &entity.Components, &patch });
                    CompositionReport report;
                    ApplyCompositionPatch(entity.Components, patch, instance.Source,
                                          { .Schema = &schema, .Null = CompositionNull::Removes }, report);
                    for (CompositionDiagnostic& diagnostic : report.Diagnostics)
                        out.Diagnostics.push_back(AtEntity(std::move(diagnostic), entity.Id));
                });
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
                if (ResolvedSceneEntity* entity = index.At(inner, path))
                {
                    for (const std::string& name : names)
                        std::erase_if(entity->Components.Members,
                            [&](const Json5Value::Member& member)
                            { return member.first == name; });
                }
                else
                {
                    out.DanglingOverrides.push_back(DanglingText(instance, path));
                }
            }
            return suppressed;
        }

        // The synthetic instance root: the placement as an ordinary entity
        // whose identity is the instance id. Editing the placement is editing
        // this entity's transform; source roots hang from it.
        static void EmitInstanceRoot(const SceneInstanceRecord& instance,
                                     SceneCompositionResult& out)
        {
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
        }

        // Remap and emit the expanded content, in two phases so the outcome
        // cannot depend on emission order: first the complete dropped set --
        // suppressed entities plus every path the placement has no minted id
        // for -- then the survivors, where an entity with any dropped ancestor
        // is out too, because a child must never outlive its branch.
        static void EmitSourceEntities(const SceneInstanceRecord& instance,
                                       const SourceIndex& index,
                                       const std::unordered_set<std::uint64_t>& suppressed,
                                       SceneCompositionResult& inner,
                                       SceneCompositionResult& out)
        {
            std::unordered_set<std::uint64_t> droppedIds = suppressed;
            for (const ResolvedSceneEntity& entity : inner.Entities)
            {
                if (!index.MintedByPath.contains(entity.Path))
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
                    const auto found = index.ById.find(current);
                    if (found == index.ById.end() || !found->second->Parent.IsValid())
                        return false;
                    current = found->second->Parent.Value;
                }
                return false;
            };

            // The drop phase guarantees a minted id for every survivor and its
            // parent; a miss here is a resolver defect, degraded to a dangling
            // diagnostic rather than a throw on document open.
            const auto parentOf =
                [&](const ResolvedSceneEntity& child) -> const PersistentEntityId*
            {
                if (!child.Parent.IsValid())
                    return nullptr;
                const auto found = index.ById.find(child.Parent.Value);
                if (found == index.ById.end())
                    return nullptr;
                const auto parentMinted = index.MintedByPath.find(found->second->Path);
                return parentMinted != index.MintedByPath.end() ? &parentMinted->second
                                                                : nullptr;
            };

            for (ResolvedSceneEntity& entity : inner.Entities)
            {
                if (isDropped(entity))
                    continue;

                const auto minted = index.MintedByPath.find(entity.Path);
                const PersistentEntityId* parentId = parentOf(entity);
                assert(minted != index.MintedByPath.end());
                if (minted == index.MintedByPath.end()
                    || (entity.Parent.IsValid() && parentId == nullptr))
                {
                    out.DanglingOverrides.push_back(DanglingText(instance, entity.Path));
                    continue;
                }

                // The trees dominate the expansion's cost, and this scratch
                // dies with the call; hand them over instead of copying. The
                // small fields stay put -- descendants still resolve their
                // parent's path through the index.
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
        }

        // Entities this placement adds inside the instance (D4). Their ids are
        // recorded in the document like a local's; an empty parent path hangs
        // them from the instance root.
        static void EmitAddedEntities(const SceneInstanceRecord& instance,
                                      const SourceIndex& index,
                                      SceneCompositionResult& out)
        {
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
                    const auto minted = index.MintedByPath.find(added.ParentPath);
                    if (minted == index.MintedByPath.end())
                    {
                        out.DanglingOverrides.push_back(
                            DanglingText(instance, added.ParentPath));
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
        }
    };
} // namespace

std::optional<SceneCompositionResult> ResolveSceneComposition(
    const SceneSourceDocument& root,
    ISceneSourceLookup& sources,
    std::string* error,
    const ISceneComponentRules* rules)
{
    SceneCompositionResult result;
    Resolver resolver(sources, error, rules);
    if (!resolver.ResolveOutermost(root, result))
        return std::nullopt;
    return result;
}
