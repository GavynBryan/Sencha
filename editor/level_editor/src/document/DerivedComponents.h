#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <string_view>
#include <vector>

class ComponentSerializerRegistry;
class World;

//=============================================================================
// Derived components
//
// What an entity carries that its scene file does not describe.
//
// The inspector draws the serializer registry, which is the authored surface --
// so everything else an entity holds is invisible there: the per-tick columns a
// component declares it cannot work without, and the derived transform columns
// every placed entity gets. A designer reading a prefab has no way to learn
// that one component brought eleven others, and finding out by reading a header
// is what "no surprises" exists to rule out.
//
// This is the query, without a widget: which of the entity's components no
// serializer knows, and which component on the same entity owes each one.
//=============================================================================

struct DerivedComponentRow
{
    ComponentId      Id;
    // The component's stable declared name ("sencha.support_state"), which is a
    // wire key rather than UI copy -- a surface that shows it to a person is
    // expected to humanize it.
    std::string_view Name;
    // The component on this same entity that declared it owes this one, or
    // InvalidComponentId when nothing did: a derived column like WorldTransform
    // is seeded rather than owed.
    ComponentId      ProvidedBy = InvalidComponentId;
};

// In ascending ComponentId order, so a caller drawing this every frame gets the
// same list in the same order. Empty for a dead entity, and empty for an entity
// whose every component is authored.
[[nodiscard]] std::vector<DerivedComponentRow> DerivedComponentsOn(
    const World& world,
    const ComponentSerializerRegistry& serializers,
    EntityId entity);
