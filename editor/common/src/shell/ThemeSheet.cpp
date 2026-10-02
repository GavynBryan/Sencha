#include "shell/ThemeSheet.h"

#include "actions/ActionOffer.h"
#include "problems/ProblemList.h"
#include "ui/AuthoredThemeStyleSheet.h"
#include "ui/EditorThemeFile.h"

#include <core/console/ConsoleRegistry.h>
#include <ui/UiService.h>

#include <algorithm>
#include <variant>

namespace
{
constexpr std::string_view kThemeCVar = "editor.ui.theme";
constexpr std::string_view kProblemSource = "theme";
} // namespace

ThemeSheet::ThemeSheet(ConsoleRegistry& console, ProblemList& problems, std::filesystem::path themeDir,
                       std::string defaultTheme)
    : Console(console)
    , Problems(problems)
    , ThemeDir(std::move(themeDir))
{
    console.RegisterCVar({
        .Name = std::string(kThemeCVar),
        .Owner = "editor",
        .Type = CVarType::String,
        .DefaultValue = defaultTheme,
        .CurrentValue = defaultTheme,
        .Flags = CVarFlags::Archive,
        .Help = "Editor theme: a name in the bundled themes directory or a path to a theme JSON. Empty is the "
                "built-in palette.",
        .Source = { "editor" },
    });
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(ThemeDir, error))
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            Names.push_back(entry.path().stem().string());
    std::ranges::sort(Names);
}

void ThemeSheet::DefineActions(ActionCatalog& catalog, ActionContext& context)
{
    std::vector<std::string> names{ std::string{} };
    names.insert(names.end(), Names.begin(), Names.end());
    for (const std::string& name : names)
    {
        const std::string id = "theme.use." + (name.empty() ? std::string("builtin") : name);
        OfferAction(catalog, context,
                    { .Id = id, .Label = name.empty() ? std::string("Built-in") : name, .Category = "Theme" },
                    { .Invoke = [this, name] {
                          (void)Console.SetCVar(std::string(kThemeCVar), name,
                                                ConsoleValueSource{ .Description = "theme action" },
                                                ConsolePhase::GameplayStarted);
                      },
                      .Checked = [this, name] { return Applied == name; } });
    }
}

std::string ThemeSheet::ChosenTheme() const
{
    const CVarMetadata* var = Console.FindCVar(kThemeCVar);
    const std::string* name = var != nullptr ? std::get_if<std::string>(&var->CurrentValue) : nullptr;
    return name != nullptr ? *name : std::string{};
}

void ThemeSheet::Update(UiService* ui)
{
    if (std::string chosen = ChosenTheme(); chosen != Applied)
    {
        Apply(chosen);
        Applied = std::move(chosen);
        Published = false;
    }
    if (Published || ui == nullptr || !ui->IsReady())
        return;
    (void)ui->SetHostStyleSheet(kAuthoredThemeStyleSheetName, BuildAuthoredThemeStyleSheet());
    Published = true;
}

void ThemeSheet::Apply(const std::string& name)
{
    ResetEditorTheme();
    std::vector<Problem> problems;
    if (!name.empty())
    {
        std::filesystem::path path(name);
        std::error_code missing;
        if (!std::filesystem::exists(path, missing))
            path = ThemeDir / (name + ".json");
        std::string error;
        if (!LoadEditorTheme(path, &error) || !error.empty())
            problems.push_back({ .Severity = ProblemSeverity::Warning, .Message = error });
    }
    Problems.Publish(kProblemSource, std::move(problems));
}
