#pragma once

#include <authored/AuthoredApiDefinition.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbInvocation.h>

#include <string_view>

// A generated verb called as the dispatcher calls it once a binding compiled: every
// argument held to the schema the verb declares, then the adapter. A release or a
// probe carries no arguments.
template<typename Provider>
[[nodiscard]] VerbAnswer CallGeneratedVerb(Provider& provider, std::string_view name, const VerbInvocation& invocation)
{
    for (const AuthoredVerbEntry<Provider>& entry : AuthoredApiDefinition<Provider>::Verbs)
    {
        if (entry.Name != name)
            continue;
        const bool argued = invocation.Arguments != nullptr && invocation.Step != LeaseStep::Release
                         && invocation.Step != LeaseStep::Probe;
        const VerbDefinition definition = entry.Describe();
        for (std::size_t slot = 0; argued && slot < definition.Arguments.Children.size(); ++slot)
            if (!AuthoredValueSatisfiesField(invocation.Arguments->At(slot), definition.Arguments.Children[slot]))
                return VerbArgumentAnswer(VerbAdmission::InvalidArguments, slot);
        return entry.Invoke(provider, invocation);
    }
    return VerbAdmission::Unavailable;
}
