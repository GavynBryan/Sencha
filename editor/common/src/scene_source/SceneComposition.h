#pragma once

#include "scene_source/SceneSourceDocument.h"

#include <core/composition/Composition.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// ResolveSceneComposition
//
// Expands one scene source and its nested instances into a deterministic flat
// entity list -- the one operation the editor projection and the cook both
// consume, so the two can never disagree about what a placement means.
//
// Every instance gets a synthetic root entity whose identity IS the instance
// id: the placement transform is that root's local transform, source roots
// parent to it, and composition happens through ordinary transform propagation
// rather than matrix math here. One rule for every source, single-root or not,
// and the root is the one addressable entity a placement is.
//
// Identity follows decision D3: an expanded entity's id is looked up in the
// placement's recorded EntityIds by its path relative to the instance -- never
// derived. A path with no recorded id is reported in MissingIds and its
// subtree is left out: the editor mints and records, then resolves again; a
// cook refuses, because a cook must never mint.
//
// Overrides apply inner-to-outer: a source's own instances resolve first
// (carrying that source's overrides), then the containing placement's patch,
// add, remove, added entities, and suppressions apply over the result.
// Override paths that match nothing are reported in DanglingOverrides and
// otherwise ignored -- they stay authored in the record, visible, and it is
// the cook that fails on them.
//=============================================================================

struct ResolvedSceneEntity
{
    PersistentEntityId Id;
    PersistentEntityId Parent; // invalid = a root of the resolved scene
    // Address relative to the resolving document: [entityId] for a local,
    // [instanceId, ...] walking inward for expanded content.
    SceneElementPath Path;
    // The innermost instance this entity came from; invalid for locals.
    SceneInstanceId Instance;
    bool IsInstanceRoot = false;
    // Adopted from the document this one extends: same id, no wrapper.
    bool IsInherited = false;
    // True when the RESOLVING document's own placement record added this
    // entity (D4). An inner document's additions arrive as ordinary members:
    // the outer document overrides them, it does not own them.
    bool IsAdded = false;
    bool Hidden = false;
    bool Locked = false;
    Json5Value Components;
    // The same components as the entity's SOURCE defines them, before the
    // resolving document's own overrides are merged on. This is the baseline
    // an override is measured against: diffing live state against the
    // post-override values would make an override already recorded look like
    // no override at all, and the harvest would drop it.
    Json5Value SourceComponents;
    // The brush-mesh sidecar of the document that defined this entity, so a
    // consumer instantiating a brush component can fetch the geometry it
    // names. Non-owning; valid while the source lookup's documents are.
    const Json5Value* SourceBrushMeshes = nullptr;
    // For an entity composed from layers (a base, fragments, a patch): where
    // each component value came from, keyed "$.<Component>.<field>", and the
    // inherited components the resolving document removed.
    CompositionProvenance Provenance;
    std::vector<std::string> RemovedComponents;
};

struct SceneCompositionResult
{
    std::vector<ResolvedSceneEntity> Entities;
    // Paths (relative to their instance) the placement has no minted id for,
    // with the instance they belong to. Non-empty means the source grew since
    // the placement was recorded.
    std::vector<std::pair<SceneInstanceId, SceneElementPath>> MissingIds;
    // Override targets that matched nothing, as "<instance>: <path>" text.
    std::vector<std::string> DanglingOverrides;
    // What did not compose: fragments that disagree, a removal another
    // component owes, a fragment that is not one entity. Paths read
    // "<entity id>.<Component>.<field>" or name the envelope member.
    std::vector<CompositionDiagnostic> Diagnostics;
    // Every document the resolve read, transitively: what a cook of it must
    // be redone for.
    std::vector<std::string> Sources;
};

// Where the resolver finds a source scene by its asset://...sscene reference.
// A real boundary with three sides today: the editor's open-document cache,
// the cook's file loading, and test fixtures.
class ISceneSourceLookup
{
public:
    virtual ~ISceneSourceLookup() = default;
    [[nodiscard]] virtual const SceneSourceDocument* Find(std::string_view assetPath) = 0;
};

// What composition needs to know about a component by its JSON key, which only
// the host's serializer registry and World can say.
class ISceneComponentRules
{
public:
    virtual ~ISceneComponentRules() = default;
    // How the component's scene form composes; null by the version-1 rules.
    [[nodiscard]] virtual const DataFieldSchema* SchemaOf(std::string_view component) const = 0;
    // The components an entity holding `component` is owed, transitively.
    [[nodiscard]] virtual std::vector<std::string> OwedBy(std::string_view component) const = 0;
    [[nodiscard]] virtual bool IsRemovable(std::string_view component) const = 0;
};

// Nullopt on a structural failure: an unresolvable source reference, a cycle
// through the source graph, a local entity reusing an inherited id, or a
// parent that does not exist. Missing minted ids, dangling overrides and
// composition diagnostics are reported in the result for the caller to judge.
// Without `rules`, every component composes by the version-1 rules and no
// removal is checked.
[[nodiscard]] std::optional<SceneCompositionResult> ResolveSceneComposition(
    const SceneSourceDocument& root,
    ISceneSourceLookup& sources,
    std::string* error = nullptr,
    const ISceneComponentRules* rules = nullptr);
