#pragma once

#include "authoring/AnimationClipPreviewSession.h"

#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>
#include <render/Material.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>

#include <string>

struct RuntimeAssets;

// What the author auditions -- a mesh or a bare skeleton, a clip and a material,
// each held resident while chosen -- and the clip session that plays them.
class AnimationAuditionSelection
{
public:
    explicit AnimationAuditionSelection(RuntimeAssets& assets);

    AnimationAuditionSelection(const AnimationAuditionSelection&) = delete;
    AnimationAuditionSelection& operator=(const AnimationAuditionSelection&) = delete;
    AnimationAuditionSelection(AnimationAuditionSelection&&) = delete;
    AnimationAuditionSelection& operator=(AnimationAuditionSelection&&) = delete;

    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    // An empty path shows the bind pose.
    bool SelectClip(const std::string& path);
    // An empty path shows the neutral preview material.
    bool SelectMaterial(const std::string& path);

    [[nodiscard]] bool HasMesh() const { return static_cast<bool>(MeshLease); }
    [[nodiscard]] SkinnedMeshHandle Mesh() const { return SkinnedMeshHandle::FromToken(MeshLease.OpaqueToken()); }
    [[nodiscard]] MaterialHandle Material() const;

    AnimationClipPreviewSession Session;
    std::string MeshPath;
    std::string ClipPath;
    std::string MaterialPath;
    std::string Error;

private:
    bool SetSkeletonContent(SkeletonHandle skeleton);

    RuntimeAssets& Assets;
    AssetLease MeshLease;
    AssetLease SkeletonLease;
    AssetLease ClipLease;
    AssetLease MaterialLease;
    MaterialHandle DefaultMaterial;
    AssetLease DefaultMaterialLease;
};
