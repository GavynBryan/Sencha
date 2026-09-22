#pragma once

#include "authoring/AnimationClipPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "data/DataDocument.h"

#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>

#include <string>
#include <vector>

struct RuntimeAssets;

// Owns preview selections and their leases. Browsing/selecting preview content
// is transient and never edits the asset being inspected.
class AnimationPreviewWorkspace
{
public:
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets);
    void RefreshBrowser();
    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    bool SelectClip(const std::string& path);
    bool SelectMaterial(const std::string& path);
    void Frame(double wallSeconds);
    bool OpenRequestSchema(const std::string& path);
    void SelectDocument(std::size_t index);
    void CancelAuthoringEdit();
    void ValidateDocument(DataDocument& document);
    bool SaveDocument(DataDocument& document);
    bool ReloadDocument(DataDocument& document);

    std::vector<std::string> RequestSchemaPaths;
    std::vector<std::unique_ptr<DataDocument>> Documents;
    std::size_t ActiveDocument = 0;
    std::string DocumentError;

    AnimationClipPreviewSession Session;
    AnimationPreviewScene Scene;
    std::vector<std::string> MeshPaths;
    std::vector<std::string> SkeletonPaths;
    std::vector<std::string> ClipPaths;
    std::vector<std::string> MaterialPaths;
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
    AssetLease DefaultMaterialLease;
    MaterialHandle DefaultMaterial;
};
