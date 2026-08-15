#include <gtest/gtest.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantDiagnostics.h>
#include <participant/ParticipantLifecycle.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

namespace
{
    struct ParticipantWorld
    {
        WorldComponentSchema Schema;
        World Entities;
        ParticipantLifecycle Lifecycle;
        int Built = 0;
        int Asked = 0;

        ParticipantWorld()
        {
            ComponentRegistrar components(&Schema, nullptr, nullptr);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
        }

        EntityId Thing()
        {
            const EntityId entity = Entities.CreateEntity();
            Entities.AddComponent<LookOrientation>(entity, {});
            return entity;
        }

        const ParticipantControl& Control(EntityId participant) const
        {
            return *Entities.TryGet<ParticipantControl>(participant);
        }
    };
}

TEST(ParticipantLifecycle, ReadmissionDoesNotRecomposeTheLocalParticipant)
{
    ParticipantWorld fixture;
    fixture.Lifecycle.Policies().BuildParticipant =
        [&](World&, EntityId) { ++fixture.Built; };

    const ParticipantAdmission first = fixture.Lifecycle.Admit(
        fixture.Entities, kLocalInputActionSource, ParticipantPresence::Local);
    const ParticipantAdmission second = fixture.Lifecycle.Admit(
        fixture.Entities, kLocalInputActionSource, ParticipantPresence::Local);

    EXPECT_TRUE(first.Created());
    EXPECT_EQ(second.Status, ParticipantAdmissionStatus::Existing);
    EXPECT_EQ(second.Participant, first.Participant);
    EXPECT_EQ(fixture.Built, 1)
        << "readmission reset game-owned participant state";
}

TEST(ParticipantLifecycle, LocalSourceZeroIsRepresentedByNoInputReference)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, kLocalInputActionSource,
        ParticipantPresence::Local).Participant;
    const EntityId body = fixture.Thing();

    const ParticipantControlChange change = fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, body);

    EXPECT_TRUE(change.Changed());
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(body), nullptr);
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), body);
}

TEST(ParticipantLifecycle, NonLocalSourceFollowsControlAsOneInvariant)
{
    ParticipantWorld fixture;
    const InputActionSourceId source{ 17 };
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, source).Participant;
    const EntityId first = fixture.Thing();
    const EntityId second = fixture.Thing();

    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, first);
    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, second);

    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(first), nullptr);
    ASSERT_NE(fixture.Entities.TryGet<InputActionSourceRef>(second), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(second)->Source,
              source);
    EXPECT_EQ(fixture.Control(participant).ControlSubject, second);
}

TEST(ParticipantLifecycle, TakingASubjectClearsItsPreviousParticipant)
{
    ParticipantWorld fixture;
    const EntityId first = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 2 }).Participant;
    const EntityId second = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 3 }).Participant;
    const EntityId subject = fixture.Thing();
    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, first, subject);

    const ParticipantControlChange change = fixture.Lifecycle.SetControlSubject(
        fixture.Entities, second, subject);

    EXPECT_EQ(change.DisplacedParticipant, first);
    EXPECT_FALSE(fixture.Control(first).ControlSubject.IsValid());
    EXPECT_EQ(fixture.Control(second).ControlSubject, subject);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(subject)->Source,
              InputActionSourceId{ 3 });
}

TEST(ParticipantLifecycle, DisplacementNamesTheFirstParticipantItTookFrom)
{
    ParticipantWorld fixture;
    const EntityId first = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 2 }).Participant;
    const EntityId second = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 3 }).Participant;
    const EntityId claimant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 4 }).Participant;
    const EntityId subject = fixture.Thing();
    // Two holders at once is the state this API exists to prevent, so it has to
    // be written directly rather than reached through SetControlSubject.
    fixture.Entities.TryGet<ParticipantControl>(first)->ControlSubject = subject;
    fixture.Entities.TryGet<ParticipantControl>(second)->ControlSubject = subject;

    const ParticipantControlChange change = fixture.Lifecycle.SetControlSubject(
        fixture.Entities, claimant, subject);

    EXPECT_EQ(change.DisplacedParticipant, first);
    EXPECT_FALSE(fixture.Control(first).ControlSubject.IsValid());
    EXPECT_FALSE(fixture.Control(second).ControlSubject.IsValid());
    EXPECT_EQ(fixture.Control(claimant).ControlSubject, subject);
}

TEST(ParticipantLifecycle, BodyAssignmentIsExplicitAndDoesNotPoll)
{
    ParticipantWorld fixture;
    bool ready = false;
    fixture.Lifecycle.Policies().ProvideBody = [&](World&, EntityId) {
        ++fixture.Asked;
        return ready ? fixture.Thing() : EntityId{};
    };
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 4 }).Participant;

    EXPECT_EQ(fixture.Lifecycle.RequestBody(fixture.Entities, participant).Status,
              ParticipantBodyStatus::Unavailable);
    ready = true;
    const ParticipantBodyChange assigned =
        fixture.Lifecycle.RequestBody(fixture.Entities, participant);
    const ParticipantBodyChange repeated =
        fixture.Lifecycle.RequestBody(fixture.Entities, participant);

    EXPECT_EQ(assigned.Status, ParticipantBodyStatus::Assigned);
    EXPECT_EQ(repeated.Status, ParticipantBodyStatus::AlreadyAssigned);
    EXPECT_EQ(fixture.Asked, 2);
    EXPECT_EQ(fixture.Control(participant).Body, assigned.Body);
}

TEST(ParticipantLifecycle, RetirementReapsTheBodyButNeverTheDrivenSubject)
{
    ParticipantWorld fixture;
    fixture.Lifecycle.Policies().ProvideBody =
        [&](World&, EntityId) { return fixture.Thing(); };
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 5 }).Participant;
    const EntityId body = fixture.Lifecycle.RequestBody(
        fixture.Entities, participant).Body;
    const EntityId vehicle = fixture.Thing();
    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, vehicle);

    const ParticipantRetirement retired =
        fixture.Lifecycle.Retire(fixture.Entities, participant);

    EXPECT_EQ(retired.Status, ParticipantRetirementStatus::Retired);
    EXPECT_TRUE(retired.BodyReaped);
    EXPECT_FALSE(fixture.Entities.IsAlive(body));
    EXPECT_TRUE(fixture.Entities.IsAlive(vehicle));
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(vehicle), nullptr);
}

TEST(ParticipantLifecycle, RetirementDoesNotDestroyANonParticipant)
{
    ParticipantWorld fixture;
    const EntityId unrelated = fixture.Thing();

    const ParticipantRetirement retired =
        fixture.Lifecycle.Retire(fixture.Entities, unrelated);

    EXPECT_EQ(retired.Status, ParticipantRetirementStatus::NotFound);
    EXPECT_TRUE(fixture.Entities.IsAlive(unrelated));
}

TEST(ParticipantLifecycle, PureOperationsAddNoNetworkComponents)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 6 }).Participant;
    const EntityId subject = fixture.Thing();
    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, subject);

    EXPECT_FALSE(fixture.Entities.HasComponent<NetReplicated>(participant));
    EXPECT_FALSE(fixture.Entities.HasComponent<NetReplicated>(subject));
    EXPECT_FALSE(fixture.Entities.HasComponent<NetOwner>(subject));
    EXPECT_FALSE(fixture.Entities.HasComponent<NetDrivenBy>(subject));
}

TEST(ParticipantDiagnostics, ReportsAHandWrittenInputMismatch)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 9 }).Participant;
    const EntityId subject = fixture.Thing();
    (void)fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, subject);
    fixture.Entities.TryGet<InputActionSourceRef>(subject)->Source =
        InputActionSourceId{ 10 };

    const ParticipantValidation validation =
        ValidateParticipants(fixture.Entities);

    ASSERT_FALSE(validation.Ok());
    EXPECT_EQ(validation.Failures.front().Issue,
              ParticipantInvariantIssue::WrongInputSource);
    EXPECT_NE(FormatParticipantStatus(fixture.Entities).find("validation FAILED"),
              std::string::npos);
}
