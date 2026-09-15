#include <assets/ui/UiPackageAssetLoader.h>

#include <assets/ui/UiPackageFormat.h>
#include <assets/ui/UiPackageSerializer.h>
#include <core/logging/LoggingProvider.h>

#include <format>
#include <utility>

UiPackageAssetLoader::UiPackageAssetLoader(LoggingProvider& logging, UiPackageCache* cache)
    : Log(logging.GetLogger<UiPackageAssetLoader>())
    , Cache(cache)
{
}

AssetStaging UiPackageAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::vector<std::byte> bytes;
    if (!ReadAssetBytes(source, record, bytes))
    {
        staging.Error = std::format("could not read UI package source for '{}'", record.Path);
        return staging;
    }

    // Sniff the container: a cooked artifact keeps its source's virtual path, so
    // the path may say ".rml" while the bytes are a cooked .sui. Loose markup is
    // deliberately not accepted -- there is no second "just read the .rml"
    // path, because that is the one that quietly becomes the real development
    // architecture (docs/ui/architecture.md).
    if (!LooksLikeSui(bytes.data(), bytes.size()))
    {
        staging.Error = std::format(
            "'{}' is not a cooked .sui; authored UI reaches the runtime through the cook",
            record.Path);
        return staging;
    }

    UiPackage package;
    std::string parseError;
    if (!LoadSuiFromBytes(bytes, package, &parseError))
    {
        staging.Error = std::format("failed to parse .sui for '{}': {}",
                                    record.Path, parseError);
        return staging;
    }

    // The resource table IS the dependency declaration. Nothing later discovers
    // an asset on this package's behalf.
    staging.Dependencies = package.Resources;

    staging.Payload = std::move(package);
    return staging;
}

UiPackageHandle UiPackageAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (Cache == nullptr)
    {
        Log.Error("UiPackageAssetLoader: no cache to commit '{}' into", staged.Record.Path);
        return {};
    }
    if (!staged.IsValid())
    {
        Log.Error("UiPackageAssetLoader: refusing to commit failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }

    auto* package = std::any_cast<UiPackage>(&staged.Payload);
    if (package == nullptr)
    {
        Log.Error("UiPackageAssetLoader: staging for '{}' carries the wrong payload type",
                  staged.Record.Path);
        return {};
    }

    // Authoring problems the cooker noticed. Reported once, at the point the
    // package becomes resident, rather than per frame it draws wrong.
    for (const UiUnsupportedFeature& note : package->Unsupported)
    {
        Log.Warn("UiPackage '{}': '{}' at {}:{} is outside the supported rendering profile",
                 staged.Record.Path, note.Feature, note.SourcePath, note.Line);
    }

    return Cache->Register(staged.Record.Path, std::move(*package));
}

bool UiPackageAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (Cache == nullptr || !staged.IsValid())
        return false;

    auto* package = std::any_cast<UiPackage>(&staged.Payload);
    if (package == nullptr)
        return false;

    // Reported again on reload: an author who just introduced one wants to hear
    // about it now, not at the next cold start.
    for (const UiUnsupportedFeature& note : package->Unsupported)
    {
        Log.Warn("UiPackage '{}': '{}' at {}:{} is outside the supported rendering profile",
                 staged.Record.Path, note.Feature, note.SourcePath, note.Line);
    }

    return Cache->ReloadInPlace(staged.Record.Path, std::move(*package));
}
