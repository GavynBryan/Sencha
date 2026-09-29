#include "authoring/AnimationAuditionSelection.h"

#include <assets/runtime/RuntimeAssets.h>

AnimationAuditionSelection::AnimationAuditionSelection(RuntimeAssets& assets)
    : Assets(assets)
{
    ::Material material;
    material.BaseColor = Vec4(0.65f, 0.7f, 0.8f, 1.0f);
    DefaultMaterial = Assets.Materials.Create(material);
    DefaultMaterialLease = AssetLease::Adopt(AssetType::Material, Assets.Materials, DefaultMaterial.ToToken());
}

bool AnimationAuditionSelection::SelectMesh(const std::string& path)
{
    auto lease = Assets.Assets.LoadLease(path, AssetType::SkinnedMesh);
    if (!lease)
    {
        Error = "Could not load skinned mesh: " + path;
        return false;
    }
    const auto mesh = SkinnedMeshHandle::FromToken(lease.OpaqueToken());
    if (!SetSkeletonContent(Assets.SkinnedMeshes->GetSkeletonHandle(mesh)))
        return false;
    MeshLease = std::move(lease);
    SkeletonLease.Reset();
    MeshPath = path;
    return true;
}

bool AnimationAuditionSelection::SelectSkeleton(const std::string& path)
{
    auto lease = Assets.Assets.LoadLease(path, AssetType::Skeleton);
    if (!lease)
    {
        Error = "Could not load skeleton: " + path;
        return false;
    }
    if (!SetSkeletonContent(SkeletonHandle::FromToken(lease.OpaqueToken())))
        return false;
    SkeletonLease = std::move(lease);
    MeshLease.Reset();
    MeshPath.clear();
    return true;
}

bool AnimationAuditionSelection::SelectClip(const std::string& path)
{
    if (path.empty())
    {
        if (Session.Skeleton().Joints.empty())
            return true;
        if (!Session.SetContent(Session.SkeletonPath(), Session.Skeleton(), std::nullopt, Error))
            return false;
        ClipLease.Reset();
        ClipPath.clear();
        return true;
    }
    auto lease = Assets.Assets.LoadLease(path, AssetType::AnimationClip);
    const auto* clip = lease ? Assets.AnimationClips.Get(AnimationClipHandle::FromToken(lease.OpaqueToken())) : nullptr;
    if (!clip)
    {
        Error = "Could not load animation clip: " + path;
        return false;
    }
    AssetLease skeletonLease;
    const SkeletonData* skeleton = &Session.Skeleton();
    std::string skeletonPath = Session.SkeletonPath();
    if (skeleton->Joints.empty())
    {
        skeletonLease = Assets.Assets.LoadLease(clip->SkeletonPath, AssetType::Skeleton);
        skeleton = skeletonLease ? Assets.Skeletons.Get(SkeletonHandle::FromToken(skeletonLease.OpaqueToken())) : nullptr;
        if (!skeleton)
        {
            Error = "Could not load the clip's skeleton: " + clip->SkeletonPath;
            return false;
        }
        skeletonPath = clip->SkeletonPath;
    }
    if (!Session.SetContent(skeletonPath, *skeleton, *clip, Error))
        return false;
    if (skeletonLease)
        SkeletonLease = std::move(skeletonLease);
    ClipLease = std::move(lease);
    ClipPath = path;
    return true;
}

bool AnimationAuditionSelection::SelectMaterial(const std::string& path)
{
    if (path.empty())
    {
        MaterialLease.Reset();
        MaterialPath.clear();
        Error.clear();
        return true;
    }
    auto lease = Assets.Assets.LoadLease(path, AssetType::Material);
    if (!lease)
    {
        Error = "Could not load material: " + path;
        return false;
    }
    MaterialLease = std::move(lease);
    MaterialPath = path;
    Error.clear();
    return true;
}

MaterialHandle AnimationAuditionSelection::Material() const
{
    return MaterialLease ? MaterialHandle::FromToken(MaterialLease.OpaqueToken()) : DefaultMaterial;
}

bool AnimationAuditionSelection::SetSkeletonContent(SkeletonHandle skeleton)
{
    const auto* value = Assets.Skeletons.Get(skeleton);
    if (value == nullptr)
    {
        Error = "The selected skeleton is not resident.";
        return false;
    }
    std::optional<AnimationClipData> clip;
    if (ClipLease)
    {
        const auto* current = Assets.AnimationClips.Get(AnimationClipHandle::FromToken(ClipLease.OpaqueToken()));
        if (current && current->SkeletonPath == Assets.Skeletons.GetName(skeleton))
            clip = *current;
    }
    if (!Session.SetContent(std::string(Assets.Skeletons.GetName(skeleton)), *value, clip, Error))
        return false;
    if (!clip)
    {
        ClipLease.Reset();
        ClipPath.clear();
    }
    return true;
}
