#include <gtest/gtest.h>

#include <abilities/AbilityKit.h>
#include <app/Engine.h>
#include <app/Game.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <app/VocabularyCommands.h>
#include <ecs/World.h>
#include <equipment/EquipmentKit.h>
#include <equipment/EquipmentLayout.h>
#include <equipment/EquipmentSystem.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryKit.h>
#include <inventory/InventorySystem.h>
#include <inventory/ItemDefinition.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace
{
    enum class Kits
    {
        Equipment,
        InventoryVocabularyOnly,
        InventorySystemsOnly,
        EquipmentVocabularyBesideInventorySystems,
        EquipmentBesideAnotherAdmission,
    };

    bool AdmitEverything(const World&, EntityId)
    {
        return true;
    }

    std::string AnswerText(const ConsoleResult& result)
    {
        std::string text;
        for (const ConsoleOutputEntry& output : result.Output)
            text += output.Text;
        return text;
    }

    struct KitProbe
    {
        Engine* Host = nullptr;
        int Frames = 0;
        std::string Give;
        std::string Equip;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (Host == nullptr)
                return;
            if (++Frames == 1)
            {
                const EntityId holder = ctx.Entities.CreateEntity();
                const std::string named = std::format("{}:{}", holder.Index, holder.Generation);
                const VocabularyConsoleTarget target{ .Entities = ctx.Entities,
                                                      .Verbs = Host->TryVerbs(),
                                                      .Queries = Host->TryAuthoredQueries() };
                const std::vector<std::string> give{ "inventory.give", "into=" + named,
                                                     "definition=items/key.sdata" };
                const std::vector<std::string> equip{ "equipment.equip", "wearer=" + named, "entry=" + named };
                Give = AnswerText(RunVerbCommand(target, give));
                Equip = AnswerText(RunVerbCommand(target, equip));
            }
            if (Frames >= 3)
                Host->RequestExit();
        }
    };

    class KitGame final : public Game
    {
    public:
        KitGame(std::string root, Kits kits)
            : Root(std::move(root))
            , Installs(kits)
        {
        }

        void OnConfigure(GameConfigureContext& ctx) override
        {
            ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
            ctx.Config.Debug.ConsoleLogging = false;
            ctx.Config.Runtime.TargetFps = 1000.0;
            ctx.Config.Runtime.FixedTickRate = 120.0;
            ctx.Config.Runtime.ContentRoots = { Root };
        }

        void OnRegisterVocabulary(World& world) override
        {
            switch (Installs)
            {
            case Kits::Equipment:
            case Kits::EquipmentVocabularyBesideInventorySystems:
                InstallEquipmentVocabulary(world);
                break;
            case Kits::InventoryVocabularyOnly:
                InstallInventoryVocabulary(world);
                break;
            case Kits::InventorySystemsOnly:
                break;
            case Kits::EquipmentBesideAnotherAdmission:
                InstallInventoryVocabulary(world);
                world.SetResource(ItemMoveAdmission{ .Admit = &AdmitEverything });
                InstallEquipmentVocabulary(world);
                break;
            }
        }

        void OnRegisterSystems(SystemRegisterContext& ctx) override
        {
            switch (Installs)
            {
            case Kits::Equipment:
            case Kits::EquipmentBesideAnotherAdmission:
                RegisterEquipmentSystems(ctx.Schedule);
                break;
            case Kits::InventorySystemsOnly:
            case Kits::EquipmentVocabularyBesideInventorySystems:
                RegisterInventorySystems(ctx.Schedule);
                break;
            case Kits::InventoryVocabularyOnly:
                break;
            }
        }

    private:
        std::string Root;
        Kits Installs;
    };

    class KitRoot
    {
    public:
        KitRoot()
            : Path(std::filesystem::temp_directory_path() / "sencha-kit-installation-root")
        {
            std::filesystem::remove_all(Path);
            std::filesystem::create_directories(Path);
        }

        ~KitRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }

        [[nodiscard]] std::string String() const { return Path.string(); }

    private:
        std::filesystem::path Path;
    };

    struct KitRun
    {
        explicit KitRun(Kits kits)
            : Game(Root.String(), kits)
        {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
            GameConfigureContext configure{ .Config = Config };
            Game.OnConfigure(configure);
            Host.emplace(Config);
            Probe = &Host->Schedule().Register<KitProbe>();
            Probe->Host = &*Host;
            Exit = Host->Run(Game);
        }

        ~KitRun() { Host->Shutdown(); }

        KitRoot Root;
        KitGame Game;
        EngineConfig Config;
        std::optional<Engine> Host;
        KitProbe* Probe = nullptr;
        int Exit = -1;
    };
}

TEST(KitInstallation, InstallingEquipmentInstallsInventoryAndTheEngineBindsBoth)
{
    KitRun run(Kits::Equipment);

    ASSERT_EQ(run.Exit, 0);
    const World& world = run.Host->World().Entities();
    EXPECT_TRUE(world.HasResource<ItemRegistry>());
    EXPECT_TRUE(world.HasResource<EquipmentLayoutRegistry>());
    EXPECT_TRUE(run.Host->Schedule().Has<InventorySystem>());
    EXPECT_TRUE(run.Host->Schedule().Has<EquipmentSystem>());
    EXPECT_FALSE(run.Probe->Give.empty());
    EXPECT_EQ(run.Probe->Give.find("no implementation"), std::string::npos) << run.Probe->Give;
    EXPECT_FALSE(run.Probe->Equip.empty());
    EXPECT_EQ(run.Probe->Equip.find("no implementation"), std::string::npos) << run.Probe->Equip;
}

TEST(KitInstallation, AVocabularyWithoutItsSystemsRefusesToStart)
{
    KitRun run(Kits::InventoryVocabularyOnly);

    EXPECT_EQ(run.Exit, 1);
    EXPECT_EQ(run.Probe->Frames, 0);
}

TEST(KitInstallation, SystemsWithoutTheirVocabularyRefuseToStart)
{
    KitRun run(Kits::InventorySystemsOnly);

    EXPECT_EQ(run.Exit, 1);
    EXPECT_EQ(run.Probe->Frames, 0);
}

TEST(KitInstallation, AnEquipmentVocabularyWithoutEquipmentSystemsRefusesToStart)
{
    KitRun run(Kits::EquipmentVocabularyBesideInventorySystems);

    EXPECT_EQ(run.Exit, 1);
    EXPECT_EQ(run.Probe->Frames, 0);
}

TEST(KitInstallation, EquipmentDoesNotReplaceAMoveAdmissionAnotherLayerInstalled)
{
    World world;
    RegisterAbilityKit(world);
    world.SetResource(ItemMoveAdmission{ .Admit = &AdmitEverything });

    InstallEquipmentVocabulary(world);

    ASSERT_TRUE(world.HasResource<ItemMoveAdmission>());
    EXPECT_EQ(world.GetResource<ItemMoveAdmission>().Admit, &AdmitEverything);
}

TEST(KitInstallation, EquipmentBesideAnotherMoveAdmissionRefusesToStart)
{
    KitRun run(Kits::EquipmentBesideAnotherAdmission);

    EXPECT_EQ(run.Exit, 1);
    EXPECT_EQ(run.Probe->Frames, 0);
}

TEST(KitInstallation, InstallingEquipmentVocabularyInstallsInventoryVocabulary)
{
    World world;
    RegisterAbilityKit(world);

    InstallEquipmentVocabulary(world);

    EXPECT_TRUE(world.HasResource<ItemRegistry>());
    EXPECT_TRUE(world.HasResource<EquipmentLayoutRegistry>());
}
