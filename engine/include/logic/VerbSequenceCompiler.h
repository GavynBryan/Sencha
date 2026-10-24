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

// Compiles every element it can; one that does not resolve stays, unresolved
// and reported: such a condition never holds, such a command fails when reached.
void CompileVerbSequence(const VerbSequenceDesc& desc,
                         const VerbSequenceEnvironment& environment,
                         CompiledVerbSequence& out,
                         std::vector<std::string>& diagnostics);
