#include "viewport/ViewArrangementSettings.h"

#include "actions/ActionOffer.h"
#include "viewport/ViewportLayout.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/CVarRead.h>

#include <SDL3/SDL_keycode.h>

#include <array>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view kArrangement = "editor.view.arrangement";
constexpr std::string_view kDividerX = "editor.view.divider_x";
constexpr std::string_view kDividerY = "editor.view.divider_y";

struct ArrangementRow
{
    const char* Id;
    const char* Label;
    SDL_Keycode Key;
    ViewportArrangementKind Kind;
};
constexpr std::array<ArrangementRow, 3> kArrangements = { {
    { "view.one", "One View", SDLK_1, ViewportArrangementKind::Single },
    { "view.two", "Two Views", SDLK_2, ViewportArrangementKind::Split },
    { "view.four", "Four Views", SDLK_4, ViewportArrangementKind::Quad },
} };

struct ProjectionRow
{
    const char* Id;
    const char* Label;
    SDL_Keycode Key;
    ViewportOrientation Orientation;
};
constexpr std::array<ProjectionRow, 4> kProjections = { {
    { "view.perspective", "Perspective View", SDLK_KP_5, ViewportOrientation::Perspective },
    { "view.top", "Top View", SDLK_KP_7, ViewportOrientation::Top },
    { "view.front", "Front View", SDLK_KP_1, ViewportOrientation::Front },
    { "view.side", "Side View", SDLK_KP_3, ViewportOrientation::Right },
} };
} // namespace

ViewArrangementSettings::ViewArrangementSettings(ConsoleRegistry& console)
    : Console(console)
{
    Console.RegisterCVar({
        .Name = std::string(kArrangement),
        .Owner = "editor",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{ 0 },
        .CurrentValue = std::int64_t{ 0 },
        .Flags = CVarFlags::Archive,
        .Help = "Level views: 0 one view, 1 two side by side, 2 four in a grid.",
        .Source = { "editor" },
        .Min = 0.0,
        .Max = 2.0,
    });
    for (const auto& [name, help] :
         { std::pair{ kDividerX, "Level views: where side-by-side views meet, as a fraction of the width." },
           std::pair{ kDividerY, "Level views: where the rows of four views meet, as a fraction of the height." } })
        Console.RegisterCVar({
            .Name = std::string(name),
            .Owner = "editor",
            .Type = CVarType::Double,
            .DefaultValue = 0.5,
            .CurrentValue = 0.5,
            .Flags = CVarFlags::Archive,
            .Help = help,
            .Source = { "editor" },
            .Min = 0.15,
            .Max = 0.85,
        });
}

ViewportArrangementKind ViewArrangementSettings::Kind() const
{
    switch (ReadCVarInt(&Console, kArrangement, 0))
    {
    case 1:  return ViewportArrangementKind::Split;
    case 2:  return ViewportArrangementKind::Quad;
    default: return ViewportArrangementKind::Single;
    }
}

void ViewArrangementSettings::SetKind(ViewportArrangementKind kind)
{
    const std::int64_t value = kind == ViewportArrangementKind::Split ? 1 : kind == ViewportArrangementKind::Quad ? 2 : 0;
    (void)Console.SetCVar(kArrangement, value, ConsoleValueSource{ .Description = "view action" },
                          ConsolePhase::GameplayStarted);
}

Vec2d ViewArrangementSettings::Divider() const
{
    return Vec2d(ReadCVarFloat(&Console, kDividerX, 0.5f), ReadCVarFloat(&Console, kDividerY, 0.5f));
}

void ViewArrangementSettings::SetDivider(Vec2d divider)
{
    const ConsoleValueSource source{ .Description = "view divider" };
    (void)Console.SetCVar(kDividerX, static_cast<double>(divider.X), source, ConsolePhase::GameplayStarted);
    (void)Console.SetCVar(kDividerY, static_cast<double>(divider.Y), source, ConsolePhase::GameplayStarted);
}

void ViewArrangementSettings::Offer(ActionCatalog& catalog, ActionContext& context, ViewportLayout& layout)
{
    const ActionOffers offer{ catalog, context, "View" };
    for (const ArrangementRow& row : kArrangements)
        offer({ .Id = row.Id, .Label = row.Label, .DefaultChords = { { row.Key, { .Alt = true } } } },
              { .Invoke = [this, kind = row.Kind] { SetKind(kind); },
                .Checked = [this, kind = row.Kind] { return Kind() == kind; } });
    for (const ProjectionRow& row : kProjections)
        offer({ .Id = row.Id, .Label = row.Label, .DefaultChords = { { row.Key, {} } } },
              { .Invoke = [&layout, orientation = row.Orientation] { layout.ActivateFacing(orientation); },
                .Checked = [&layout, orientation = row.Orientation] {
                    const EditorViewport* active = layout.Active();
                    return active != nullptr && active->Orientation == orientation;
                } });
}
