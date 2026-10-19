#pragma once
#include <authored/VerbBinding.h>
#include <authored/VerbInvocation.h>
class DataAssetCache;
class PersistentEntityIndex;
[[nodiscard]] VerbAdmission ResolveAuthoredArguments(const AuthoredArguments &constants,
                                                     std::span<const VerbCompiledInput> bindings,
                                                     std::span<const AuthoredValue> inputs,
                                                     const PersistentEntityIndex *entities,
                                                     const DataAssetCache *dataAssets, AuthoredArguments &scratch);
