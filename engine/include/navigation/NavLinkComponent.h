#pragma once

#include <core/text/InlineString.h>
#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>
#include <navigation/NavigationIds.h>
#include <world/serialization/SceneFieldCodec.h>

#include <cstdint>
#include <string_view>

// Travel directions a navigation link permits, entry to exit and back.
enum NavLinkDirection : std::uint32_t
{
    NavLinkDirectionForward = 1u << 0,
    NavLinkDirectionReverse = 1u << 1,
    NavLinkDirectionBoth = NavLinkDirectionForward | NavLinkDirectionReverse,
};

// The gameplay-tag name of a traversal kind, stored inline so the component
// stays trivially copyable.
using NavTraversalName = InlineString<64>;

// An authored navigation link: an agent may travel from the entry anchor to the
// exit anchor through a traversal that is not continuous walkable surface. The
// entity's transform origin is the entry anchor; ExitOffset places the exit in
// the entity's local frame. The anchors need not be adjacent -- a link may be
// spatially discontinuous.
//
// This is authoring data. The navigation cook projects it, validates it, and
// writes it into the zone's cooked navigation file; the cooked scene does not
// carry the component, so a runtime world never contains one.
struct SENCHA_COMPONENT("sencha.navigation.link")
       SENCHA_SCHEMA("Nav Link")
       SENCHA_SCENE_CHUNK("NLNK")
NavLink
{
    SENCHA_FIELD("id")
    NavLinkId Id;

    SENCHA_FIELD("exit_offset")
    Vec3d ExitOffset{ 0.0f, 0.0f, 2.0f };

    SENCHA_FIELD("directions")
    std::uint32_t Directions = NavLinkDirectionForward;

    // Gameplay-tag name of the traversal this link requires, for example
    // "navigation.traversal.jump". Bound to a tag id at runtime.
    SENCHA_FIELD("traversal")
    NavTraversalName Traversal;

    // Cost of crossing the link before any query policy is applied, in the same
    // units as walking cost (metres of default-area ground).
    SENCHA_FIELD("base_cost")
    float BaseCost = 1.0f;

    // How close an agent must be to the entry anchor to begin the traversal.
    SENCHA_FIELD("entry_radius")
    float EntryRadius = 0.5f;
};

template <>
struct SceneFieldCodec<NavLinkId>
{
    static bool Save(IWriteArchive&, std::string_view, NavLinkId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, NavLinkId&,
                     SceneSerializationContext&);
};

#if !defined(SENCHA_CODEGEN)
#  include <navigation/NavLinkComponent.sencha.h>
#endif
