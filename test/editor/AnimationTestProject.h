#pragma once

// A temporary animation project: a content root in the temp directory and a
// RuntimeAssets over it, with procedural skeletons and clips registered the
// way an import would register them.

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct AnimationTestProject
{
    std::filesystem::path Root;
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    std::unique_ptr<RuntimeAssets> Assets;
    std::vector<AssetLease> Leases;

    // `reopen` keeps what an earlier project left in the folder and registers
    // it, as a restarted editor would find it.
    explicit AnimationTestProject(std::string_view name, bool reopen = false)
        : Root(std::filesystem::temp_directory_path() / name)
    {
        if (!reopen)
            std::filesystem::remove_all(Root);
        std::filesystem::create_directories(Root);
        Assets = std::make_unique<RuntimeAssets>(Logging, Serializers);
        if (reopen)
            (void)ScanAssetsDirectory(Root.generic_string(), Assets->Registry, Assets->Assets.Kinds());
    }

    ~AnimationTestProject()
    {
        Leases.clear();
        std::filesystem::remove_all(Root);
    }

    // Joints by name and parent index, root first.
    void Skeleton(const char* path, std::vector<std::pair<const char*, int>> joints = { { "root", -1 } })
    {
        SkeletonData data;
        for (const auto& [name, parent] : joints)
        {
            SkeletonJoint joint;
            joint.Name = name;
            joint.ParentIndex = parent;
            data.Joints.push_back(joint);
        }
        EXPECT_TRUE(Assets->Registry.RegisterOrVerify(
            AssetRecord{ .Type = AssetType::Skeleton, .SourceKind = AssetSourceKind::Procedural, .Path = path }));
        (void)Assets->Skeletons.Register(path, std::move(data));
        Leases.push_back(Assets->Assets.TryAcquireLease(path, AssetType::Skeleton));
    }

    void Clip(const char* path, const char* skeleton, float seconds = 1.0f)
    {
        EXPECT_TRUE(Assets->Registry.RegisterOrVerify(
            AssetRecord{ .Type = AssetType::AnimationClip, .SourceKind = AssetSourceKind::Procedural, .Path = path }));
        AnimationClipData clip;
        clip.DurationSeconds = seconds;
        clip.SkeletonPath = skeleton;
        (void)Assets->AnimationClips.Register(path, std::move(clip), Assets->Skeletons.AcquireOwned(skeleton));
        Leases.push_back(Assets->Assets.TryAcquireLease(path, AssetType::AnimationClip));
    }

    // The engine's own content: the fact schema the selector tiers read.
    void ScanEngineAssets()
    {
        (void)ScanAssetsDirectory((std::filesystem::path(SENCHA_REPO_ROOT) / "engine/assets").generic_string(),
                                  Assets->Registry, Assets->Assets.Kinds());
    }

    void Write(const std::string& relative, std::string_view text) const
    {
        std::filesystem::create_directories((Root / relative).parent_path());
        std::ofstream(Root / relative) << text;
    }

    [[nodiscard]] JsonValue Read(const std::string& relative) const
    {
        std::ifstream in(Root / relative);
        std::stringstream text;
        text << in.rdbuf();
        return *JsonParse(text.str());
    }
};
