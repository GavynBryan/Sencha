#pragma once

#include <authored/VerbBinding.h>
#include <authored/VerbRegistry.h>

#include <string>
#include <vector>

class GameplayTagRegistry;
class PersistentEntityIndex;

//=============================================================================
// VerbBindingCompiler
//
// The owner-thread pass that turns an authored binding into something one World
// can invoke: names to ids, reference text to resolved values, missing
// arguments to their declared defaults, declared inputs to slot indices.
//
// Its inputs are named explicitly. There is no World handle and no generic
// resource lookup here, because "the compiler can reach anything" is how a
// binding pass acquires the ability to start services while validating content.
// A registry it is not given is a registry whose references it refuses rather
// than resolves.
//=============================================================================
struct VerbBindingEnvironment
{
    // The catalog to resolve names against. Required.
    const VerbRegistry* Verbs = nullptr;

    // Where a tag constant resolves. Null means this World has no tag
    // vocabulary, so a binding naming a tag fails to compile rather than
    // compiling to an invalid id.
    const GameplayTagRegistry* Tags = nullptr;

    // Where an entity constant resolves. Null, or an identity the index does
    // not hold, fails the same way: a reference to an entity that is not here
    // is not a reference that becomes valid by being ignored.
    const PersistentEntityIndex* Entities = nullptr;
};

// Compiles one authored record against one catalog. False leaves `out`
// unspecified and appends at least one located diagnostic.
//
// The diagnostics name the binding key and the argument, because the author is
// looking at a file with a dozen of them and "type mismatch" is not an answer.
[[nodiscard]] bool CompileVerbBinding(const VerbBindingDesc& desc,
                                      const VerbBindingEnvironment& environment,
                                      CompiledVerbBinding& out,
                                      std::vector<std::string>& errors);

// Whether a compiled binding still describes the catalog it was compiled
// against. False after a World teardown, a schema change, or a retirement, and
// the reason a dispatcher rechecks rather than trusting the ids it is handed.
[[nodiscard]] bool IsVerbBindingCurrent(const CompiledVerbBinding& binding,
                                        const VerbRegistry& registry);
