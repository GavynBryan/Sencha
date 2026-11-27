#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/ILogSink.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/CharacterMovementSerializer.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/HeldMotion.h>
#include <world/serialization/SceneSerializationContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace
{
    struct Messages final : ILogSink
    {
        void Write(LogLevel, std::string_view, std::string_view message) override { Lines.emplace_back(message); }
        std::vector<std::string> Lines;
    };

    struct ModeWorld
    {
        ModeWorld()
            : Sink(Logging.AddSink<Messages>())
            , Context(Logging)
            , Modes(Entities.AddResource<LocomotionModeRegistry>(Entities.AddResource<GameplayTagRegistry>()))
        {
            Free = Modes.RegisterFree();
            Climbing = Modes.Register<KinematicState>("movement.mode.climb");
            Entities.RegisterComponent<CharacterMovement>();
            Entities.RegisterComponent<LocomotionModeLease>();
        }

        bool Load(EntityId entity, std::string_view text)
        {
            const std::optional<JsonValue> parsed = JsonParse(text);
            JsonReadArchive archive(*parsed);
            return Serializer->LoadIntoWorld(archive, entity, Entities, Context);
        }

        LoggingProvider Logging;
        Messages& Sink;
        SceneSerializationContext Context;
        World Entities;
        LocomotionModeRegistry& Modes;
        LocomotionModeId Free;
        LocomotionModeId Climbing;
        std::unique_ptr<IComponentSerializer> Serializer = MakeCharacterMovementSerializer();
    };
}

TEST(CharacterMovementScene, AnUnregisteredModeLoadsAsTheFreeModeAndSaysSo)
{
    ModeWorld world;
    const EntityId entity = world.Entities.CreateEntity();
    ASSERT_TRUE(world.Load(entity, R"({"mode":"movement.mode.hoverboard"})"));
    EXPECT_EQ(world.Entities.TryGet<CharacterMovement>(entity)->Mode, world.Free);
    EXPECT_TRUE(std::ranges::any_of(world.Sink.Lines, [](const std::string& line) {
        return line.find("'mode' names locomotion mode 'movement.mode.hoverboard', which is not registered; it reads "
                         "as 'movement.mode.free'")
            != std::string::npos;
    }));
}

TEST(CharacterMovementScene, TheModeIsANamedFieldWrittenByNameAndLeftOutWhileALeaseHoldsIt)
{
    ModeWorld world;
    const std::span<const RuntimeField> fields = world.Serializer->RuntimeFields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].Names, &NamedIdField<LocomotionModeId>::Vocabulary());
    EXPECT_EQ(fields[0].Label, "Starting mode");
    EXPECT_EQ(world.Serializer->JsonKey(), "CharacterMovement");

    const EntityId climbing = world.Entities.CreateEntity();
    world.Entities.AddComponent<CharacterMovement>(climbing, CharacterMovement{ .Mode = world.Climbing });
    JsonWriteArchive written;
    ASSERT_TRUE(world.Serializer->Save(written, climbing, world.Entities, world.Context));
    EXPECT_EQ(JsonStringify(written.TakeValue()), R"({"mode":"movement.mode.climb"})");

    *world.Entities.TryGet<LocomotionModeLease>(climbing) =
        LocomotionModeLease{ .Owner = LeaseOwner{ .Entity = climbing, .Key = 1 }, .Mode = world.Climbing };
    JsonWriteArchive leased;
    ASSERT_TRUE(world.Serializer->Save(leased, climbing, world.Entities, world.Context));
    EXPECT_EQ(JsonStringify(leased.TakeValue()), R"({})");
    EXPECT_FALSE(world.Serializer->SavedFormFromRowAlone());

    const EntityId unnamed = world.Entities.CreateEntity();
    ASSERT_TRUE(world.Load(unnamed, R"({})"));
    EXPECT_EQ(world.Entities.TryGet<CharacterMovement>(unnamed)->Mode, world.Free);
    const EntityId named = world.Entities.CreateEntity();
    ASSERT_TRUE(world.Load(named, R"({"mode":"movement.mode.climb"})"));
    EXPECT_EQ(world.Entities.TryGet<CharacterMovement>(named)->Mode, world.Climbing);
}

TEST(CharacterMovementScene, TheRecordTheTemplatePawnsAuthorLoadsAndSavesUnchanged)
{
    ModeWorld world;
    const EntityId pawn = world.Entities.CreateEntity();
    ASSERT_TRUE(world.Load(pawn, R"({"mode":"movement.mode.free"})"));
    EXPECT_EQ(world.Entities.TryGet<CharacterMovement>(pawn)->Mode, world.Free);
    JsonWriteArchive written;
    ASSERT_TRUE(world.Serializer->Save(written, pawn, world.Entities, world.Context));
    EXPECT_EQ(JsonStringify(written.TakeValue()), R"({"mode":"movement.mode.free"})");
    EXPECT_TRUE(world.Sink.Lines.empty());
}
