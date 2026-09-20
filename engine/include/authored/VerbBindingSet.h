#pragma once

#include <authored/VerbBinding.h>
#include <authored/VerbBindingCompiler.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

//=============================================================================
// VerbBindingSet
//
// One binding library, instantiated into one World and addressed by key.
//
// The instantiation step is the same for every consumer -- the shell, a screen
// controller, a relay -- so it lives here once instead of being written three
// times with three slightly different answers to "what happens to the binding
// that did not compile".
//
// The answer is: it is left out, and it is reported. A file with one bad
// binding still gives the author the other nine, and the one that is missing
// refuses invocation with a located diagnostic rather than silently doing
// something else.
//
// This is derived state. The authored records stay in the library, which is the
// shared asset; nothing here is edited, and rebuilding it from the library is
// always the way to change it.
//=============================================================================
class VerbBindingSet
{
public:
    // Replaces everything this set holds. A reload that removes a binding
    // removes it: the old behaviour must not keep running because the new file
    // stopped mentioning it.
    void Instantiate(const VerbBindingLibrary& library,
                     const VerbBindingEnvironment& environment,
                     std::vector<std::string>& errors);

    // Adds another library's bindings beside what this set already holds. A
    // key already present is refused and reported rather than replaced: two
    // files claiming one key is an authoring conflict, not a precedence rule.
    void Append(const VerbBindingLibrary& library,
                const VerbBindingEnvironment& environment,
                std::vector<std::string>& errors);

    void Clear() { Bindings.clear(); }

    [[nodiscard]] const CompiledVerbBinding* Find(VerbBindingKey key) const;
    [[nodiscard]] const CompiledVerbBinding* Find(std::string_view key) const;

    [[nodiscard]] std::span<const CompiledVerbBinding> All() const { return Bindings; }
    [[nodiscard]] std::size_t Size() const { return Bindings.size(); }

private:
    // Authored order, and short: a set is one file's bindings, resolved when a
    // consumer is composed or the asset reloads, never per frame.
    std::vector<CompiledVerbBinding> Bindings;
};
