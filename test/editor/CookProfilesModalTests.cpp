#include <gtest/gtest.h>

#include "project/CookProfile.h"
#include "project/Project.h"
#include "ui/CookProfilesModal.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <project/ProjectContentMount.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

// Kyusu's first authored workflow, driven the way a person drives it.
//
// The claim being tested is the division of labour: the document presents and
// asks, and this controller decides what an ask means, validates it, changes the
// project, and saves it. Nothing here draws, which is why it runs with no window
// and no device -- and is itself the evidence that moving a workflow to authored
// UI takes logic out of the shell rather than moving it around inside one.

namespace
{
#ifndef SENCHA_EDITOR_UI_DIR
#define SENCHA_EDITOR_UI_DIR "."
#endif

class TempProject
{
public:
    TempProject()
    {
        std::random_device rd;
        Dir = std::filesystem::temp_directory_path()
            / ("sencha_cook_profiles_test_" + std::to_string(rd()));
        std::filesystem::create_directories(Dir / "assets");

        Descriptor.Directory = Dir.generic_string();
        Descriptor.ContentRoots = { (Dir / "assets").generic_string() };

        // One project-owned profile beside the built-ins, because the built-ins
        // are what the workflow must refuse to rename.
        CookProfile custom;
        custom.Id = "nightly";
        custom.Name = "Nightly";
        custom.TargetSteps = { std::string(CookStepIds::RenderMeshes),
                               std::string(CookStepIds::Collision) };
        Descriptor.CookProfiles.push_back(std::move(custom));
    }

    ~TempProject()
    {
        std::error_code ec;
        std::filesystem::remove_all(Dir, ec);
    }

    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;

    [[nodiscard]] std::filesystem::path SavedPath() const
    {
        return Dir / "project.senchaproj";
    }

    ProjectDescriptor Descriptor;

private:
    std::filesystem::path Dir;
};

// Everything the modal needs, with no window and no device.
class Harness
{
public:
    Harness()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
    {
        MountEditorContent(SENCHA_EDITOR_UI_DIR, Assets, Logging, nullptr);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
    }

    ~Harness()
    {
        Modal.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
    }

    void Start(ProjectDescriptor& project)
    {
        Modal = std::make_unique<CookProfilesModal>(*Ui, &project);
        Modal->Open();
        Ui->Update();
    }

    // One frame: the engine updates the UI after host controllers, so the
    // controller runs first here too.
    void Frame()
    {
        Modal->Update();
        Ui->Update();
    }

    void ClickAt(float x, float y)
    {
        SDL_Event move{};
        move.type = SDL_EVENT_MOUSE_MOTION;
        move.motion.x = x;
        move.motion.y = y;
        (void)Ui->ProcessPlatformEvent(move);

        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = x;
        down.button.y = y;
        (void)Ui->ProcessPlatformEvent(down);

        SDL_Event up = down;
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        (void)Ui->ProcessPlatformEvent(up);
    }

    UiService& Service() { return *Ui; }
    CookProfilesModal& Dialog() { return *Modal; }

private:
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    std::unique_ptr<UiService> Ui;
    std::unique_ptr<CookProfilesModal> Modal;
};

// Positions the document's own stylesheet fixes. Read off the authored layout:
// the dialog sits at (280,140), its list starts 12px below a 34px title bar,
// and a row is 26px plus padding.
constexpr float kListX = 400.0f;
constexpr float kNameFieldX = 700.0f;
constexpr float kNameFieldY = 227.0f;
constexpr float kFooterButtonY = 577.0f;
constexpr float kValidateButtonX = 830.0f;
constexpr float kSaveButtonX = 937.0f;
[[nodiscard]] float RowY(int index)
{
    return 140.0f + 34.0f + 12.0f + 13.0f + static_cast<float>(index) * 31.0f;
}
} // namespace

TEST(CookProfilesModal, OpensAgainstTheRealProfileSet)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);
    ASSERT_TRUE(harness.Dialog().IsOpen()) << "the authored document did not open";
}

TEST(CookProfilesModal, ClosingDiscardsTheEditRatherThanCommittingIt)
{
    // The interruption case. A name typed into the field lives in the screen's
    // presentation copy; closing takes it with nothing to roll back, because it
    // never reached the project.
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    const std::string before = project.Descriptor.CookProfiles.front().Name;
    harness.Dialog().Close();

    EXPECT_FALSE(harness.Dialog().IsOpen());
    EXPECT_EQ(project.Descriptor.CookProfiles.front().Name, before)
        << "closing the dialog changed the project";
    EXPECT_FALSE(std::filesystem::exists(project.SavedPath()))
        << "closing the dialog wrote the project file";
}

TEST(CookProfilesModal, SelectingAProfileChangesWhatTheFormShows)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    const std::vector<CookProfile> profiles =
        ResolveCookProfiles(project.Descriptor.CookProfiles);
    ASSERT_GE(profiles.size(), 2u);

    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();
    const UiModelArrayId steps = ui.FindArray(screen, "steps");
    const UiModelArrayId list = ui.FindArray(screen, "profiles");
    ASSERT_TRUE(steps.IsValid());
    ASSERT_TRUE(list.IsValid());

    EXPECT_EQ(ui.ArraySize(screen, list), profiles.size())
        << "the list is not the project's resolved profile set";
    EXPECT_EQ(ui.ArraySize(screen, steps), profiles.front().TargetSteps.size());

    // The project-owned profile is last: ResolveCookProfiles puts built-ins
    // first. Clicking its row must republish that profile's steps.
    const int last = static_cast<int>(profiles.size()) - 1;
    ASSERT_NE(profiles[last].TargetSteps.size(), profiles.front().TargetSteps.size())
        << "the fixture cannot tell the two selections apart";

    harness.ClickAt(kListX, RowY(last));
    harness.Frame();

    EXPECT_EQ(ui.ArraySize(screen, steps), profiles[last].TargetSteps.size())
        << "selecting a row did not change what the form shows";
    EXPECT_EQ(ui.GetValue(screen, ui.FindProperty(screen, "selected_name")).AsString(),
              profiles[last].Name);

    // And selecting changed nothing the project owns.
    EXPECT_EQ(project.Descriptor.CookProfiles.front().Name, "Nightly");
    EXPECT_FALSE(std::filesystem::exists(project.SavedPath()));
}

TEST(CookProfilesModal, TypingAlonePersistsNothingAndSaveIsWhatCommits)
{
    // The whole workflow, and the point of the boundary: the field's value is
    // presentation state until an explicit action tells the controller to read
    // it, validate it, and write it through the project's own save path.
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();

    const std::vector<CookProfile> profiles =
        ResolveCookProfiles(project.Descriptor.CookProfiles);
    const int last = static_cast<int>(profiles.size()) - 1;

    harness.ClickAt(kListX, RowY(last));
    harness.Frame();
    ASSERT_EQ(ui.GetValue(screen, ui.FindProperty(screen, "selected_name")).AsString(),
              "Nightly");

    // Focus the name field and type. Positions come from the authored layout.
    harness.ClickAt(kNameFieldX, kNameFieldY);
    harness.Frame();

    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "X";
    (void)ui.ProcessPlatformEvent(text);
    harness.Frame();

    const std::string typed(
        ui.GetValue(screen, ui.FindProperty(screen, "selected_name")).AsString());
    ASSERT_NE(typed, "Nightly") << "typing never reached the presentation copy";

    // Nothing has been committed. This is the assertion the whole design is for.
    EXPECT_EQ(project.Descriptor.CookProfiles.front().Name, "Nightly")
        << "typing changed the project";
    EXPECT_FALSE(std::filesystem::exists(project.SavedPath()))
        << "typing wrote the project file";

    // Save is what tells the controller to read, validate and persist.
    harness.ClickAt(kSaveButtonX, kFooterButtonY);
    harness.Frame();

    EXPECT_EQ(project.Descriptor.CookProfiles.front().Name, typed)
        << "save did not commit what the field was showing";
    EXPECT_TRUE(std::filesystem::exists(project.SavedPath()))
        << "save did not write the project through its own save path";
}

TEST(CookProfilesModal, ValidateReportsWithoutWriting)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();

    harness.ClickAt(kValidateButtonX, kFooterButtonY);
    harness.Frame();

    EXPECT_FALSE(ui.GetValue(screen, ui.FindProperty(screen, "status")).AsString().empty())
        << "validate said nothing, so a user learns nothing from pressing it";
    EXPECT_FALSE(std::filesystem::exists(project.SavedPath()))
        << "validating wrote the project file";
}

TEST(CookProfilesModal, ABuiltInProfileIsRefusedRatherThanSilentlyOverridden)
{
    // A built-in is inherited, not owned. Renaming one would mean writing a
    // project-local override, which is a different decision than the dialog is
    // offering -- so the controller refuses and says so.
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();
    const std::size_t ownedBefore = project.Descriptor.CookProfiles.size();

    // Row 0 is a built-in.
    harness.ClickAt(kListX, RowY(0));
    harness.Frame();

    harness.ClickAt(kNameFieldX, kNameFieldY);
    harness.Frame();
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "Z";
    (void)ui.ProcessPlatformEvent(text);
    harness.Frame();

    harness.ClickAt(kSaveButtonX, kFooterButtonY);
    harness.Frame();

    EXPECT_EQ(project.Descriptor.CookProfiles.size(), ownedBefore)
        << "renaming a built-in quietly created a project-local override";
    EXPECT_NE(ui.GetValue(screen, ui.FindProperty(screen, "status")).AsString().find("Built-in"),
              std::string_view::npos)
        << "the refusal was silent";
}
