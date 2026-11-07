#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <render/PresentationLayers.h>
#include <render/PresentationSettings.h>
#include <render/PresentationSettingsPublisher.h>
#include <render/ZonePresentation.h>
#include <render/post/EnginePostEffects.h>
#include <render/post/PostEffectCatalog.h>
#include <render/post/PostEffectContributions.h>
#include <world/RuntimeWorld.h>
#include <zone/WorldPartitionManifest.h>

#include <string>

namespace
{
PresentationSettingsLayer Parse(const char* text)
{
    const std::optional<JsonValue> json = JsonParse(text);
    EXPECT_TRUE(json.has_value());
    PresentationSettingsLayer layer;
    std::string error;
    EXPECT_TRUE(ReadPresentationSettingsLayer(*json, layer, error)) << error;
    return layer;
}

std::string Refusal(const char* text)
{
    const std::optional<JsonValue> json = JsonParse(text);
    EXPECT_TRUE(json.has_value());
    PresentationSettingsLayer layer;
    std::string error;
    EXPECT_FALSE(ReadPresentationSettingsLayer(*json, layer, error));
    return error;
}

PresentationSettingsLayer Intensity(float value)
{
    PresentationSettingsLayer layer;
    layer.Bloom.Intensity = value;
    return layer;
}

WorldComponentSchema EmptySchema()
{
    WorldComponentSchema schema;
    schema.Seal();
    return schema;
}

struct Views
{
    World Entities;
    PostEffectCatalog Catalog;
    EntityId Camera = Entities.CreateEntity();

    Views() { RegisterEnginePostEffects(Catalog); }

    [[nodiscard]] PostEffectFrame Frame() const
    {
        PostEffectFrame frame;
        ExtractPostEffects(Entities, Camera, Catalog, 0.0f, frame);
        return frame;
    }
};
} // namespace

TEST(PresentationSettings, ALayerWritesOnlyWhatItSetsAndReadsItBack)
{
    const PresentationSettingsLayer layer = Parse(R"({ "bloom": { "enabled": true, "intensity": 0.8 } })");
    EXPECT_EQ(layer.Bloom.Enabled, true);
    EXPECT_EQ(layer.Bloom.Intensity, 0.8f);
    EXPECT_FALSE(layer.Bloom.Threshold.has_value());
    EXPECT_FALSE(layer.Bloom.Spread.has_value());

    PresentationSettingsLayer back;
    std::string error;
    ASSERT_TRUE(ReadPresentationSettingsLayer(WritePresentationSettingsLayer(layer), back, error)) << error;
    EXPECT_EQ(back, layer);

    const JsonValue empty = WritePresentationSettingsLayer({});
    ASSERT_TRUE(empty.IsObject());
    EXPECT_TRUE(empty.AsObject().empty());
}

TEST(PresentationSettings, ReadRefusesValuesTheSchemaDoesNotAllow)
{
    EXPECT_NE(Refusal(R"({ "bloom": { "intensity": -1 } })").find("intensity"), std::string::npos);
    EXPECT_NE(Refusal(R"({ "bloom": { "enabled": 1 } })").find("enabled"), std::string::npos);
    EXPECT_NE(Refusal(R"({ "bloom": { "glow": 2 } })").find("glow"), std::string::npos);
    EXPECT_NE(Refusal(R"({ "fog": {} })").find("fog"), std::string::npos);
}

TEST(PresentationSettings, EachLayerOverridesOnlyTheFieldsItSets)
{
    PresentationSettings settings;
    EXPECT_FALSE(settings.Bloom.Enabled);

    ApplyPresentationSettingsLayer(settings, Parse(R"({ "bloom": { "enabled": true, "threshold": 2, "intensity": 3 } })"));
    ApplyPresentationSettingsLayer(settings, Intensity(0.5f));

    EXPECT_TRUE(settings.Bloom.Enabled);
    EXPECT_EQ(settings.Bloom.Threshold, 2.0f);
    EXPECT_EQ(settings.Bloom.Intensity, 0.5f);
    EXPECT_EQ(settings.Bloom.Spread, 1.0f);
}

TEST(PresentationLayers, AZoneSeesTheGameThenItsWorldThenItsGraphThenItsScene)
{
    WorldPartitionManifest world;
    world.Name = "City";
    world.Presentation = Intensity(2.0f);
    world.Graphs.push_back(GraphRecord{ .Id = GraphId{ 7 }, .Name = "Downtown", .Streaming = {},
                                        .Presentation = Intensity(3.0f) });
    ZoneHeader street;
    street.Id = ZoneId{ 42 };
    street.Graph = GraphId{ 7 };
    world.Zones.push_back(street);

    const PresentationSettingsLayer game = Parse(R"({ "bloom": { "enabled": true, "intensity": 1 } })");
    const PresentationSettingsLayer scene = Intensity(4.0f);

    const PresentationLayerStack stack = PresentationLayersAt(&game, &world, ZoneId{ 42 }, &scene);
    ASSERT_EQ(stack.Layers().size(), 4u);
    EXPECT_EQ(stack.Layers()[0].Level, PresentationLayerLevel::Game);
    EXPECT_EQ(stack.Layers()[1].Name, "City");
    EXPECT_EQ(stack.Layers()[2].Name, "Downtown");
    EXPECT_EQ(stack.Layers()[3].Level, PresentationLayerLevel::Scene);
    EXPECT_EQ(ResolvePresentationSettings(stack.Layers()).Bloom.Intensity, 4.0f);
    EXPECT_TRUE(ResolvePresentationSettings(stack.Layers()).Bloom.Enabled);

    const PresentationLayerStack withoutScene = PresentationLayersAt(&game, &world, ZoneId{ 42 }, nullptr);
    EXPECT_EQ(ResolvePresentationSettings(withoutScene.Layers()).Bloom.Intensity, 3.0f);

    const PresentationLayerStack elsewhere = PresentationLayersAt(&game, &world, ZoneId{ 9 }, nullptr);
    ASSERT_EQ(elsewhere.Layers().size(), 2u);
    EXPECT_EQ(ResolvePresentationSettings(elsewhere.Layers()).Bloom.Intensity, 2.0f);
}

TEST(PresentationLayers, ASingleSceneSeesOnlyTheGameAndItself)
{
    const PresentationSettingsLayer game = Parse(R"({ "bloom": { "enabled": true } })");
    const PresentationSettingsLayer scene = Parse(R"({ "bloom": { "enabled": false } })");
    const PresentationLayerStack stack = PresentationLayersAt(&game, nullptr, ZoneId{ 1 }, &scene);
    ASSERT_EQ(stack.Layers().size(), 2u);
    EXPECT_FALSE(ResolvePresentationSettings(stack.Layers()).Bloom.Enabled);
    EXPECT_TRUE(PresentationLayersAt(nullptr, nullptr, ZoneId{ 1 }, nullptr).Layers().empty());
}

TEST(ZonePresentation, AZonesLayerLastsWhileTheZoneIsResident)
{
    const WorldComponentSchema schema = EmptySchema();
    RuntimeWorld runtime(schema);

    RuntimeZoneRecord& importing = runtime.BeginZoneImport(ZoneId{ 5 });
    AttachZonePresentation(importing, Intensity(2.0f));
    EXPECT_EQ(FindZonePresentation(runtime, ZoneId{ 5 }), nullptr);

    ASSERT_TRUE(runtime.PublishZone(ZoneId{ 5 }));
    const PresentationSettingsLayer* layer = FindZonePresentation(runtime, ZoneId{ 5 });
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->Bloom.Intensity, 2.0f);

    ASSERT_TRUE(runtime.RequestDetach(ZoneId{ 5 }));
    runtime.FlushLifecycleRequests();
    (void)runtime.BeginResidencyProcessing();
    runtime.FinalizeResidencyProcessing();
    EXPECT_EQ(FindZonePresentation(runtime, ZoneId{ 5 }), nullptr);
}

TEST(ZonePresentation, AnEmptyLayerHoldsNothing)
{
    const WorldComponentSchema schema = EmptySchema();
    RuntimeWorld runtime(schema);
    AttachZonePresentation(runtime.AttachZone(ZoneId{ 5 }), {});
    EXPECT_EQ(FindZonePresentation(runtime, ZoneId{ 5 }), nullptr);
}

TEST(PresentationSettingsPublisher, EnabledBloomRunsInEveryViewWithTheResolvedValues)
{
    Views views;
    PresentationSettingsPublisher publisher(views.Catalog);
    PresentationSettings settings;
    settings.Bloom = { .Enabled = true, .Threshold = 0.9f, .Intensity = 0.7f, .Spread = 1.5f };
    publisher.Publish(views.Entities, settings);

    const PostEffectFrame frame = views.Frame();
    ASSERT_EQ(frame.Entries.size(), 1u);
    EXPECT_EQ(frame.Entries[0].Effect, views.Catalog.Find("sencha.bloom"));
    EXPECT_EQ(frame.Entries[0].Weight, 1.0f);
    EXPECT_EQ(frame.Entries[0].Params[0], 0.9f);
    EXPECT_EQ(frame.Entries[0].Params[1], 0.7f);
    EXPECT_EQ(frame.Entries[0].Params[2], 1.5f);

    settings.Bloom.Enabled = false;
    publisher.Publish(views.Entities, settings);
    EXPECT_TRUE(views.Frame().Entries.empty());
}

TEST(PresentationSettingsPublisher, AGameContributionAtTheDefaultOrderWins)
{
    Views views;
    PresentationSettingsPublisher publisher(views.Catalog);
    PresentationSettings settings;
    settings.Bloom.Enabled = true;
    publisher.Publish(views.Entities, settings);

    const PostEffectId bloom = views.Catalog.Find("sencha.bloom");
    PostEffectContribution game;
    game.Owner = { .Writer = RegisterPresentationWriter(views.Entities, "game"), .Holder = {} };
    game.Weight = 1.0f;
    game.Params[1] = 5.0f;
    SetPostEffect(views.Entities, bloom, game);
    ASSERT_EQ(views.Frame().Entries.size(), 1u);
    EXPECT_EQ(views.Frame().Entries[0].Params[1], 5.0f);

    game.Weight = 0.0f;
    SetPostEffect(views.Entities, bloom, game);
    EXPECT_TRUE(views.Frame().Entries.empty());

    ClearPostEffect(views.Entities, bloom, game.Owner);
    ASSERT_EQ(views.Frame().Entries.size(), 1u);
    EXPECT_EQ(views.Frame().Entries[0].Params[1], 1.0f);
}
