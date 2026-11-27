#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/serialization/JsonArchive.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <logic/decision/DecisionAgent.h>
#include <movement/components/CharacterFacts.h>
#include <navigation/NavAgent.h>
#include <net/ReplicationLayout.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/identity/PendingReferences.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    constexpr PersistentEntityId kPlayer{ 0x00000000000000a1ull };

    struct FormWorld
    {
        FormWorld()
            : Context(Logging)
        {
            Entities.RegisterComponent<DecisionAgent>();
            Entities.RegisterComponent<NavAgentRequest>();
            Entities.RegisterComponent<PersistentIdComponent>();
            Entities.AddResource<PersistentEntityIndex>();
        }

        JsonValue Write(const IComponentSerializer& serializer, EntityId entity, SceneRecordPurpose purpose)
        {
            Context.Purpose = purpose;
            JsonWriteArchive archive;
            EXPECT_TRUE(serializer.Save(archive, entity, Entities, Context));
            Context.Purpose = SceneRecordPurpose::Authoring;
            return archive.TakeValue();
        }

        EntityId Read(IComponentSerializer& serializer, const JsonValue& record)
        {
            const EntityId entity = Entities.CreateEntity();
            JsonReadArchive archive(record);
            EXPECT_TRUE(serializer.LoadIntoWorld(archive, entity, Entities, Context));
            return entity;
        }

        EntityId Identified(PersistentEntityId id)
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ id });
            return entity;
        }

        LoggingProvider Logging;
        SceneSerializationContext Context;
        World Entities;
        ComponentSerializer<DecisionAgent> Agents;
        ComponentSerializer<NavAgentRequest> Orders;
    };

    NavAgentRequest FollowOrder(EntityId target)
    {
        NavAgentRequest order;
        order.Kind = NavDestinationKind::Entity;
        order.Follow = true;
        order.Target = target;
        return order;
    }

    struct EngineVocabulary
    {
        EngineVocabulary()
            : Registrar(&Schema, &Serializers, &Replication)
        {
            RegisterEngineComponents(Registrar);
            Schema.Seal();
        }

        WorldComponentSchema Schema;
        ComponentSerializerRegistry Serializers;
        ReplicationLayout Replication;
        ComponentRegistrar Registrar;
    };
}

TEST(SavedFieldForms, AComponentOfRunTimeFieldsAloneIsWrittenInASaveAndHasNoAuthoringForm)
{
    FormWorld world;
    const EntityId agent = world.Entities.CreateEntity();
    DecisionAgent engaged;
    engaged.Engaged = true;
    engaged.ContactPosition = Vec3d(4.0f, 0.0f, -2.0f);
    engaged.SecondsSinceContact = 1.5f;
    engaged.Decisions = 7;
    world.Entities.AddComponent<DecisionAgent>(agent, engaged);

    EXPECT_FALSE(world.Agents.Authorable());
    EXPECT_TRUE(world.Agents.RuntimeFields().empty()) << "a tool would offer a run-time field as a row";
    EXPECT_TRUE(world.Write(world.Agents, agent, SceneRecordPurpose::Authoring).IsNull())
        << "a scene written for authoring holds a component with no authoring form";

    const JsonValue saved = world.Write(world.Agents, agent, SceneRecordPurpose::Save);
    ASSERT_TRUE(saved.IsObject());
    EXPECT_EQ(JsonStringify(saved),
              R"({"engaged":true,"contact_position":[4,0,-2],"contact_velocity":[0,0,0],"contact_facing":[0,0,0],)"
              R"("seconds_since_contact":1.5})");

    const DecisionAgent& loaded = *world.Entities.TryGet<DecisionAgent>(world.Read(world.Agents, saved));
    EXPECT_TRUE(loaded.Engaged);
    EXPECT_FLOAT_EQ(loaded.ContactPosition.X, 4.0f);
    EXPECT_FLOAT_EQ(loaded.SecondsSinceContact, 1.5f);
    EXPECT_FALSE(loaded.Started) << "a loaded agent starts its clock again";
    EXPECT_EQ(loaded.Decisions, 0u);
}

TEST(SavedFieldForms, ARunTimeFieldIsOnNoWireItsComponentDoesNotTravelAndAReplicatedOneKeepsItsWireForm)
{
    EngineVocabulary vocabulary;
    ASSERT_EQ(vocabulary.Replication.Error(), ReplicationLayoutError::None);
    EXPECT_EQ(vocabulary.Replication.Find(ResolveComponentTypeId<DecisionAgent>()), nullptr);
    EXPECT_EQ(vocabulary.Replication.Find(ResolveComponentTypeId<NavAgentRequest>()), nullptr);
    const ReplicatedComponent* kinematic = vocabulary.Replication.Find(ResolveComponentTypeId<KinematicState>());
    ASSERT_NE(kinematic, nullptr) << "a character's velocity still travels to its owner";
    EXPECT_TRUE(kinematic->Predicted);

    const IComponentSerializer* velocity = vocabulary.Serializers.FindByType(ResolveComponentTypeId<KinematicState>());
    ASSERT_NE(velocity, nullptr);
    EXPECT_EQ(velocity->Persistence(), ComponentPersistence::Saved);
    EXPECT_FALSE(velocity->Authorable());
    EXPECT_TRUE(velocity->SavedFormFromRowAlone());
}

TEST(SavedFieldForms, AnEntityFieldIsWrittenAsItsTargetsIdOrAsNothingAndAWriteMintsNone)
{
    FormWorld world;
    const EntityId stranger = world.Entities.CreateEntity();
    const EntityId agent = world.Entities.CreateEntity();
    world.Entities.AddComponent<NavAgentRequest>(agent, FollowOrder(stranger));

    const JsonValue unnamed = world.Write(world.Orders, agent, SceneRecordPurpose::Save);
    EXPECT_EQ(unnamed.Find("target"), nullptr) << JsonStringify(unnamed);
    EXPECT_FALSE(world.Entities.HasComponent<PersistentIdComponent>(stranger)) << "a write minted an id";
    EXPECT_FALSE(world.Orders.SavedFormFromRowAlone()) << "an entity field is written through a lookup";

    const EntityId player = world.Identified(kPlayer);
    world.Entities.TryGet<NavAgentRequest>(agent)->Target = player;
    const JsonValue named = world.Write(world.Orders, agent, SceneRecordPurpose::Save);
    ASSERT_NE(named.Find("target"), nullptr) << JsonStringify(named);
    EXPECT_EQ(named.Find("target")->AsString(), PersistentEntityIdToString(kPlayer));
}

TEST(SavedFieldForms, ALoadedReferenceToAResidentTargetResolvesAtOnceAndALoadedOrderTakesAnId)
{
    FormWorld world;
    const EntityId player = world.Identified(kPlayer);
    const EntityId agent = world.Entities.CreateEntity();
    world.Entities.AddComponent<NavAgentRequest>(agent, FollowOrder(player));
    const JsonValue saved = world.Write(world.Orders, agent, SceneRecordPurpose::Save);

    const NavAgentRequest& loaded = *world.Entities.TryGet<NavAgentRequest>(world.Read(world.Orders, saved));
    EXPECT_EQ(loaded.Target, player);
    EXPECT_TRUE(loaded.Follow);
    EXPECT_TRUE(loaded.Id.IsValid()) << "a loaded order with no id is never adopted";
    EXPECT_EQ(world.Entities.TryGetResource<PendingReferences>(), nullptr)
        << "a reference that resolved left something waiting";
}

TEST(SavedFieldForms, AReferenceWhoseTargetArrivesLaterIsFilledWhenItsIdArrives)
{
    FormWorld world;
    const EntityId first = world.Identified(kPlayer);
    const EntityId agent = world.Entities.CreateEntity();
    world.Entities.AddComponent<NavAgentRequest>(agent, FollowOrder(first));
    const JsonValue saved = world.Write(world.Orders, agent, SceneRecordPurpose::Save);
    world.Entities.DestroyEntity(first);

    const EntityId loaded = world.Read(world.Orders, saved);
    EXPECT_FALSE(world.Entities.TryGet<NavAgentRequest>(loaded)->Target.IsValid());
    const PendingReferences* pending = world.Entities.TryGetResource<PendingReferences>();
    ASSERT_NE(pending, nullptr);
    EXPECT_TRUE(pending->Awaits(loaded, ResolveComponentTypeId<NavAgentRequest>()));

    (void)world.Identified(PersistentEntityId{ 0x00000000000000b2ull });
    EXPECT_FALSE(world.Entities.TryGet<NavAgentRequest>(loaded)->Target.IsValid());
    const EntityId arrived = world.Identified(kPlayer);
    EXPECT_EQ(world.Entities.TryGet<NavAgentRequest>(loaded)->Target, arrived);
    EXPECT_FALSE(pending->Awaits(loaded, ResolveComponentTypeId<NavAgentRequest>()));
}

TEST(SavedFieldForms, AWaitingReferenceIsDroppedWithItsOwnerAndForgottenWhenCodeSetsItAfresh)
{
    FormWorld world;
    const EntityId first = world.Identified(kPlayer);
    const EntityId agent = world.Entities.CreateEntity();
    world.Entities.AddComponent<NavAgentRequest>(agent, FollowOrder(first));
    const JsonValue saved = world.Write(world.Orders, agent, SceneRecordPurpose::Save);
    world.Entities.DestroyEntity(first);

    const EntityId dies = world.Read(world.Orders, saved);
    const EntityId reordered = world.Read(world.Orders, saved);
    PendingReferences& pending = world.Entities.GetResource<PendingReferences>();
    ASSERT_EQ(pending.Count(), 2u);
    world.Entities.DestroyEntity(dies);
    ForgetPendingReferences(world.Entities, reordered, ResolveComponentTypeId<NavAgentRequest>());
    EXPECT_FALSE(pending.Awaits(reordered, ResolveComponentTypeId<NavAgentRequest>()));

    (void)world.Identified(kPlayer);
    EXPECT_EQ(pending.Count(), 0u) << "a dead owner's reference outlived it";
    EXPECT_FALSE(world.Entities.TryGet<NavAgentRequest>(reordered)->Target.IsValid())
        << "a forgotten reference was filled";
}
