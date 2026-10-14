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

// The local source is assigned explicitly, like a peer's. It used to be
// represented by absence, and absence meant "read the local source" -- so a
// body released from a peer read this machine's keyboard the moment its
// reference went away.
TEST(ParticipantLifecycle, TheLocalSourceIsAssignedExplicitly)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, kLocalInputActionSource,
        ParticipantPresence::Local).Participant;
    const EntityId body = fixture.Thing();

    const ParticipantControlChange change = fixture.Lifecycle.SetControlSubject(
        fixture.Entities, participant, body);

    EXPECT_TRUE(change.Changed());
    const InputActionSourceRef* ref = fixture.Entities.TryGet<InputActionSourceRef>(body);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->Source, kLocalInputActionSource);
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), body);
}

TEST(ParticipantLifecycle, AReleasedSubjectKeepsNoInputReference)
{
    ParticipantWorld fixture;
    const InputActionSourceId source{ 17 };
    const EntityId participant = fixture.Lifecycle.Admit(fixture.Entities, source).Participant;
    const EntityId first = fixture.Thing();
    const EntityId second = fixture.Thing();

    (void)fixture.Lifecycle.SetControlSubject(fixture.Entities, participant, first);
    (void)fixture.Lifecycle.SetControlSubject(fixture.Entities, participant, second);

    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(first), nullptr)
        << "the body that was stepped out of still names a source";
    ASSERT_NE(fixture.Entities.TryGet<InputActionSourceRef>(second), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(second)->Source, source);
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

TEST(ParticipantLifecycle, ABodyThatCouldNotBeBoundIsDestroyed)
{
    ParticipantWorld fixture;
    EntityId provided;
    // Providing a body is the game's, and it is structural, so the participant
    // it was asked for can be gone by the time it answers.
    fixture.Lifecycle.Policies().ProvideBody =
        [&](World& world, EntityId participant) {
            provided = fixture.Thing();
            world.DestroyEntity(participant);
            return provided;
        };
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 11 }).Participant;

    const ParticipantBodyChange change =
        fixture.Lifecycle.RequestBody(fixture.Entities, participant);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::Unavailable);
    EXPECT_FALSE(change.Body.IsValid());
    EXPECT_FALSE(fixture.Entities.IsAlive(provided))
        << "an unbindable body was left standing with nothing driving it";
}

// A policy cannot give away what is not its to give. The answer used to be
// rebound regardless, so a buggy policy moved one participant's body under
// another; and destroying it as "unbound" would kill the first one's body.
TEST(ParticipantLifecycle, ARequestedBodyHeldByAnotherParticipantIsRefusedAndKept)
{
    ParticipantWorld fixture;
    EntityId answer;
    fixture.Lifecycle.Policies().ProvideBody =
        [&](World&, EntityId) { return answer.IsValid() ? answer : fixture.Thing(); };
    const EntityId owner = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 12 }).Participant;
    const EntityId other = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 13 }).Participant;
    answer = fixture.Lifecycle.RequestBody(fixture.Entities, owner).Body;
    ASSERT_TRUE(answer.IsValid());

    const ParticipantBodyChange change =
        fixture.Lifecycle.RequestBody(fixture.Entities, other);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::BodyInUse);
    EXPECT_TRUE(fixture.Entities.IsAlive(answer));
    EXPECT_EQ(fixture.Control(owner).Body, answer);
    EXPECT_EQ(fixture.Control(owner).ControlSubject, answer);
    EXPECT_FALSE(fixture.Control(other).Body.IsValid());
}

TEST(ParticipantLifecycle, AssigningAnExistingEntityBindsItAsTheBody)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 14 }).Participant;
    const EntityId body = fixture.Thing();

    const ParticipantBodyChange change =
        fixture.Lifecycle.AssignBody(fixture.Entities, participant, body);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::Assigned);
    EXPECT_EQ(change.Body, body);
    EXPECT_EQ(fixture.Control(participant).Body, body);
    EXPECT_EQ(fixture.Control(participant).ControlSubject, body);
    const InputActionSourceRef* ref = fixture.Entities.TryGet<InputActionSourceRef>(body);
    ASSERT_NE(ref, nullptr);
    EXPECT_EQ(ref->Source, InputActionSourceId{ 14 });
}

// Every refusal leaves the offered entity alive and nothing rebound: the
// caller still owns it.
TEST(ParticipantLifecycle, ARefusedAssignmentDestroysNothingAndChangesNothing)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 15 }).Participant;
    const EntityId other = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 16 }).Participant;
    const EntityId othersBody = fixture.Thing();
    ASSERT_EQ(fixture.Lifecycle.AssignBody(fixture.Entities, other, othersBody).Status,
              ParticipantBodyStatus::Assigned);
    const EntityId dead = fixture.Thing();
    fixture.Entities.DestroyEntity(dead);
    const EntityId gone = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 17 }).Participant;
    fixture.Entities.DestroyEntity(gone);

    struct Case
    {
        const char* Name;
        EntityId Participant;
        EntityId Offered;
        ParticipantBodyStatus Expected;
    };
    const EntityId spare = fixture.Thing();
    const Case cases[] = {
        { "dead entity", participant, dead, ParticipantBodyStatus::InvalidBody },
        { "invalid entity", participant, EntityId{}, ParticipantBodyStatus::InvalidBody },
        { "a participant", participant, other, ParticipantBodyStatus::InvalidBody },
        { "another's body", participant, othersBody, ParticipantBodyStatus::BodyInUse },
        { "gone participant", gone, spare, ParticipantBodyStatus::InvalidParticipant },
        { "not a participant", spare, fixture.Thing(), ParticipantBodyStatus::InvalidParticipant },
    };
    for (const Case& c : cases)
    {
        const ParticipantBodyChange change =
            fixture.Lifecycle.AssignBody(fixture.Entities, c.Participant, c.Offered);
        EXPECT_EQ(change.Status, c.Expected) << c.Name;
        if (c.Offered.IsValid() && c.Offered != dead)
        {
            EXPECT_TRUE(fixture.Entities.IsAlive(c.Offered)) << c.Name;
        }
    }
    EXPECT_FALSE(fixture.Control(participant).Body.IsValid());
    EXPECT_FALSE(fixture.Control(participant).ControlSubject.IsValid());
    EXPECT_EQ(fixture.Control(other).Body, othersBody);
    EXPECT_EQ(fixture.Control(other).ControlSubject, othersBody);
}

TEST(ParticipantLifecycle, AssigningOverALivingBodyIsRefusedAndKeepsBoth)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 18 }).Participant;
    const EntityId body = fixture.Thing();
    const EntityId offered = fixture.Thing();
    ASSERT_EQ(fixture.Lifecycle.AssignBody(fixture.Entities, participant, body).Status,
              ParticipantBodyStatus::Assigned);

    const ParticipantBodyChange change =
        fixture.Lifecycle.AssignBody(fixture.Entities, participant, offered);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::AlreadyAssigned);
    EXPECT_EQ(change.Body, body);
    EXPECT_TRUE(fixture.Entities.IsAlive(offered));
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(offered), nullptr);
    EXPECT_EQ(fixture.Control(participant).Body, body);
}

// Control may be taken from a participant that only drives the entity, as a
// requested body takes it; a body is never taken.
TEST(ParticipantLifecycle, AssigningADrivenEntityDisplacesItsDriver)
{
    ParticipantWorld fixture;
    const EntityId driver = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 19 }).Participant;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 20 }).Participant;
    const EntityId vehicle = fixture.Thing();
    (void)fixture.Lifecycle.SetControlSubject(fixture.Entities, driver, vehicle);

    const ParticipantBodyChange change =
        fixture.Lifecycle.AssignBody(fixture.Entities, participant, vehicle);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::Assigned);
    EXPECT_EQ(change.Control.DisplacedParticipant, driver);
    EXPECT_FALSE(fixture.Control(driver).ControlSubject.IsValid());
    EXPECT_EQ(fixture.Control(participant).Body, vehicle);
}

TEST(ParticipantLifecycle, ABodyCanBeAssignedAgainAfterTheLastOneDied)
{
    ParticipantWorld fixture;
    const EntityId participant = fixture.Lifecycle.Admit(
        fixture.Entities, InputActionSourceId{ 21 }).Participant;
    const EntityId first = fixture.Thing();
    ASSERT_EQ(fixture.Lifecycle.AssignBody(fixture.Entities, participant, first).Status,
              ParticipantBodyStatus::Assigned);
    fixture.Entities.DestroyEntity(first);

    const EntityId second = fixture.Thing();
    const ParticipantBodyChange change =
        fixture.Lifecycle.AssignBody(fixture.Entities, participant, second);

    EXPECT_EQ(change.Status, ParticipantBodyStatus::Assigned);
    EXPECT_EQ(fixture.Control(participant).Body, second);
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
