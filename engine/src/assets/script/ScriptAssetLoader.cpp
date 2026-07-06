#include <assets/script/ScriptAssetLoader.h>

#include <core/logging/LoggingProvider.h>
#include <script/ScriptBytecodeValidator.h>
#include <script/ScriptModule.h>

#include <format>
#include <memory>
#include <utility>

namespace
{
    // Shared staging: read bytes, parse, validate. Returns the module or an
    // error string. Pure with respect to engine state.
    std::string StageModule(const AssetRecord& record, IAssetSource& source,
                            std::shared_ptr<const ScriptModule>& out)
    {
        std::vector<std::byte> bytes;
        if (!ReadAssetBytes(source, record, bytes))
        {
            return std::format("could not read script bytecode for '{}'", record.Path);
        }
        ScriptModuleParseResult parsed = ParseScriptModule(std::span<const std::byte>(bytes));
        if (!parsed.Ok)
        {
            return std::format("invalid .tbc for '{}': {}", record.Path, parsed.Error);
        }
        const ScriptValidationResult validated = ValidateScriptModule(parsed.Module);
        if (!validated.Ok)
        {
            return std::format("bytecode validation failed for '{}': {} ({})",
                               record.Path, validated.Error, validated.Rule);
        }
        out = std::make_shared<const ScriptModule>(std::move(parsed.Module));
        return {};
    }
}

ScriptAssetLoader::ScriptAssetLoader(LoggingProvider& logging, ScriptCache* cache)
    : Log(logging.GetLogger<ScriptAssetLoader>())
    , Cache(cache)
{
}

AssetStaging ScriptAssetLoader::LoadStaged(const AssetRecord& record, IAssetSource& source)
{
    AssetStaging staging;
    staging.Record = record;

    std::shared_ptr<const ScriptModule> module;
    std::string error = StageModule(record, source, module);
    if (!error.empty())
    {
        staging.Error = std::move(error);
        return staging;
    }
    staging.Payload = std::move(module);
    return staging;
}

AssetCommitResult ScriptAssetLoader::Commit(AssetStaging&& staged)
{
    return { CommitTyped(std::move(staged)).IsValid() };
}

ScriptHandle ScriptAssetLoader::CommitTyped(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("ScriptAssetLoader: commit of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return {};
    }
    if (!Cache)
    {
        Log.Error("ScriptAssetLoader: missing ScriptCache for '{}'", staged.Record.Path);
        return {};
    }
    auto* module = std::any_cast<std::shared_ptr<const ScriptModule>>(&staged.Payload);
    if (module == nullptr)
    {
        Log.Error("ScriptAssetLoader: staging payload for '{}' is not a ScriptModule",
                  staged.Record.Path);
        return {};
    }
    ScriptHandle handle = Cache->Register(staged.Record.Path, *module);
    if (!handle.IsValid())
    {
        Log.Error("ScriptAssetLoader: failed to register script '{}'", staged.Record.Path);
    }
    return handle;
}

ScriptReloadResult ScriptAssetLoader::CommitReload(AssetStaging&& staged)
{
    if (!staged.IsValid())
    {
        Log.Error("ScriptAssetLoader: reload of failed staging for '{}': {}",
                  staged.Record.Path, staged.Error);
        return ScriptReloadResult::NotResident;
    }
    if (!Cache)
    {
        Log.Error("ScriptAssetLoader: missing ScriptCache for reload of '{}'",
                  staged.Record.Path);
        return ScriptReloadResult::NotResident;
    }
    auto* module = std::any_cast<std::shared_ptr<const ScriptModule>>(&staged.Payload);
    if (module == nullptr)
    {
        Log.Error("ScriptAssetLoader: reload payload for '{}' is not a ScriptModule",
                  staged.Record.Path);
        return ScriptReloadResult::NotResident;
    }
    return Cache->ReloadInPlace(staged.Record.Path, *module);
}
