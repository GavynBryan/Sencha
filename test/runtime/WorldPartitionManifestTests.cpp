#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <zone/WorldPartitionManifest.h>

namespace
{

constexpr const char* CanonicalFixture = R"json(
{
  "format_version": 1,
  "name": "TestWorld",
  "start_zone": "00000000000000a1",
  "regions": [
    { "id": "00000000000000b1", "name": "Chozo Ruins" }
  ],
  "zones": [
    {
      "id": "00000000000000a1",
      "name": "Hub Room",
      "region": "00000000000000b1",
      "scene": "levels/hub_room.level.json",
      "bounds": { "min": [-8.0, 0.0, -8.0], "max": [8.0, 4.0, 8.0] },
      "bounds_overridden": false
    },
    {
      "id": "00000000000000a2",
      "name": "East Hall",
      "region": "00000000000000b1",
      "scene": "levels/east_hall.level.json",
      "bounds": { "min": [8.0, 0.0, -4.0], "max": [24.0, 4.0, 4.0] },
      "bounds_overridden": true
    }
  ],
  "transitions": [
    {
      "id": "00000000000000c1",
      "from": "00000000000000a1",
      "to": "00000000000000a2",
      "topology": "doorway",
      "one_way": false,
      "preload_priority": 0
    }
  ]
}
)json";

WorldPartitionManifest ParseFixture(const char* text)
{
    const auto json = JsonParse(text);
    EXPECT_TRUE(json.has_value());
    std::string error;
    const auto manifest = ReadWorldPartitionManifest(*json, &error);
    EXPECT_TRUE(manifest.has_value()) << error;
    return *manifest;
}

std::optional<WorldPartitionManifest> TryParse(const char* text, std::string* error)
{
    const auto json = JsonParse(text);
    EXPECT_TRUE(json.has_value());
    return ReadWorldPartitionManifest(*json, error);
}

} // namespace

TEST(WorldPartitionManifest, ManifestJsonRoundTrip)
{
    const WorldPartitionManifest first = ParseFixture(CanonicalFixture);

    const JsonValue written = WriteWorldPartitionManifest(first);
    std::string error;
    const auto second = ReadWorldPartitionManifest(written, &error);

    ASSERT_TRUE(second.has_value()) << error;
    EXPECT_EQ(first, *second);
    EXPECT_EQ(first.Name, "TestWorld");
    ASSERT_EQ(second->Zones.size(), 2u);
    EXPECT_EQ(second->Zones[0].Name, "Hub Room");
    EXPECT_EQ(second->Zones[1].BoundsOverridden, true);
    ASSERT_EQ(second->Transitions.size(), 1u);
    EXPECT_EQ(second->Transitions[0].Topology, TransitionTopology::Doorway);
}

TEST(WorldPartitionManifest, ReadRejectsUnsupportedFormatVersion)
{
    std::string error;
    const auto manifest = TryParse(R"({ "format_version": 2 })", &error);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_NE(error.find("format_version"), std::string::npos);
    EXPECT_NE(error.find('2'), std::string::npos);
}

TEST(WorldPartitionManifest, ReadRejectsMalformedZoneId)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "zones": [
        { "id": "not-hex", "bounds": { "min": [0,0,0], "max": [1,1,1] } }
      ]
    })json", &error);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(error, "zones[0].id is malformed");
}

TEST(WorldPartitionManifest, ReadRejectsUnknownTopology)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "transitions": [
        {
          "id": "00000000000000c1",
          "from": "00000000000000a1",
          "to": "00000000000000a2",
          "topology": "wormhole"
        }
      ]
    })json", &error);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(error, "transitions[0].topology is unknown");
}

TEST(WorldPartitionManifest, ReadRejectsMissingBounds)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "zones": [
        { "id": "00000000000000a1" }
      ]
    })json", &error);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(error, "zones[0].bounds is missing");
}

TEST(WorldPartitionManifest, ReadIgnoresUnknownKeys)
{
    const WorldPartitionManifest baseline = ParseFixture(CanonicalFixture);

    const auto json = JsonParse(CanonicalFixture);
    ASSERT_TRUE(json.has_value());
    JsonValue augmented = *json;
    augmented.AsObject().emplace_back("future_field", JsonValue{ "ignored" });
    augmented.AsObject()[3].second.AsArray()[0].AsObject().emplace_back("streaming_hint", JsonValue{ 4.0 });

    std::string error;
    const auto manifest = ReadWorldPartitionManifest(augmented, &error);

    ASSERT_TRUE(manifest.has_value()) << error;
    EXPECT_EQ(*manifest, baseline);
}

TEST(WorldPartitionManifest, ReadAcceptsMissingOptionals)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "name": "Sparse",
      "zones": [
        {
          "id": "00000000000000a1",
          "region": "00000000000000b1",
          "scene": "levels/a.level.json",
          "bounds": { "min": [0,0,0], "max": [1,1,1] }
        }
      ],
      "transitions": [
        {
          "id": "00000000000000c1",
          "from": "00000000000000a1",
          "to": "00000000000000a1"
        }
      ]
    })json", &error);

    ASSERT_TRUE(manifest.has_value()) << error;
    EXPECT_FALSE(manifest->StartZone.IsValid());
    EXPECT_FALSE(manifest->Transitions[0].Flags.OneWay);
    EXPECT_EQ(manifest->Transitions[0].PreloadPriority, 0);
    EXPECT_FALSE(manifest->Zones[0].BoundsOverridden);
}

TEST(WorldPartitionManifest, WriteOmitsEmptyCookedFields)
{
    const WorldPartitionManifest manifest = ParseFixture(CanonicalFixture);
    const std::string text = JsonStringify(WriteWorldPartitionManifest(manifest));

    EXPECT_EQ(text.find("cooked_scene"), std::string::npos);
    EXPECT_EQ(text.find("cooked_collision"), std::string::npos);
    EXPECT_EQ(text.find("content_hash"), std::string::npos);
}

TEST(WorldPartitionManifest, ReadParsesButDoesNotValidate)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "zones": [
        {
          "id": "00000000000000a1",
          "scene": "levels/a.level.json",
          "bounds": { "min": [0,0,0], "max": [1,1,1] }
        }
      ],
      "transitions": [
        {
          "id": "00000000000000c1",
          "from": "00000000000000a1",
          "to": "00000000000000ff"
        }
      ]
    })json", &error);

    ASSERT_TRUE(manifest.has_value()) << error;
    EXPECT_EQ(manifest->Transitions[0].To, ZoneId{ 0xff });
}

TEST(WorldPartitionManifest, TransitionNameRoundTripsAndStaysOptional)
{
    WorldPartitionManifest manifest = ParseFixture(CanonicalFixture);
    ASSERT_EQ(manifest.Transitions.size(), 1u);
    EXPECT_TRUE(manifest.Transitions[0].Name.empty());   // fixture has no name key

    manifest.Transitions[0].Name = "Front Door";
    const JsonValue written = WriteWorldPartitionManifest(manifest);
    std::string error;
    const auto reread = ReadWorldPartitionManifest(written, &error);
    ASSERT_TRUE(reread.has_value()) << error;
    EXPECT_EQ(reread->Transitions[0].Name, "Front Door");

    // An empty name never writes a key (WriteOmitsEmptyCookedFields precedent).
    manifest.Transitions[0].Name.clear();
    const JsonValue bare = WriteWorldPartitionManifest(manifest);
    const auto rereadBare = ReadWorldPartitionManifest(bare, &error);
    ASSERT_TRUE(rereadBare.has_value()) << error;
    EXPECT_TRUE(rereadBare->Transitions[0].Name.empty());
}

TEST(WorldPartitionManifest, GateAndDepthRoundTripAndStayOptional)
{
    WorldPartitionManifest manifest = ParseFixture(CanonicalFixture);
    ASSERT_EQ(manifest.Transitions.size(), 1u);
    EXPECT_TRUE(manifest.Transitions[0].RequiredTags.empty());
    EXPECT_EQ(manifest.Transitions[0].PreloadDepth, 0);

    manifest.Transitions[0].RequiredTags = { "quest.bridge" };
    manifest.Transitions[0].PreloadDepth = 2;
    const JsonValue written = WriteWorldPartitionManifest(manifest);
    std::string error;
    const auto reread = ReadWorldPartitionManifest(written, &error);
    ASSERT_TRUE(reread.has_value()) << error;
    ASSERT_EQ(reread->Transitions[0].RequiredTags.size(), 1u);
    EXPECT_EQ(reread->Transitions[0].RequiredTags[0], "quest.bridge");
    EXPECT_EQ(reread->Transitions[0].PreloadDepth, 2);
}

TEST(WorldPartitionManifest, RegionStreamingRoundTripsPresentSubset)
{
    WorldPartitionManifest manifest = ParseFixture(CanonicalFixture);
    manifest.Regions[0].Streaming.HopCount = 2;
    manifest.Regions[0].Streaming.Radius = 250.0;

    const JsonValue written = WriteWorldPartitionManifest(manifest);
    std::string error;
    const auto reread = ReadWorldPartitionManifest(written, &error);
    ASSERT_TRUE(reread.has_value()) << error;
    EXPECT_EQ(reread->Regions[0].Streaming.HopCount, 2);
    EXPECT_EQ(reread->Regions[0].Streaming.Radius, 250.0);
    EXPECT_FALSE(reread->Regions[0].Streaming.ResidentZoneCap.has_value());
    EXPECT_EQ(*reread, manifest);
}

TEST(WorldPartitionManifest, RegionStreamingOmittedWhenAllAbsent)
{
    // Legacy manifests carry no streaming key and load with every field
    // inherited; writing them back emits no streaming object.
    const WorldPartitionManifest manifest = ParseFixture(CanonicalFixture);
    EXPECT_EQ(manifest.Regions[0].Streaming, RegionStreamingConfig{});

    const std::string text = JsonStringify(WriteWorldPartitionManifest(manifest));
    EXPECT_EQ(text.find("streaming"), std::string::npos);
}

TEST(WorldPartitionManifest, ReadRejectsMalformedRegionStreaming)
{
    std::string error;
    const auto manifest = TryParse(R"json(
    {
      "format_version": 1,
      "regions": [
        { "id": "00000000000000b1", "streaming": { "hop_count": "two" } }
      ]
    })json", &error);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(error, "regions[0].streaming.hop_count must be a 32-bit integer");
}
