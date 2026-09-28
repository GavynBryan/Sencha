#include <authored/BindingDependencyStamp.h>

#include <assets/data/DataAssetCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>

TagVocabularyStamp TagVocabularyStamp::Of(const GameplayTagRegistry& tags)
{
    return TagVocabularyStamp{ tags.Size() };
}

bool TagVocabularyStamp::Matches(const GameplayTagRegistry& tags) const
{
    return Count == tags.Size();
}

DataAssetStamp DataAssetStamp::Of(const DataAssetCache& cache, DataAssetHandle asset)
{
    return DataAssetStamp{ asset, cache.GetReloadVersion(asset) };
}

bool DataAssetStamp::Matches(const DataAssetCache& cache) const
{
    return ReloadVersion == cache.GetReloadVersion(Asset);
}
