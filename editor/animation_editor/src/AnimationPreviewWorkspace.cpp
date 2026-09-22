#include "AnimationPreviewWorkspace.h"

#include <assets/runtime/RuntimeAssets.h>
#include <assets/data/DataAssetSubtype.h>
#include <anim/AnimRequestSchema.h>

#include <algorithm>

AnimationPreviewWorkspace::AnimationPreviewWorkspace(RuntimeAssets& assets)
    : Assets(assets)
{
    Material material;
    material.BaseColor = Vec4(0.65f, 0.7f, 0.8f, 1.0f);
    DefaultMaterial = Assets.Materials.Create(material);
    DefaultMaterialLease = AssetLease::Adopt(
        AssetType::Material, Assets.Materials, DefaultMaterial.ToToken());
    RefreshBrowser();
}

void AnimationPreviewWorkspace::RefreshBrowser()
{
    MeshPaths.clear();
    SkeletonPaths.clear();
    ClipPaths.clear();
    MaterialPaths.clear();
    RequestSchemaPaths.clear();
    for (const auto& [path, record] : Assets.Registry.Records())
    {
        if (record.Type == AssetType::SkinnedMesh)
            MeshPaths.push_back(path);
        else if (record.Type == AssetType::Skeleton)
            SkeletonPaths.push_back(path);
        else if (record.Type == AssetType::AnimationClip)
            ClipPaths.push_back(path);
        else if (record.Type == AssetType::Material)
            MaterialPaths.push_back(path);
        else if (record.Type == AssetType::Data
            && PeekDataAssetSubtype(Assets.Assets.DefaultSource(), record) == kAnimRequestSchemaType)
            RequestSchemaPaths.push_back(path);
    }
    for (auto* paths : { &MeshPaths, &SkeletonPaths, &ClipPaths, &MaterialPaths, &RequestSchemaPaths })
        std::sort(paths->begin(), paths->end());
}

bool AnimationPreviewWorkspace::OpenRequestSchema(const std::string& path)
{
    for (std::size_t i = 0; i < Documents.size(); ++i)
        if (Documents[i]->VirtualPath() == path) { SelectDocument(i); return true; }
    const auto* record = Assets.Registry.FindByPath(path);
    if (!record || record->Type != AssetType::Data)
    {
        DocumentError = "The request schema is not registered in this project.";
        return false;
    }
    auto document = DataDocument::Open(record->FilePath, path, Assets.DataTypes,
                                      Assets.DataSchemas, &DocumentError);
    if (!document) return false;
    if (document->Subtype() != kAnimRequestSchemaType)
    {
        DocumentError = "Select an animation.request_schema asset.";
        return false;
    }
    Documents.push_back(std::move(document));
    SelectDocument(Documents.size() - 1);
    DocumentError.clear();
    return true;
}

void AnimationPreviewWorkspace::SelectDocument(std::size_t index)
{
    if (index >= Documents.size() || index == ActiveDocument) return;
    CancelAuthoringEdit();
    ActiveDocument = index;
}

void AnimationPreviewWorkspace::CancelAuthoringEdit()
{
    if (ActiveDocument < Documents.size() && Documents[ActiveDocument]->IsEditing())
    {
        Documents[ActiveDocument]->CancelEdit();
        ValidateDocument(*Documents[ActiveDocument]);
    }
}

void AnimationPreviewWorkspace::ValidateDocument(DataDocument& document)
{
    document.Validate(Assets.DataTypes, Assets.DataSchemas);
}

bool AnimationPreviewWorkspace::SaveDocument(DataDocument& document)
{
    document.CommitEdit();
    ValidateDocument(document);
    if (document.IsExternallyModified())
    {
        DocumentError = "File changed on disk. Resolve or reload it before saving; external edits were not overwritten.";
        return false;
    }
    DocumentError.clear();
    return document.Save(&DocumentError);
}

bool AnimationPreviewWorkspace::ReloadDocument(DataDocument& document)
{
    document.CancelEdit();
    if (document.IsDirty())
    {
        DocumentError = "Reload refused: undo or save local edits first.";
        return false;
    }
    DocumentError.clear();
    return document.Reload(Assets.DataTypes, Assets.DataSchemas, &DocumentError);
}

bool AnimationPreviewWorkspace::SetSkeletonContent(SkeletonHandle skeleton)
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
        const auto* current = Assets.AnimationClips.Get(
            AnimationClipHandle::FromToken(ClipLease.OpaqueToken()));
        if (current && current->SkeletonPath == Assets.Skeletons.GetName(skeleton))
            clip = *current;
    }
    if (!Session.SetContent(std::string(Assets.Skeletons.GetName(skeleton)), *value,
                            clip, Error))
        return false;
    if (!clip)
    {
        ClipLease.Reset();
        ClipPath.clear();
    }
    return true;
}

bool AnimationPreviewWorkspace::SelectMesh(const std::string& path)
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

bool AnimationPreviewWorkspace::SelectSkeleton(const std::string& path)
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

bool AnimationPreviewWorkspace::SelectClip(const std::string& path)
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
    const auto* clip = lease ? Assets.AnimationClips.Get(
        AnimationClipHandle::FromToken(lease.OpaqueToken())) : nullptr;
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
        skeleton = skeletonLease ? Assets.Skeletons.Get(
            SkeletonHandle::FromToken(skeletonLease.OpaqueToken())) : nullptr;
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

bool AnimationPreviewWorkspace::SelectMaterial(const std::string& path)
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

void AnimationPreviewWorkspace::Frame(double wallSeconds)
{
    Session.Advance(wallSeconds);
    Scene.Queue.Reset();
    Scene.Poses->Reset();
    Scene.Bounds = Aabb3d::Empty();
    if (!MeshLease)
        return;
    const auto mesh = SkinnedMeshHandle::FromToken(MeshLease.OpaqueToken());
    const auto* geometry = Assets.SkinnedMeshes->Get(mesh);
    if (!geometry)
        return;
    Scene.Bounds = geometry->LocalBounds;
    const auto& palette = Session.Palette();
    // This viewport owns its pose cache and one instance. Scope is nonzero to
    // keep this editor identity distinct from runtime entity namespaces.
    const auto slot = Scene.Poses->AppendInstance(mesh, RenderEntityKey{ .Scope = 1, .Entity = {} },
                                                 static_cast<std::uint32_t>(palette.size()));
    const auto offset = Scene.Poses->Instances[slot].PaletteOffset;
    std::copy(palette.begin(), palette.end(), Scene.Poses->Palettes.begin() + offset);
    const auto material = MaterialLease
        ? MaterialHandle::FromToken(MaterialLease.OpaqueToken()) : DefaultMaterial;
    const auto* value = Assets.Materials.Get(material);
    if (!value)
        return;
    for (std::size_t section = 0; section < geometry->Sections.size(); ++section)
    {
        RenderQueueItem item;
        item.SkinnedMesh = mesh;
        item.Material = material;
        item.SectionIndex = static_cast<std::uint32_t>(section);
        item.WorldBounds = geometry->LocalBounds;
        item.PoseSlot = slot;
        item.Pipeline = SelectOpaquePipeline(*value);
        item.Pass = ResolveMaterialPass(*value);
        if (item.Pass == ShaderPassId::ForwardTransparent)
            Scene.Queue.AddTransparent(item);
        else
            Scene.Queue.AddOpaque(item);
    }
    Scene.Queue.SortOpaque();
}
