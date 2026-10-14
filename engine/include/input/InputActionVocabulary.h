#pragma once

#include <cstdint>

class InputActionRegistry;

// The action vocabulary this World's tick records are indexed by, as the resolve
// pass last bound it: what a consumer resolves action names against. Revision moves
// when the vocabulary is rebuilt under the same registry.
struct InputActionVocabulary
{
    const InputActionRegistry* Actions = nullptr;
    std::uint64_t Revision = 0;
};
