#pragma once

#include "scene_source/Json5Value.h"
#include "scene_source/SceneElementPath.h"

#include <core/identity/Id.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//=============================================================================
// SceneSourceDocument
//
// The typed form of one .sscene file: what the format guarantees, independent
// of any editor session. Entities are keyed by stable authored ids -- the
// positional hierarchy array of the retired format does not exist here; parent
// is a field on the record. Instances are placements of other .sscene sources
// with their minted entity ids and sparse overrides.
//
// Component payloads stay as Json5 subtrees at this layer. The document bridge
// above refreshes the values a serializer knows from live state on save;
// everything it does not know -- unknown components, unknown fields, comments
// -- rides through untouched, so opening a file in an editor missing a game
// module loses nothing.
//
// Validation here is everything one file can know about itself. What needs the
// source graph -- resolving a source path, a dangling override target, a
// cross-file cycle -- belongs to composition resolution.
//=============================================================================

struct SceneSourceEntity
{
    PersistentEntityId Id;
    // Another local entity's id, or a scene instance's id (parenting to the
    // instance root). Invalid = a root.
    PersistentEntityId Parent;
    bool Hidden = false;
    bool Locked = false;
    Json5Value Components; // object keyed by component name
    std::vector<std::string> LeadingComments;
};

// One entity the containing document adds inside a placed instance (D4). Its
// id is minted by and owned by this document, like a local entity's.
struct SceneAddedEntity
{
    PersistentEntityId Id;
    SceneElementPath ParentPath; // where inside the instance it hangs
    Json5Value Components;
};

struct SceneInstanceRecord
{
    SceneInstanceId Id;
    // Optional local parent: an entity id or another instance's id.
    PersistentEntityId Parent;
    std::string Source; // asset://....sscene
    Transform3f Placement = Transform3f::Identity();

    // The persistent ids this document minted for the entities the instance
    // contributes, keyed by their path through the source graph. Recorded, not
    // derived (decision D3): identity survives source renames and id-map loss
    // because it is written down, not recomputed.
    std::vector<std::pair<SceneElementPath, PersistentEntityId>> EntityIds;

    // Sparse overrides, path-keyed. Patch objects merge field-by-field over
    // the source values; Add supplies whole components the source entity lacks;
    // Remove suppresses component names. AddedEntities and Suppressed are the
    // structural pair: new children inside the instance, and source entities
    // this placement omits.
    std::vector<std::pair<SceneElementPath, Json5Value>> Patches;
    std::vector<std::pair<SceneElementPath, Json5Value>> AddedComponents;
    std::vector<std::pair<SceneElementPath, std::vector<std::string>>> RemovedComponents;
    std::vector<SceneAddedEntity> AddedEntities;
    std::vector<SceneElementPath> Suppressed;

    std::vector<std::string> LeadingComments;
};

struct SceneSourceDocument
{
    static constexpr std::uint32_t Version = 1;

    std::vector<SceneSourceEntity> Entities;
    std::vector<SceneInstanceRecord> Instances;
    Json5Value Settings;    // object; absent members mean defaults
    Json5Value BrushMeshes; // opaque editor sidecar, object or null
    // Root members this build does not know, retained in order for round-trip.
    std::vector<Json5Value::Member> UnknownRoot;

    std::vector<std::string> LeadingComments;
    std::vector<std::string> TrailingComments;
    std::vector<std::string> EndComments;
};

// Parses and validates one .sscene document. On failure returns nullopt with a
// message naming the offending id or path; a file in the retired .level.json
// shape gets the hard-cutover diagnostic rather than a shape error.
[[nodiscard]] std::optional<SceneSourceDocument> ParseSceneSource(
    std::string_view text, std::string* error = nullptr);

// Renders the document in the canonical style. Deterministic: writing an
// unmodified parse reproduces the same bytes the canonical writer produced.
[[nodiscard]] std::string WriteSceneSource(const SceneSourceDocument& document);
