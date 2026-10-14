#include "AbilityWorkspace.h"

#include "authoring/AbilityDocumentEdits.h"
#include "AbilityPhaseStrip.h"
#include "authoring/AbilityArguments.h"
#include "authoring/AbilityExplorers.h"
#include "authoring/AbilityOverview.h"
#include "authoring/AbilityProblemProperties.h"
#include "authoring/AbilityPalette.h"
#include "authoring/AbilityRecipes.h"
#include "authoring/AbilityStrip.h"
#include "authoring/AbilityWorkbench.h"
#include "text/PathLeaf.h"

#include "actions/ActionContext.h"
#include "actions/ActionOffer.h"
#include "data/DataCreation.h"
#include "data/DataDocument.h"
#include "data/DataDocumentSheet.h"
#include "data/DataPath.h"
#include "data/DataProperties.h"
#include "data/JsonObjectEdit.h"
#include "documents/DocumentSourceSet.h"
#include "problems/ProblemList.h"
#include "project/Project.h"
#include "workspaces/WorkspaceWindow.h"

#include <abilities/AbilityDefinitionData.h>
#include <effects/EffectDefinitionData.h>
#include <app/Engine.h>
#include <app/Game.h>
#include <app/GameContexts.h>
#include <app/RuntimeContent.h>
#include <ui/UiService.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <format>
#include <utility>

namespace
{
constexpr std::string_view kProblemSource = "ability";
constexpr std::string_view kAssetsExplorer = "ability_assets";
constexpr std::string_view kElementsExplorer = "ability_elements";
constexpr std::string_view kPaletteExplorer = "ability_palette";
} // namespace

ActionDefinition NewAbilityAction()
{
    return { .Id = "ability.new", .Label = "New Ability", .Category = "File",
             .DefaultChords = { { SDLK_N, { .Ctrl = true } } } };
}

AbilityWorkspace::AbilityWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module,
                                   DocumentSourceSet& sources, DataDocumentStore& store, ProjectFileSteps* files,
                                   PromptQueue& prompts, ActionCatalog& catalog, ProblemList& problems,
                                   const DataSubtypeAuthoringSetup& authorings)
    : Sources(sources)
    , Problems(problems)
    , Prompts(prompts.MakeAsker())
{
    std::function<void(VocabularyCatalog&)> vocabulary;
    if (module != nullptr)
        vocabulary = [module](VocabularyCatalog& names) { names.InstallModuleVocabulary(*module); };
    std::vector<std::filesystem::path> roots;
    for (const std::string& root : project.ContentRoots)
        roots.emplace_back(root);
    Workbench = std::make_unique<AbilityWorkbench>(engine.Content().Assets(), store, std::move(roots),
                                                   std::move(vocabulary));
    Sheet = std::make_unique<DataDocumentSheet>(Workbench->Documents, prompts, authorings, files);
    if (authorings)
        authorings(Authoring);
    ElementFields = std::make_unique<DataProperties>(
        Workbench->Documents, [this] { return Workbench->Ability(); }, &Authoring,
        [this] {
            const std::optional<AbilityElement> element = Workbench->SelectedElement();
            return element ? element->Path : std::string{};
        });
    Creation = std::make_unique<DataCreation>(
        [] {
            std::vector<std::string> names;
            for (const AbilityRecipe& recipe : AbilityRecipes())
                names.emplace_back(recipe.Name);
            return names;
        },
        [this](const std::string& recipe, const std::string& path, std::string& error) {
            const AbilityRecipe* chosen = FindAbilityRecipe(recipe);
            const JsonValue data = chosen != nullptr ? chosen->Make() : JsonValue(JsonValue::Object{});
            if (Workbench->Documents.Create(kAbilityDefinitionType, path, error, &data) == nullptr)
                return false;
            Workbench->RefreshAssets();
            Workbench->Select({});
            Subject = CardSubject::Document;
            return true;
        });

    Arguments = std::make_unique<AbilityArgumentProperties>(
        *Workbench, [this](std::string argument) { CreateEffect(std::move(argument)); });
    ElementProblems = std::make_unique<AbilityProblemProperties>(*Workbench);
    Overview = std::make_unique<AbilityOverviewProperties>(*Workbench);
    Palette = AbilityPaletteOf(Workbench->Names());
    if (UiService* ui = engine.TryUi(); ui != nullptr && ui->IsReady())
        Strip = std::make_unique<AbilityPhaseStrip>(*ui);

    DefineActions(catalog);
    DefineExplorers(catalog);
}

AbilityWorkspace::~AbilityWorkspace()
{
    Sources.CancelEdits();
    Strip.reset();
    Problems.Clear(kProblemSource);
}

void AbilityWorkspace::DefineActions(ActionCatalog& catalog)
{
    const auto ability = [this] { return Workbench->Ability() != nullptr; };
    const auto selected = [this] { return !Workbench->Selected().empty(); };
    OfferAction(catalog, ActionHandlers, NewAbilityAction(), { .Invoke = [this] {
                    Creation->Clear();
                    Subject = CardSubject::Creating;
                } });
    OfferAction(catalog, ActionHandlers, SaveAction(), { .Invoke = [this] {
                    if (const std::optional<DocumentRef> active = Sheet->ActiveRef())
                        (void)Sources.Save(*active);
                } });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ability.close", .Label = "Close", .Category = "File",
                  .DefaultChords = { { SDLK_W, { .Ctrl = true } } } },
                { .Invoke = [this] { Sheet->CloseActive(); },
                  .Enabled = [this] { return Workbench->Documents.Active() != nullptr; } });
    OfferAction(catalog, ActionHandlers, StepBackAction(), { .Invoke = [this] {
                    Sources.CancelEdits();
                    Workbench->Select({});
                    Subject = CardSubject::Document;
                } });
    OfferAction(catalog, ActionHandlers, { .Id = "ability.phase.add", .Label = "Add Phase", .Category = "Edit" },
                { .Invoke = [this] { AddPhase(); }, .Enabled = ability });
    OfferAction(catalog, ActionHandlers, DuplicateAction(), { .Invoke = [this] {
                    std::string key;
                    const std::string original = Workbench->Selected();
                    if (Workbench->Edit([&](JsonValue& data) {
                            key = DuplicateAbilityElement(data, original);
                            return !key.empty();
                        }))
                        Workbench->Select(key);
                },
                .Enabled = selected });
    OfferAction(catalog, ActionHandlers, DeleteAction(),
                { .Invoke = [this] { DeleteSelected(); }, .Enabled = selected });
    OfferAction(catalog, ActionHandlers,
                { .Id = "ability.refresh_names", .Label = "Check Against Current Content", .Category = "Resource" },
                { .Invoke = [this] {
                    Workbench->RefreshNames();
                    Workbench->RefreshAssets();
                    Palette = AbilityPaletteOf(Workbench->Names());
                } });
}

void AbilityWorkspace::AddPhase()
{
    std::string key;
    const std::optional<AbilityElement> element = Workbench->SelectedElement();
    const std::string after = element ? (element->Phase.empty() ? element->Key : element->Phase) : std::string{};
    if (Workbench->Edit([&](JsonValue& data) {
            key = AddAbilityPhase(data, "Phase", after);
            return !key.empty();
        }))
        Workbench->Select(key);
}

void AbilityWorkspace::InsertOp(std::string_view entry)
{
    const JsonValue* data = Workbench->AbilityData();
    JsonValue op = AbilityPaletteOp(entry);
    if (data == nullptr || op.IsNull())
        return;
    const std::optional<AbilityOpPlacement> placement =
        AbilityOpPlacementFor(*data, InsertSlot, Workbench->Selected(), op.Find("lease") != nullptr);
    InsertSlot.clear();
    if (!placement)
        return;
    std::string key;
    if (Workbench->Edit([&](JsonValue& edited) {
            key = InsertAbilityOp(edited, placement->Phase, placement->List, placement->Index, std::move(op));
            return !key.empty();
        }))
        Workbench->Select(key);
}

void AbilityWorkspace::DefineExplorers(ActionCatalog& catalog)
{
    ExplorerRows.Offer(catalog, ActionHandlers,
                       { .Id = std::string(kAssetsExplorer), .Title = "Abilities", .Icon = IconId::Folder,
                         .Chords = { { SDLK_A, {} } },
                         .Explore =
                             [this](const ExplorerQuery& query) {
                                 const DataDocument* open = Workbench->Ability();
                                 ExplorerPage page = AssetOutline.Explore(
                                     AbilityAssetNodes(Workbench->Abilities(),
                                                       open != nullptr ? open->VirtualPath() : std::string_view{}),
                                     query);
                                 page.Actions = { NewAbilityAction().Id, "ability.refresh_names" };
                                 page.Empty = "No ability's name holds that.";
                                 return page;
                             },
                         .Choose = [this](std::string_view key, ExplorerChoice,
                                          std::size_t) { (void)OpenDocument(std::string(key)); } });
    ExplorerRows.Offer(catalog, ActionHandlers,
                       { .Id = std::string(kElementsExplorer), .Title = "Elements", .Chords = { { SDLK_E, {} } },
                         .Explore =
                             [this](const ExplorerQuery& query) {
                                 const JsonValue* data = Workbench->AbilityData();
                                 ExplorerPage page = ElementOutline.Explore(
                                     data != nullptr ? AbilityElementNodes(*data, Workbench->Selected())
                                                     : std::vector<OutlineNode>{},
                                     query);
                                 page.Actions = { "ability.phase.add" };
                                 page.Empty = data != nullptr ? "No element's name holds that." : "No ability is open.";
                                 return page;
                             },
                         .Choose = [this](std::string_view key, ExplorerChoice, std::size_t) {
                             if (key.starts_with(kAbilityGroupKey))
                             {
                                 ElementOutline.Flip(key);
                                 return;
                             }
                             ShowAbility();
                             Workbench->Select(std::string(key));
                         } });
    ExplorerRows.Offer(catalog, ActionHandlers,
                       { .Id = std::string(kPaletteExplorer), .Title = "Operations", .Chords = { { SDLK_O, {} } },
                         .Explore =
                             [this](const ExplorerQuery& query) {
                                 ExplorerPage page = PaletteOutline.Explore(AbilityPaletteNodes(Palette), query);
                                 page.Empty = "No verb, lease or query's name holds that.";
                                 return page;
                             },
                         .Choose = [this](std::string_view key, ExplorerChoice, std::size_t) {
                             if (key.starts_with(kAbilityGroupKey))
                                 PaletteOutline.Flip(key);
                             else
                                 InsertOp(key);
                         } });
}

void AbilityWorkspace::DeleteSelected()
{
    const std::optional<AbilityElement> element = Workbench->SelectedElement();
    const JsonValue* data = Workbench->AbilityData();
    if (!element || data == nullptr)
        return;
    const auto remove = [this, key = element->Key, phase = element->Kind == AbilityElementKind::Phase] {
        if (Workbench->Edit([&](JsonValue& edited) {
                return phase ? RemoveAbilityPhase(edited, key) : RemoveAbilityElement(edited, key);
            }))
            Workbench->Select({});
    };
    const std::size_t into = element->Kind == AbilityElementKind::Phase ? AbilityTransitionsInto(*data, element->Key) : 0;
    if (into == 0)
    {
        remove();
        return;
    }
    Prompts.Ask({ .Title = std::format("Delete {}", AbilityElementLabel(*data, *element)),
                  .Message = std::format("{} transition{} go{} to this phase; they will end the ability instead.", into,
                                         into == 1 ? "" : "s", into == 1 ? "es" : ""),
                  .Choices = { { .Key = "delete", .Label = "Delete", .Tone = ActionTone::Destructive },
                               { .Key = "cancel", .Label = "Keep" } },
                  .Answer = [remove](std::string_view answer, std::string_view) {
                      if (answer == "delete")
                          remove();
                  } });
}

void AbilityWorkspace::PublishProblems()
{
    std::vector<Problem> problems = Workbench->Problems();
    for (Problem& problem : Sheet->Problems())
        problems.push_back(std::move(problem));
    std::string signature;
    for (const Problem& problem : problems)
        signature += problem.Message + problem.Location + "\n";
    if (signature == ProblemSignature)
        return;
    ProblemSignature = std::move(signature);
    Problems.Publish(kProblemSource, std::move(problems));
}

void AbilityWorkspace::Tick(FrameUpdateContext&)
{
    (void)Workbench->Documents.Store().PushWaiting();
    PublishProblems();
    if (!Visible)
        return;
    Sheet->Update();
    ShowStrip();
}

void AbilityWorkspace::ShowStrip()
{
    if (Strip == nullptr)
        return;
    const JsonValue* data = Workbench->AbilityData();
    const AbilityStripRows rows = data != nullptr ? AbilityStripOf(*data, Workbench->Diagnostics(), Workbench->Selected())
                                                  : AbilityStripRows{};
    for (const AbilityStripRequest& request :
         Strip->Update(ContentRect, rows, "Open an ability from the Abilities explorer (A), or make one with Ctrl+N."))
    {
        switch (request.Ask)
        {
        case AbilityStripAsk::Pick:
            ShowAbility();
            Workbench->Select(request.Key);
            break;
        case AbilityStripAsk::Carry:
            Carried = request.Key;
            break;
        case AbilityStripAsk::Drop:
            // The innermost target is told first; the rest of the drop is for what it already took.
            if (!Carried.empty())
                (void)Workbench->Edit([&](JsonValue& edited) { return DropAbilityElement(edited, Carried, request.Key); });
            Carried.clear();
            break;
        case AbilityStripAsk::Put:
            Carried.clear();
            break;
        case AbilityStripAsk::Add:
            InsertSlot = request.Key;
            if (WindowActions != nullptr)
                (void)WindowActions->Invoke(ExplorerTable::ActionOf(kPaletteExplorer));
            break;
        case AbilityStripAsk::AddPhase:
            AddPhase();
            break;
        }
    }
}

void AbilityWorkspace::Interrupt()
{
    Carried.clear();
    Sources.CancelEdits();
}

void AbilityWorkspace::SetVisible(bool visible)
{
    Visible = visible;
    if (Strip != nullptr)
        Strip->SetVisible(visible);
    if (!visible)
        Interrupt();
}

void AbilityWorkspace::MoveTo(const WorkspaceWindow& window)
{
    WindowActions = window.Actions;
    if (Strip != nullptr)
        Strip->MoveTo(window.Interactive);
}

void AbilityWorkspace::HandlePlatformEvent(PlatformEventContext& ctx)
{
    if (ctx.Event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        Interrupt();
}

bool AbilityWorkspace::ClaimPlatformEvent(PlatformEventContext& ctx)
{
    return Sheet->ClaimPlatformEvent(ctx.Event);
}

bool AbilityWorkspace::OwnsDocument(const DocumentRef& document) const
{
    return Sheet->Owns(document);
}

void AbilityWorkspace::RevealDocument(const DocumentRef& document)
{
    ShowDocument(document.Key);
}

WorkspaceDocumentFacet AbilityWorkspace::Document() const
{
    WorkspaceDocumentFacet facet;
    Sheet->AppendOpen(facet);
    if (const std::optional<WorkspaceOpenDocument> active = Sheet->ActiveEntry())
    {
        facet.Label = active->Label;
        facet.Changed = active->Changed;
        facet.Active = active->Key;
    }
    return facet;
}

void AbilityWorkspace::ShowDocument(std::string_view key)
{
    if (!Sheet->Show(key))
        return;
    Subject = CardSubject::Document;
    Workbench->FollowActive();
}

void AbilityWorkspace::ShowAbility()
{
    if (const DataDocument* ability = Workbench->Ability(); ability != nullptr && !Workbench->ShowingAbility())
        (void)Sheet->Show(ability->VirtualPath());
    Subject = CardSubject::Document;
}

void AbilityWorkspace::CreateEffect(std::string argument)
{
    const DataDocument* ability = Workbench->Ability();
    const std::string op = Workbench->Selected();
    if (ability == nullptr || op.empty())
        return;
    std::string leaf(PathLeaf(ability->VirtualPath()));
    leaf = leaf.substr(0, leaf.find('.'));
    Prompts.Ask({ .Title = "New effect",
                  .Message = "Where the new effect goes, relative to the content root.",
                  .Choices = { { .Key = "create", .Label = "Create" }, { .Key = "cancel", .Label = "Cancel" } },
                  .Field = std::format("effects/{}_{}.sdata", leaf, argument),
                  .Answer = [this, op, argument](std::string_view answer, std::string_view written) {
                      if (answer != "create")
                          return;
                      const JsonValue starter(JsonValue::Object{ { "duration", JsonValue("instant") },
                                                                 { "modifiers", JsonValue(JsonValue::Array{}) } });
                      std::string error;
                      const DataDocument* effect =
                          Workbench->Documents.Create(kEffectDefinitionType, written, error, &starter);
                      if (effect == nullptr)
                      {
                          Sheet->SetError(std::move(error));
                          return;
                      }
                      const std::string path = effect->VirtualPath();
                      (void)Workbench->Edit([&](JsonValue& data) {
                          const std::optional<AbilityElement> element = FindAbilityElement(data, op);
                          JsonValue* held = element ? FindDataValue(data, element->Path) : nullptr;
                          if (held == nullptr)
                              return false;
                          if (FindMember(*held, "arguments") == nullptr)
                              SetMember(*held, "arguments", JsonValue(JsonValue::Object{}));
                          SetMember(*FindMember(*held, "arguments"), argument,
                                    JsonValue(JsonValue::Object{ { "data", JsonValue(path) } }));
                          return true;
                      });
                      Workbench->RefreshNames();
                      Workbench->RefreshAssets();
                      Subject = CardSubject::Document;
                  } });
}

bool AbilityWorkspace::OpenAsset(const ProjectFile& file)
{
    return OpenDocument(file.AssetPath);
}

bool AbilityWorkspace::OpenDocument(const std::string& path)
{
    if (!Sheet->Open(path))
        return false;
    Workbench->FollowActive();
    Subject = CardSubject::Document;
    return true;
}

bool AbilityWorkspace::RevealProblem(const Problem& problem)
{
    if (!problem.Document || !OwnsDocument(*problem.Document))
        return false;
    ShowDocument(problem.Document->Key);
    if (Workbench->Ability() != nullptr)
        Workbench->Reveal(problem.Location);
    return true;
}

std::vector<WorkspaceResourceFacet> AbilityWorkspace::Resources()
{
    const DataDocument* open = Workbench->Ability();
    return { { .Role = "Ability", .Primary = true, .Explorer = std::string(kAssetsExplorer),
               .Active = { .Label = open != nullptr ? std::string(PathLeaf(open->VirtualPath())) : std::string{} } } };
}

WorkspaceSelectionFacet AbilityWorkspace::Selection()
{
    if (Subject == CardSubject::Creating)
        return { .Title = "New Ability", .Sections = { { .Heading = {}, .Source = Creation.get() } } };
    const DataDocument* document = Workbench->Documents.Active();
    if (document == nullptr)
        return {};
    if (!Workbench->ShowingAbility())
        return { .Title = std::string(PathLeaf(document->VirtualPath())),
                 .Actions = { "file.save", "ability.close" },
                 .Sections = Sheet->Sections(),
                 .Primary = true };
    const JsonValue* data = Workbench->AbilityData();
    if (const std::optional<AbilityElement> element = Workbench->SelectedElement(); element && data != nullptr)
        return { .Title = AbilityElementLabel(*data, *element),
                 .Subtitle = std::string(PathLeaf(document->VirtualPath())),
                 .Actions = { DuplicateAction().Id, DeleteAction().Id },
                 .Sections = { { .Heading = {}, .Source = ElementFields.get() },
                               { .Heading = {}, .Source = Arguments.get() },
                               { .Heading = "Problems", .Source = ElementProblems.get() } },
                 .Focus = Workbench->Focus(),
                 .Primary = true };
    std::vector<PropertySection> sections{ { .Heading = "Overview", .Source = Overview.get() } };
    for (PropertySection& section : Sheet->Sections())
        sections.push_back(section);
    return { .Title = std::string(PathLeaf(document->VirtualPath())),
             .Actions = { "file.save", "ability.phase.add", "ability.close" },
             .Sections = std::move(sections),
             .Focus = Workbench->Focus(),
             .Primary = true };
}
