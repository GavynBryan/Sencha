#include "shell/Shelf.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionOffer.h"
#include "icons/IconNames.h"
#include "notices/NoticeQueue.h"
#include "project/AssetKinds.h"
#include "project/ProjectFileSteps.h"
#include "project/ReferenceIndex.h"
#include "shell/AssetTabs.h"
#include "shell/ShellWindow.h"
#include "text/PathLeaf.h"
#include "thumbnails/AssetPictures.h"
#include "ui/DocumentLengths.h"
#include "workspaces/WorkspaceHost.h"

#include <core/assets/AssetPath.h>
#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>

namespace
{
const UiModelPropertyId kHeight = UiPropertyIdAt(0);
const UiModelPropertyId kQuery = UiPropertyIdAt(1);
const UiModelPropertyId kView = UiPropertyIdAt(2);
const UiModelPropertyId kPinned = UiPropertyIdAt(3);
const UiModelPropertyId kCount = UiPropertyIdAt(4);
const UiModelPropertyId kEmpty = UiPropertyIdAt(5);
const UiModelPropertyId kPreviewName = UiPropertyIdAt(6);
const UiModelPropertyId kPreviewKind = UiPropertyIdAt(7);
const UiModelPropertyId kPreviewPath = UiPropertyIdAt(8);
const UiModelPropertyId kPreviewFacts = UiPropertyIdAt(9);
const UiModelPropertyId kPreviewIcon = UiPropertyIdAt(10);
const UiModelPropertyId kPreviewOpens = UiPropertyIdAt(11);
const UiModelPropertyId kPreviewImage = UiPropertyIdAt(12);
const UiModelPropertyId kPreviewColor = UiPropertyIdAt(13);
const UiModelPropertyId kPicture = UiPropertyIdAt(14);
const UiModelPropertyId kTile = UiPropertyIdAt(15);
const UiModelPropertyId kChoosing = UiPropertyIdAt(16);
const UiModelPropertyId kOffersNone = UiPropertyIdAt(17);
const UiModelPropertyId kCanGoBack = UiPropertyIdAt(18);
const UiModelPropertyId kOwnWindow = UiPropertyIdAt(19);

const UiModelRowsId kCrumbs = UiRowsIdAt(0);
const UiModelRowsId kChips = UiRowsIdAt(1);
const UiModelRowsId kLists = UiRowsIdAt(2);
const UiModelRowsId kFolders = UiRowsIdAt(3);
const UiModelRowsId kItems = UiRowsIdAt(4);
const UiModelRowsId kUses = UiRowsIdAt(5);
const UiModelRowsId kUsedBy = UiRowsIdAt(6);
const UiModelRowsId kMissing = UiRowsIdAt(7);

const UiActionId kGrab = UiActionIdAt(0);
const UiActionId kDrag = UiActionIdAt(1);
const UiActionId kDrop = UiActionIdAt(2);
const UiActionId kQueryAction = UiActionIdAt(3);
const UiActionId kKey = UiActionIdAt(4);
const UiActionId kCategory = UiActionIdAt(5);
const UiActionId kList = UiActionIdAt(6);
const UiActionId kFolder = UiActionIdAt(7);
const UiActionId kBranch = UiActionIdAt(8);
const UiActionId kPick = UiActionIdAt(9);
const UiActionId kOpen = UiActionIdAt(10);
const UiActionId kViewAction = UiActionIdAt(11);
const UiActionId kPin = UiActionIdAt(12);
const UiActionId kClose = UiActionIdAt(13);
const UiActionId kZoom = UiActionIdAt(14);
const UiActionId kNone = UiActionIdAt(15);
const UiActionId kFollow = UiActionIdAt(16);
const UiActionId kBack = UiActionIdAt(17);
const UiActionId kPress = UiActionIdAt(18);
const UiActionId kNew = UiActionIdAt(19);
const UiActionId kCarry = UiActionIdAt(20);
const UiActionId kInto = UiActionIdAt(21);
const UiActionId kPut = UiActionIdAt(22);
const UiActionId kWindow = UiActionIdAt(23);
const UiActionId kKeys = UiActionIdAt(24);

// How much wider a tile is than its picture, and the gap after it, in dp.
constexpr float kTileMarginDp = 24.0f;
constexpr float kTileGapDp = 8.0f;
constexpr float kSmallestPictureDp = 40.0f;
constexpr float kLargestPictureDp = 200.0f;
constexpr float kMinimumHeightDp = 180.0f;
constexpr float kMostOfTheWindow = 0.75f;
constexpr std::size_t kMostRecent = 30;
constexpr std::size_t kMostReferencesShown = 8;

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://shelf.rml";
    desc.ModelName = "shelf";
    desc.Properties = {
        UiModelProperty{ "height", UiValue(std::string("300px")) },
        UiModelProperty{ "query", UiValue(std::string{}) },
        UiModelProperty{ "view", UiValue(std::string("tiles")) },
        UiModelProperty{ "pinned", UiValue(false) },
        UiModelProperty{ "count", UiValue(std::string{}) },
        UiModelProperty{ "empty", UiValue(std::string{}) },
        UiModelProperty{ "preview_name", UiValue(std::string{}) },
        UiModelProperty{ "preview_kind", UiValue(std::string{}) },
        UiModelProperty{ "preview_path", UiValue(std::string{}) },
        UiModelProperty{ "preview_facts", UiValue(std::string{}) },
        UiModelProperty{ "preview_icon", UiValue(std::string{}) },
        UiModelProperty{ "preview_opens", UiValue(false) },
        UiModelProperty{ "preview_image", UiValue(std::string{}) },
        UiModelProperty{ "preview_color", UiValue(std::string{}) },
        UiModelProperty{ "picture", UiValue(std::string("72dp")) },
        UiModelProperty{ "tile", UiValue(std::string("96dp")) },
        UiModelProperty{ "choosing", UiValue(std::string{}) },
        UiModelProperty{ "offers_none", UiValue(false) },
        UiModelProperty{ "can_go_back", UiValue(false) },
        UiModelProperty{ "own_window", UiValue(false) },
    };
    desc.RowLists = { "crumbs", "chips", "lists", "folders", "items", "uses", "used_by", "missing" };
    desc.Actions = { "shelf_grab",   "shelf_drag",   "shelf_drop", "shelf_query", "shelf_key",
                     "shelf_category", "shelf_list", "shelf_folder", "shelf_branch", "shelf_pick",
                     "shelf_open",   "shelf_view",   "shelf_pin",  "shelf_close", "shelf_zoom", "shelf_none",
                     "shelf_follow", "shelf_back", "shelf_press", "shelf_new",
                     "shelf_carry", "shelf_into", "shelf_put", "shelf_window", "shelf_keys" };
    return desc;
}

std::string Text(const UiAction& action, std::size_t index = 0)
{
    return std::string(action.Arg(index).AsString());
}

double Number(const UiAction& action, std::size_t index = 0)
{
    return action.Arg(index).AsFloat();
}

// A switch: a bool, or a number that is not zero.
bool Flag(const UiAction& action, std::size_t index)
{
    const UiValue& value = action.Arg(index);
    return value.Kind() == UiValueKind::Bool ? value.AsBool() : value.AsFloat() != 0.0;
}

std::string SizeText(std::uintmax_t bytes)
{
    if (bytes < 1024)
        return std::format("{} B", bytes);
    if (bytes < 1024 * 1024)
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

std::string AgeText(std::filesystem::file_time_type modified)
{
    const auto age = std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now()
                                                                      - modified);
    const long long minutes = age.count();
    if (minutes < 1)
        return "changed just now";
    if (minutes < 60)
        return std::format("changed {} min ago", minutes);
    if (minutes < 60 * 24)
        return std::format("changed {} h ago", minutes / 60);
    return std::format("changed {} days ago", minutes / (60 * 24));
}

} // namespace

bool SearchFieldKeeps(const KeyChord& chord, bool empty)
{
    const SDL_Keycode key = chord.Key;
    if ((key >= SDLK_F1 && key <= SDLK_F12) || chord.Mods.Alt)
        return false;
    if (!chord.Mods.Ctrl)
        return !(key == SDLK_DELETE && empty);
    switch (key)
    {
    case SDLK_A:
    case SDLK_C:
    case SDLK_X:
    case SDLK_V:
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_HOME:
    case SDLK_END:
    case SDLK_BACKSPACE:
    case SDLK_DELETE: return true;
    default:          return false;
    }
}

std::string RenamedPath(std::string_view from, std::string_view written)
{
    const std::size_t first = written.find_first_not_of(" /");
    if (first == std::string_view::npos)
        return {};
    const std::size_t last = written.find_last_not_of(" /");
    return AssetPathOf(written.substr(first, last - first + 1))
           + std::filesystem::path(from).extension().string();
}


Shelf::Shelf(const ShellWindowServices& services, UiSurfaceId surface)
    : Ui(services.Ui)
    , Surface(surface)
    , Workspaces(services.Workspaces)
    , Presentation(services.Presentation)
    , Sources(services.Content)
    , CloseWindow(services.CloseWindow)
    , Catalog(services.Catalog)
    , Actions(services.Actions)
    , Notices(services.Notices)
    , FileWheelModel(services.Catalog, FileActions, { "asset.open", "edit.rename", "edit.duplicate", "edit.delete" })
    , NewWheelModel(services.Workspaces, [this] {
        // What is being made is the work now; a raised shelf goes, as it does
        // once something is opened.
        if (State == Placement::Raised)
            Hide();
    })
    , Asker(services.Prompts.MakeAsker())
{
    OfferFileActions();
}

Shelf::~Shelf()
{
    if (Screen.IsValid())
        Ui.CloseScreen(Screen);
}

void Shelf::Show(std::string category)
{
    Rescan();
    if (!category.empty())
        Query.Category = std::move(category);
    if (State == Placement::Down)
        State = Placement::Raised;
    // Opened as it rises, so its search field takes the keys.
    if (!Screen.IsValid() && Surface.IsValid())
        Screen = Ui.OpenScreen(Surface, Describe());
}

void Shelf::Fill(float height)
{
    if (State != Placement::Filling)
    {
        Show();
        State = Placement::Filling;
    }
    HeightDp = height / Dp;
}

void Shelf::Hide()
{
    // A window of its own is nothing else: only closing the window takes it.
    if (State == Placement::Filling)
        return;
    State = Placement::Down;
    TopEdge = Edge::Resting;
    Serve(Purpose::Opening, {});
    Keys = KeyHolder::Elsewhere;
    if (Screen.IsValid())
        Ui.CloseScreen(Screen);
    Screen = {};
}

void Shelf::Pick(ShelfPick pick)
{
    Show();
    Serve(Purpose::Choosing, std::move(pick));
    Selected = Picking.Current;
    // What it offers is the chooser's; a kind narrowed to while browsing would hide it.
    Query.Category.clear();
}

void Shelf::Reveal(const std::string& assetPath)
{
    Show();
    // In its own folder, so a project of more files than are shown still shows it.
    Query = {};
    Query.Folder = FolderOf(ProjectFile{ .AssetPath = assetPath });
    List = Listing::Everything;
    Selected = assetPath;
    Trail.clear();
}

void Shelf::Follow(const std::string& assetPath)
{
    if (assetPath.empty() || FileAt(assetPath) == nullptr || assetPath == Selected)
        return;
    Trail.push_back(Selected);
    Query = {};
    List = Listing::Everything;
    Selected = assetPath;
}

bool ShelfPick::Offers(const ProjectFile& file) const
{
    if (std::ranges::find(Withheld, file.AssetPath) != Withheld.end())
        return false;
    if (!Only.empty())
        return std::ranges::find(Only, file.AssetPath) != Only.end();
    return Accepts.empty() || std::ranges::any_of(Accepts, [&](const ContentKind& kind) {
               return kind.Extension == file.Extension && (kind.Subtype.empty() || kind.Subtype == file.Subtype);
           });
}

bool Shelf::Offers(const ProjectFile& file) const
{
    return Use == Purpose::Opening || Picking.Offers(file);
}

void Shelf::Toggle()
{
    if (State == Placement::Down)
        Show();
    else
        Hide();
}

float Shelf::Reach() const
{
    return State == Placement::Down ? 0.0f : HeightDp * Dp;
}

void Shelf::Rescan()
{
    Files = ScanProjectFiles(Sources.ContentRoots);
    FilesVersion = Sources.Files != nullptr ? Sources.Files->Version() : 0;
    ++Offering;
    if (Sources.References != nullptr)
        Sources.References->Refresh(Files, ScanProjectFiles(Sources.OtherRoots), ReadProjectFileText);
}

const ProjectFile* Shelf::FileAt(const std::string& assetPath) const
{
    const auto found = std::ranges::lower_bound(Files, assetPath, {}, &ProjectFile::AssetPath);
    return found != Files.end() && found->AssetPath == assetPath ? &*found : nullptr;
}

void Shelf::Serve(Purpose use, ShelfPick pick)
{
    Use = use;
    Picking = std::move(pick);
    ++Offering;
}

void Shelf::Remember()
{
    for (const AssetTab& tab : Tabs)
        if (tab.Current && tab.Document.starts_with(kAssetPathPrefix))
        {
            if (!Recent.empty() && Recent.front() == tab.Document)
                return;
            std::erase(Recent, tab.Document);
            Recent.insert(Recent.begin(), tab.Document);
            if (Recent.size() > kMostRecent)
                Recent.pop_back();
            return;
        }
}

bool Shelf::Listed(const ProjectFile& file) const
{
    const ReferenceIndex* references = Sources.References;
    switch (List)
    {
    case Listing::Unused:   return references != nullptr && references->IsUnused(file.AssetPath);
    case Listing::Problems: return references != nullptr && !references->Missing(file.AssetPath).empty();
    default:                return true;
    }
}

std::vector<const ProjectFile*> Shelf::Shown()
{
    if (List == Listing::Everything || List == Listing::Unused || List == Listing::Problems)
    {
        if (Finding.Offering != Offering || Finding.List != List || Finding.Query != Query)
        {
            std::vector<const ProjectFile*> found = FindAssets(Files, Query);
            std::erase_if(found, [&](const ProjectFile* file) { return !Offers(*file) || !Listed(*file); });
            Finding = { .Offering = Offering, .Query = Query, .List = List, .Files = std::move(found) };
        }
        return Finding.Files;
    }
    std::vector<std::string> keys;
    if (List == Listing::Recent)
        keys = Recent;
    else
        for (const AssetTab& tab : Tabs)
            if (!tab.Document.empty() && (List == Listing::Open || tab.Changed))
                keys.push_back(tab.Document);
    std::vector<ProjectFile> listed;
    for (const std::string& key : keys)
        if (const ProjectFile* file = FileAt(key))
            listed.push_back(*file);
    AssetQuery query = Query;
    query.KeepOrder = true;
    std::vector<const ProjectFile*> found;
    for (const ProjectFile* file : FindAssets(listed, query))
        if (const ProjectFile* own = FileAt(file->AssetPath); own != nullptr && Offers(*own))
            found.push_back(own);
    return found;
}

void Shelf::Open(const std::string& assetPath)
{
    const ProjectFile* file = FileAt(assetPath);
    if (Use == Purpose::Choosing)
    {
        if (file == nullptr || !Offers(*file))
            return;
        // Taken before it is handed over: a choice may raise the shelf again.
        const ShelfPick pick = std::exchange(Picking, {});
        Serve(Purpose::Opening, {});
        if (State == Placement::Raised)
            Hide();
        if (pick.Choose)
            pick.Choose(assetPath, true);
        return;
    }
    if (file == nullptr || !Workspaces.RequestOpen(*file))
        return;
    std::erase(Recent, assetPath);
    Recent.insert(Recent.begin(), assetPath);
    if (State == Placement::Raised)
        Hide();
}

std::pair<std::size_t, std::size_t> Shelf::InView(std::size_t count) const
{
    const auto columns = static_cast<std::size_t>(Columns());
    const std::size_t rows = (count + columns - 1) / columns;
    const std::optional<UiScrollBox> scroll = Ui.MeasureScroll(Screen, "shelf_items");
    // Before the tiles are laid out, the first few rows are what shows.
    if (!scroll.has_value() || rows == 0 || scroll->ScrollHeight <= 0.0f)
        return { 0, std::min(count, columns * 4) };
    const float rowHeight = scroll->ScrollHeight / static_cast<float>(rows);
    const auto top = static_cast<std::size_t>(std::max(0.0f, std::floor(scroll->Top / rowHeight)));
    const auto bottom = static_cast<std::size_t>(std::ceil((scroll->Top + scroll->ClientHeight) / rowHeight));
    // A row either side, so a picture is ready as it scrolls in.
    const std::size_t first = top > 0 ? top - 1 : 0;
    return { std::min(count, first * columns), std::min(count, (bottom + 1) * columns) };
}

int Shelf::Columns() const
{
    if (View == Look::List || !Screen.IsValid())
        return 1;
    const std::optional<UiElementBox> items = Ui.MeasureElement(Screen, "shelf_items");
    if (!items.has_value() || items->Width <= 0.0f)
        return 1;
    const float stride = (PictureDp + kTileMarginDp + kTileGapDp) * Dp;
    return std::max(1, static_cast<int>(std::floor(items->Width / stride)));
}

void Shelf::Move(int by)
{
    const std::vector<const ProjectFile*> shown = Shown();
    if (shown.empty())
        return;
    const auto at = std::ranges::find(shown, Selected, &ProjectFile::AssetPath);
    const int index = at == shown.end() ? 0 : static_cast<int>(at - shown.begin()) + by;
    Selected = shown[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(shown.size()) - 1))]->AssetPath;
}

void Shelf::HandleAction(const UiAction& action, float windowHeight)
{
    if (action.Id == kGrab && State != Placement::Filling)
    {
        TopEdge = Edge::Sizing;
        GrabbedAt = static_cast<float>(Number(action));
        GrabbedHeightDp = HeightDp;
    }
    else if (action.Id == kDrag && TopEdge == Edge::Sizing)
    {
        const float raised = (GrabbedAt - static_cast<float>(Number(action))) / Dp;
        HeightDp = std::clamp(GrabbedHeightDp + raised, kMinimumHeightDp, windowHeight * kMostOfTheWindow / Dp);
    }
    else if (action.Id == kDrop)
        TopEdge = Edge::Resting;
    else if (action.Id == kQueryAction)
        Query.Text = Text(action);
    else if (action.Id == kKeys)
    {
        Keys = Flag(action, 0) ? KeyHolder::Search : KeyHolder::Elsewhere;
        SyncFileLayer();
    }
    else if (action.Id == kKey)
    {
        const bool plain = !Flag(action, 1) && !Flag(action, 3);
        const std::optional<UiKey> key =
            plain ? UiKeyFromEvent(static_cast<std::int64_t>(Number(action))) : std::nullopt;
        if (!key.has_value())
            PassOn(action);
        else if (key == UiKey::Escape)
            Hide();
        else if (key == UiKey::Return)
            Open(Selected);
        else if (key == UiKey::Left)
            Move(-1);
        else if (key == UiKey::Right)
            Move(1);
        else if (key == UiKey::Up)
            Move(-Columns());
        else if (key == UiKey::Down)
            Move(Columns());
    }
    else if (action.Id == kCategory)
        Query.Category = Text(action);
    else if (action.Id == kList)
    {
        const std::string list = Text(action);
        List = list == "recent"    ? Listing::Recent
             : list == "open"      ? Listing::Open
             : list == "changed"   ? Listing::Changed
             : list == "unused"    ? Listing::Unused
             : list == "problems"  ? Listing::Problems
                                   : Listing::Everything;
    }
    else if (action.Id == kFolder)
        Query.Folder = Text(action);
    else if (action.Id == kBranch)
    {
        const std::string folder = Text(action);
        if (!OpenFolders.erase(folder))
            OpenFolders.insert(folder);
    }
    else if (action.Id == kPick)
    {
        Selected = Text(action);
        if (Use == Purpose::Choosing && Picking.Choose)
            Picking.Choose(Selected, false);
    }
    else if (action.Id == kNone && Use == Purpose::Choosing && Picking.OffersNone)
    {
        const ShelfPick pick = std::exchange(Picking, {});
        Hide();
        if (pick.Choose)
            pick.Choose({}, true);
    }
    else if (action.Id == kOpen)
        Open(Text(action));
    else if (action.Id == kPress && Number(action, 1) == 1.0)
    {
        Selected = Text(action);
        Wanted = ShelfWheelRequest{ ShelfWheel::File,
                                    { static_cast<float>(Number(action, 2)), static_cast<float>(Number(action, 3)) } };
    }
    else if (action.Id == kCarry && Use == Purpose::Opening)
    {
        Carrying = Carry::Carrying;
        Carried = Text(action);
        Selected = Carried;
    }
    else if (action.Id == kInto && Carrying == Carry::Carrying)
        Into(Text(action));
    // Let go where nothing took it: above the shelf, it is the content's.
    else if (action.Id == kPut)
    {
        const std::string carried = TakeCarried();
        const Vec2d at{ static_cast<float>(Number(action, 0)), static_cast<float>(Number(action, 1)) };
        if (const ProjectFile* file = FileAt(carried); file != nullptr && at.Y < windowHeight - Reach())
            Dropped = ShelfDrop{ *file, at };
    }
    else if (action.Id == kNew)
        Wanted = ShelfWheelRequest{ ShelfWheel::NewContent,
                                    { static_cast<float>(Number(action, 0)), static_cast<float>(Number(action, 1)) } };
    else if (action.Id == kFollow)
        Follow(Text(action));
    else if (action.Id == kBack && !Trail.empty())
    {
        Query = {};
        List = Listing::Everything;
        Selected = Trail.back();
        Trail.pop_back();
    }
    else if (action.Id == kViewAction)
        View = View == Look::Tiles ? Look::List : Look::Tiles;
    else if (action.Id == kPin && State != Placement::Filling)
        State = State == Placement::Pinned ? Placement::Raised : Placement::Pinned;
    else if (action.Id == kClose && State == Placement::Filling)
    {
        if (CloseWindow)
            CloseWindow();
    }
    else if (action.Id == kClose)
        Hide();
    else if (action.Id == kWindow)
        (void)Actions.Invoke("shelf.window");
    else if (action.Id == kZoom && Flag(action, 1))
        PictureDp = std::clamp(PictureDp - static_cast<float>(Number(action)) * 8.0f, kSmallestPictureDp,
                               kLargestPictureDp);
}

void Shelf::PassOn(const UiAction& action)
{
    const auto key = static_cast<SDL_Keycode>(UiKeyCodeFromEvent(static_cast<std::int64_t>(Number(action))));
    const bool ctrl = Flag(action, 1);
    const bool shift = Flag(action, 2);
    const bool alt = Flag(action, 3);
    const KeyChord chord{ key, { .Ctrl = ctrl, .Shift = shift, .Alt = alt } };
    if (key == SDLK_UNKNOWN || SearchFieldKeeps(chord, Query.Text.empty()))
        return;
    (void)Actions.Dispatch(Catalog, chord);
}

void Shelf::OfferFileActions()
{
    const auto enabled = [this] { return CanChangeFiles(); };
    OfferAction(Catalog, FileActions,
                { .Id = "asset.open", .Label = "Open", .Category = "File", .Icon = IconId::File },
                { .Invoke = [this] { Open(Selected); },
                  .Enabled = [this] {
                      const ProjectFile* file = FileAt(Selected);
                      return file != nullptr
                             && (Use == Purpose::Choosing ? Offers(*file) : Workspaces.KindOpening(*file) != nullptr);
                  } });
    OfferAction(Catalog, FileActions, RenameAction(), { .Invoke = [this] { AskRename(); }, .Enabled = enabled });
    OfferAction(Catalog, FileActions, DuplicateAction(),
                { .Invoke = [this] { DuplicateChosen(); }, .Enabled = enabled });
    OfferAction(Catalog, FileActions, DeleteAction(), { .Invoke = [this] { AskTrash(); }, .Enabled = enabled });
}

bool Shelf::CanChangeFiles() const
{
    return Sources.Files != nullptr && Use == Purpose::Opening && FileAt(Selected) != nullptr;
}

void Shelf::AskRename()
{
    const ProjectFile* file = FileAt(Selected);
    if (file == nullptr)
        return;
    // Written without its scheme and extension, which a rename keeps.
    const std::string extension = std::filesystem::path(file->AssetPath).extension().string();
    std::string where = std::string(ContentRelativePath(file->AssetPath));
    where.resize(where.size() - extension.size());
    Asker.Ask({ .Title = "Rename " + std::string(PathLeaf(file->AssetPath)),
                .Message = "Where it is in the project: a slash puts it in another folder.",
                .Choices = { { "rename", "Rename" }, { "cancel", "Cancel" } },
                .Field = std::move(where),
                .Answer = [this, from = file->AssetPath](std::string_view key, std::string_view written) {
                    if (key == "rename")
                        Rename(from, written);
                } });
}

void Shelf::Rename(const std::string& from, std::string_view written)
{
    const std::string to = RenamedPath(from, written);
    if (to.empty() || to == from || Sources.Files == nullptr)
        return;
    if (std::string refused = Sources.Files->Move(from, to); !refused.empty())
        return Tell(std::move(refused), true);
    Selected = to;
    Tell(std::format("Moved {} to {}", PathLeaf(from), ContentRelativePath(to)), false);
}

void Shelf::DuplicateChosen()
{
    const ProjectFile* file = FileAt(Selected);
    if (file == nullptr || Sources.Files == nullptr)
        return;
    const std::string copy =
        CopyPath(file->AssetPath, [this](const std::string& path) { return FileAt(path) != nullptr; });
    if (std::string refused = Sources.Files->Duplicate(file->AssetPath, copy); !refused.empty())
        return Tell(std::move(refused), true);
    Selected = copy;
    Tell(std::format("Duplicated as {}", PathLeaf(copy)), false);
}

void Shelf::AskTrash()
{
    const ProjectFile* file = FileAt(Selected);
    if (file == nullptr)
        return;
    const std::span<const std::string> users =
        Sources.References != nullptr ? Sources.References->UsedBy(file->AssetPath) : std::span<const std::string>{};
    if (users.empty())
        return Trash(file->AssetPath);
    std::vector<std::string> items;
    for (const std::string& user : users | std::views::take(kMostReferencesShown))
        items.emplace_back(PathLeaf(user));
    if (users.size() > kMostReferencesShown)
        items.push_back(std::format("and {} more", users.size() - kMostReferencesShown));
    Asker.Ask({ .Title = "Trash " + std::string(PathLeaf(file->AssetPath)),
                .Message = users.size() == 1 ? "One file uses it, and will name a file that is not there."
                                             : std::format("{} files use it, and will name a file that is not there.",
                                                           users.size()),
                .Items = std::move(items),
                .Choices = { { "trash", "Trash it", ActionTone::Destructive }, { "cancel", "Keep it" } },
                .Answer = [this, path = file->AssetPath](std::string_view key, std::string_view) {
                    if (key == "trash")
                        Trash(path);
                } });
}

void Shelf::Trash(const std::string& assetPath)
{
    if (Sources.Files == nullptr)
        return;
    if (std::string refused = Sources.Files->Trash(assetPath); !refused.empty())
        return Tell(std::move(refused), true);
    if (Selected == assetPath)
        Selected.clear();
    const std::string undo = Catalog.ChordText("edit.undo");
    Tell(undo.empty() ? std::format("Trashed {}", PathLeaf(assetPath))
                      : std::format("Trashed {}; {} puts it back", PathLeaf(assetPath), undo),
         false);
}

std::string Shelf::TakeCarried()
{
    if (Carrying != Carry::Carrying)
        return {};
    Carrying = Carry::None;
    return std::exchange(Carried, std::string{});
}

void Shelf::Into(const std::string& folder)
{
    const std::string from = TakeCarried();
    const std::string to = AssetPathOf(folder.empty() ? std::string{} : folder + "/")
                         + std::string(PathLeaf(from));
    if (from.empty() || to == from || Sources.Files == nullptr)
        return;
    if (std::string refused = Sources.Files->Move(from, to); !refused.empty())
        return Tell(std::move(refused), true);
    Selected = to;
    Tell(std::format("Moved {} to {}", PathLeaf(from), folder.empty() ? std::string("the top") : folder), false);
}

void Shelf::Import(std::span<const std::filesystem::path> files)
{
    if (Sources.Files == nullptr || files.empty())
        return;
    const std::string folder = Query.Folder;
    std::string arrived;
    for (const std::filesystem::path& file : files)
    {
        const std::string to = AssetPathOf(folder.empty() ? std::string{} : folder + "/")
                             + file.filename().string();
        if (std::string refused = Sources.Files->Import(file, to); !refused.empty())
        {
            Tell(std::move(refused), true);
            continue;
        }
        arrived = to;
        Tell(std::format("Imported {}", file.filename().string()), false);
    }
    if (!arrived.empty())
        Reveal(arrived);
}

void Shelf::Tell(std::string text, bool failed)
{
    Notices.Post(std::move(text), failed ? NoticeTone::Warning : NoticeTone::Done, NoticeClockNow());
}

void Shelf::SyncFileLayer()
{
    // Raised, the whole of its window, or holding the keys in its search field,
    // it is in front of the work, and Delete is the chosen file's.
    const bool inFront = State == Placement::Raised || State == Placement::Filling || Keys == KeyHolder::Search;
    if (inFront == FileLayer.IsActive())
        return;
    if (inFront)
        FileLayer = Actions.Push(FileActions, ActionRank::Surface);
    else
        FileLayer.Reset();
}

void Shelf::Update(float dp, float windowHeight, std::span<const AssetTab> tabs)
{
    Tabs.assign(tabs.begin(), tabs.end());
    SyncFileLayer();
    Dp = dp > 0.0f ? dp : 1.0f;
    if (Screen.IsValid())
        for (const UiAction& action : Ui.DrainActions(Screen))
            HandleAction(action, windowHeight);
    Remember();
    if (State != Placement::Down && Sources.Files != nullptr && Sources.Files->Version() != FilesVersion)
        Rescan();
    if (Screen.IsValid())
    {
        Publish();
        // Once its tile is laid out, a file chosen by any means is in view.
        if (Selected != ScrolledTo && Ui.ScrollIntoView(Screen, "tile:" + Selected))
            ScrolledTo = Selected;
    }
}

void Shelf::Publish()
{
    (void)Ui.SetValue(Screen, kHeight, UiValue(PixelLength(HeightDp * Dp)));
    (void)Ui.SetValue(Screen, kView, UiValue(std::string(View == Look::List ? "list" : "tiles")));
    (void)Ui.SetValue(Screen, kPinned, UiValue(State == Placement::Pinned));
    (void)Ui.SetValue(Screen, kOwnWindow, UiValue(State == Placement::Filling));
    (void)Ui.SetValue(Screen, kPicture, UiValue(std::format("{:.0f}dp", PictureDp)));
    (void)Ui.SetValue(Screen, kTile, UiValue(std::format("{:.0f}dp", PictureDp + kTileMarginDp)));
    (void)Ui.SetValue(Screen, kChoosing, UiValue(Picking.Title));
    (void)Ui.SetValue(Screen, kOffersNone, UiValue(Use == Purpose::Choosing && Picking.OffersNone));

    std::vector<UiRow> crumbs;
    UiRow everywhere{ "Content", "", "" };
    everywhere.Selected = Query.Folder.empty();
    crumbs.push_back(std::move(everywhere));
    for (std::size_t from = 0; from < Query.Folder.size();)
    {
        const std::size_t slash = Query.Folder.find('/', from);
        const std::size_t end = slash == std::string::npos ? Query.Folder.size() : slash;
        UiRow crumb{ Query.Folder.substr(from, end - from), "", "" };
        crumb.Key = Query.Folder.substr(0, end);
        crumb.Selected = end == Query.Folder.size();
        crumbs.push_back(std::move(crumb));
        from = end + 1;
    }
    (void)Ui.SetRows(Screen, kCrumbs, crumbs);

    Derive();
    const auto offered = [&](const std::string& assetPath) {
        const ProjectFile* file = FileAt(assetPath);
        return file != nullptr && Offers(*file);
    };

    // A chip shows only where it would narrow: among two kinds or more.
    std::vector<UiRow> chips;
    if (Known.Categories.size() >= 2)
    {
        UiRow all{ "All", "", "" };
        all.Selected = Query.Category.empty();
        chips.push_back(std::move(all));
        for (const std::string_view category : Known.Categories)
        {
            UiRow chip{ std::string(category), "", "" };
            chip.Key = category;
            chip.Selected = Query.Category == category;
            chips.push_back(std::move(chip));
        }
    }
    (void)Ui.SetRows(Screen, kChips, chips);

    const std::vector<AssetTab>& tabs = Tabs;
    const auto counted = [&](bool changedOnly) {
        return std::ranges::count_if(tabs, [&](const AssetTab& tab) {
            return !tab.Document.empty() && offered(tab.Document) && (!changedOnly || tab.Changed);
        });
    };
    std::vector<UiRow> lists;
    const auto list = [&](const char* label, const char* key, std::size_t count, Listing which) {
        UiRow row{ label, std::to_string(count), "" };
        row.Key = key;
        row.Selected = List == which;
        lists.push_back(std::move(row));
    };
    list("Everything", "everything", Known.Offered, Listing::Everything);
    list("Recent", "recent", static_cast<std::size_t>(std::ranges::count_if(Recent, offered)), Listing::Recent);
    list("Open", "open", static_cast<std::size_t>(counted(false)), Listing::Open);
    list("Changed", "changed", static_cast<std::size_t>(counted(true)), Listing::Changed);
    if (Sources.References != nullptr)
    {
        list("Unused", "unused", Known.Unused, Listing::Unused);
        list("Problems", "problems", Known.Problems, Listing::Problems);
    }
    (void)Ui.SetRows(Screen, kLists, lists);

    std::vector<UiRow> folders;
    for (const DerivedFolder& folder : Known.Folders)
    {
        // Shown when every folder above it is open.
        bool visible = true;
        for (std::size_t slash = folder.Path.find('/'); slash != std::string::npos;
             slash = folder.Path.find('/', slash + 1))
            visible = visible && OpenFolders.contains(folder.Path.substr(0, slash));
        if (!visible)
            continue;
        UiRow row{ std::string(PathLeaf(folder.Path)),
                   folder.HoldsFolders ? (OpenFolders.contains(folder.Path) ? "open" : "closed") : "", "" };
        row.Key = folder.Path;
        row.Depth = static_cast<int>(std::ranges::count(folder.Path, '/'));
        row.Selected = Query.Folder == folder.Path;
        folders.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kFolders, folders);

    const std::vector<const ProjectFile*> shown = Shown();
    if ((Selected.empty() || std::ranges::find(shown, Selected, &ProjectFile::AssetPath) == shown.end())
        && !shown.empty())
        Selected = shown.front()->AssetPath;
    std::vector<UiRow> items;
    const auto [firstPictured, pastPictured] = InView(shown.size());
    for (const ProjectFile* file : shown)
    {
        const AssetKind kind = KindOf(*file);
        const auto tab = std::ranges::find(tabs, file->AssetPath, &AssetTab::Document);
        const std::string_view state = tab != tabs.end()        ? (tab->Changed ? "changed" : "open")
                                       : file->AssetPath == Picking.Current ? "current"
                                                                            : "";
        UiRow item{ std::string(NameOf(*file)), std::string(state), kind.Label };
        item.Key = file->AssetPath;
        item.Icon = IconName(kind.Icon);
        // Only a tile in view is asked for its picture: those scrolled away
        // never load theirs.
        const bool inView = items.size() >= firstPictured && items.size() < pastPictured;
        const AssetPicture picture = Sources.Pictures != nullptr && View == Look::Tiles && inView
                                         ? Sources.Pictures->PictureOf(file->AssetPath)
                                         : AssetPicture{};
        item.Parts = { picture.Image, picture.Color };
        item.Selected = file->AssetPath == Selected;
        items.push_back(std::move(item));
    }
    (void)Ui.SetRows(Screen, kItems, items);
    (void)Ui.SetValue(Screen, kCount, UiValue(std::format("{} of {}", shown.size(), Known.Offered)));
    (void)Ui.SetValue(Screen, kEmpty, UiValue(std::string(Query.Text.empty() ? "Nothing here."
                                                                            : "Nothing holds that.")));

    const ProjectFile* chosen = FileAt(Selected);
    const AssetKind kind = chosen != nullptr ? KindOf(*chosen) : AssetKind{};
    (void)Ui.SetValue(Screen, kPreviewName, UiValue(chosen != nullptr ? std::string(NameOf(*chosen)) : std::string{}));
    (void)Ui.SetValue(Screen, kPreviewKind, UiValue(kind.Label));
    (void)Ui.SetValue(Screen, kPreviewPath, UiValue(chosen != nullptr ? chosen->AssetPath : std::string{}));
    (void)Ui.SetValue(Screen, kPreviewFacts,
                      UiValue(chosen != nullptr ? SizeText(chosen->Size) + " \xC2\xB7 " + AgeText(chosen->Modified)
                                                : std::string{}));
    (void)Ui.SetValue(Screen, kPreviewIcon, UiValue(std::string(IconName(kind.Icon))));
    const AssetPicture picture = chosen != nullptr && Sources.Pictures != nullptr ? Sources.Pictures->PictureOf(chosen->AssetPath)
                                                                          : AssetPicture{};
    (void)Ui.SetValue(Screen, kPreviewImage, UiValue(picture.Image));
    (void)Ui.SetValue(Screen, kPreviewColor, UiValue(picture.Color));
    PublishReferences(chosen);
    (void)Ui.SetValue(Screen, kCanGoBack, UiValue(!Trail.empty()));
    (void)Ui.SetValue(Screen, kPreviewOpens, UiValue(chosen != nullptr && (Use == Purpose::Choosing
                                                                             || Workspaces.KindOpening(*chosen) != nullptr)));
}

void Shelf::Derive()
{
    if (Known.Offering == Offering)
        return;
    Known = { .Offering = Offering };
    const auto offers = [&](const ProjectFile& file) { return Offers(file); };
    Known.Offered = static_cast<std::size_t>(std::ranges::count_if(Files, offers));
    for (const std::string_view category : AssetCategories())
        if (std::ranges::any_of(Files, [&](const ProjectFile& file) {
                return KindOf(file).Category == category && Offers(file);
            }))
            Known.Categories.push_back(category);
    if (const ReferenceIndex* references = Sources.References)
        for (const ProjectFile& file : Files)
            if (Offers(file))
            {
                Known.Unused += references->IsUnused(file.AssetPath) ? 1 : 0;
                Known.Problems += references->Missing(file.AssetPath).empty() ? 0 : 1;
            }
    std::vector<ProjectFile> offered;
    if (Use == Purpose::Choosing)
        std::ranges::copy_if(Files, std::back_inserter(offered), offers);
    const std::vector<std::string> folders = FoldersOf(Use == Purpose::Choosing ? offered : Files);
    for (const std::string& folder : folders)
        Known.Folders.push_back({ .Path = folder,
                                  .HoldsFolders = std::ranges::any_of(folders, [&](const std::string& other) {
                                      return other.size() > folder.size() && other.starts_with(folder)
                                          && other[folder.size()] == '/';
                                  }) });
}

void Shelf::PublishReferences(const ProjectFile* chosen)
{
    const ReferenceIndex* references = chosen != nullptr ? Sources.References : nullptr;
    const auto rows = [&](std::span<const std::string> paths, bool followable) {
        std::vector<UiRow> out;
        for (const std::string& path : paths)
        {
            if (out.size() == kMostReferencesShown)
            {
                out.push_back(UiRow{ std::format("and {} more", paths.size() - kMostReferencesShown), "", "" });
                break;
            }
            const ProjectFile* file = followable ? FileAt(path) : nullptr;
            // What is missing is said as it was written, to be found and mended.
            UiRow row{ file != nullptr ? std::string(NameOf(*file)) : followable ? std::string(PathLeaf(path)) : path, "",
                       file != nullptr ? KindOf(*file).Label : std::string{} };
            row.Key = file != nullptr ? path : std::string{};
            out.push_back(std::move(row));
        }
        return out;
    };
    (void)Ui.SetRows(Screen, kUses, rows(references != nullptr ? references->Uses(chosen->AssetPath)
                                                               : std::span<const std::string>{}, true));
    (void)Ui.SetRows(Screen, kUsedBy, rows(references != nullptr ? references->UsedBy(chosen->AssetPath)
                                                                 : std::span<const std::string>{}, true));
    (void)Ui.SetRows(Screen, kMissing, rows(references != nullptr ? references->Missing(chosen->AssetPath)
                                                                  : std::span<const std::string>{}, false));
}
