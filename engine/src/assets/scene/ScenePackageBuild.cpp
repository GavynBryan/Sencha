#include <assets/scene/ScenePackageBuild.h>

#include <assets/runtime/AssetSystem.h>
#include <assets/scene/SceneCache.h>
#include <world/build/EntityBuildPackage.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <any>
#include <utility>

ScenePackageBuild::ScenePackageBuild(AssetSystem& assets, SceneCache& scenes,
                                     const AssetRecord& record)
    : Assets(&assets)
    , Scenes(&scenes)
    , Stager(assets.LoaderFor(AssetType::Scene))
    , Record(record)
    , SceneRef(assets.TryAcquireLease(record.Path, AssetType::Scene))
{
    if (SceneRef.IsValid())
        Resident = scenes.GetShared(SceneHandle::FromToken(SceneRef.OpaqueToken()));
}

ScenePackageBuild::~ScenePackageBuild() = default;

void ScenePackageBuild::Build(const ComponentSerializerRegistry& serializers,
                              const SmapPackageOptions& options)
{
    const SmapContents* contents = Resident.get();
    if (contents == nullptr)
    {
        if (Stager == nullptr)
        {
            ErrorText = "no scene loader is registered";
            return;
        }
        Staging = Stager->LoadStaged(Record, Assets->DefaultSource());
        if (!Staging.IsValid())
        {
            ErrorText = std::move(Staging.Error);
            return;
        }
        Staged = true;
        contents = std::any_cast<SmapContents>(&Staging.Payload);
    }

    auto package = std::make_unique<EntityBuildPackage>();
    SmapError buildError;
    if (!BuildEntityPackageFromSmap(*contents, serializers, *package, options,
                                    &buildError))
    {
        ErrorText = std::move(buildError.Message);
        return;
    }
    Built = std::move(package);
}

EntityBuildPackage* ScenePackageBuild::Package()
{
    return Built.get();
}

const SmapContents* ScenePackageBuild::Contents() const
{
    if (Resident != nullptr)
        return Resident.get();
    return Staged ? std::any_cast<SmapContents>(&Staging.Payload) : nullptr;
}

bool ScenePackageBuild::Settle()
{
    if (ErrorText.empty() && Staged)
    {
        SceneRef = Assets->Commit(std::move(Staging));
        Staged = false;
        if (SceneRef.IsValid())
        {
            Resident = Scenes->GetShared(SceneHandle::FromToken(SceneRef.OpaqueToken()));
        }
        else
        {
            ErrorText = "scene contents did not commit into the cache";
        }
    }

    if (!ErrorText.empty() || Built == nullptr)
    {
        if (ErrorText.empty())
            ErrorText = "package was not produced";
        ReleaseScene();
        return false;
    }
    return true;
}

std::unique_ptr<EntityBuildPackage> ScenePackageBuild::TakePackage()
{
    return std::move(Built);
}

std::shared_ptr<const SmapContents> ScenePackageBuild::ContentsShared() const
{
    return Resident;
}

void ScenePackageBuild::ReleaseScene()
{
    SceneRef.Reset();
}
