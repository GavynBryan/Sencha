// End-to-end: a brush level with an authored IrradianceVolumeComponent cooks
// to a per-zone .sprobe (probe volumes baked against the level's occlusion
// BVH), Indirect lights feed the probe bounce without baking a lightmap, no
// volume means no file, and probe inputs restale the cook hash. Headless.

#include "document/DocumentCook.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"

#include <assets/cook/ProbeBake.h>
#include <assets/probes/ProbeVolumeFormat.h>
#include <core/assets/AssetRef.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryReader.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

class ProbeCookTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_probecook_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);
        const fs::path material = Root / "materials/dev/gray.smat";
        fs::create_directories(material.parent_path());
        std::ofstream(material, std::ios::trunc) << "{}";
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    // A floor slab, one point light with the given contribution, and
    // optionally one probe volume above the floor.
    fs::path AuthorLevel(LightBakeContribution contribution, bool withVolume,
                         Vec3d volumePosition = Vec3d{ 0, 2, 0 },
                         Vec3d lightPosition = Vec3d{ 0, 4, 0 })
    {
        EditorDocument doc(Logging);
        doc.SetDefaultMaterial(
            AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
        doc.GetScene().CreateBrush(Vec3d{ 0, 0, 0 }, Vec3d{ 4.0, 0.25, 4.0 });

        const EntityId lightEntity = doc.GetScene().CreateEntity(lightPosition);
        PointLightComponent light{};
        light.Intensity = 40.0f;
        light.Range = 12.0f;
        light.BakeContribution = contribution;
        doc.GetScene().GetRegistry().Components.AddComponent(lightEntity, light);

        if (withVolume)
        {
            const EntityId volumeEntity = doc.GetScene().CreateEntity(volumePosition);
            IrradianceVolumeComponent volume{};
            volume.HalfExtents = Vec3d(2.0f, 1.0f, 2.0f);
            volume.CellSize = 1.0f;
            volume.Priority = 3;
            doc.GetScene().GetRegistry().Components.AddComponent(volumeEntity, volume);
        }

        const fs::path levelPath = Root / "levels/test.json";
        fs::create_directories(levelPath.parent_path());
        EXPECT_TRUE(doc.SaveAs(levelPath.generic_string()));
        return levelPath;
    }

    bool LoadCookedProbes(ProbeVolumeFile& file)
    {
        std::ifstream stream(Root / ".cooked/levels/test/probes.sprobe",
                             std::ios::binary);
        if (!stream.is_open())
            return false;
        BinaryReader reader(stream);
        return ReadProbeVolumeFile(reader, file);
    }

    static bool AnyNonZero(const std::vector<std::uint16_t>& halves)
    {
        for (const std::uint16_t h : halves)
            if (h != 0)
                return true;
        return false;
    }

    fs::path Root;
    LoggingProvider Logging;
};
} // namespace

TEST_F(ProbeCookTest, AuthoredVolumeBakesSprobeArtifact)
{
    const fs::path levelPath = AuthorLevel(LightBakeContribution::Direct, true);
    const DocumentCookResult result = CookDocument(levelPath, Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    EXPECT_EQ(result.ProbeVolumeCount, 1u);
    // Half extents (2,1,2) at cell 1: point convention gives 5 x 3 x 5.
    EXPECT_EQ(result.ProbeCount, 75u);

    ProbeVolumeFile file;
    ASSERT_TRUE(LoadCookedProbes(file));
    ASSERT_EQ(file.Volumes.size(), 1u);
    const ProbeVolumeRecord& volume = file.Volumes[0];
    EXPECT_EQ(volume.Grid.DimsX, 5u);
    EXPECT_EQ(volume.Grid.DimsY, 3u);
    EXPECT_EQ(volume.Grid.DimsZ, 5u);
    EXPECT_EQ(volume.Priority, 3);
    EXPECT_EQ(volume.StableIndex, 0u);
    EXPECT_EQ(volume.ShHalf.size(),
              volume.Grid.PointCount() * kProbeShChannels * kProbeShCoefficients);
    EXPECT_FALSE(volume.ValidityBits.empty());

    // The default cook ambient is black, so any energy in the payload is
    // bounce from the Direct light off the floor.
    EXPECT_TRUE(AnyNonZero(volume.ShHalf));
}

// A structure-only recook neither targets nor withdraws probes, so they are
// preserved: the .sprobe stays on disk at the stem the reassembled scene locates
// it by. A structure-only cook cannot bake probes, so its presence proves the
// carry-forward.
TEST_F(ProbeCookTest, StructureOnlyRecookPreservesProbes)
{
    const fs::path level = AuthorLevel(LightBakeContribution::Direct, true);
    ASSERT_TRUE(CookDocument(level, Root, 16.0).Success);
    ASSERT_TRUE(fs::exists(Root / ".cooked/levels/test/probes.sprobe"));
    ProbeVolumeFile before;
    ASSERT_TRUE(LoadCookedProbes(before));

    CookProfile structureOnly;
    structureOnly.Id = "structure_only_test";
    structureOnly.Name = "Structure Only Test";
    structureOnly.TargetSteps = { std::string(CookStepIds::RenderMeshes) };

    const DocumentCookResult preserved = CookDocument(
        level, Root, 16.0, nullptr, nullptr, {}, {},
        DocumentCookOptions{ .Profile = &structureOnly });
    ASSERT_TRUE(preserved.Success) << preserved.Error;

    ASSERT_TRUE(fs::exists(Root / ".cooked/levels/test/probes.sprobe"));
    ProbeVolumeFile after;
    ASSERT_TRUE(LoadCookedProbes(after));
    EXPECT_EQ(after.Volumes.size(), before.Volumes.size());
}

TEST_F(ProbeCookTest, NoVolumeCooksNoSprobe)
{
    const fs::path levelPath = AuthorLevel(LightBakeContribution::Direct, false);
    const DocumentCookResult result = CookDocument(levelPath, Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    EXPECT_EQ(result.ProbeVolumeCount, 0u);
    EXPECT_EQ(result.ProbeCount, 0u);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/test/probes.sprobe"));
}

TEST_F(ProbeCookTest, IndirectLightFeedsProbeBounceWithoutBakingDirect)
{
    const fs::path levelPath = AuthorLevel(LightBakeContribution::Indirect, true);
    const DocumentCookResult result = CookDocument(levelPath, Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // The Indirect light stays out of the lightmap entirely...
    EXPECT_EQ(result.DirectLightCount, 0u);
    EXPECT_EQ(result.LightmapAtlasWidth, 0u);
    EXPECT_FALSE(fs::exists(Root / ".cooked/levels/test/lightmap.stex"));

    // ...but its bounce reaches the probes (black ambient, so the payload's
    // only possible energy source is the Indirect light).
    ProbeVolumeFile file;
    ASSERT_TRUE(LoadCookedProbes(file));
    ASSERT_EQ(file.Volumes.size(), 1u);
    EXPECT_TRUE(AnyNonZero(file.Volumes[0].ShHalf));
}

TEST_F(ProbeCookTest, NoneLightContributesNothingToProbes)
{
    const fs::path levelPath = AuthorLevel(LightBakeContribution::None, true);
    const DocumentCookResult result = CookDocument(levelPath, Root, 16.0);
    ASSERT_TRUE(result.Success) << result.Error;

    // Volume still bakes (the file exists) but a None light adds no bounce,
    // and the default ambient is black: an all-zero payload.
    ProbeVolumeFile file;
    ASSERT_TRUE(LoadCookedProbes(file));
    ASSERT_EQ(file.Volumes.size(), 1u);
    EXPECT_EQ(result.ProbeVolumeCount, 1u);
    EXPECT_FALSE(AnyNonZero(file.Volumes[0].ShHalf));
}

TEST_F(ProbeCookTest, RestalesOnVolumeMoveAndProbeTuningOnlyWhenVolumesExist)
{
    const uint64_t at0 = CookDocument(
        AuthorLevel(LightBakeContribution::Direct, true, Vec3d{ 0, 2, 0 }),
        Root, 16.0).ContentHash;
    const uint64_t at1 = CookDocument(
        AuthorLevel(LightBakeContribution::Direct, true, Vec3d{ 1, 2, 0 }),
        Root, 16.0).ContentHash;
    EXPECT_NE(at0, at1);

    LightingCookParams defaults{};
    LightingCookParams brightSky{};
    brightSky.Probe.SkyColor = Vec3d(1.0f, 1.0f, 1.0f);

    const fs::path withVolume = AuthorLevel(LightBakeContribution::Direct, true);
    const uint64_t volDefault =
        CookDocument(withVolume, Root, 16.0, nullptr, nullptr, defaults).ContentHash;
    const uint64_t volSky =
        CookDocument(withVolume, Root, 16.0, nullptr, nullptr, brightSky).ContentHash;
    EXPECT_NE(volDefault, volSky);

    const fs::path noVolume = AuthorLevel(LightBakeContribution::Direct, false);
    const uint64_t plainDefault =
        CookDocument(noVolume, Root, 16.0, nullptr, nullptr, defaults).ContentHash;
    const uint64_t plainSky =
        CookDocument(noVolume, Root, 16.0, nullptr, nullptr, brightSky).ContentHash;
    EXPECT_EQ(plainDefault, plainSky);
}

TEST_F(ProbeCookTest, IndirectLightMoveRestalesCookedSceneAndProbeBounce)
{
    const Vec3d volumeAt{ 0, 2, 0 };
    const Vec3d lightA{ 0, 4, 0 };
    const Vec3d lightB{ 2, 4, 0 };

    // Without a volume an Indirect light changes only passthrough scene state.
    // That still changes the published generation so a cache hit cannot return
    // a cooked scene containing the light's previous transform.
    const uint64_t noVolA = CookDocument(
        AuthorLevel(LightBakeContribution::Indirect, false, volumeAt, lightA),
        Root, 16.0).ContentHash;
    const uint64_t noVolB = CookDocument(
        AuthorLevel(LightBakeContribution::Indirect, false, volumeAt, lightB),
        Root, 16.0).ContentHash;
    EXPECT_NE(noVolA, noVolB);

    // With a volume its bounce is baked, so the same move must restale.
    const uint64_t volA = CookDocument(
        AuthorLevel(LightBakeContribution::Indirect, true, volumeAt, lightA),
        Root, 16.0).ContentHash;
    const uint64_t volB = CookDocument(
        AuthorLevel(LightBakeContribution::Indirect, true, volumeAt, lightB),
        Root, 16.0).ContentHash;
    EXPECT_NE(volA, volB);
}
