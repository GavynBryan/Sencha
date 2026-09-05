#include <gtest/gtest.h>

#include <app/SessionParticipantProjection.h>
#include <app/SessionParticipantDiagnostics.h>
#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <net/ClientPrediction.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetPeerInputSource.h>
#include <net/NetReplicationComponents.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantControl.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>

namespace
{
    struct ProjectionWorld
    {
        WorldComponentSchema Schema;
        World Entities;
        SessionParticipantProjection Projection;
        int Built = 0;
        int Asked = 0;

        ProjectionWorld()
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

        // Opt-in rather than a constructor default: other tests here install
        // policies that answer differently, and a test asserting on Built or
        // Asked should show where those counts come from.
        void CountPolicyCalls()
        {
            Projection.Policies().BuildParticipant =
                [&](World&, EntityId) { ++Built; };
            Projection.Policies().ProvideBody = [&](World&, EntityId) {
                ++Asked;
                return Thing();
            };
        }
    };
}

TEST(SessionParticipantProjection, AdmissionOwnsTheWholePeerInvariant)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();

    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 3 });
    const EntityId participant = admitted.Admission.Participant;
    const EntityId body = admitted.Body.Body;

    ASSERT_TRUE(participant.IsValid());
    ASSERT_TRUE(body.IsValid());
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(participant));
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(body));
    EXPECT_EQ(fixture.Entities.TryGet<NetParticipantIdentity>(participant)->Peer,
              3u);
    EXPECT_EQ(fixture.Entities.TryGet<NetOwner>(body)->Peer, 3u);
    EXPECT_EQ(fixture.Entities.TryGet<NetDrivenBy>(body)->Peer, 3u);
    EXPECT_EQ(fixture.Entities.TryGet<InputActionSourceRef>(body)->Source,
              fixture.Control(participant).Source);
}

TEST(SessionParticipantProjection, ReadmissionDoesNotRebuildOrRebind)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();
    const SessionParticipantAdmission first =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 4 });

    const SessionParticipantAdmission second =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 4 });

    EXPECT_EQ(second.Admission.Status, ParticipantAdmissionStatus::Existing);
    EXPECT_EQ(second.Admission.Participant, first.Admission.Participant);
    EXPECT_EQ(second.Body.Status, ParticipantBodyStatus::AlreadyAssigned);
    EXPECT_EQ(fixture.Built, 1);
    EXPECT_EQ(fixture.Asked, 1);
}

TEST(SessionParticipantProjection, ReadmissionDoesNotRetryAnUnavailableBody)
{
    ProjectionWorld fixture;
    fixture.Projection.Policies().ProvideBody = [&](World&, EntityId) {
        ++fixture.Asked;
        return EntityId{};
    };
    const SessionParticipantAdmission first =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 14 });
    ASSERT_EQ(first.Body.Status, ParticipantBodyStatus::Unavailable);

    const SessionParticipantAdmission second =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 14 });

    EXPECT_EQ(second.Admission.Status, ParticipantAdmissionStatus::Existing);
    EXPECT_EQ(second.Body.Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(fixture.Asked, 1)
        << "readmission retried a policy decision without an explicit request";
}

TEST(SessionParticipantProjection, ControlChangeCannotLeaveDrivenByStale)
{
    ProjectionWorld fixture;
    const EntityId participant = fixture.Projection
        .AdmitPeer(fixture.Entities, PeerId{ 5 }).Admission.Participant;
    const EntityId body = fixture.Thing();
    const EntityId turret = fixture.Thing();
    (void)fixture.Projection.SetControlSubject(
        fixture.Entities, participant, body);

    (void)fixture.Projection.SetControlSubject(
        fixture.Entities, participant, turret);

    EXPECT_FALSE(fixture.Entities.HasComponent<NetDrivenBy>(body));
    ASSERT_NE(fixture.Entities.TryGet<NetDrivenBy>(turret), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<NetDrivenBy>(turret)->Peer, 5u);
    EXPECT_EQ(fixture.Control(participant).ControlSubject, turret);
}

TEST(SessionParticipantProjection, PeerDepartureClosesEveryProjection)
{
    ProjectionWorld fixture;
    fixture.Entities.AddResource<InputActionSourceTable>();
    fixture.Projection.Policies().ProvideBody =
        [&](World&, EntityId) { return fixture.Thing(); };
    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 6 });
    const EntityId participant = admitted.Admission.Participant;
    const EntityId body = admitted.Body.Body;
    const InputActionSourceId source = fixture.Control(participant).Source;
    (void)fixture.Entities.GetResource<InputActionSourceTable>().Open(source, 4);

    const SessionParticipantRetirement retired =
        fixture.Projection.RetirePeer(fixture.Entities, PeerId{ 6 });

    EXPECT_TRUE(retired.Retirement.BodyReaped);
    EXPECT_FALSE(fixture.Entities.IsAlive(participant));
    EXPECT_FALSE(fixture.Entities.IsAlive(body));
    EXPECT_EQ(fixture.Entities.GetResource<InputActionSourceTable>().Find(source),
              nullptr);
    EXPECT_EQ(NetFindSourceForPeer(fixture.Entities, PeerId{ 6 }),
              kLocalInputActionSource);
}

TEST(SessionParticipantProjection, ClientReconciliationIsQueryThenEffect)
{
    ProjectionWorld fixture;
    ClientPrediction prediction;
    const EntityId mine = fixture.Thing();
    fixture.Entities.AddComponent<NetDrivenBy>(mine,
                                                NetDrivenBy{ .Peer = 7 });

    fixture.Projection.ReconcileClientControl(
        fixture.Entities, PeerId{ 7 }, prediction);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), mine);
    EXPECT_EQ(prediction.Predicted(), mine);
    // Identity only: which controller facilities follow from it is a game's
    // rule, so the projection adds no controller component.
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(mine));
}

// A singleplayer game's participant and body have no business in the
// replicated table. Without a session there is nobody to replicate to, and
// stamping anyway meant every FPS pawn sat in it for its whole life.
TEST(SessionParticipantProjection, AdmissionWithoutASessionCarriesNoNetState)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();

    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitLocal(fixture.Entities, /*sessionActive=*/false);
    const EntityId participant = admitted.Admission.Participant;
    const EntityId body = admitted.Body.Body;
    ASSERT_TRUE(participant.IsValid());
    ASSERT_TRUE(body.IsValid());

    EXPECT_FALSE(fixture.Entities.HasComponent<NetParticipantIdentity>(participant));
    EXPECT_FALSE(fixture.Entities.HasComponent<NetReplicated>(participant));
    EXPECT_FALSE(fixture.Entities.HasComponent<NetReplicated>(body));
    // The participant is still this machine's person for every other purpose.
    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), body);
}

TEST(SessionParticipantProjection, AdmissionInsideASessionStampsBoth)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();

    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitLocal(fixture.Entities, /*sessionActive=*/true);

    EXPECT_TRUE(fixture.Entities.HasComponent<NetParticipantIdentity>(
        admitted.Admission.Participant));
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(
        admitted.Admission.Participant));
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(admitted.Body.Body));
}

// Hosting after singleplayer play: the person already here becomes the
// authority's participant, retroactively and exactly once.
TEST(SessionParticipantProjection, SessionStartStampsWhatWasAdmittedBeforeIt)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();
    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitLocal(fixture.Entities, false);
    const EntityId participant = admitted.Admission.Participant;
    const EntityId body = admitted.Body.Body;

    fixture.Projection.ProjectSessionStart(fixture.Entities);

    const NetParticipantIdentity* identity =
        fixture.Entities.TryGet<NetParticipantIdentity>(participant);
    ASSERT_NE(identity, nullptr);
    EXPECT_EQ(identity->Peer, kNetAuthorityPeer);
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(participant));
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(body));

    // Idempotent: a second start changes nothing and adds nothing.
    fixture.Projection.ProjectSessionStart(fixture.Entities);
    EXPECT_EQ(fixture.Entities.TryGet<NetParticipantIdentity>(participant)->Peer,
              kNetAuthorityPeer);
    EXPECT_EQ(fixture.Asked, 1) << "a session start is not a body request";
}

// A peer only exists inside a session, so its admission stamps regardless of
// what the caller knows about one.
TEST(SessionParticipantProjection, PeerAdmissionAlwaysStamps)
{
    ProjectionWorld fixture;
    fixture.CountPolicyCalls();

    const SessionParticipantAdmission admitted =
        fixture.Projection.AdmitPeer(fixture.Entities, PeerId{ 3 });
    const EntityId participant = admitted.Admission.Participant;
    ASSERT_TRUE(participant.IsValid());

    ASSERT_NE(fixture.Entities.TryGet<NetParticipantIdentity>(participant), nullptr);
    EXPECT_EQ(fixture.Entities.TryGet<NetParticipantIdentity>(participant)->Peer, 3u);
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(participant));
    EXPECT_TRUE(fixture.Entities.HasComponent<NetReplicated>(admitted.Body.Body));
}

// SetLocalControlSubject is a fact, not a switch: it changes which entity is
// named and nothing about that entity's components.
TEST(SessionParticipantProjection, LocalControlSubjectChangesNoComponentMembership)
{
    ProjectionWorld fixture;
    const EntityId first = fixture.Thing();
    const EntityId second = fixture.Thing();

    (void)SetLocalControlSubject(fixture.Entities, first);
    (void)SetLocalControlSubject(fixture.Entities, second);

    EXPECT_EQ(LocalControlSubjectOf(fixture.Entities), second);
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(first));
    EXPECT_FALSE(fixture.Entities.HasComponent<LocalLookControl>(second));
}

TEST(SessionParticipantProjection, GenericRetirementRejectsPeerBoundParticipants)
{
    ProjectionWorld fixture;
    const EntityId participant = fixture.Projection
        .AdmitPeer(fixture.Entities, PeerId{ 8 }).Admission.Participant;

    const SessionParticipantRetirement retired =
        fixture.Projection.RetireParticipant(fixture.Entities, participant);

    EXPECT_EQ(retired.Status, SessionParticipantRetirementStatus::PeerBound);
    EXPECT_TRUE(fixture.Entities.IsAlive(participant));
}

TEST(SessionParticipantDiagnostics, ReportsAHandWrittenDrivenByMismatch)
{
    ProjectionWorld fixture;
    const EntityId participant = fixture.Projection
        .AdmitPeer(fixture.Entities, PeerId{ 9 }).Admission.Participant;
    const EntityId subject = fixture.Thing();
    (void)fixture.Projection.SetControlSubject(
        fixture.Entities, participant, subject);
    fixture.Entities.TryGet<NetDrivenBy>(subject)->Peer = 12;

    const SessionParticipantValidation validation =
        ValidateSessionParticipants(fixture.Entities);

    EXPECT_FALSE(validation.Ok());
    EXPECT_NE(FormatSessionParticipantStatus(fixture.Entities).find(
                  "session projection FAILED"),
              std::string::npos);
}
