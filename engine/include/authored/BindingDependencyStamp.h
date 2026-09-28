#pragma once

#include <assets/data/DataAssetHandle.h>
#include <authored/AuthoredCatalog.h>

#include <cstddef>
#include <cstdint>

class DataAssetCache;
class GameplayTagRegistry;

// What a binding was compiled against, one stamp per dependency. A consumer
// rebuilds when any stamp no longer matches; matching allocates nothing.

// Same object, nothing published or retired since.
struct CatalogStamp
{
    std::uint64_t Catalog = 0;
    std::uint64_t Generation = 0;

    template<AuthoredCatalogTraits Traits>
    [[nodiscard]] static CatalogStamp Of(const AuthoredCatalog<Traits>& catalog)
    {
        return CatalogStamp{ catalog.Catalog().Value, catalog.Generation() };
    }

    template<AuthoredCatalogTraits Traits>
    [[nodiscard]] bool Matches(const AuthoredCatalog<Traits>& catalog) const
    {
        return *this == Of(catalog);
    }

    friend bool operator==(const CatalogStamp&, const CatalogStamp&) = default;
};

// Tags only accumulate, so growth is the only change that can resolve a name.
struct TagVocabularyStamp
{
    std::size_t Count = 0;

    [[nodiscard]] static TagVocabularyStamp Of(const GameplayTagRegistry& tags);
    [[nodiscard]] bool Matches(const GameplayTagRegistry& tags) const;
};

struct DataAssetStamp
{
    DataAssetHandle Asset;
    std::uint64_t ReloadVersion = 0;

    [[nodiscard]] static DataAssetStamp Of(const DataAssetCache& cache, DataAssetHandle asset);
    [[nodiscard]] bool Matches(const DataAssetCache& cache) const;
};
