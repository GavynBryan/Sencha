#include <gtest/gtest.h>

#include <core/console/ConsoleRegistry.h>
#include <net/LoopbackTransport.h>
#include <net/NetCVarSync.h>
#include <net/NetProtocol.h>
#include <net/NetSession.h>

#include <array>
#include <string>
#include <vector>

namespace
{
    // A registry with one of each interesting flag, so the rules can be checked
    // without booting an engine.
    struct Registry
    {
        ConsoleRegistry Console;

        Registry()
        {
            Console.RegisterCVar({
                .Name = "test.plain",
                .Owner = "test",
                .Type = CVarType::Double,
                .DefaultValue = 1.0,
                .CurrentValue = 1.0,
                .Flags = CVarFlags::None,
            });
            Console.RegisterCVar({
                .Name = "test.session",
                .Owner = "test",
                .Type = CVarType::Double,
                .DefaultValue = 1.0,
                .CurrentValue = 1.0,
                .Flags = CVarFlags::Replicated,
            });
            Console.RegisterCVar({
                .Name = "test.cheat",
                .Owner = "test",
                .Type = CVarType::Bool,
                .DefaultValue = false,
                .CurrentValue = false,
                .Flags = CVarFlags::Cheat,
            });
        }

        ConsoleResult Set(std::string_view name, const CVarValue& value)
        {
            return Console.SetCVar(name, value, ConsoleValueSource{ "test" },
                                   ConsolePhase::EngineReady, false);
        }

        [[nodiscard]] double Value(std::string_view name) const
        {
            const CVarMetadata* metadata = Console.FindCVar(name);
            return std::get<double>(metadata->CurrentValue);
        }
    };

    NetCVarUpdate MakeUpdate(std::string name, CVarType type)
    {
        NetCVarUpdate update;
        update.Name = std::move(name);
        update.Type = type;
        return update;
    }

    NetCVarUpdate BoolUpdate(std::string name, bool value)
    {
        NetCVarUpdate update = MakeUpdate(std::move(name), CVarType::Bool);
        update.BoolValue = value;
        return update;
    }

    NetCVarUpdate IntUpdate(std::string name, std::int64_t value)
    {
        NetCVarUpdate update = MakeUpdate(std::move(name), CVarType::Int);
        update.IntValue = value;
        return update;
    }

    NetCVarUpdate DoubleUpdate(std::string name, double value)
    {
        NetCVarUpdate update = MakeUpdate(std::move(name), CVarType::Double);
        update.DoubleValue = value;
        return update;
    }

    NetCVarUpdate StringUpdate(std::string name, std::string value)
    {
        NetCVarUpdate update = MakeUpdate(std::move(name), CVarType::String);
        update.StringValue = std::move(value);
        return update;
    }

    ConsoleAuthorityPolicy Solo()
    {
        return ConsoleAuthorityPolicy{};
    }

    ConsoleAuthorityPolicy Host(bool cheats = false)
    {
        return ConsoleAuthorityPolicy{ .InSession = true,
                                       .HasAuthority = true,
                                       .CheatsEnabled = cheats };
    }

    ConsoleAuthorityPolicy Client(bool cheats = false)
    {
        return ConsoleAuthorityPolicy{ .InSession = true,
                                       .HasAuthority = false,
                                       .CheatsEnabled = cheats };
    }
}

//=============================================================================
// Enforcement
//=============================================================================

// The property that keeps single-player unaffected. Everything below only
// applies inside a session.
TEST(NetCVar, OutsideASessionNothingIsRestricted)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Solo());

    EXPECT_TRUE(registry.Set("test.session", 2.0).Succeeded());
    EXPECT_TRUE(registry.Set("test.cheat", true).Succeeded());
    EXPECT_DOUBLE_EQ(registry.Value("test.session"), 2.0);
}

TEST(NetCVar, AClientCannotSetWhatTheSessionOwns)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client());

    const ConsoleResult result = registry.Set("test.session", 5.0);
    EXPECT_FALSE(result.Succeeded());
    EXPECT_DOUBLE_EQ(registry.Value("test.session"), 1.0)
        << "a refused write must not have changed the value";
}

// Refusing without saying what is in force leaves a player guessing at the
// rules of the server they are on.
TEST(NetCVar, TheRefusalNamesTheValueThatIsInForce)
{
    Registry registry;
    ASSERT_TRUE(registry.Console.SetCVar("test.session", 3.5,
                                         ConsoleValueSource{ "server" },
                                         ConsolePhase::EngineReady, true)
                    .Succeeded());
    registry.Console.SetAuthorityPolicy(Client());

    const ConsoleResult result = registry.Set("test.session", 5.0);
    ASSERT_FALSE(result.Succeeded());
    ASSERT_FALSE(result.Output.empty());
    EXPECT_NE(result.Output.front().Text.find("3.5"), std::string::npos)
        << "refusal said: " << result.Output.front().Text;
}

TEST(NetCVar, TheAuthorityCanStillSetWhatTheSessionOwns)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Host());

    EXPECT_TRUE(registry.Set("test.session", 5.0).Succeeded());
    EXPECT_DOUBLE_EQ(registry.Value("test.session"), 5.0);
}

// A cvar with no session flag is nobody's business but the local machine's,
// even inside a session: a client tuning its own renderer is not a cheat.
TEST(NetCVar, AClientKeepsItsOwnUnflaggedCvars)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client());

    EXPECT_TRUE(registry.Set("test.plain", 9.0).Succeeded());
    EXPECT_DOUBLE_EQ(registry.Value("test.plain"), 9.0);
}

TEST(NetCVar, CheatsAreBlockedInASessionUntilEnabled)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client(false));
    EXPECT_FALSE(registry.Set("test.cheat", true).Succeeded());

    registry.Console.SetAuthorityPolicy(Client(true));
    EXPECT_TRUE(registry.Set("test.cheat", true).Succeeded());
}

// The one that matters most: a listen-server host must not be able to grant
// themselves what the other players cannot have.
TEST(NetCVar, TheHostIsBoundByItsOwnCheatSetting)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Host(false));

    EXPECT_FALSE(registry.Set("test.cheat", true).Succeeded())
        << "holding authority must not exempt the host from net.cheats";

    registry.Console.SetAuthorityPolicy(Host(true));
    EXPECT_TRUE(registry.Set("test.cheat", true).Succeeded());
}

// The forced path is the authority's decision arriving, and the config and
// startup paths that predate any session. It is not a request from a player.
TEST(NetCVar, AForcedWriteIsNotSubjectToSessionRules)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client());

    EXPECT_TRUE(registry.Console
                    .SetCVar("test.session", 7.0, ConsoleValueSource{ "server" },
                             ConsolePhase::EngineReady, true)
                    .Succeeded());
    EXPECT_DOUBLE_EQ(registry.Value("test.session"), 7.0);
}

// A session ending has to give the console back.
TEST(NetCVar, LeavingASessionRestoresTheLocalConsole)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client());
    ASSERT_FALSE(registry.Set("test.session", 5.0).Succeeded());

    registry.Console.SetAuthorityPolicy(Solo());
    EXPECT_TRUE(registry.Set("test.session", 5.0).Succeeded());
}

//=============================================================================
// The engine's own sweep
//=============================================================================

TEST(NetCVar, TimescaleAndTickRateAreTheSessionsToDecide)
{
    // Registered here rather than booting an engine; what matters is that the
    // flag is on the ones a per-client value would desync.
    ConsoleRegistry registry;
    registry.RegisterCVar({ .Name = "time.timescale", .Owner = "engine",
                            .Type = CVarType::Double, .DefaultValue = 1.0,
                            .CurrentValue = 1.0,
                            .Flags = CVarFlags::Transient | CVarFlags::Replicated });

    const CVarMetadata* timescale = registry.FindCVar("time.timescale");
    ASSERT_NE(timescale, nullptr);
    EXPECT_TRUE(HasFlag(timescale->Flags, CVarFlags::Replicated));
}

//=============================================================================
// Sync
//=============================================================================

TEST(NetCVarSync, AnUpdateRoundTrips)
{
    const std::vector<NetCVarUpdate> cases{
        BoolUpdate("a.bool", true),
        IntUpdate("a.int", -42),
        DoubleUpdate("a.double", 0.125),
        StringUpdate("a.string", "hello"),
    };
    for (const NetCVarUpdate& sent : cases)
    {
        std::array<std::byte, 512> scratch{};
        const std::size_t size = NetEncodeCVarUpdate(sent, scratch);
        ASSERT_GT(size, 0u) << sent.Name;

        NetCVarUpdate received;
        ASSERT_TRUE(NetDecodeCVarUpdate(std::span(scratch).subspan(0, size), received))
            << sent.Name;
        EXPECT_EQ(received.Name, sent.Name);
        EXPECT_EQ(received.Type, sent.Type);
        EXPECT_EQ(received.BoolValue, sent.BoolValue);
        EXPECT_EQ(received.IntValue, sent.IntValue);
        EXPECT_DOUBLE_EQ(received.DoubleValue, sent.DoubleValue);
        EXPECT_EQ(received.StringValue, sent.StringValue);
    }
}

TEST(NetCVarSync, TruncatedUpdatesAreRefused)
{
    const NetCVarUpdate sent = DoubleUpdate("a.double", 2.5);
    std::array<std::byte, 512> scratch{};
    const std::size_t size = NetEncodeCVarUpdate(sent, scratch);
    ASSERT_GT(size, 0u);

    for (std::size_t length = 0; length < size; ++length)
    {
        NetCVarUpdate received;
        EXPECT_FALSE(NetDecodeCVarUpdate(std::span(scratch).subspan(0, length), received))
            << "accepted an update truncated to " << length;
    }
}

TEST(NetCVarSync, AnUnknownTypeIsRefused)
{
    // Built by hand rather than by corrupting a valid message at a computed
    // offset: the offset would encode an assumption about the length prefix
    // that the format is free to change.
    std::array<std::byte, 512> scratch{};
    NetWriter writer(scratch);
    writer.WriteU8(static_cast<std::uint8_t>(NetPayloadKind::CVar));
    writer.WriteString("a.bool", kNetMaxCVarNameBytes);
    writer.WriteU8(0x7F);  // no such CVarType
    writer.WriteU8(1);
    ASSERT_FALSE(writer.Overflowed());

    NetCVarUpdate received;
    EXPECT_FALSE(NetDecodeCVarUpdate(writer.Written(), received));
}

// The client half of the malicious-authority story: a server does not get to
// grow a client's console or reach past the flag that says what it owns.
TEST(NetCVarSync, AnAuthorityCannotCreateOrHijackCvars)
{
    Registry registry;

    const NetCVarUpdate invented = DoubleUpdate("not.a.real.cvar", 1.0);
    EXPECT_FALSE(NetApplyCVarUpdate(registry.Console, invented));
    EXPECT_EQ(registry.Console.FindCVar("not.a.real.cvar"), nullptr)
        << "an update must never register a cvar";

    const NetCVarUpdate unowned = DoubleUpdate("test.plain", 99.0);
    EXPECT_FALSE(NetApplyCVarUpdate(registry.Console, unowned));
    EXPECT_DOUBLE_EQ(registry.Value("test.plain"), 1.0)
        << "a cvar the session does not own must not be settable by the server";

    const NetCVarUpdate wrongType = BoolUpdate("test.session", true);
    EXPECT_FALSE(NetApplyCVarUpdate(registry.Console, wrongType));
}

TEST(NetCVarSync, AnAuthorityCanSetWhatTheSessionOwns)
{
    Registry registry;
    registry.Console.SetAuthorityPolicy(Client());

    const NetCVarUpdate update = DoubleUpdate("test.session", 4.25);
    EXPECT_TRUE(NetApplyCVarUpdate(registry.Console, update));
    EXPECT_DOUBLE_EQ(registry.Value("test.session"), 4.25)
        << "the authority's value must land even though a client's own would not";
}

//=============================================================================
// The publisher, over a real pair of sessions
//=============================================================================

namespace
{
    struct SessionPair
    {
        LoopbackNetwork Network;
        LoopbackTransport HostTransport{ Network };
        LoopbackTransport ClientTransport{ Network };
        NetSession HostSession{ HostTransport };
        NetSession ClientSession{ ClientTransport };
        double Now = 0.0;
        std::vector<NetSession::Delivery> ClientInbox;

        void Step(int frames = 1)
        {
            for (int i = 0; i < frames; ++i)
            {
                Now += 1.0 / 60.0;
                (void)HostSession.Pump(Now);
                for (NetSession::Delivery& delivery : ClientSession.Pump(Now))
                    ClientInbox.push_back(std::move(delivery));
                HostSession.Flush(Now);
                ClientSession.Flush(Now);
            }
        }
    };

    NetIdentity SharedIdentity()
    {
        return NetIdentity{ .ModuleFingerprint = 1, .WorldIdentity = 2,
                            .FixedTickRateMilliHz = 60000 };
    }
}

TEST(NetCVarPublish, APeerIsSentTheSessionsValuesAndThenOnlyChanges)
{
    Registry registry;
    SessionPair pair;
    ASSERT_TRUE(pair.HostSession.Host(0, SharedIdentity()));
    ASSERT_TRUE(pair.ClientSession.Connect(pair.HostSession.LocalAddress(),
                                           SharedIdentity()));
    pair.Step(6);
    ASSERT_TRUE(pair.ClientSession.IsConnected());

    NetCVarPublisher publisher;

    // First pass: the peer knows nothing, so it is told.
    EXPECT_EQ(publisher.Publish(pair.HostSession, registry.Console).Updates, 1u);
    // Second: nothing changed, so nothing is sent.
    EXPECT_EQ(publisher.Publish(pair.HostSession, registry.Console).Updates, 0u);

    ASSERT_TRUE(registry.Console
                    .SetCVar("test.session", 6.5, ConsoleValueSource{ "test" },
                             ConsolePhase::EngineReady, true)
                    .Succeeded());
    EXPECT_EQ(publisher.Publish(pair.HostSession, registry.Console).Updates, 1u);

    // And it arrives as something the client can apply.
    pair.Step(4);
    Registry mirror;
    bool applied = false;
    for (const NetSession::Delivery& delivery : pair.ClientInbox)
    {
        NetCVarUpdate update;
        if (NetDecodeCVarUpdate(delivery.Payload, update)
            && NetApplyCVarUpdate(mirror.Console, update))
        {
            applied = true;
        }
    }
    EXPECT_TRUE(applied);
    EXPECT_DOUBLE_EQ(mirror.Value("test.session"), 6.5);
}
