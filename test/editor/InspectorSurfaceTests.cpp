#include <gtest/gtest.h>

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"
#include "ui/InspectorSurface.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <project/ProjectContentMount.h>
#include <ui/UiService.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// The authored inspector, driven the way a person drives it.
//
// The claim is that a registry-driven inspector works with the widgets on the
// far side of a document boundary: rows come from the component schema, an edit
// lives in the presentation copy until the document says it is finished, and a
// finished edit becomes the same undoable command the ImGui inspector commits.
//
// No window and no device, which is the other half of the claim: a workflow
// moved to authored UI is testable without the shell it used to live in.

namespace
{
#ifndef SENCHA_EDITOR_UI_DIR
#define SENCHA_EDITOR_UI_DIR "."
#endif

// The authored surface is right-anchored and full height, so these follow from
// the stylesheet: a 340px panel against the right edge, a 30px title bar, and
// 24px rows. The value control sits right of a 118px label inside 8px padding.
constexpr float kSurfaceWidth = 1280.0f;
constexpr float kSurfaceHeight = 720.0f;
constexpr float kPanelLeft = kSurfaceWidth - 340.0f;
constexpr float kRowsTop = 30.0f;
constexpr float kRowHeight = 24.0f;

[[nodiscard]] float ValueX() { return kPanelLeft + 8.0f + 118.0f + 40.0f; }
[[nodiscard]] float RowY(std::size_t row)
{
    return kRowsTop + static_cast<float>(row) * kRowHeight + kRowHeight * 0.5f;
}

class Harness
{
public:
    Harness()
        : Assets(Logging, Serializers, RuntimeAssets::ReferenceOnly{})
        , World(Logging)
        , Selection(SelectionCtx)
    {
        RegisterDocumentSerializers();
        MountEditorContent(SENCHA_EDITOR_UI_DIR, Assets, Logging, nullptr);
        Ui = std::make_unique<UiService>(Logging, Assets.Assets, Assets.UiPackages,
                                         Assets.Fonts, nullptr, nullptr);
        Surface = Ui->CreateSurface(
            "test", RenderExtent{ static_cast<std::uint32_t>(kSurfaceWidth),
                                  static_cast<std::uint32_t>(kSurfaceHeight) });
        Selection.BindDocument(&Components());
    }

    ~Harness()
    {
        Inspector.reset();
        if (Ui != nullptr)
            Ui->Shutdown();
    }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    [[nodiscard]] EditorScene& Scene() { return World.FocusDocument().GetScene(); }
    [[nodiscard]] ::World& Components() { return Scene().GetRegistry().Components; }

    void Start()
    {
        Inspector = std::make_unique<InspectorSurface>(*Ui, Surface, World,
                                                       Selection, Commands);
        Inspector->Open();
        Ui->Update();
        Frame();
    }

    // One frame: the engine updates the UI after host controllers, so the
    // controller runs first here too.
    void Frame()
    {
        Inspector->Update();
        Ui->Update();
    }

    void Select(EntityId entity)
    {
        Selection.SetSelection(
            { SelectableRef::EntitySelection(Scene().GetRegistry().Id, entity) });
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

    void PressKey(SDL_Scancode code)
    {
        SDL_Event down{};
        down.type = SDL_EVENT_KEY_DOWN;
        down.key.scancode = code;
        (void)Ui->ProcessPlatformEvent(down);
        SDL_Event up = down;
        up.type = SDL_EVENT_KEY_UP;
        (void)Ui->ProcessPlatformEvent(up);
    }

    void TypeText(const std::string& text)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = text.c_str();
        (void)Ui->ProcessPlatformEvent(event);
    }

    // Focus a row's field, clear it, type, and click away -- the whole edit as
    // somebody performs it, ending in the blur that says it is finished.
    void ReplaceRowText(std::size_t row, const std::string& text)
    {
        ClickAt(ValueX(), RowY(row));
        Frame();
        PressKey(SDL_SCANCODE_END);
        for (int i = 0; i < 64; ++i)
            PressKey(SDL_SCANCODE_BACKSPACE);
        TypeText(text);
        Frame();
        // Somewhere with nothing on it: the title bar. Blur is what commits.
        ClickAt(kPanelLeft + 170.0f, 15.0f);
        Frame();
    }

    [[nodiscard]] std::vector<UiRow> Rows() const
    {
        return Ui->GetRows(Inspector->CurrentScreen(), UiRowsIdAt(0));
    }

    [[nodiscard]] std::size_t RowIndexOf(std::string_view label) const
    {
        const std::vector<UiRow> rows = Rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].Label == label && rows[i].Editable)
                return i;
        }
        return static_cast<std::size_t>(-1);
    }

    [[nodiscard]] std::string Status() const
    {
        return std::string(
            Ui->GetValue(Inspector->CurrentScreen(),
                         UiPropertyIdAt(1)).AsString());
    }

    UiService& Service() { return *Ui; }
    InspectorSurface& Panel() { return *Inspector; }

    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets;
    WorldDocument World;
    SelectionContext SelectionCtx;
    SelectionService Selection;
    CommandStack Commands;
    std::unique_ptr<UiService> Ui;
    UiSurfaceId Surface;
    std::unique_ptr<InspectorSurface> Inspector;
};
} // namespace

TEST(InspectorSurface, WithNothingSelectedItPresentsNothing)
{
    Harness harness;
    harness.Start();
    ASSERT_TRUE(harness.Panel().IsOpen()) << "the authored inspector did not open";

    EXPECT_TRUE(harness.Rows().empty());
    EXPECT_FALSE(harness.Service()
                     .GetValue(harness.Panel().CurrentScreen(), UiPropertyIdAt(2))
                     .AsBool());
}

TEST(InspectorSurface, TheSelectedEntitysFieldsArriveAsRowsFromTheSchema)
{
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    harness.Select(entity);
    harness.Frame();

    const std::vector<UiRow> rows = harness.Rows();
    ASSERT_FALSE(rows.empty()) << "a selected entity presented no fields";

    // Every row says which component it came from, and no component is named
    // by the inspector: these are whatever the serializer registry describes.
    for (const UiRow& row : rows)
    {
        EXPECT_FALSE(row.Label.empty());
        EXPECT_FALSE(row.Detail.empty()) << "a row did not say which component it is";
    }

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1))
        << "the transform's position was not presented as an editable row";
    EXPECT_EQ(harness.Rows()[position].Value, "1, 2, 3");
}

TEST(InspectorSurface, AFinishedEditBecomesOneUndoableCommand)
{
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    harness.Select(entity);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ReplaceRowText(position, "4, 5, 6");

    EXPECT_EQ(harness.Rows()[position].Value, "4, 5, 6")
        << "the edit never reached the component";
    EXPECT_TRUE(harness.Commands.CanUndo())
        << "the edit was applied without anything to undo it with";

    harness.Commands.Undo();
    harness.Frame();
    EXPECT_EQ(harness.Rows()[position].Value, "1, 2, 3")
        << "an undo from outside the UI was not republished";
}

TEST(InspectorSurface, TextTheFieldCannotHoldIsRefusedAndTheRowGoesBack)
{
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    harness.Select(entity);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ReplaceRowText(position, "1.2.3");

    EXPECT_FALSE(harness.Commands.CanUndo())
        << "text the field cannot hold reached the component";
    EXPECT_EQ(harness.Rows()[position].Value, "1, 2, 3")
        << "the refused text was left on screen";
    EXPECT_NE(harness.Status().find("1.2.3"), std::string::npos)
        << "the refusal was silent";
}

TEST(InspectorSurface, FocusingAFieldAndLeavingItAloneIsNotAnEdit)
{
    // The rounding trap: what a field shows is a formatted number, so a commit
    // that re-parsed the display would write a slightly different value every
    // time somebody merely clicked into a row.
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(0.1f, 0.2f, 0.3f));
    harness.Select(entity);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ClickAt(ValueX(), RowY(position));
    harness.Frame();
    harness.ClickAt(kPanelLeft + 170.0f, 15.0f);
    harness.Frame();

    EXPECT_FALSE(harness.Commands.CanUndo())
        << "clicking into a field and out of it committed an edit";
}

TEST(InspectorSurface, ChangingTheSelectionMidEditDiscardsIt)
{
    // The interruption case, and the reason the edit lives in the presentation
    // copy: there is nothing to roll back.
    Harness harness;
    harness.Start();

    const EntityId first = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    const EntityId second = harness.Scene().CreateEntity(Vec3d(7.0f, 8.0f, 9.0f));
    harness.Select(first);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ClickAt(ValueX(), RowY(position));
    harness.Frame();
    harness.PressKey(SDL_SCANCODE_END);
    for (int i = 0; i < 64; ++i)
        harness.PressKey(SDL_SCANCODE_BACKSPACE);
    harness.TypeText("99, 99, 99");
    harness.Frame();

    harness.Select(second);
    harness.Frame();

    EXPECT_FALSE(harness.Commands.CanUndo())
        << "an abandoned edit was committed to the entity that was selected";
    EXPECT_EQ(harness.Rows()[position].Value, "7, 8, 9")
        << "the rows still describe the entity that is no longer selected";
}

TEST(InspectorSurface, ClosingMidEditCommitsNothing)
{
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    harness.Select(entity);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ClickAt(ValueX(), RowY(position));
    harness.Frame();
    harness.TypeText("9");
    harness.Frame();

    harness.Panel().Close();
    EXPECT_FALSE(harness.Panel().IsOpen());
    EXPECT_FALSE(harness.Commands.CanUndo())
        << "closing the inspector mid-edit committed what was being typed";
}

TEST(InspectorSurface, ARepublishDoesNotDeleteWhatIsBeingTyped)
{
    // The inspector republishes from the live component every frame, so without
    // the begin/commit transaction a half-typed number would be overwritten
    // between one keystroke and the next and nothing could ever be typed.
    Harness harness;
    harness.Start();

    const EntityId entity = harness.Scene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
    harness.Select(entity);
    harness.Frame();

    const std::size_t position = harness.RowIndexOf("Position");
    ASSERT_NE(position, static_cast<std::size_t>(-1));

    harness.ClickAt(ValueX(), RowY(position));
    harness.Frame();
    harness.PressKey(SDL_SCANCODE_END);
    for (int i = 0; i < 64; ++i)
        harness.PressKey(SDL_SCANCODE_BACKSPACE);
    harness.TypeText("4");
    harness.Frame();
    harness.Frame();
    harness.Frame();

    EXPECT_EQ(harness.Rows()[position].Value, "4")
        << "the host republished over what was being typed";
}

TEST(InspectorSurface, ThePointerOutsideThePanelStillBelongsToTheEditor)
{
    // Kyusu's input guard now folds this surface's claim into the one it hands
    // the router, so a document that reported the pointer everywhere would make
    // the whole editor unclickable for as long as the inspector is open.
    Harness harness;
    harness.Start();

    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = 400.0f;
    move.motion.y = 400.0f;
    (void)harness.Service().ProcessPlatformEvent(move);
    EXPECT_FALSE(harness.Service().Capture().Mouse)
        << "the inspector claimed the pointer over the viewport";

    move.motion.x = kPanelLeft + 100.0f;
    move.motion.y = 300.0f;
    (void)harness.Service().ProcessPlatformEvent(move);
    EXPECT_TRUE(harness.Service().Capture().Mouse)
        << "the pointer is over the inspector and nothing said so";
}
