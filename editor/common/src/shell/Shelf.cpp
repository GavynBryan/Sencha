#include "shell/Shelf.h"

#include "icons/IconNames.h"
#include "project/AssetKinds.h"
#include "project/ReferenceIndex.h"
#include "shell/AssetTabs.h"
#include "text/PathLeaf.h"
#include "thumbnails/AssetPictures.h"
#include "workspaces/WorkspaceHost.h"

#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iterator>
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

// How much wider a tile is than its picture, and the gap after it, in dp.
constexpr float kTileMarginDp = 24.0f;
constexpr float kTileGapDp = 8.0f;
constexpr float kSmallestPictureDp = 40.0f;
constexpr float kLargestPictureDp = 200.0f;
constexpr float kMinimumHeightDp = 180.0f;
constexpr float kMostOfTheWindow = 0.75f;
// Past this many, the rest are found by narrowing.
constexpr std::size_t kMostShown = 240;
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
    };
    desc.RowLists = { "crumbs", "chips", "lists", "folders", "items", "uses", "used_by", "missing" };
    desc.Actions = { "shelf_grab",   "shelf_drag",   "shelf_drop", "shelf_query", "shelf_key",
                     "shelf_category", "shelf_list", "shelf_folder", "shelf_branch", "shelf_pick",
                     "shelf_open",   "shelf_view",   "shelf_pin",  "shelf_close", "shelf_zoom", "shelf_none",
                     "shelf_follow", "shelf_back" };
    return desc;
}

std::string Text(const UiAction& action, std::size_t index = 0)
{
    return index < action.Arguments.size() && action.Arguments[index].Kind() == UiValueKind::String
               ? std::string(action.Arguments[index].AsString())
               : std::string{};
}

double Number(const UiAction& action, std::size_t index = 0)
{
    if (index >= action.Arguments.size())
        return 0.0;
    const UiValue& value = action.Arguments[index];
    return value.Kind() == UiValueKind::Float ? value.AsFloat()
           : value.Kind() == UiValueKind::Int ? static_cast<double>(value.AsInt())
                                               : 0.0;
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

std::string_view FolderName(std::string_view folder)
{
    const std::size_t slash = folder.rfind('/');
    return slash == std::string_view::npos ? folder : folder.substr(slash + 1);
}
} // namespace

Shelf::Shelf(UiService& ui, UiSurfaceId surface, WorkspaceHost& workspaces, PresentationId presentation,
             ShelfSources sources)
    : Ui(ui)
    , Surface(surface)
    , Workspaces(workspaces)
    , Presentation(presentation)
    , Sources(std::move(sources))
{
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

void Shelf::Hide()
{
    State = Placement::Down;
    TopEdge = Edge::Resting;
    Use = Purpose::Opening;
    Picking = {};
    if (Screen.IsValid())
        Ui.CloseScreen(Screen);
    Screen = {};
}

void Shelf::Pick(ShelfPick pick)
{
    Show();
    Use = Purpose::Choosing;
    Picking = std::move(pick);
    Selected = Picking.Current;
    // What it offers is the chooser's; a kind narrowed to while browsing would hide it.
    Query.Category.clear();
}

void Shelf::Reveal(const std::string& assetPath)
{
    Show();
    Query = {};
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
    if (std::ranges::any_of(Withheld, [&](const std::string& withheld) {
            return withheld == file.AssetPath || std::filesystem::path(withheld) == file.FilePath;
        }))
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
    if (Sources.References != nullptr)
        Sources.References->Refresh(Files, ScanProjectFiles(Sources.OtherRoots), ReadProjectFileText);
}

const ProjectFile* Shelf::FileAt(const std::string& assetPath) const
{
    const auto found = std::ranges::lower_bound(Files, assetPath, {}, &ProjectFile::AssetPath);
    return found != Files.end() && found->AssetPath == assetPath ? &*found : nullptr;
}

void Shelf::Remember()
{
    for (const AssetTab& tab : AssetTabsIn(Workspaces, Presentation))
        if (tab.Current && tab.Document.starts_with("asset://"))
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

std::vector<const ProjectFile*> Shelf::Shown() const
{
    if (List == Listing::Everything || List == Listing::Unused || List == Listing::Problems)
    {
        std::vector<const ProjectFile*> found = FindAssets(Files, Query);
        std::erase_if(found, [&](const ProjectFile* file) { return !Offers(*file) || !Listed(*file); });
        return found;
    }
    std::vector<std::string> keys;
    if (List == Listing::Recent)
        keys = Recent;
    else
        for (const AssetTab& tab : AssetTabsIn(Workspaces, Presentation))
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
        Use = Purpose::Opening;
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
    if (action.Id == kGrab)
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
    else if (action.Id == kKey)
    {
        const std::optional<UiKey> key = UiKeyFromEvent(static_cast<std::int64_t>(Number(action)));
        if (key == UiKey::Escape)
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
    else if (action.Id == kPin)
        State = State == Placement::Pinned ? Placement::Raised : Placement::Pinned;
    else if (action.Id == kClose)
        Hide();
    else if (action.Id == kZoom && action.Arguments.size() > 1 && action.Arguments[1].Kind() == UiValueKind::Bool
             && action.Arguments[1].AsBool())
        PictureDp = std::clamp(PictureDp - static_cast<float>(Number(action)) * 8.0f, kSmallestPictureDp,
                               kLargestPictureDp);
}

void Shelf::Update(float dp, float windowHeight)
{
    Dp = dp > 0.0f ? dp : 1.0f;
    if (Screen.IsValid())
        for (const UiAction& action : Ui.DrainActions(Screen))
            HandleAction(action, windowHeight);
    Remember();
    if (Screen.IsValid())
        Publish();
}

void Shelf::Publish()
{
    (void)Ui.SetValue(Screen, kHeight, UiValue(std::format("{:.0f}px", HeightDp * Dp)));
    (void)Ui.SetValue(Screen, kView, UiValue(std::string(View == Look::List ? "list" : "tiles")));
    (void)Ui.SetValue(Screen, kPinned, UiValue(State == Placement::Pinned));
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

    const auto offered = [&](const std::string& assetPath) {
        const ProjectFile* file = FileAt(assetPath);
        return file != nullptr && Offers(*file);
    };
    const std::size_t offeredCount =
        static_cast<std::size_t>(std::ranges::count_if(Files, [&](const ProjectFile& file) { return Offers(file); }));

    // A chip shows only where it would narrow: among two kinds or more.
    std::vector<UiRow> chips;
    UiRow all{ "All", "", "" };
    all.Selected = Query.Category.empty();
    chips.push_back(std::move(all));
    for (const std::string_view category : AssetCategories())
        if (std::ranges::any_of(Files, [&](const ProjectFile& file) {
                return KindOf(file).Category == category && Offers(file);
            }))
        {
            UiRow chip{ std::string(category), "", "" };
            chip.Key = category;
            chip.Selected = Query.Category == category;
            chips.push_back(std::move(chip));
        }
    if (chips.size() < 3)
        chips.clear();
    (void)Ui.SetRows(Screen, kChips, chips);

    const std::vector<AssetTab> tabs = AssetTabsIn(Workspaces, Presentation);
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
    list("Everything", "everything", offeredCount, Listing::Everything);
    list("Recent", "recent", static_cast<std::size_t>(std::ranges::count_if(Recent, offered)), Listing::Recent);
    list("Open", "open", static_cast<std::size_t>(counted(false)), Listing::Open);
    list("Changed", "changed", static_cast<std::size_t>(counted(true)), Listing::Changed);
    if (const ReferenceIndex* references = Sources.References)
    {
        const auto countWhere = [&](auto&& holds) {
            return static_cast<std::size_t>(std::ranges::count_if(Files, [&](const ProjectFile& file) {
                return Offers(file) && holds(file.AssetPath);
            }));
        };
        list("Unused", "unused", countWhere([&](const std::string& path) { return references->IsUnused(path); }),
             Listing::Unused);
        list("Problems", "problems",
             countWhere([&](const std::string& path) { return !references->Missing(path).empty(); }),
             Listing::Problems);
    }
    (void)Ui.SetRows(Screen, kLists, lists);

    std::vector<UiRow> folders;
    std::vector<ProjectFile> offeredFiles;
    if (Use == Purpose::Choosing)
        std::ranges::copy_if(Files, std::back_inserter(offeredFiles), [&](const ProjectFile& file) { return Offers(file); });
    const std::vector<std::string> known = FoldersOf(Use == Purpose::Choosing ? offeredFiles : Files);
    for (const std::string& folder : known)
    {
        // Shown when every folder above it is open.
        bool visible = true;
        for (std::size_t slash = folder.find('/'); slash != std::string::npos; slash = folder.find('/', slash + 1))
            visible = visible && OpenFolders.contains(folder.substr(0, slash));
        if (!visible)
            continue;
        const bool holdsFolders = std::ranges::any_of(known, [&](const std::string& other) {
            return other.size() > folder.size() && other.starts_with(folder + "/");
        });
        UiRow row{ std::string(FolderName(folder)),
                   holdsFolders ? (OpenFolders.contains(folder) ? "open" : "closed") : "", "" };
        row.Key = folder;
        row.Depth = static_cast<int>(std::ranges::count(folder, '/'));
        row.Selected = Query.Folder == folder;
        folders.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kFolders, folders);

    const std::vector<const ProjectFile*> shown = Shown();
    if ((Selected.empty() || std::ranges::find(shown, Selected, &ProjectFile::AssetPath) == shown.end())
        && !shown.empty())
        Selected = shown.front()->AssetPath;
    std::vector<UiRow> items;
    for (const ProjectFile* file : shown)
    {
        if (items.size() == kMostShown)
            break;
        const AssetKind kind = KindOf(*file);
        const auto tab = std::ranges::find(tabs, file->AssetPath, &AssetTab::Document);
        const std::string_view state = tab != tabs.end()        ? (tab->Changed ? "changed" : "open")
                                       : file->AssetPath == Picking.Current ? "current"
                                                                            : "";
        UiRow item{ std::string(NameOf(*file)), std::string(state), kind.Label };
        item.Key = file->AssetPath;
        item.Icon = IconName(kind.Icon);
        // Only a tile drawn as one is asked for its picture: tiles past what is
        // shown never load theirs.
        const AssetPicture picture = Sources.Pictures != nullptr && View == Look::Tiles ? Sources.Pictures->PictureOf(file->AssetPath)
                                                                                : AssetPicture{};
        item.Parts = { picture.Image, picture.Color };
        item.Selected = file->AssetPath == Selected;
        items.push_back(std::move(item));
    }
    (void)Ui.SetRows(Screen, kItems, items);
    (void)Ui.SetValue(Screen, kCount, UiValue(shown.size() > kMostShown
                                                  ? std::format("{} of {} shown: type to narrow", kMostShown,
                                                                shown.size())
                                                  : std::format("{} of {}", shown.size(), offeredCount)));
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
