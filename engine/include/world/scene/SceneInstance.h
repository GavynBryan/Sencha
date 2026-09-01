#pragma once

#include <core/assets/AssetId.h>
#include <core/identity/Id.h>
#include <core/identity/StrongId.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneFieldCodec.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

//=============================================================================
// SceneInstance
//
// Which placed scene an entity came from. Carried by every member of a cooked
// placement and of a runtime spawn alike, so "this prefab, as a unit" has one
// meaning regardless of how the entity got here. Names the innermost owning
// instance; nesting is recovered from the authored instance records, not by
// stacking components. A SceneInstanceIndex resource keeps the live group
// addressable through this component's own lifecycle hooks.
//=============================================================================

// Stable identity of one placed scene instance within its containing scene.
// Same 64-bit minting discipline as PersistentEntityId, including the runtime
// namespace bit; distinct type because the two name different things -- an
// instance is a placement of a whole source, an entity id is one entity.
using SceneInstanceId = StrongId<struct SceneInstanceIdTag, std::uint64_t>;

// Runtime-minted instance ids (spawns) live in the high-bit namespace so they
// can never collide with editor-authored placement ids.
inline constexpr std::uint64_t SceneInstanceIdRuntimeBit = 1ull << 63;

// The 16-hex-digit text form, the same spelling every scene-source id uses.
[[nodiscard]] inline std::string SceneInstanceIdToString(SceneInstanceId id)
{
    return PersistentEntityIdToString(PersistentEntityId{ id.Value });
}

[[nodiscard]] inline std::optional<SceneInstanceId>
SceneInstanceIdFromString(std::string_view text)
{
    const std::optional<PersistentEntityId> parsed =
        PersistentEntityIdFromString(text);
    if (!parsed.has_value())
        return std::nullopt;
    return SceneInstanceId{ parsed->Value };
}

struct SceneInstance
{
    // The scene asset this instance expands, as its cook-stamped stable id.
    // Invalid when no id map covered the source (dev cooks); group identity
    // and the index never depend on it.
    AssetId Source;
    SceneInstanceId Id;
};

template <>
struct SceneFieldCodec<SceneInstanceId>
{
    static bool Save(IWriteArchive&, std::string_view, SceneInstanceId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, SceneInstanceId&,
                     SceneSerializationContext&);
};

SENCHA_DECLARE_COMPONENT_TYPE(SceneInstance, "scene_instance");
