#pragma once

#include <core/assets/AssetLease.h>

#include <string>
#include <utility>

// What a commit produced: the handle, or, when it resolved another asset
// through the front door that did not load, that asset's path. A commit that
// failed for any other reason leaves both empty and logs its own cause.
template <typename THandle>
struct AssetCommitResult
{
    THandle Handle{};
    std::string FailedDependency;

    AssetCommitResult() = default;
    AssetCommitResult(THandle handle)
        : Handle(std::move(handle))
    {
    }

    [[nodiscard]] static AssetCommitResult DependencyFailed(std::string path)
    {
        AssetCommitResult result;
        result.FailedDependency = std::move(path);
        return result;
    }

    [[nodiscard]] bool IsValid() const { return Handle.IsValid(); }
};

// The type-erased form a registered kind's commit returns.
using AssetCommitOutcome = AssetCommitResult<AssetLease>;
