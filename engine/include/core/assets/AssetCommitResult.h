#pragma once

#include <core/assets/AssetLease.h>

#include <string>
#include <utility>

// What a commit produced: the handle, or, when it resolved another asset
// through the front door that did not load, that asset's path, or why else it
// failed when the commit can say.
template <typename THandle>
struct AssetCommitResult
{
    THandle Handle{};
    std::string FailedDependency;
    std::string Error;

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

    [[nodiscard]] static AssetCommitResult Failed(std::string error)
    {
        AssetCommitResult result;
        result.Error = std::move(error);
        return result;
    }

    [[nodiscard]] bool IsValid() const { return Handle.IsValid(); }
};

// The type-erased form a registered kind's commit returns.
using AssetCommitOutcome = AssetCommitResult<AssetLease>;

// What an in-place reload did, and why it did not when the kind can say.
struct AssetReloadOutcome
{
    bool Reloaded = false;
    std::string Error;

    AssetReloadOutcome(bool reloaded = false)
        : Reloaded(reloaded)
    {
    }

    [[nodiscard]] static AssetReloadOutcome Failed(std::string error)
    {
        AssetReloadOutcome outcome;
        outcome.Error = std::move(error);
        return outcome;
    }

    [[nodiscard]] explicit operator bool() const { return Reloaded; }
};
