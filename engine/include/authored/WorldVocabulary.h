#pragma once

#include <authored/AuthoredEventRegistry.h>
#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbRegistry.h>

#include <string>
#include <vector>

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

// Installs the verb, query and event catalogs this World lacks. Idempotent.
void InstallAuthoredVocabulary(World& world);

[[nodiscard]] AuthoredQueryRegistry* FindAuthoredQueryRegistry(World& world);
[[nodiscard]] const AuthoredQueryRegistry* FindAuthoredQueryRegistry(const World& world);
[[nodiscard]] AuthoredEventRegistry* FindAuthoredEventRegistry(World& world);
[[nodiscard]] const AuthoredEventRegistry* FindAuthoredEventRegistry(const World& world);

// Verbs, then queries, then events.
[[nodiscard]] std::vector<std::string> AuthoredInstallationErrors(const World& world);

[[nodiscard]] VerbRegistry* FindVerbRegistry(World& world);
[[nodiscard]] const VerbRegistry* FindVerbRegistry(const World& world);

// The catalog and tag registry this World actually has. A registry that is
// absent stays null, and a binding naming a tag it cannot resolve fails to
// compile rather than compiling to an invalid id. Asset metadata is not a
// World's to give; a host that has it sets the environment's asset members.
[[nodiscard]] VerbBindingEnvironment MakeVerbBindingEnvironment(const World& world);
