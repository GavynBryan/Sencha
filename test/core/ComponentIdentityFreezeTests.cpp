#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ReplicationLayout.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Frozen identity contract for the engine's component vocabulary.
//
// Each value here escapes the build: a ComponentTypeId names a component in
// cooked content and in the replication handshake, a JSON key names it in scene
// text, and a chunk id names it in binary scenes. Moving a schema between
// translation units must move none of them.
//
// The ids are written as literals on purpose. Recomputing one from the name it
// is checked against would agree with itself whatever the name became, which is
// exactly the drift this table exists to catch.
//
// Updating the table is a deliberate act: run core_tests with
// --gtest_also_run_disabled_tests and
// --gtest_filter=ComponentIdentityFreeze.DISABLED_PrintFrozenTable,
// then paste the output, having satisfied yourself that every moved value was
// meant to move. A component that disappears from the engine is removed here in
// the same commit that removes it.

namespace
{

struct FrozenComponent
{
    std::string_view Name;
    std::uint64_t    TypeId;
};

struct FrozenSerializer
{
    std::string_view JsonKey;
    std::uint64_t    TypeId;
    std::uint32_t    ChunkId;
};

// clang-format off
constexpr FrozenComponent kFrozenComponents[] = {
    { "Transform",                            0xC1FFF4F356DFB2FBull },
    { "sencha.world_transform",               0xADA13F3A30FD12F8ull },
    { "sencha.parent",                        0x7E1914B78AE2CEA5ull },
    { "sencha.world_transform_history",       0x1F8E43B74AB5D91Full },
    { "persistent_id",                        0xBB21CC0122FC1DD8ull },
    { "scene_instance",                       0x17084CA474E1957Full },
    { "StaticMesh",                           0xC8A13ED72D0FBD7Eull },
    { "SkinnedMesh",                          0x1EE6CB7FBD7486D6ull },
    { "AnimationClipPlayer",                  0xFB8C76C712E6CA02ull },
    { "ZoneLightmap",                         0xC85ECD44D42C5D5Dull },
    { "IrradianceVolume",                     0xB477A83303BC3F19ull },
    { "PointLight",                           0x6A79ACB9CBC5CDDBull },
    { "SpotLight",                            0x553229AC9C575AC7ull },
    { "AudioSource",                          0x917CF03FFE5623A4ull },
    { "AudioCaption",                         0x4513D14CA77A1639ull },
    { "World Dock",                           0xBC443581184484CEull },
    { "World Link",                           0x9046133F6F99B85Dull },
    { "Dock Gate Binding",                    0x9D8D4FD13397EBFCull },
    { "Camera",                               0x54D1B2A64667E32Eull },
    { "sencha.camera_seat",                   0x38312E5094398722ull },
    { "sencha.camera_rig",                    0x21AB9932CFFD0BC3ull },
    { "sencha.physics.collider",              0x4956A40D955D2368ull },
    { "sencha.physics.rigid_body",            0x9A16533E4A7370C4ull },
    { "sencha.physics.character_controller",  0x9F1D2CE24A903B74ull },
    { "sencha.physics.body_link",             0x8B840E7CC819CCC9ull },
    { "sencha.physics.mover_link",            0xAC0202E5DE0803D2ull },
    { "sencha.gameplay_tag_container",        0x8AD70174123D7D06ull },
    { "sencha.attribute_set",                 0xCBAB285D411E9BC8ull },
    { "sencha.ability_set",                   0x398F71246E3983A2ull },
    { "sencha.active_effect",                 0xE29C7992C1DB0669ull },
    { "sencha.movement_intent",               0x0F43CD9973AE4EB3ull },
    { "sencha.jump_state",                    0x13238ECE2DCCCF73ull },
    { "sencha.kinematic_state",               0x912794F991AC3D64ull },
    { "sencha.support_state",                 0x99E14A8B98F4C26Eull },
    { "sencha.immersion",                     0x4994E4DE11A9E224ull },
    { "sencha.character_movement",            0xD1A31FA332384D88ull },
    { "sencha.movement_tuning_source",        0xF266148A7AF04584ull },
    { "sencha.resolved_movement_tuning",      0xD2DD0F275ACF40C1ull },
    { "sencha.locomotion_output",             0x7666C21F460A9000ull },
    { "sencha.motion_axis_override",          0x870F610964B49382ull },
    { "sencha.motion_impulse",                0xF5F90F367BD2C763ull },
    { "sencha.motion_request",                0xEEE111979D546B91ull },
    { "sencha.mode_transition_request",       0x182610060AE27BEAull },
    { "sencha.cling_session",                 0x78AA21E9EB51419Dull },
    { "sencha.flight_session",                0xACD1FA3B9ACE52F8ull },
    { "sencha.look_orientation",              0x457DFC612E50E277ull },
    { "sencha.local_look_control",            0xE888F26C210DFF94ull },
    { "sencha.aim_facing",                    0xA70764228D1A908Dull },
    { "sencha.input_action_source_ref",       0x44FC82E3E41D495Cull },
    { "sencha.participant_control",           0x9450F63E453A5E6Cull },
    { "sencha.local_participant",             0x83A186C072919ED6ull },
    { "sencha.net_replicated",                0x3DB3201913065DD8ull },
    { "sencha.net_owner",                     0x036BDB97E879B99Cull },
    { "sencha.net_driven_by",                 0xFF4CAA5385294CD1ull },
    { "sencha.net_spawn_prefab",              0x83F9926C441E229Bull },
    { "sencha.net_participant_identity",      0xE57E247501B6AB85ull },
};

constexpr FrozenSerializer kFrozenSerializers[] = {
    { "AbilitySet",          0x398F71246E3983A2ull, MakeFourCC('A','B','L','S') },
    { "AimFacing",           0xA70764228D1A908Dull, MakeFourCC('A','I','M','F') },
    { "AnimationClipPlayer", 0xFB8C76C712E6CA02ull, MakeFourCC('A','C','L','P') },
    { "Attributes",          0xCBAB285D411E9BC8ull, MakeFourCC('A','T','T','R') },
    { "AudioCaption",        0x4513D14CA77A1639ull, MakeFourCC('A','C','A','P') },
    { "AudioSource",         0x917CF03FFE5623A4ull, MakeFourCC('A','S','R','C') },
    { "Camera",              0x54D1B2A64667E32Eull, MakeFourCC('C','A','M','R') },
    { "CameraSeat",          0x38312E5094398722ull, MakeFourCC('C','S','E','T') },
    { "CharacterController", 0x9F1D2CE24A903B74ull, MakeFourCC('C','H','C','T') },
    { "CharacterMovement",   0xD1A31FA332384D88ull, MakeFourCC('C','H','M','V') },
    { "Dock Gate Binding",   0x9D8D4FD13397EBFCull, MakeFourCC('D','G','A','T') },
    { "GameplayTags",        0x8AD70174123D7D06ull, MakeFourCC('G','T','A','G') },
    { "IrradianceVolume",    0xB477A83303BC3F19ull, MakeFourCC('I','R','V','L') },
    { "LookOrientation",     0x457DFC612E50E277ull, MakeFourCC('L','O','O','K') },
    { "MovementTuning",      0xF266148A7AF04584ull, MakeFourCC('M','T','U','N') },
    { "PointLight",          0x6A79ACB9CBC5CDDBull, MakeFourCC('P','L','G','T') },
    { "SkinnedMesh",         0x1EE6CB7FBD7486D6ull, MakeFourCC('S','K','I','N') },
    { "SpotLight",           0x553229AC9C575AC7ull, MakeFourCC('S','P','O','T') },
    { "StaticMesh",          0xC8A13ED72D0FBD7Eull, MakeFourCC('M','E','S','H') },
    { "Transform",           0xC1FFF4F356DFB2FBull, MakeFourCC('X','F','R','M') },
    { "World Dock",          0xBC443581184484CEull, MakeFourCC('W','D','C','K') },
    { "World Link",          0x9046133F6F99B85Dull, MakeFourCC('W','L','N','K') },
    { "ZoneLightmap",        0xC85ECD44D42C5D5Dull, MakeFourCC('Z','L','M','P') },
    { "persistent_id",       0xBB21CC0122FC1DD8ull, MakeFourCC('P','S','I','D') },
    { "scene_instance",      0x17084CA474E1957Full, MakeFourCC('S','N','I','N') },
};
// clang-format on

// Components whose lifecycle hooks are load-bearing, and the fact that the
    // engine's own registration captures them.
    //
    // Whether a component has hooks is decided where it is registered, from the
    // ComponentTraits that registration can see. A registration site that cannot
    // see them registers the component with no hooks at all, and nothing says so:
    // the component still stores, still serializes, still replicates, and only the
    // behaviour the hooks carried goes missing -- an asset never retained, an index
    // never populated. That is what this table is for.
    constexpr std::string_view kComponentsWithLifecycleHooks[] = {
        "StaticMesh",
        "SkinnedMesh",
        "AnimationClipPlayer",
        "ZoneLightmap",
        "AudioSource",
        "AudioCaption",
        "persistent_id",
        "scene_instance",
        "sencha.movement_tuning_source",
    };

// The wire contract: two builds refuse each other unless these agree.
constexpr std::uint64_t kFrozenReplicationTableHash = 0x524A2DFCC6407759ull;

struct EngineVocabulary
{
    EngineVocabulary()
        : Registrar(&Schema, &Serializers, &Replication)
    {
        RegisterEngineComponents(Registrar);
        Schema.Seal();
    }

    WorldComponentSchema        Schema;
    ComponentSerializerRegistry Serializers;
    ReplicationLayout           Replication;
    ComponentRegistrar          Registrar;
};

std::string HexLiteral(std::uint64_t value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%016llXull",
                  static_cast<unsigned long long>(value));
    return buffer;
}

std::string FourCcLiteral(std::uint32_t chunk)
{
    const auto glyph = [chunk](int shift) {
        return static_cast<char>((chunk >> shift) & 0xFFu);
    };
    // MakeFourCC packs the first character into the low byte.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "MakeFourCC('%c','%c','%c','%c')",
                  glyph(0), glyph(8), glyph(16), glyph(24));
    return buffer;
}

} // namespace

// A frozen component must still be registered under the same name and id.
// Components absent from the table are tolerated so that adding one is not an
// unrelated test failure; a renamed, re-keyed, or removed component is not,
// because its frozen entry goes missing.
TEST(ComponentIdentityFreeze, EveryFrozenComponentKeepsItsIdentity)
{
    EngineVocabulary vocabulary;

    for (const FrozenComponent& frozen : kFrozenComponents)
    {
        const auto entries = vocabulary.Schema.Entries();
        const auto found = std::find_if(
            entries.begin(), entries.end(),
            [&](const WorldComponentSchema::Entry& entry) { return entry.Name == frozen.Name; });

        ASSERT_NE(found, entries.end())
            << "component vanished from the engine vocabulary: " << frozen.Name;
        EXPECT_EQ(found->Type.Value, frozen.TypeId)
            << "stable identity moved for " << frozen.Name
            << ": cooked content and replication peers name it by this value";
    }
}

// The persisted half of the same contract.
TEST(ComponentIdentityFreeze, EveryFrozenSerializerKeepsItsKeyAndChunk)
{
    EngineVocabulary vocabulary;

    for (const FrozenSerializer& frozen : kFrozenSerializers)
    {
        IComponentSerializer* serializer = vocabulary.Serializers.FindByJsonKey(frozen.JsonKey);

        ASSERT_NE(serializer, nullptr)
            << "no serializer answers to the scene key " << frozen.JsonKey
            << ": every scene already written names it";
        EXPECT_EQ(serializer->TypeId().Value, frozen.TypeId) << "for " << frozen.JsonKey;
        EXPECT_EQ(serializer->BinaryChunkId(), frozen.ChunkId)
            << "binary chunk id moved for " << frozen.JsonKey;
    }
}

TEST(ComponentIdentityFreeze, ReplicationTableHashIsUnchanged)
{
    EngineVocabulary vocabulary;

    ASSERT_EQ(vocabulary.Replication.Error(), ReplicationLayoutError::None)
        << ReplicationLayoutErrorToString(vocabulary.Replication.Error()) << ": "
        << vocabulary.Replication.ErrorDetail();

    EXPECT_EQ(vocabulary.Replication.TableHash(), kFrozenReplicationTableHash)
        << "the replication handshake compares this value: a build whose hash "
           "moved refuses every peer that has not moved with it";
}

TEST(ComponentIdentityFreeze, LifecycleHooksSurviveRegistration)
{
    EngineVocabulary vocabulary;

    World world;
    vocabulary.Schema.Apply(world);

    for (const std::string_view name : kComponentsWithLifecycleHooks)
    {
        const auto entries = vocabulary.Schema.Entries();
        const auto found = std::find_if(
            entries.begin(), entries.end(),
            [&](const WorldComponentSchema::Entry& entry) { return entry.Name == name; });
        ASSERT_NE(found, entries.end()) << "component vanished: " << name;

        const ComponentMeta* meta = world.GetMeta(world.GetComponentIdByType(found->Type));
        ASSERT_NE(meta, nullptr) << name;
        EXPECT_TRUE(meta->OnAddHook != nullptr || meta->OnRemoveHook != nullptr)
            << name << " registered without its lifecycle hooks: whatever they "
                       "retained or indexed is now silently unowned";
    }
}

// Not a test. Prints the table above in source form; see the header comment.
TEST(ComponentIdentityFreeze, DISABLED_PrintFrozenTable)
{
    EngineVocabulary vocabulary;

    std::cout << "constexpr FrozenComponent kFrozenComponents[] = {\n";
    for (const WorldComponentSchema::Entry& entry : vocabulary.Schema.Entries())
    {
        std::cout << "    { \"" << entry.Name << "\", " << HexLiteral(entry.Type.Value) << " },\n";
    }
    std::cout << "};\n\nconstexpr FrozenSerializer kFrozenSerializers[] = {\n";

    std::vector<const IComponentSerializer*> serializers;
    for (const auto& entry : vocabulary.Serializers.Entries())
        serializers.push_back(entry.get());
    std::sort(serializers.begin(), serializers.end(),
              [](const IComponentSerializer* a, const IComponentSerializer* b) {
                  return a->JsonKey() < b->JsonKey();
              });

    for (const IComponentSerializer* serializer : serializers)
    {
        std::cout << "    { \"" << serializer->JsonKey() << "\", "
                  << HexLiteral(serializer->TypeId().Value) << ", "
                  << FourCcLiteral(serializer->BinaryChunkId()) << " },\n";
    }
    std::cout << "};\n\nconstexpr std::uint64_t kFrozenReplicationTableHash = "
              << HexLiteral(vocabulary.Replication.TableHash()) << ";\n";
}
