#include <gtest/gtest.h>

#include <core/console/CVarArchive.h>
#include <core/console/ConsoleRegistry.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    // One temporary directory per test, removed with it, so nothing a test
    // writes can reach another one or the developer's real settings.
    class ArchiveFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Dir = std::filesystem::temp_directory_path()
                / ("sencha_cvar_archive_" + std::to_string(::testing::UnitTest::GetInstance()
                                                               ->random_seed())
                   + "_"
                   + ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::remove_all(Dir);
            std::filesystem::create_directories(Dir);
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(Dir, ec);
        }

        [[nodiscard]] std::filesystem::path FilePath() const { return Dir / "settings.json"; }

        [[nodiscard]] std::string ReadFile() const
        {
            std::ifstream in(FilePath(), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }

        void WriteFile(std::string_view text) const
        {
            std::ofstream out(FilePath(), std::ios::binary | std::ios::trunc);
            out << text;
        }

        std::filesystem::path Dir;
    };

    void RegisterArchived(ConsoleRegistry& registry,
                          std::string name,
                          CVarType type,
                          CVarValue value)
    {
        ASSERT_TRUE(registry.RegisterCVar({
            .Name = std::move(name),
            .Owner = "test",
            .Type = type,
            .DefaultValue = value,
            .CurrentValue = value,
            .Flags = CVarFlags::Archive,
        }));
    }
}

TEST_F(ArchiveFixture, AChangedSettingComesBackAfterAReload)
{
    ConsoleRegistry saving;
    RegisterArchived(saving, "audio.volume", CVarType::Double, 1.0);
    ASSERT_TRUE(saving.SetCVar("audio.volume", CVarValue{ 0.25 }, { "test" },
                               ConsolePhase::EngineReady).Succeeded());

    CVarArchive out(FilePath());
    EXPECT_TRUE(out.Save(saving));

    ConsoleRegistry loading;
    RegisterArchived(loading, "audio.volume", CVarType::Double, 1.0);
    CVarArchive in(FilePath());
    in.Load(loading, ConsolePhase::EngineReady);

    EXPECT_TRUE(in.LoadDiagnostics().empty());
    EXPECT_DOUBLE_EQ(std::get<double>(loading.FindCVar("audio.volume")->CurrentValue), 0.25);
}

TEST_F(ArchiveFixture, AStringWithQuotesAndSpacesSurvivesTheRoundTrip)
{
    // The reason the archive is JSON rather than console-script lines: this
    // value has to come back byte-identical without the store owning any
    // escaping rules of its own.
    const std::string awkward = R"(a "quoted" name\with \ backslashes	and a tab)";

    ConsoleRegistry saving;
    RegisterArchived(saving, "player.name", CVarType::String, std::string{});
    ASSERT_TRUE(saving.SetCVar("player.name", CVarValue{ awkward }, { "test" },
                               ConsolePhase::EngineReady).Succeeded());

    CVarArchive out(FilePath());
    ASSERT_TRUE(out.Save(saving));

    ConsoleRegistry loading;
    RegisterArchived(loading, "player.name", CVarType::String, std::string{});
    CVarArchive in(FilePath());
    in.Load(loading, ConsolePhase::EngineReady);

    EXPECT_EQ(std::get<std::string>(loading.FindCVar("player.name")->CurrentValue), awkward);
}

TEST_F(ArchiveFixture, ADoubleComesBackAsTheNumberThatWasSavedRatherThanARoundedOne)
{
    ConsoleRegistry saving;
    RegisterArchived(saving, "input.look_sensitivity", CVarType::Double, 1.0);
    const double precise = 0.123456789012345;
    ASSERT_TRUE(saving.SetCVar("input.look_sensitivity", CVarValue{ precise }, { "test" },
                               ConsolePhase::EngineReady).Succeeded());

    CVarArchive out(FilePath());
    ASSERT_TRUE(out.Save(saving));

    ConsoleRegistry loading;
    RegisterArchived(loading, "input.look_sensitivity", CVarType::Double, 1.0);
    CVarArchive in(FilePath());
    in.Load(loading, ConsolePhase::EngineReady);

    EXPECT_DOUBLE_EQ(std::get<double>(loading.FindCVar("input.look_sensitivity")->CurrentValue),
                     precise);
}

TEST_F(ArchiveFixture, ATransientCVarIsNotASetting)
{
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCVar({
        .Name = "time.timescale",
        .Owner = "engine",
        .Type = CVarType::Double,
        .DefaultValue = 1.0,
        .CurrentValue = 1.0,
        .Flags = CVarFlags::Transient,
    }));
    ASSERT_TRUE(registry.SetCVar("time.timescale", CVarValue{ 0.0 }, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());

    CVarArchive archive(FilePath());
    // Nothing archived changed, so there is nothing to write at all.
    EXPECT_FALSE(archive.Save(registry));
    EXPECT_FALSE(std::filesystem::exists(FilePath()));
}

TEST_F(ArchiveFixture, AValueLeftAtItsDefaultIsNotWritten)
{
    ConsoleRegistry registry;
    RegisterArchived(registry, "render.sky.enabled", CVarType::Bool, true);
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);
    ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ 0.5 }, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());

    CVarArchive archive(FilePath());
    ASSERT_TRUE(archive.Save(registry));

    const std::string text = ReadFile();
    EXPECT_NE(text.find("audio.volume"), std::string::npos);
    // Baking today's default into the file would stop a future engine changing
    // one for a player who never touched it.
    EXPECT_EQ(text.find("render.sky.enabled"), std::string::npos);
}

TEST_F(ArchiveFixture, NothingChangedMeansNoWriteAtAll)
{
    ConsoleRegistry registry;
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);
    ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ 0.5 }, { "test" },
                                 ConsolePhase::EngineReady).Succeeded());

    CVarArchive archive(FilePath());
    ASSERT_TRUE(archive.Save(registry));
    EXPECT_FALSE(archive.IsDirty(registry));
    EXPECT_FALSE(archive.Save(registry));
}

TEST_F(ArchiveFixture, ASliderDraggedManyTimesCostsOneWrite)
{
    // The reason Save is called at commit boundaries rather than per frame:
    // a hundred intermediate values are one setting, not a hundred writes.
    ConsoleRegistry registry;
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);
    for (int i = 0; i < 100; ++i)
    {
        ASSERT_TRUE(registry.SetCVar("audio.volume", CVarValue{ i / 100.0 }, { "test" },
                                     ConsolePhase::EngineReady).Succeeded());
    }

    CVarArchive archive(FilePath());
    EXPECT_TRUE(archive.Save(registry));
    EXPECT_FALSE(archive.Save(registry));
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("audio.volume")->CurrentValue), 0.99);
}

TEST_F(ArchiveFixture, ANameNoCVarClaimsYetIsQueuedAndAppliedWhenItRegisters)
{
    WriteFile(R"({ "cvars": { "game.difficulty": 3 } })");

    ConsoleRegistry registry;
    CVarArchive archive(FilePath());
    archive.Load(registry, ConsolePhase::EngineReady);
    EXPECT_TRUE(archive.LoadDiagnostics().empty());

    // The module registers late, exactly as a game module's cvar does.
    RegisterArchived(registry, "game.difficulty", CVarType::Int, std::int64_t{ 1 });
    EXPECT_EQ(std::get<std::int64_t>(registry.FindCVar("game.difficulty")->CurrentValue), 3);
}

TEST_F(ArchiveFixture, AnEntryOfTheWrongShapeCostsItselfAndNotTheRestOfTheFile)
{
    WriteFile(R"({ "cvars": { "audio.volume": "loud", "render.sky.enabled": false } })");

    ConsoleRegistry registry;
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);
    RegisterArchived(registry, "render.sky.enabled", CVarType::Bool, true);

    CVarArchive archive(FilePath());
    archive.Load(registry, ConsolePhase::EngineReady);

    ASSERT_EQ(archive.LoadDiagnostics().size(), 1u);
    EXPECT_NE(archive.LoadDiagnostics()[0].find("audio.volume"), std::string::npos);
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("audio.volume")->CurrentValue), 1.0);
    EXPECT_FALSE(std::get<bool>(registry.FindCVar("render.sky.enabled")->CurrentValue));
}

TEST_F(ArchiveFixture, AnArchiveEntryCannotInvokeACommand)
{
    // The settings file lives in a user-writable directory, so its format is a
    // record of assignments and there is no path by which an entry runs a
    // console command. A name matching one is an unresolved assignment.
    int invocations = 0;
    ConsoleRegistry registry;
    ASSERT_TRUE(registry.RegisterCommand({
        .Name = "quit",
        .Owner = "test",
        .Callback = [&invocations](ConsoleExecutionContext&, std::span<const std::string>) {
            ++invocations;
            return ConsoleResult{};
        },
    }));

    WriteFile(R"({ "cvars": { "quit": true } })");

    CVarArchive archive(FilePath());
    archive.Load(registry, ConsolePhase::EngineReady);

    EXPECT_EQ(invocations, 0);
}

TEST_F(ArchiveFixture, AMissingFileIsAFirstRunRatherThanAFailure)
{
    ConsoleRegistry registry;
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);

    CVarArchive archive(FilePath());
    archive.Load(registry, ConsolePhase::EngineReady);

    EXPECT_TRUE(archive.LoadDiagnostics().empty());
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("audio.volume")->CurrentValue), 1.0);
    EXPECT_FALSE(archive.IsDirty(registry));
}

TEST_F(ArchiveFixture, AnUnreadableFileReportsAndLeavesTheDefaultsAlone)
{
    WriteFile("{ this is not json");

    ConsoleRegistry registry;
    RegisterArchived(registry, "audio.volume", CVarType::Double, 1.0);

    CVarArchive archive(FilePath());
    archive.Load(registry, ConsolePhase::EngineReady);

    EXPECT_FALSE(archive.LoadDiagnostics().empty());
    EXPECT_DOUBLE_EQ(std::get<double>(registry.FindCVar("audio.volume")->CurrentValue), 1.0);
}

TEST_F(ArchiveFixture, LoadingWhatWasJustSavedDoesNotMakeTheStoreDirty)
{
    ConsoleRegistry saving;
    RegisterArchived(saving, "audio.volume", CVarType::Double, 1.0);
    ASSERT_TRUE(saving.SetCVar("audio.volume", CVarValue{ 0.25 }, { "test" },
                               ConsolePhase::EngineReady).Succeeded());
    CVarArchive out(FilePath());
    ASSERT_TRUE(out.Save(saving));

    ConsoleRegistry loading;
    RegisterArchived(loading, "audio.volume", CVarType::Double, 1.0);
    CVarArchive in(FilePath());
    in.Load(loading, ConsolePhase::EngineReady);

    // Applying the file moved the registry's counter; the store has to account
    // for that or every run would rewrite the file it just read.
    EXPECT_FALSE(in.IsDirty(loading));
}

// The file an application's settings live in is named by the application, and
// that is the whole of the rule: a slug of the display name under the root.
TEST(CVarArchiveFileFor, ADisplayNameBecomesOnePathComponent)
{
    EXPECT_EQ(CVarArchive::FileFor("/cfg", "Sencha FPS Template"),
              std::filesystem::path("/cfg/sencha-fps-template/settings.json"));
    EXPECT_EQ(CVarArchive::FileFor("/cfg", "  Weird -- Name!! "),
              std::filesystem::path("/cfg/weird-name/settings.json"));
}

TEST(CVarArchiveFileFor, DifferentlySluggedNamesGetDifferentFiles)
{
    EXPECT_NE(CVarArchive::FileFor("/cfg", "Sencha FPS Template"),
              CVarArchive::FileFor("/cfg", "Sencha Arena Template"));
}

TEST(CVarArchiveFileFor, NamesThatSlugAlikeShareAFile)
{
    // Documented behaviour, pinned so it cannot become an accident: the name
    // is a namespace, not a unique key. "My Game" and "my-game" are one game
    // as far as saved settings are concerned.
    EXPECT_EQ(CVarArchive::FileFor("/cfg", "My Game"), CVarArchive::FileFor("/cfg", "my-game"));
    EXPECT_EQ(CVarArchive::FileFor("/cfg", "MY GAME"), CVarArchive::FileFor("/cfg", "my-game"));
}

TEST(CVarArchiveFileFor, ANamelessApplicationStillGetsAFile)
{
    EXPECT_EQ(CVarArchive::FileFor("/cfg", "---"),
              std::filesystem::path("/cfg/sencha-application/settings.json"));
}
