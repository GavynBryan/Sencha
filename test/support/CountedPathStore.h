#pragma once

#include <core/assets/AssetLease.h>
#include <core/assets/AssetStore.h>
#include <core/handle/ILifetimeOwner.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

// An asset store for one kind that holds nothing but names and reference
// counts, for tests that need an asset resident by path without a device.
class CountedPathStore final : public IAssetStore, public ILifetimeOwner
{
public:
    explicit CountedPathStore(AssetType kind)
        : Kind(kind)
    {
    }

    // The token a path's leases carry; tokens start at one so none is empty.
    std::uint64_t Add(std::string_view path)
    {
        const std::uint64_t token = Paths.size() + 1;
        Paths.emplace(token, std::string(path));
        return token;
    }

    [[nodiscard]] int Count(std::uint64_t token) const
    {
        const auto found = Counts.find(token);
        return found != Counts.end() ? found->second : 0;
    }

    AssetType Type() const override { return Kind; }
    bool IsResident(std::string_view path) const override { return TokenOf(path) != 0; }
    AssetLease TryAcquireLease(std::string_view path) override
    {
        const std::uint64_t token = TokenOf(path);
        return token != 0 ? AssetLease::Retain(Kind, *this, token) : AssetLease{};
    }
    void RetainToken(std::uint64_t token) override { Attach(token); }
    void ReleaseToken(std::uint64_t token) override { Detach(token); }
    std::string_view GetPath(std::uint64_t token) const override
    {
        const auto found = Paths.find(token);
        return found != Paths.end() ? std::string_view(found->second) : std::string_view();
    }

    void Attach(std::uint64_t token) override
    {
        if (token != 0)
            ++Counts[token];
    }
    void Detach(std::uint64_t token) override
    {
        if (token != 0)
            --Counts[token];
    }

private:
    std::uint64_t TokenOf(std::string_view path) const
    {
        for (const auto& [token, name] : Paths)
            if (name == path)
                return token;
        return 0;
    }

    AssetType Kind;
    std::map<std::uint64_t, std::string> Paths;
    std::map<std::uint64_t, int> Counts;
};
