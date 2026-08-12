#include <gtest/gtest.h>

#include <net/LoopbackTransport.h>
#include <net/NetSession.h>
#include <net/NetStats.h>
#include <net/NetStatusReport.h>
#include <net/PeerCommandRuntime.h>
#include <net/NetOwnership.h>
#include <net/NetReplicationComponents.h>
#include <net/ReplicationRuntime.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <string>

//=============================================================================
// The account a dedicated host can read
//
// A host runs with no graphics API and no overlay, so until this existed the
// one process where budget occupancy, deferral age, unsendable entities, and
// per-peer input depth decide whether a session is healthy could see none of
// them. These are not tests of formatting. Each one asks a question the brief
// says has to be answerable from a terminal, and fails if the answer is absent
// from the text -- which is the only failure mode that matters here, because a
// diagnostic nobody can read is the same as one that was never written.
//=============================================================================

namespace
{
    NetIdentity SampleIdentity()
    {
        return NetIdentity{
            .ModuleFingerprint = 0xABCDEF,
            .ReplicationTableHash = 0xFEDCBA,
            .WorldIdentity = 0,
            .FixedTickRateMilliHz = 60000,
        };
    }

    // A host with one admitted peer, stepped by hand. Enough of a session that
    // the per-peer section has a row to print.
    struct Session
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession Host{ HostTransport };
        NetSession Client{ ClientTransport };
        double Now = 0.0;

        Session()
        {
            EXPECT_TRUE(Host.Host(0, SampleIdentity()));
            EXPECT_TRUE(Client.Connect(Host.LocalAddress(), SampleIdentity()));
            Step(12);
            EXPECT_TRUE(Client.IsConnected());
        }

        void Step(int frames)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                (void)Host.Pump(Now);
                (void)Client.Pump(Now);
                Host.Flush(Now);
                Client.Flush(Now);
            }
        }
    };

    bool Mentions(const std::string& text, std::string_view what)
    {
        return text.find(what) != std::string::npos;
    }
}

TEST(NetStatusReport, WithoutASessionItSaysSoRatherThanPrintingNothing)
{
    const std::string text = NetFormatStatus(NetStatusSources{});
    EXPECT_EQ(text, "standalone (no session)");
}

// "Which traffic grew" is the first question anyone asks, and the answer used
// to exist only behind ImGui.
TEST(NetStatusReport, AHostAccountsForTrafficByKind)
{
    Session session;
    NetStats traffic;
    double clock = 0.0;
    traffic.Sample(clock);
    traffic.RecordOut(NetTrafficKind::Snapshot, 4096, 8);
    traffic.RecordIn(NetTrafficKind::Command, 512, 8);
    clock += 1.0;
    traffic.Sample(clock);

    NetStatusSources sources;
    sources.Session = &session.Host;
    sources.Traffic = &traffic;
    const std::string text = NetFormatStatus(sources);

    EXPECT_TRUE(Mentions(text, "host")) << text;
    EXPECT_TRUE(Mentions(text, "snapshot")) << text;
    EXPECT_TRUE(Mentions(text, "command")) << text;
    EXPECT_TRUE(Mentions(text, "4.0 KiB/s")) << text;
    EXPECT_TRUE(Mentions(text, "lifetime")) << text;
    // A kind that carried nothing is left out rather than printed as a row of
    // zeroes between the two that moved.
    EXPECT_FALSE(Mentions(text, "cvar")) << text;
}

// The question the panel could answer and a dedicated host could not: is the
// budget the thing holding entities back, and is the queue draining.
TEST(NetStatusReport, AHostReportsBudgetOccupancyAndDeferral)
{
    Session session;
    ReplicationRuntime replication;

    NetStatusSources sources;
    sources.Session = &session.Host;
    sources.Replication = &replication;
    EXPECT_TRUE(Mentions(NetFormatStatus(sources), "nothing published yet"))
        << "a session that has not published has to say that rather than "
           "report zeroes, which read as a session that stopped";
}

TEST(NetStatusReport, AHostReportsWhatEachPeerIsCosting)
{
    Session session;
    PeerCommandRuntime commands;

    NetStatusSources sources;
    sources.Session = &session.Host;
    sources.Commands = &commands;
    const std::string text = NetFormatStatus(sources);

    EXPECT_TRUE(Mentions(text, "peers")) << text;
    EXPECT_TRUE(Mentions(text, "queued")) << text;
    EXPECT_TRUE(Mentions(text, "starved")) << text;
    // Backing up on the reliable channel is a question with counters that
    // already existed and nothing outside a test ever read.
    EXPECT_TRUE(Mentions(text, "outstanding")) << text;
    EXPECT_TRUE(Mentions(text, "resent")) << text;
    // The one admitted peer, by id.
    EXPECT_TRUE(Mentions(text, "1")) << text;
}

// A client's half. Different questions, so a different set of sections rather
// than the host's with the numbers zeroed.
TEST(NetStatusReport, AClientReportsWhatItIsGuessingAndPresenting)
{
    Session session;
    ClientPrediction prediction;
    ReplicationInterpolation interpolation;

    NetStatusSources sources;
    sources.Session = &session.Client;
    sources.Prediction = &prediction;
    sources.Interpolation = &interpolation;
    const std::string text = NetFormatStatus(sources);

    EXPECT_TRUE(Mentions(text, "client")) << text;
    EXPECT_TRUE(Mentions(text, "admitted as peer")) << text;
    EXPECT_TRUE(Mentions(text, "no pawn yet")) << text;
    EXPECT_TRUE(Mentions(text, "nothing mirrored yet")) << text;
    // Its one channel toward the authority, which a host reports per peer.
    EXPECT_TRUE(Mentions(text, "to host")) << text;
    // And not the host's sections: a client publishes nothing and serves nobody.
    EXPECT_FALSE(Mentions(text, "publish")) << text;
    EXPECT_FALSE(Mentions(text, "starved")) << text;
}

// Every source is optional, because the caller that has all of them is the
// engine and the callers that do not are tests and anything embedding a
// session. A missing source must not be a missing report.
TEST(NetStatusReport, ASessionWithNoOtherSourcesStillReports)
{
    Session session;
    NetStatusSources sources;
    sources.Session = &session.Host;

    const std::string text = NetFormatStatus(sources);
    EXPECT_TRUE(Mentions(text, "host")) << text;
    EXPECT_TRUE(Mentions(text, "strike")) << text;
    EXPECT_FALSE(Mentions(text, "traffic")) << text;
}

//=============================================================================
// One object, and who drives what
//
// "Why is this not replicating" and "why does that peer own the wrong thing"
// were answerable only by reading the writer and grepping a game's log line.
//=============================================================================

namespace
{
    // A host with a world, a replicated entity, and one peer that has been
    // published to. Enough that every field of the record has a real value
    // rather than a default.
    struct Published
    {
        Session Link;
        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        World Entities;
        ReplicationRuntime Replication;
        EntityId Subject;

        Published()
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            RegisterEngineComponents(components);
            Schema.Seal();
            Schema.Apply(Entities);
            Layout.Seal();

            Subject = Entities.CreateEntity();
            Entities.AddComponent<LocalTransform>(
                Subject, LocalTransform{ Transform3f{ Vec3d(1.0f, 2.0f, 3.0f),
                                                      Quatf::Identity(),
                                                      Vec3d::One() } });
            Entities.AddComponent<NetReplicated>(Subject, NetReplicated{});
            NetSetOwner(Entities, Subject, PeerId{ 1 });

            (void)Replication.Publish(Link.Host, Entities, Layout, 1);
            Link.Step(2);
        }

        [[nodiscard]] NetEntityId Name() const
        {
            return Replication.AuthorityEntities().TryFind(Subject);
        }

        [[nodiscard]] NetEntityReportSources Sources() const
        {
            NetEntityReportSources sources;
            sources.Session = &Link.Host;
            sources.Replication = &Replication;
            sources.Layout = &Layout;
            return sources;
        }
    };
}

TEST(NetEntityReport, AnIdThisAuthorityNeverMintedIsSaidToBeUnknown)
{
    Published published;
    const std::string text =
        NetFormatEntity(published.Sources(), NetEntityId{ 999999 }, PeerId{});
    EXPECT_TRUE(Mentions(text, "no record of it")) << text;
}

TEST(NetEntityReport, ARecordCarriesTheOwnerAndWhenEachRunLastMoved)
{
    Published published;
    const NetEntityId id = published.Name();
    ASSERT_TRUE(id.IsValid());

    const std::string text = NetFormatEntity(published.Sources(), id, PeerId{});

    EXPECT_TRUE(Mentions(text, "owned by peer 1")) << text;
    // Per field run, because a rotating entity that is not moving sends
    // rotation alone -- "the transform changed" is not the answer.
    EXPECT_TRUE(Mentions(text, "local.position")) << text;
    EXPECT_TRUE(Mentions(text, "local.rotation")) << text;
    EXPECT_TRUE(Mentions(text, "moved at gen")) << text;
    EXPECT_TRUE(Mentions(text, "floor")) << text;
    EXPECT_TRUE(Mentions(text, "owner")) << text;
}

// The state a peer is actually in until it acknowledges anything: written into
// a snapshot, confirmed in none, so everything about it is still owed. It reads
// identically to "never sent" from the floor alone, and the two send someone to
// entirely different places.
TEST(NetEntityReport, APeerThatHasConfirmedNothingIsDistinguishedFromOneNeverSent)
{
    Published published;
    const NetEntityId id = published.Name();
    ASSERT_TRUE(id.IsValid());

    const std::string text = NetFormatEntity(published.Sources(), id, PeerId{ 1 });
    EXPECT_TRUE(Mentions(text, "sent but never confirmed")) << text;
    // And what it is owed, by name, which is the "why can that player not see
    // this" form of the question.
    EXPECT_TRUE(Mentions(text, "local.position")) << text;
    EXPECT_TRUE(Mentions(text, "sencha.net_owner:peer")) << text;
}

TEST(NetOwnersReport, AHostReportsWhatEachPeerDrives)
{
    Published published;
    const std::string text =
        NetFormatOwners(&published.Link.Host, published.Entities,
                        &published.Replication);

    EXPECT_TRUE(Mentions(text, "peer")) << text;
    EXPECT_TRUE(Mentions(text, "owns 1")) << text;
    EXPECT_TRUE(Mentions(text, "net ")) << text;
    EXPECT_FALSE(Mentions(text, "NOT REPLICATED")) << text;
}

// The failure that looks like possession working on the host and nowhere else.
TEST(NetOwnersReport, AnOwnedEntityReplicationCannotNameIsCalledOut)
{
    Published published;
    const EntityId hidden = published.Entities.CreateEntity();
    published.Entities.AddComponent<LocalTransform>(hidden, LocalTransform{});
    NetSetOwner(published.Entities, hidden, PeerId{ 1 });

    const std::string text =
        NetFormatOwners(&published.Link.Host, published.Entities,
                        &published.Replication);
    EXPECT_TRUE(Mentions(text, "NOT REPLICATED")) << text;
}

TEST(NetOwnersReport, WhatThisMachineDrivesIsReportedWithOrWithoutASession)
{
    Published published;
    EXPECT_TRUE(Mentions(NetFormatOwners(nullptr, published.Entities, nullptr),
                         "nothing driven here"));

    NetSetLocalControl(published.Entities, published.Subject, nullptr);
    EXPECT_TRUE(Mentions(NetFormatOwners(nullptr, published.Entities, nullptr),
                         "entity "));
}
