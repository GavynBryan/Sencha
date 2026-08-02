#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementProfileRuntime.h>

#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <optional>

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
