// The mesh source's import sidecar: where clip events are authored, because
// the cooked clip is rebuilt from the source on every import.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/MeshImportSettings.h>

#include <span>
#include <string>

namespace
{
    std::span<const std::byte> AsBytes(const std::string& text)
    {
        return { reinterpret_cast<const std::byte*>(text.data()), text.size() };
    }

    constexpr std::string_view kSidecar = R"({
  "version": 1,
  "clips": {
    "Walk": {
      "events": [
        { "key": 4, "name": "Right foot", "time": 0.75, "binding": "anim.footstep",
          "inputs": { "surface": { "tag": "Surface.Grass" } } },
        { "key": 3, "name": "Left foot", "time": 0.25, "binding": "anim.footstep",
          "scope": "cosmetic", "min_weight": 0.3,
          "inputs": { "surface": { "tag": "Surface.Grass" }, "volume": { "const": 0.8 } } }
      ]
    },
    "Swing": { "events": [ { "key": 1, "time": 0.5, "binding": "melee.hit", "scope": "gameplay" } ] }
  }
})";
}

TEST(MeshImportSettings, AnEmptySidecarIsTheDefaults)
{
    MeshImportSettings settings;
    std::string error;
    ASSERT_TRUE(ParseMeshImportSettings({}, settings, &error)) << error;
    EXPECT_TRUE(settings.ClipEvents.empty());
}

TEST(MeshImportSettings, ClipEventsParseInAuthoredOrder)
{
    MeshImportSettings settings;
    std::string error;
    ASSERT_TRUE(ParseMeshImportSettings(AsBytes(std::string(kSidecar)), settings, &error)) << error;
    ASSERT_EQ(settings.ClipEvents.size(), 2u);

    const std::vector<AnimationClipEvent>& walk = settings.ClipEvents.at("Walk");
    ASSERT_EQ(walk.size(), 2u);
    EXPECT_EQ(walk[0].Key, 4u);
    EXPECT_EQ(walk[1].Key, 3u);
    EXPECT_EQ(walk[1].Name, "Left foot");
    EXPECT_FLOAT_EQ(walk[1].Time, 0.25f);
    EXPECT_EQ(walk[1].Binding, "anim.footstep");
    ASSERT_TRUE(walk[1].MinWeight.has_value());
    EXPECT_FLOAT_EQ(*walk[1].MinWeight, 0.3f);
    ASSERT_EQ(walk[1].Inputs.size(), 2u);
    EXPECT_EQ(walk[1].Inputs[0].Source, VerbArgumentSource::Tag);
    EXPECT_EQ(walk[1].Inputs[0].Text, "Surface.Grass");
    EXPECT_EQ(walk[1].Inputs[1].Source, VerbArgumentSource::Literal);
    EXPECT_DOUBLE_EQ(walk[1].Inputs[1].Literal.AsNumber(), 0.8);

    EXPECT_EQ(settings.ClipEvents.at("Swing").at(0).Scope, AnimEventScope::Gameplay);
}

TEST(MeshImportSettings, WhatIsWrittenParsesBackUnchanged)
{
    MeshImportSettings original;
    std::string error;
    ASSERT_TRUE(ParseMeshImportSettings(AsBytes(std::string(kSidecar)), original, &error)) << error;

    const std::string written = WriteMeshImportSettings(original);
    MeshImportSettings reread;
    ASSERT_TRUE(ParseMeshImportSettings(AsBytes(written), reread, &error)) << error << "\n" << written;
    EXPECT_EQ(WriteMeshImportSettings(reread), written);
    ASSERT_EQ(reread.ClipEvents.at("Walk").size(), 2u);
    EXPECT_EQ(reread.ClipEvents.at("Walk")[1].Inputs[1].Literal.AsNumber(), 0.8);
}

TEST(MeshImportSettings, MistakesAreRejectedWithTheirPlace)
{
    const auto rejects = [](std::string text, std::string_view expected) {
        MeshImportSettings settings;
        std::string error;
        EXPECT_FALSE(ParseMeshImportSettings(AsBytes(text), settings, &error)) << text;
        EXPECT_NE(error.find(expected), std::string::npos) << error;
    };
    rejects(R"({"clip": {}})", "'clip' is not a mesh import setting");
    rejects(R"({"clips": {"Walk": {"event": []}}})", "'event' is not a clip setting");
    rejects(R"({"clips": {"Walk": {"events": [{"key": 1, "time": 0.5, "binding": "x", "when": 2}]}}})",
            "'when'");
    rejects(R"({"clips": {"Walk": {"events": [{"key": 1, "time": 0.5, "binding": "x",
                "inputs": {"fx": {"asset": "asset://fx/dust.smat"}}}]}}})",
            "constants on the binding");
    rejects(R"({"clips": {"Walk": {"events": [{"key": 1, "time": 0.5, "binding": "x"},
                                              {"key": 1, "time": 0.6, "binding": "y"}]}}})",
            "event key 1 is used twice");
    rejects(R"({"clips": {"Walk": {"events": [{"key": 2, "time": 1.5, "binding": "x"}]}}})",
            "normalized");
    rejects(R"({"clips": {"Walk": {"events": [{"key": 2, "time": 0.5, "binding": "x", "scope": "gameplay",
                "min_weight": 0.5}]}}})",
            "only a cosmetic event");
}

TEST(MeshImportSettings, AClipPathNamesItsSourceAndItsKey)
{
    const std::optional<MeshClipSource> source = MeshClipSourceOf("asset://chars/hero.blend#anim:Walk");
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->SourceRelPath, "chars/hero.blend");
    EXPECT_EQ(source->ClipName, "Walk");
    EXPECT_FALSE(MeshClipSourceOf("asset://chars/hero.blend#model:Rig").has_value());
    EXPECT_FALSE(MeshClipSourceOf("asset://chars/hero.blend").has_value());
    EXPECT_FALSE(MeshClipSourceOf("asset://chars/hero.blend#anim:").has_value());
}

#endif // SENCHA_ENABLE_COOK
