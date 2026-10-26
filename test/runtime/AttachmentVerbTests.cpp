#include <gtest/gtest.h>

#include "AttachmentFixture.h"

#include <attach/AttachmentVerbs.h>
#include <authored/AuthoredValue.h>

#include <string>
#include <vector>

namespace
{
    using attach_test::AttachFixture;
    using attach_test::kZone;

    struct VerbFixture : AttachFixture
    {
        AttachmentOperations Operations{ Entities };

        AuthoredArguments Prop(EntityId target, std::string socket, std::string mesh = "asset://meshes/prop.smesh")
        {
            AuthoredArguments arguments;
            arguments.Resize(7);
            arguments.Set(0, AuthoredValue::Entity(target));
            arguments.Set(1, AuthoredValue::String(std::move(socket)));
            arguments.Set(2, AuthoredValue::Asset({ .Type = AssetType::StaticMesh, .Path = std::move(mesh), .Id = {} }));
            arguments.Set(3, AuthoredValue::Asset(
                                 { .Type = AssetType::Material, .Path = "asset://materials/prop.smat", .Id = {} }));
            arguments.Set(6, AuthoredValue::Bool(true));
            return arguments;
        }

        VerbAdmission Acquire(const LeaseOwner& owner, const AuthoredArguments& arguments)
        {
            VerbInvocation invocation;
            invocation.Owner = owner;
            invocation.Step = LeaseStep::Acquire;
            invocation.Arguments = &arguments;
            return Operations.Attach(invocation);
        }

        VerbAdmission Release(const LeaseOwner& owner)
        {
            VerbInvocation invocation;
            invocation.Owner = owner;
            invocation.Step = LeaseStep::Release;
            return Operations.Attach(invocation);
        }

        [[nodiscard]] std::vector<EntityId> Worn() const
        {
            std::vector<EntityId> worn;
            ForEachAttachment(Entities, Body, [&](EntityId entity, const Attachment&) { worn.push_back(entity); });
            return worn;
        }
    };
}

TEST(PresentationAttachLease, AHolderHoldsAPropUntilItLetsGo)
{
    VerbFixture fx;
    const EntityId actor = fx.Entities.CreateEntity();
    const LeaseOwner first{ .Entity = actor, .Key = 1 };
    const LeaseOwner second{ .Entity = actor, .Key = 2 };
    ASSERT_EQ(fx.Acquire(first, fx.Prop(fx.Body, "hand_r")), VerbAdmission::Accepted);
    ASSERT_EQ(fx.Acquire(second, fx.Prop(fx.Body, "hand_r")), VerbAdmission::Accepted);
    ASSERT_EQ(fx.Worn().size(), 2u);
    const World& reader = fx.Entities;
    EXPECT_GT(fx.Meshes.Count(fx.PropMesh), 0) << "the prop's mesh is held while it is shown";

    EXPECT_EQ(fx.Release(first), VerbAdmission::Accepted);
    ASSERT_EQ(fx.Worn().size(), 1u);
    EXPECT_EQ(reader.TryGet<Attachment>(fx.Worn()[0])->Owner.Holder, second) << "only that holder's";
    EXPECT_EQ(fx.Release(second), VerbAdmission::Accepted);
    EXPECT_TRUE(fx.Worn().empty());
    EXPECT_EQ(fx.Meshes.Count(fx.PropMesh), 0) << "and let go with the last of them";
}

TEST(PresentationAttachLease, ARefusedPropHoldsNothing)
{
    VerbFixture fx;
    const LeaseOwner owner{ .Entity = fx.Entities.CreateEntity(), .Key = 1 };
    EXPECT_EQ(fx.Acquire(owner, fx.Prop(fx.Body, "hand_l")), VerbAdmission::Refused) << "no socket of that name";
    EXPECT_EQ(fx.Acquire(owner, fx.Prop(fx.Body, "hand_r", "asset://meshes/missing.smesh")), VerbAdmission::Refused);
    EXPECT_TRUE(fx.Worn().empty());
    EXPECT_EQ(fx.Release(owner), VerbAdmission::Accepted) << "letting go of nothing is not an error";
}

TEST(PresentationAttachLease, AcquiringAgainChangesThePropInPlace)
{
    VerbFixture fx;
    const LeaseOwner owner{ .Entity = fx.Entities.CreateEntity(), .Key = 1 };
    ASSERT_EQ(fx.Acquire(owner, fx.Prop(fx.Body, "hand_r")), VerbAdmission::Accepted);
    const EntityId shown = fx.Worn().front();
    ASSERT_EQ(fx.Acquire(owner, fx.Prop(fx.Body, "")), VerbAdmission::Accepted);
    EXPECT_EQ(fx.Worn(), std::vector<EntityId>{ shown });
    const World& reader = fx.Entities;
    EXPECT_EQ(reader.TryGet<Attachment>(shown)->Binding, AttachmentBinding::Origin);
}

TEST(PresentationAttachLease, AHolderThatDiesTakesItsPropWithinAFrame)
{
    VerbFixture fx;
    const EntityId actor = fx.Entities.CreateEntity(kZone);
    ASSERT_EQ(fx.Acquire({ .Entity = actor, .Key = 1 }, fx.Prop(fx.Body, "hand_r")), VerbAdmission::Accepted);
    fx.Entities.DestroyEntity(actor);
    PlaceAttachments(fx.Entities, 1.0);
    EXPECT_TRUE(fx.Worn().empty()) << "no later acquire or release needed";
}

TEST(PresentationAttachLease, AWorldThatPresentsNothingCannotHoldAProp)
{
    struct Bare : AttachFixture
    {
        Bare() : AttachFixture(false) {}
        AttachmentOperations Operations{ Entities };
    } fx;
    AuthoredArguments arguments;
    arguments.Resize(7);
    arguments.Set(0, AuthoredValue::Entity(fx.Body));
    arguments.Set(1, AuthoredValue::String("hand_r"));
    arguments.Set(2, AuthoredValue::Asset({ .Type = AssetType::StaticMesh, .Path = "asset://meshes/prop.smesh", .Id = {} }));
    arguments.Set(3, AuthoredValue::Asset({ .Type = AssetType::Material, .Path = "asset://materials/prop.smat", .Id = {} }));
    VerbInvocation invocation;
    invocation.Owner = { .Entity = fx.Body, .Key = 1 };
    invocation.Step = LeaseStep::Acquire;
    invocation.Arguments = &arguments;
    EXPECT_EQ(fx.Operations.Attach(invocation), VerbAdmission::Unavailable);
}
