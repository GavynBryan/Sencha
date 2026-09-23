#include <gtest/gtest.h>

#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <authored/AuthoredApi.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/World.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Hand-written companion, shaped like a generated one.
struct FramePulse
{
    std::int64_t Tick = 0;
};

template<>
struct AuthoredApiDefinition<FramePulse>
{
    static constexpr std::string_view EventName = "test.frame.pulse";

    static AuthoredEventDefinition DescribeEvent()
    {
        AuthoredEventDefinition definition;
        definition.Name = "test.frame.pulse";
        DataFieldSchema field;
        field.Key = "tick";
        AuthoredValueTraits<std::int64_t>::Describe(field);
        definition.Payload.Children.push_back(std::move(field));
        return definition;
    }

    static void Encode(const FramePulse& event, AuthoredArguments& payload)
    {
        payload.Resize(1);
        payload.Set(0, AuthoredValueTraits<std::int64_t>::Encode(event.Tick));
    }
};

namespace
{
class EmptyRoot
{
public:
    EmptyRoot()
        : Path(std::filesystem::temp_directory_path() / "sencha-authored-event-frame-root")
    {
        std::filesystem::remove_all(Path);
        std::filesystem::create_directories(Path);
    }
    ~EmptyRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Path, ec);
    }
    [[nodiscard]] std::string String() const { return Path.string(); }

private:
    std::filesystem::path Path;
};

// Stops after enough frames for several fixed ticks.
struct PulseSystem
{
    Engine* Host = nullptr;
    std::vector<std::uint64_t>* Announced = nullptr;
    int Frames = 0;

    void FixedLogic(FixedLogicContext& ctx)
    {
        AuthoredEventDispatcher* events = Host->TryAuthoredEvents();
        if (events != nullptr
            && events->Publish(EntityId{}, FramePulse{ .Tick = static_cast<std::int64_t>(ctx.Time.TickIndex) }))
        {
            Announced->push_back(ctx.Time.TickIndex);
        }
    }

    void FrameUpdate(FrameUpdateContext&)
    {
        if (++Frames >= 120)
            Host->RequestExit();
    }
};

class PulseGame final : public Game
{
public:
    PulseGame(std::string root, bool declareBadly)
        : Root(std::move(root))
        , DeclareBadly(declareBadly)
    {
    }

    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Debug.ConsoleLogging = false;
        ctx.Config.Runtime.TargetFps = 1000.0;
        ctx.Config.Runtime.ContentRoots = { Root };
    }

    void OnRegisterVocabulary(World& world) override
    {
        if (DeclareBadly)
        {
            AuthoredEventDefinition bad;
            bad.Name = "test..pulse";
            AuthoredEventRegistrationScope scope(*FindAuthoredEventRegistry(world), "test");
            (void)scope.Declare(std::move(bad));
            (void)scope.Commit();
            return;
        }
        AuthoredVocabularyScope vocabulary(world, "test");
        vocabulary.Declare<FramePulse>();
        (void)vocabulary.Commit();
    }

    void OnStart(GameStartupContext&) override
    {
        Started = true;
        AuthoredEventDispatcher* events = GetEngine().TryAuthoredEvents();
        if (events == nullptr)
            return;
        Heard = events->Subscribe<&PulseGame::Deliver>(
            events->Registry().Resolve("test.frame.pulse"), EntityId{}, *this);
    }

    void OnRegisterSystems(SystemRegisterContext& ctx) override
    {
        PulseSystem& pulses = ctx.Schedule.Register<PulseSystem>();
        pulses.Host = &GetEngine();
        pulses.Announced = &Announced;
    }

    void OnShutdown(GameShutdownContext&) override { Heard.Reset(); }

    static void Deliver(PulseGame& self, const AuthoredEventDelivery& delivery)
    {
        std::int64_t announced = -1;
        (void)delivery.Payload->TryGetInt(0, announced);
        self.Delivered.push_back({ static_cast<std::uint64_t>(announced), delivery.Tick });
    }

    struct Delivery
    {
        std::uint64_t AnnouncedAt = 0;
        std::uint64_t DeliveredAt = 0;
    };

    std::string Root;
    bool DeclareBadly = false;
    bool Started = false;
    std::vector<std::uint64_t> Announced;
    std::vector<Delivery> Delivered;
    AuthoredEventSubscription Heard;
};
} // namespace

TEST(AuthoredEventFrame, AnEventAnnouncedInFixedLogicIsDeliveredInTheSameTick)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const EmptyRoot root;
    PulseGame game(root.String(), false);

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    game.OnConfigure(configure);

    Engine engine(config);
    ASSERT_EQ(engine.Run(game), 0);
    engine.Shutdown();

    ASSERT_FALSE(game.Announced.empty()) << "no fixed tick ran";
    ASSERT_EQ(game.Delivered.size(), game.Announced.size());
    for (std::size_t index = 0; index < game.Delivered.size(); ++index)
    {
        EXPECT_EQ(game.Delivered[index].AnnouncedAt, game.Announced[index]);
        EXPECT_EQ(game.Delivered[index].DeliveredAt, game.Announced[index])
            << "delivered on a later tick than it was announced on";
    }
}

TEST(AuthoredEventFrame, ARefusedEventDeclarationStopsStartup)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const EmptyRoot root;
    PulseGame game(root.String(), true);

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    game.OnConfigure(configure);

    Engine engine(config);
    EXPECT_NE(engine.Run(game), 0);
    EXPECT_FALSE(game.Started);
}
