#include <gtest/gtest.h>

#include <app/Engine.h>
#include <app/Game.h>
#include <app/EngineSchedule.h>
#include <app/EngineVerbs.h>
#include <app/GameContexts.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementRegistration.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

// Where in a real startup the vocabulary is installed, and what happens when
// a game gets it wrong.
//
// The hook runs once, before content can name anything, through a scope the
// host checks afterwards; a declaration the catalog refused stops the process
// before its first frame. The engine's own verbs are there beside the game's,
// and a game binds what it declared from OnStart, through the dispatcher the
// host composed.

// The mode the boot game declares. A locomotion mode is registered by a
// component type, which needs a stable identity; a specialization cannot live
// in an unnamed namespace.
struct BootHoverMode
{
};
SENCHA_DECLARE_COMPONENT_TYPE(BootHoverMode, "test.boot_hover_mode");

namespace
{
class EmptyRoot
{
public:
    EmptyRoot()
        : Path(std::filesystem::temp_directory_path() / "sencha-verb-boot-root")
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

struct StopAfterFrames
{
    Engine* Host = nullptr;
    int Frames = 0;
    // What a system cancelling queued work through the dispatcher would see
    // at scheduled-system shutdown: the dependency, alive, and closed. Written
    // through to the game, because the schedule deletes this system right
    // after calling Shutdown.
    bool* DispatcherAliveAtShutdown = nullptr;
    bool* AdmissionClosedAtShutdown = nullptr;

    void FrameUpdate(FrameUpdateContext&)
    {
        if (++Frames >= 4 && Host != nullptr)
            Host->RequestExit();
    }

    void Shutdown()
    {
        const VerbDispatcher* verbs = Host != nullptr ? Host->TryVerbs() : nullptr;
        if (DispatcherAliveAtShutdown != nullptr)
            *DispatcherAliveAtShutdown = verbs != nullptr;
        if (AdmissionClosedAtShutdown != nullptr)
            *AdmissionClosedAtShutdown = verbs != nullptr && !verbs->IsAdmitting();
    }
};

class PingOperation
{
public:
    VerbAdmission Invoke(const VerbInvocation&)
    {
        ++Calls;
        return VerbAdmission::Accepted;
    }
    int Calls = 0;
};

class VocabularyGame final : public Game
{
public:
    explicit VocabularyGame(std::string root, bool declareBadly = false)
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
        ++VocabularyCalls;
        // The engine's names are already there when the game's turn comes, and
        // nothing content-related has been resolved yet.
        VerbRegistry* verbs = FindVerbRegistry(world);
        SawEngineVerbsFirst = verbs != nullptr && verbs->Find(kRuntimeResumeVerb).IsValid();
        if (verbs == nullptr)
            return;

        // A tag beside the verb, into the same World: the registries the hook
        // declares into exist here exactly as they do in an editor document.
        if (auto* tags = world.TryGetResource<GameplayTagRegistry>())
            TagDeclared = tags->RegisterTag("Test.Declared").has_value();

        // A mode is movement's vocabulary, which this game opts into here, in
        // the one hook every host runs -- so the declaration lands at runtime
        // exactly as it does in an editor document.
        InstallMovementVocabulary(world);
        LocomotionModeRegistry& modes = world.GetResource<LocomotionModeRegistry>();
        if (modes.Find("test.mode.hover") == nullptr)
            ModeDeclared = modes.Register<BootHoverMode>("test.mode.hover").IsValid();

        VerbRegistrationScope scope(*verbs, "test");
        VerbDefinition ping;
        ping.Name = DeclareBadly ? "test..ping" : "test.ping";
        DataFieldSchema kind;
        kind.Key = "Kind";
        kind.Kind = DataFieldKind::GameplayTag;
        ping.Arguments.Children.push_back(std::move(kind));
        (void)scope.Declare(std::move(ping));
        // Deliberately unchecked, the way a careless module would leave it.
        (void)scope.Commit();
    }

    void OnStart(GameStartupContext&) override
    {
        ++StartCalls;
        VocabularyCallsAtStart = VocabularyCalls;
        VerbDispatcher* dispatcher = GetEngine().TryVerbs();
        HadDispatcherAtStart = dispatcher != nullptr;
        if (const auto* modes =
                GetEngine().World().Entities().TryGetResource<LocomotionModeRegistry>())
            ModePresentAtStart = modes->Find("test.mode.hover") != nullptr;
        if (dispatcher == nullptr)
            return;

        const VerbRegistry& verbs = dispatcher->Registry();
        Token = dispatcher->Bind(verbs.Find("test.ping"), Ping);

        // A binding compiled against the live catalog, invoked from the game's
        // own startup: the whole path, with no UI in the process at all.
        VerbBindingDesc desc;
        desc.Key = "ping";
        desc.KeyId = MakeVerbBindingKey(desc.Key);
        desc.VerbName = "test.ping";
        VerbBindingArgument kind;
        kind.Key = "Kind";
        kind.Source = VerbArgumentSource::Tag;
        kind.Text = "Test.Declared";
        desc.Arguments.push_back(std::move(kind));
        std::vector<std::string> errors;
        CompiledVerbBinding compiled;
        if (CompileVerbBinding(desc, MakeVerbBindingEnvironment(GetEngine().World().Entities()),
                               compiled, errors))
        {
            Admission = dispatcher->Invoke(compiled, {}).Status;
        }
        EngineVerbsDeclared = verbs.Find(kApplicationQuitVerb).IsValid();
        // No session: this process decides what happens, the way a standalone
        // game always has.
        AuthoritativeAtStart = IsSimulationAuthority(GetEngine().World().Entities())
            && GetEngine().World().Entities().HasResource<SimulationAuthority>();
        // Declared by the engine, but this headless host composed no shell to
        // bind them: discovery without execution.
        EngineVerbsUnavailable = !dispatcher->HasImplementation(verbs.Find(kRuntimeResumeVerb));
    }

    void OnRegisterSystems(SystemRegisterContext& ctx) override
    {
        StopAfterFrames& stop = ctx.Schedule.Register<StopAfterFrames>();
        stop.Host = &GetEngine();
        stop.DispatcherAliveAtShutdown = &DispatcherAliveAtShutdown;
        stop.AdmissionClosedAtShutdown = &AdmissionClosedAtShutdown;
    }

    void OnShutdown(GameShutdownContext&) override
    {
        // Given back while the dispatcher and this object both exist.
        Token.Reset();
    }

    std::string Root;
    bool DeclareBadly = false;
    int VocabularyCalls = 0;
    int StartCalls = 0;
    int VocabularyCallsAtStart = 0;
    bool SawEngineVerbsFirst = false;
    bool TagDeclared = false;
    bool AuthoritativeAtStart = false;
    bool ModeDeclared = false;
    bool ModePresentAtStart = false;
    bool DispatcherAliveAtShutdown = false;
    bool AdmissionClosedAtShutdown = false;
    bool HadDispatcherAtStart = false;
    bool EngineVerbsDeclared = false;
    bool EngineVerbsUnavailable = false;
    VerbAdmission Admission = VerbAdmission::Unavailable;
    PingOperation Ping;
    VerbBindingToken Token;
};
}

TEST(VerbVocabularyBoot, TheHookRunsOnceBeforeStartAndTheGameBindsFromStart)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const EmptyRoot root;
    VocabularyGame game(root.String());

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    game.OnConfigure(configure);

    Engine engine(config);
    ASSERT_EQ(engine.Run(game), 0);

    EXPECT_EQ(game.VocabularyCalls, 1);
    EXPECT_EQ(game.StartCalls, 1);
    EXPECT_EQ(game.VocabularyCallsAtStart, 1) << "the hook ran after OnStart, or not at all";
    EXPECT_TRUE(game.SawEngineVerbsFirst);
    EXPECT_TRUE(game.HadDispatcherAtStart);
    EXPECT_TRUE(game.EngineVerbsDeclared);
    EXPECT_TRUE(game.EngineVerbsUnavailable);
    EXPECT_TRUE(game.TagDeclared) << "the runtime World had no tag registry when the hook ran";
    EXPECT_TRUE(game.ModeDeclared);
    EXPECT_TRUE(game.AuthoritativeAtStart);
    EXPECT_TRUE(game.ModePresentAtStart) << "a mode declared in the hook was not there at runtime";
    EXPECT_EQ(game.Admission, VerbAdmission::Accepted)
        << "a binding naming the hook's own tag did not resolve at runtime";
    EXPECT_EQ(game.Ping.Calls, 1);

    // The dispatcher outlives the run, because the scheduled systems that hold
    // it are shut down by Engine::Shutdown; it goes there, before the World
    // whose catalog it reads. The catalog is the World's and lives on.
    EXPECT_NE(engine.TryVerbs(), nullptr);
    EXPECT_FALSE(engine.TryVerbs()->IsAdmitting());
    EXPECT_NE(FindVerbRegistry(engine.World().Entities()), nullptr);
    engine.Shutdown();
    EXPECT_EQ(engine.TryVerbs(), nullptr);
    // What the system saw when the schedule shut it down: a dispatcher still
    // there to report through, already refusing new work.
    EXPECT_TRUE(game.DispatcherAliveAtShutdown);
    EXPECT_TRUE(game.AdmissionClosedAtShutdown);
}

TEST(VerbVocabularyBoot, ARefusedDeclarationStopsTheProcessBeforeItsFirstFrame)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    const EmptyRoot root;
    VocabularyGame game(root.String(), /*declareBadly=*/true);

    EngineConfig config;
    GameConfigureContext configure{ .Config = config };
    game.OnConfigure(configure);

    Engine engine(config);
    EXPECT_NE(engine.Run(game), 0);
    EXPECT_EQ(game.VocabularyCalls, 1);
    // The game ignored its own failed Commit; the host did not.
    EXPECT_EQ(game.StartCalls, 0);
    EXPECT_EQ(engine.TryVerbs(), nullptr);
    engine.Shutdown();
}
