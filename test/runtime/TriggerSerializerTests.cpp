#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <logic/TriggerSerializer.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>

#include <memory>
#include <sstream>
#include <string>

// A trigger's scene form: its policy, and its sequence exactly as written --
// including a verb this World has never heard of.

namespace
{
constexpr std::string_view kAuthored = R"({
    "activation": "stay", "enabled": false, "once": true, "cooldown": 1.5,
    "cooldown_scope": "activator", "stay_interval": 0.25,
    "sequence": {"conditions": [{"entity": "activator", "has": ["sencha.character_movement"]}],
                 "commands": [{"verb": "game.not_declared", "arguments": {"target": {"port": "activator"}},
                               "on_failure": "stop"}]}})";

class TriggerSerializerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Serializer = MakeTriggerSerializer();
        Serializer->RegisterStorage(Scene);
    }

    [[nodiscard]] bool LoadFrom(const JsonValue& json, EntityId entity)
    {
        JsonReadArchive in{ json };
        SceneSerializationContext context{ Logging };
        return Serializer->Load(in, entity, Scene, context);
    }

    [[nodiscard]] JsonValue SaveOf(EntityId entity)
    {
        JsonWriteArchive out;
        SceneSerializationContext context{ Logging };
        EXPECT_TRUE(Serializer->Save(out, entity, Scene, context));
        return out.TakeValue();
    }

    LoggingProvider Logging;
    Registry Scene;
    std::unique_ptr<IComponentSerializer> Serializer;
};
} // namespace

TEST_F(TriggerSerializerFixture, ThePolicyAndTheSequenceRoundTripVerbatim)
{
    const JsonValue authored = *JsonParse(kAuthored);
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(authored, entity));

    const Trigger* trigger = Scene.Components.TryGet<Trigger>(entity);
    ASSERT_NE(trigger, nullptr);
    EXPECT_EQ(trigger->Activation, TriggerActivation::Stay);
    EXPECT_FALSE(trigger->Enabled);
    EXPECT_TRUE(trigger->Once);
    EXPECT_FLOAT_EQ(trigger->CooldownSeconds, 1.5f);
    EXPECT_EQ(trigger->CooldownScope, TriggerCooldownScope::Activator);
    EXPECT_FLOAT_EQ(trigger->StayIntervalSeconds, 0.25f);

    const JsonValue saved = SaveOf(entity);
    ASSERT_NE(saved.Find("sequence"), nullptr);
    EXPECT_EQ(JsonStringify(*saved.Find("sequence")), JsonStringify(*authored.Find("sequence")));
    EXPECT_EQ(saved.Find("activation")->AsString(), "stay");
}

TEST_F(TriggerSerializerFixture, AMissingSequenceLoadsAsAnEmptyOneAndPolicyDefaultsApply)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse("{}"), entity));
    const Trigger* trigger = Scene.Components.TryGet<Trigger>(entity);
    ASSERT_NE(trigger, nullptr);
    EXPECT_EQ(trigger->Activation, TriggerActivation::Enter);
    EXPECT_TRUE(trigger->Enabled);
    const JsonValue* sequence = TriggerSequenceSource(Scene.Components, entity);
    ASSERT_NE(sequence, nullptr);
    EXPECT_EQ(JsonStringify(*sequence), JsonStringify(EmptyVerbSequence()));
}

TEST_F(TriggerSerializerFixture, TheBinaryArchiveCarriesTheSequenceToo)
{
    const EntityId entity = Scene.Components.CreateEntity();
    ASSERT_TRUE(LoadFrom(*JsonParse(kAuthored), entity));

    std::stringstream bytes;
    {
        BinaryWriter writer(bytes);
        BinaryWriteArchive out(writer);
        SceneSerializationContext context{ Logging };
        ASSERT_TRUE(Serializer->Save(out, entity, Scene, context));
    }
    const EntityId copy = Scene.Components.CreateEntity();
    BinaryReader reader(bytes);
    BinaryReadArchive in(reader);
    SceneSerializationContext context{ Logging };
    ASSERT_TRUE(Serializer->Load(in, copy, Scene, context));
    EXPECT_EQ(Scene.Components.TryGet<Trigger>(copy)->Sequence, Scene.Components.TryGet<Trigger>(entity)->Sequence)
        << "the same sequence did not intern to the same entry";
}
