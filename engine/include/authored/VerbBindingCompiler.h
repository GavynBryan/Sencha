#pragma once

#include <authored/VerbBinding.h>
#include <authored/VerbRegistry.h>

#include <string>
#include <string_view>
#include <vector>

class AssetRegistry;
class DataAssetCache;
class GameplayTagRegistry;

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

    // Which assets exist and of what kind, for a reference the schema
    // constrains. Null means unchecked: the reference compiles on its authored
    // form alone. A host that has a registry gives it, and a reference to a
    // missing asset or one of the wrong kind then fails here rather than in
    // the operation that finally leases it.
    const AssetRegistry* Assets = nullptr;

    // Where a data asset's subtype is read, for a field that constrains one.
    // The binding asset declares its data references as eager dependencies, so
    // they are resident by the time an owner-thread pass runs; one that is not
    // fails the subtype check rather than passing it unseen.
    const DataAssetCache* DataAssets = nullptr;
};

// Compiles one authored record against one catalog. False leaves `out`
// unspecified and appends at least one located diagnostic.
//
// An entity constant compiles to its persistent identity, not a handle: the
// dispatcher resolves it at each invocation, so the binding is valid whether
// or not its target is present when it is compiled.
//
// The diagnostics name the binding key and the argument, because the author is
// looking at a file with a dozen of them and "type mismatch" is not an answer.
[[nodiscard]] bool CompileVerbBinding(const VerbBindingDesc& desc,
                                      const VerbBindingEnvironment& environment,
                                      CompiledVerbBinding& out,
                                      std::vector<std::string>& errors);

// Compiles a value a producer authored for one of a compiled binding's inputs,
// in the forms a binding's own constants take: a literal or a tag. The one
// value must satisfy every argument the input fills, exactly as the dispatcher
// checks it. For producers whose inputs are content rather than computed -- a
// clip's events -- so the conversion runs once when the content binds, not on
// every call. False appends a located diagnostic.
[[nodiscard]] bool CompileVerbInputValue(const VerbBindingArgument& authored,
                                         const VerbCompiledInput& input,
                                         const VerbBindingEnvironment& environment,
                                         std::string_view bindingKey,
                                         AuthoredValue& out,
                                         std::vector<std::string>& errors);

// Whether a compiled binding still describes the catalog it was compiled
// against. False after a World teardown, a schema change, or a retirement, and
// the reason a dispatcher rechecks rather than trusting the ids it is handed.
[[nodiscard]] bool IsVerbBindingCurrent(const CompiledVerbBinding& binding,
                                        const VerbRegistry& registry);
