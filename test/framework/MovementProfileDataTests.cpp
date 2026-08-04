#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementProfileRuntime.h>

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <optional>
#include <span>

namespace
{
    std::shared_ptr<const CompiledMovementProfile> CompileProfile(
        DataAssetTypeRegistry& types,
        DataSchemaRegistry& schemas,
        std::string_view json)
    {
        RegisterMovementProfileData(types, schemas);
        const auto parsed = JsonParse(json);
        if (!parsed || !parsed->IsObject())
            return {};

        const DataSchema* schema = schemas.Find("movement.profile");
        if (schema == nullptr)
            return {};
        std::vector<DataValidationError> errors;
        if (!ValidateDataAgainstSchema(*parsed, *schema, errors))
            return {};

        const DataAssetTypeRegistration* type = types.Find("movement.profile");
        if (type == nullptr)
            return {};
        DataAssetCompileResult result = type->Compile(*parsed);
        return std::static_pointer_cast<const CompiledMovementProfile>(result.Value);
    }
}

TEST(MovementProfileData, OrderedLayersResolveAndTrace)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "test",
        "layers": [
            { "set": { "acceleration": 20, "gravity_scale": 1, "jump_speed": 6 } },
            { "when": { "support": "none" },
              "set": { "acceleration": 8 },
              "scale": { "gravity_scale": 0.5 } },
            { "when": { "tags": { "all": ["movement.gliding"] } },
              "set": { "gravity_scale": 0.2 },
              "add": { "acceleration": 2 } }
        ],
        "modes": []
    })");
    ASSERT_NE(profile, nullptr);

    GameplayTagRegistry tags;
    const auto gliding = tags.RegisterTag("movement.gliding");
    ASSERT_TRUE(gliding.has_value());

    const MovementProfileBindResult bound = BindMovementProfile(
        *profile, tags,
        [](std::string_view name)
        {
            return name == "movement.mode.free" ? LocomotionModeId{1} : LocomotionModeId{};
        });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    GameplayTagContainer activeTags;
    activeTags.Grant(*gliding);
    MovementResolveContext context;
    context.Support = SupportKind::None;
    context.Mode = LocomotionModeId{1};
    context.Tags = &activeTags;

    const MovementResolveResult resolved = ResolveMovementTuning(
        *bound.Profile, context, 7.0f, true);
    EXPECT_FLOAT_EQ(resolved.Tuning.MaxSpeed, 7.0f);
    EXPECT_FLOAT_EQ(resolved.Tuning.Acceleration, 10.0f);
    EXPECT_FLOAT_EQ(resolved.Tuning.GravityScale, 0.2f);
    EXPECT_FLOAT_EQ(resolved.Tuning.JumpSpeed, 6.0f);
    ASSERT_EQ(resolved.Trace.size(), 3u);
    EXPECT_TRUE(resolved.Trace[0].Matched);
    EXPECT_TRUE(resolved.Trace[1].Matched);
    EXPECT_TRUE(resolved.Trace[2].Matched);
}

TEST(MovementProfileData, LayerNamesSurviveCompileAndBind)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "named",
        "layers": [
            { "name": "Ground base", "set": { "friction": 4 } },
            { "when": { "support": "none" }, "set": { "friction": 0 } }
        ],
        "modes": []
    })");
    ASSERT_NE(profile, nullptr);
    ASSERT_EQ(profile->Layers.size(), 2u);
    EXPECT_EQ(profile->Layers[0].Name, "Ground base");
    EXPECT_TRUE(profile->Layers[1].Name.empty());

    GameplayTagRegistry tags;
    const MovementProfileBindResult bound = BindMovementProfile(
        *profile, tags, [](std::string_view) { return LocomotionModeId{1}; });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;
    EXPECT_EQ(bound.Profile->Layers[0].Name, "Ground base");
    EXPECT_TRUE(bound.Profile->Layers[1].Name.empty());

    MovementResolveContext context;
    context.Support = SupportKind::None;
    const MovementResolveResult resolved = ResolveMovementTuning(
        *bound.Profile, context, 7.0f, true);
    ASSERT_EQ(resolved.Trace.size(), 2u);
    EXPECT_EQ(resolved.Trace[0].Name, "Ground base");
    EXPECT_TRUE(resolved.Trace[1].Name.empty());
}

// Profiles authored before layer names existed must keep loading unchanged;
// the field is optional precisely so no migration is required.
TEST(MovementProfileData, NamelessLayersStillCompile)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "player_movement",
        "layers": [
            { "set": { "max_speed": 4.5, "friction": 4.0 } },
            { "when": { "support": "none" }, "set": { "friction": 0.0 } }
        ],
        "modes": []
    })");
    ASSERT_NE(profile, nullptr);
    ASSERT_EQ(profile->Layers.size(), 2u);
    EXPECT_TRUE(profile->Layers[0].Name.empty());
    EXPECT_FLOAT_EQ(*profile->Layers[0].Set.MaxSpeed, 4.5f);
}

// The per-layer snapshot is what authoring and in-game diagnostics read to show
// which layer last moved a coefficient, so it must match an incremental
// application of the same layers.
TEST(MovementProfileData, TraceSnapshotsRecordPerLayerEffectiveValues)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "snapshots",
        "layers": [
            { "set": { "friction": 4, "gravity_scale": 2 } },
            { "when": { "support": "stable" }, "set": { "friction": 9 } },
            { "when": { "support": "none" }, "scale": { "gravity_scale": 0.5 },
              "add": { "friction": 1 } }
        ],
        "modes": []
    })");
    ASSERT_NE(profile, nullptr);

    GameplayTagRegistry tags;
    const MovementProfileBindResult bound = BindMovementProfile(
        *profile, tags, [](std::string_view) { return LocomotionModeId{1}; });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    MovementResolveContext context;
    context.Support = SupportKind::None;
    const MovementResolveResult resolved = ResolveMovementTuning(
        *bound.Profile, context, 7.0f, true);

    ASSERT_EQ(resolved.Trace.size(), 3u);
    EXPECT_FLOAT_EQ(resolved.Trace[0].After.Friction, 4.0f);
    EXPECT_FLOAT_EQ(resolved.Trace[0].After.GravityScale, 2.0f);

    // The unmatched layer carries the running values forward untouched.
    EXPECT_FALSE(resolved.Trace[1].Matched);
    EXPECT_FLOAT_EQ(resolved.Trace[1].After.Friction, 4.0f);

    EXPECT_TRUE(resolved.Trace[2].Matched);
    EXPECT_FLOAT_EQ(resolved.Trace[2].After.Friction, 5.0f);
    EXPECT_FLOAT_EQ(resolved.Trace[2].After.GravityScale, 1.0f);
    EXPECT_FLOAT_EQ(resolved.Trace.back().After.Friction, resolved.Tuning.Friction);
    EXPECT_FLOAT_EQ(resolved.Trace.back().After.GravityScale, resolved.Tuning.GravityScale);
}

TEST(MovementProfileData, TuningFieldTableCoversEveryCoefficient)
{
    const std::span<const MovementTuningField> fields = MovementTuningFields();
    ASSERT_EQ(fields.size(), 8u);

    // Every row must address a distinct pair; a copy-paste slip in the table
    // would otherwise silently drop or double-apply a coefficient.
    MovementTuningPatch patch;
    ResolvedMovementTuning tuning;
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        patch.*fields[index].Patch = static_cast<float>(index + 1);
        tuning.*fields[index].Resolved = static_cast<float>(index + 1);
    }
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        ASSERT_TRUE((patch.*fields[index].Patch).has_value());
        EXPECT_FLOAT_EQ(*(patch.*fields[index].Patch), static_cast<float>(index + 1));
        EXPECT_FLOAT_EQ(tuning.*fields[index].Resolved, static_cast<float>(index + 1));
    }
}

TEST(MovementProfileData, BindingRejectsUnknownNames)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "bad",
        "layers": [
            { "when": { "tags": { "all": ["missing.tag"] } },
              "set": { "friction": 0 } }
        ],
        "modes": []
    })");
    ASSERT_NE(profile, nullptr);

    GameplayTagRegistry tags;
    const MovementProfileBindResult bound = BindMovementProfile(
        *profile, tags, [](std::string_view) { return LocomotionModeId{1}; });
    EXPECT_FALSE(bound.IsValid());
    EXPECT_NE(bound.Error.find("missing.tag"), std::string::npos);
}

TEST(MovementProfileData, ModeSustainUsesBoundTagIds)
{
    DataAssetTypeRegistry types;
    DataSchemaRegistry schemas;
    const auto profile = CompileProfile(types, schemas, R"({
        "name": "flight",
        "layers": [],
        "modes": [
            { "mode": "movement.mode.flight",
              "sustain": { "all": ["volume.flight"] },
              "layers": [] }
        ]
    })");
    ASSERT_NE(profile, nullptr);

    GameplayTagRegistry tags;
    const auto volume = tags.RegisterTag("volume.flight");
    ASSERT_TRUE(volume.has_value());
    const MovementProfileBindResult bound = BindMovementProfile(
        *profile, tags,
        [](std::string_view name)
        {
            return name == "movement.mode.flight" ? LocomotionModeId{2} : LocomotionModeId{};
        });
    ASSERT_TRUE(bound.IsValid()) << bound.Error;

    GameplayTagContainer active;
    EXPECT_FALSE(MovementModeSustainMatches(*bound.Profile, LocomotionModeId{2}, active));
    active.Grant(*volume);
    EXPECT_TRUE(MovementModeSustainMatches(*bound.Profile, LocomotionModeId{2}, active));
}
