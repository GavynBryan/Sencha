#pragma once

#include <line/ContactLine.h>
#include <world/ComponentRegistrar.h>

// A line prefab's authored definition, the mesh it stretches, and the
// authority's working state while it is out.
using ContactLineComponents = ComponentSet<ContactLine, ContactLineSpan, ContactLineState>;

inline void RegisterContactLineComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<ContactLineComponents>();
}

// A line out from something relocated draws home (docs/gameplay/relocation.md).
void RegisterContactLineRelocation(World& world);
