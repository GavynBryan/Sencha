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
        // The host's surface, as Kyusu's is: authored screens arbitrate focus
        // within one, so every authored workflow shares the window's.
        Surface = Ui->CreateSurface("test", RenderExtent{ 1280, 720 });
    }

    ~Harness()
    {
        Modal.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
    }

    void Start(ProjectDescriptor& project)
    {
        Modal = std::make_unique<CookProfilesModal>(*Ui, project);
        Modal->MoveTo(Surface);
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
    UiSurfaceId Surface;
    std::unique_ptr<CookProfilesModal> Modal;
};

// The middle of a profile's row, from where the document laid out its list;
// a row is 26dp.
[[nodiscard]] Vec2d RowCentre(UiService& ui, UiScreenHandle screen, int index)
{
    const std::optional<UiElementBox> list = ui.MeasureElement(screen, "list");
    if (!list.has_value())
        return {};
    return Vec2d(list->X + list->Width * 0.5f, list->Y + 14.0f + static_cast<float>(index) * 26.0f);
}
} // namespace

TEST(CookProfilesModal, OpensOnTheProjectsProfiles)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);
    ASSERT_TRUE(harness.Dialog().IsOpen()) << "the authored document did not open";

    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();
    EXPECT_EQ(ui.ArraySize(screen, ui.FindArray(screen, "profiles")),
              ResolveCookProfiles(project.Descriptor.CookProfiles).size());
}

TEST(CookProfilesModal, ClickingAProfileSelectsIt)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);

    const std::vector<CookProfile> profiles = ResolveCookProfiles(project.Descriptor.CookProfiles);
    const int last = static_cast<int>(profiles.size()) - 1;
    UiService& ui = harness.Service();
    const UiScreenHandle screen = harness.Dialog().CurrentScreen();
    const Vec2d row = RowCentre(ui, screen, last);
    harness.ClickAt(row.X, row.Y);
    harness.Frame();

    EXPECT_EQ(harness.Dialog().Editor().Selected(), static_cast<std::size_t>(last));
    EXPECT_EQ(ui.GetValue(screen, ui.FindProperty(screen, "selected_index")).AsInt(), last);
    EXPECT_FALSE(ui.GetValue(screen, ui.FindProperty(screen, "builtin")).AsBool())
        << "the project's own profile reads as built in";
}

TEST(CookProfilesModal, ClosingWritesNothing)
{
    TempProject project;
    Harness harness;
    harness.Start(project.Descriptor);
    harness.Dialog().Close();
    EXPECT_FALSE(harness.Dialog().IsOpen());
    EXPECT_FALSE(std::filesystem::exists(project.SavedPath()));
}
