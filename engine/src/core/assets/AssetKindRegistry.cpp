#include <core/assets/AssetKindRegistry.h>

#include <algorithm>
#include <utility>

bool AssetKindRegistry::Register(AssetKindRegistration registration)
{
    if (registration.Type == AssetType::Unknown || registration.Name.empty()
        || Find(registration.Type) != nullptr)
    {
        return false;
    }

    // A half-wired kind would fail later, at a commit or a lease, far from the
    // registration that caused it.
    if ((registration.Store != nullptr) != (registration.Commit != nullptr))
        return false;

    if (registration.Store != nullptr && registration.Store->Type() != registration.Type)
        return false;

    if (registration.Reload != nullptr && !registration.IsLoadable())
        return false;

    for (const std::string& extension : registration.RuntimeExtensions)
    {
        if (extension.empty() || FindTypeByExtension(extension) != AssetType::Unknown)
            return false;
    }

    Registrations.push_back(std::move(registration));
    return true;
}

const AssetKindRegistration* AssetKindRegistry::Find(AssetType type) const
{
    const auto it = std::find_if(Registrations.begin(), Registrations.end(),
        [type](const AssetKindRegistration& registration)
        {
            return registration.Type == type;
        });
    return it == Registrations.end() ? nullptr : &*it;
}

AssetKindRegistration* AssetKindRegistry::Find(AssetType type)
{
    const auto it = std::find_if(Registrations.begin(), Registrations.end(),
        [type](const AssetKindRegistration& registration)
        {
            return registration.Type == type;
        });
    return it == Registrations.end() ? nullptr : &*it;
}

AssetType AssetKindRegistry::FindTypeByExtension(std::string_view extension) const
{
    for (const AssetKindRegistration& registration : Registrations)
    {
        const auto it = std::find(registration.RuntimeExtensions.begin(),
                                  registration.RuntimeExtensions.end(),
                                  extension);
        if (it != registration.RuntimeExtensions.end())
            return registration.Type;
    }
    return AssetType::Unknown;
}

std::span<const AssetKindRegistration> AssetKindRegistry::Entries() const
{
    return Registrations;
}

AssetKindRegistration MakeBuiltinAssetKind(AssetType type)
{
    const auto make = [type](std::string_view name,
                             std::initializer_list<std::string_view> extensions)
    {
        AssetKindRegistration registration;
        registration.Type = type;
        registration.Name = name;
        for (std::string_view extension : extensions)
            registration.RuntimeExtensions.emplace_back(extension);
        return registration;
    };

    // Source formats (.png, .gltf, ...) are deliberately absent: they reach the
    // registry through import-on-demand, registered under their virtual path
    // against the cooked artifact (docs/assets/pipeline.md, Decision B).
    switch (type)
    {
    case AssetType::StaticMesh:    return make("StaticMesh", {".smesh"});
    case AssetType::SkinnedMesh:   return make("SkinnedMesh", {".skmesh"});
    case AssetType::Material:      return make("Material", {".smat"});
    case AssetType::Texture:       return make("Texture", {".stex"});
    case AssetType::Scene:         return make("Scene", {".smap"});
    case AssetType::Audio:         return make("Audio", {".sclip"});
    case AssetType::Skeleton:      return make("Skeleton", {".sskel"});
    case AssetType::AnimationClip: return make("AnimationClip", {".sanim"});
    case AssetType::Data:          return make("Data", {".sdata"});
    default:                       return {};
    }
}

const AssetKindRegistry& BuiltinAssetKindRegistry()
{
    static const AssetKindRegistry kinds = []
    {
        AssetKindRegistry built;
        for (AssetType type : BuiltinAssetKinds())
            (void)built.Register(MakeBuiltinAssetKind(type));
        return built;
    }();
    return kinds;
}

std::span<const AssetType> BuiltinAssetKinds()
{
    static constexpr AssetType kKinds[] = {
        AssetType::StaticMesh,
        AssetType::SkinnedMesh,
        AssetType::Material,
        AssetType::Texture,
        AssetType::Scene,
        AssetType::Audio,
        AssetType::Skeleton,
        AssetType::AnimationClip,
        AssetType::Data,
    };
    return kKinds;
}
