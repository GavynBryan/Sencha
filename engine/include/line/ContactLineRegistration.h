#pragma once

#include <line/ContactLine.h>
#include <world/ComponentRegistrar.h>
#include <world/relocation/RelocationRules.h>

// A line prefab's authored definition, the mesh it stretches, and the
// authority's working state while it is out.
using ContactLineComponents = ComponentSet<ContactLine, ContactLineSpan, ContactLineState>;

inline void RegisterContactLineComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ContactLineComponents>();
}

// A line's tip is anchored in the space it was cast in, and nothing defines
// carrying a line across, so a relocation of the line entity is refused.
inline void RegisterContactLineRelocation(World& world)
{
    RefuseRelocation<ContactLineState>(world);
}
