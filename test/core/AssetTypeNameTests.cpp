#include <assets/cook/CookedCache.h>
#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
    // Every AssetType that names a real asset kind. Unknown is excluded on
    // purpose: it is the "no type" sentinel and carries no round-trip
    // contract. A new asset type that is missing here still trips -Wswitch in
    // AssetTypeToString, which is the compile-time half of this guard.
    constexpr AssetType kNamedAssetTypes[] = {
        AssetType::StaticMesh,
        AssetType::Material,
        AssetType::Texture,
        AssetType::Scene,
        AssetType::Geometry,
        AssetType::Audio,
        AssetType::Script,
        AssetType::Skeleton,
        AssetType::AnimationClip,
        AssetType::SkinnedMesh,
        AssetType::Collision,
        AssetType::ProbeVolume,
    };

    CookedSourceEntry MakeEntry(std::string sourceRel,
                                std::string artifactRel,
                                AssetType type)
    {
        CookedSourceEntry entry;
        entry.SourceRelPath = std::move(sourceRel);
        entry.InputFingerprint = 0xabcdef0123456789ULL;
        entry.Artifacts.push_back(CookedArtifact{
            "asset://" + artifactRel, artifactRel, type, 77 });
        return entry;
    }
} // namespace

// A type whose name does not survive the mapping is silently written as
// "Unknown" and then rejected on read, so every named type must round trip.
TEST(AssetTypeName, EveryNamedTypeRoundTripsThroughItsString)
{
    for (const AssetType type : kNamedAssetTypes)
    {
        const std::string_view name = AssetTypeToString(type);
        EXPECT_NE(name, "Unknown")
            << "AssetType " << static_cast<uint16_t>(type) << " has no name";

        AssetType parsed = AssetType::Unknown;
        ASSERT_TRUE(AssetTypeFromString(name, parsed))
            << "name '" << name << "' does not parse back";
        EXPECT_EQ(parsed, type);
    }
}

// Catches a gap in the middle of the enum range as well as one at the end.
TEST(AssetTypeName, EveryValueInTheDeclaredRangeIsNamed)
{
    const auto last = static_cast<uint16_t>(AssetType::ProbeVolume);
    for (uint16_t raw = 1; raw <= last; ++raw)
    {
        const auto type = static_cast<AssetType>(raw);
        EXPECT_NE(AssetTypeToString(type), "Unknown")
            << "AssetType value " << raw << " is unnamed";
    }
}

TEST(AssetTypeName, UnknownHasNoNameAndUnnamedTextDoesNotParse)
{
    EXPECT_EQ(AssetTypeToString(AssetType::Unknown), "Unknown");

    AssetType parsed = AssetType::StaticMesh;
    EXPECT_FALSE(AssetTypeFromString("Unknown", parsed));
    EXPECT_FALSE(AssetTypeFromString("NotAnAssetType", parsed));
    EXPECT_FALSE(AssetTypeFromString("", parsed));
}

TEST(CookedCacheIndexAssetTypes, ProbeVolumeArtifactSurvivesRoundTrip)
{
    CookedCacheIndex index;
    index.Put(MakeEntry("levels/test.json",
                        ".cooked/levels/test/probes.sprobe",
                        AssetType::ProbeVolume));

    CookedCacheIndex parsed;
    std::string error;
    ASSERT_TRUE(CookedCacheIndex::FromJson(index.ToJson(), parsed, &error)) << error;

    const CookedSourceEntry* entry = parsed.Find("levels/test.json");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->Artifacts.size(), 1u);
    EXPECT_EQ(entry->Artifacts.front().Type, AssetType::ProbeVolume);
}

// An unreadable artifact type fails the whole index parse, not just its own
// entry, so one unnamed type discards every unrelated source with it and the
// next cook rebuilds the world.
TEST(CookedCacheIndexAssetTypes, ProbeVolumeEntryDoesNotDiscardUnrelatedSources)
{
    CookedCacheIndex index;
    index.Put(MakeEntry("meshes/crate.gltf",
                        ".cooked/meshes/crate.smesh",
                        AssetType::StaticMesh));
    index.Put(MakeEntry("levels/test.json",
                        ".cooked/levels/test/probes.sprobe",
                        AssetType::ProbeVolume));

    CookedCacheIndex parsed;
    std::string error;
    ASSERT_TRUE(CookedCacheIndex::FromJson(index.ToJson(), parsed, &error)) << error;

    EXPECT_EQ(parsed.Size(), 2u);
    EXPECT_NE(parsed.Find("meshes/crate.gltf"), nullptr);
    EXPECT_NE(parsed.Find("levels/test.json"), nullptr);
}
