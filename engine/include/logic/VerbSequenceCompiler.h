#pragma once

#include <authored/AuthoredQueryRegistry.h>
#include <authored/VerbBindingCompiler.h>
#include <logic/VerbSequence.h>

#include <span>
#include <string>
#include <vector>

class World;

// What a sequence resolves against: the binding environment its verbs compile
// in, the query catalog its conditions ask, the World whose component
// identities its entity conditions name, and the ports its producer offers.
struct VerbSequenceEnvironment
{
    VerbBindingEnvironment Bindings;
    const AuthoredQueryRegistry* Queries = nullptr;
    const World* Components = nullptr;
    std::span<const SequencePort> Ports;
};

// Compiles every element it can. One that does not resolve stays in `out`,
// marked unresolved, and is reported in `diagnostics`: an unresolved condition
// never holds, and an unresolved command fails when it is reached. Values are
// checked against what the verb or query declares here, not first at a dispatch.
void CompileVerbSequence(const VerbSequenceDesc& desc,
                         const VerbSequenceEnvironment& environment,
                         CompiledVerbSequence& out,
                         std::vector<std::string>& diagnostics);
