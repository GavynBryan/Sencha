#pragma once

#include <authored/VerbBindingCompiler.h>
#include <authored/VerbRegistry.h>

class World;

//=============================================================================
// Where a World's authored vocabulary lives, and how a binding pass finds what
// it needs to resolve against.
//
// The catalog is a World resource because names are a property of the entity
// universe content was loaded into. Everything else a binding needs -- the tag
// vocabulary, the persistent entity index -- already lives there too, so this
// is where the explicit environment the compiler takes is assembled.
//
// That assembly is a named composition step, not a lookup the compiler could
// have done for itself: a compiler holding a World would be a compiler that can
// reach anything, and validating content is not a reason to acquire that.
//=============================================================================

// Adds the catalog if this World has none, and returns the one it now has.
// Idempotent: a host that installs twice has installed once.
VerbRegistry& InstallVerbRegistry(World& world);

[[nodiscard]] VerbRegistry* FindVerbRegistry(World& world);
[[nodiscard]] const VerbRegistry* FindVerbRegistry(const World& world);

// The catalog, tag registry and entity index this World actually has. A
// registry that is absent stays null, and a binding naming a reference it
// cannot resolve fails to compile rather than compiling to an invalid id.
[[nodiscard]] VerbBindingEnvironment MakeVerbBindingEnvironment(const World& world);
