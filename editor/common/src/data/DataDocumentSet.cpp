#include "data/DataDocumentSet.h"

#include <algorithm>
#include <format>

DataDocumentSet::DataDocumentSet(DataDocumentStore& store, DataDocumentSetConfig config)
    : Documents_(store)
    , Config(std::move(config))
{
    ChangeToken = Documents_.OnChanged([this](DataDocument& document, bool residentChanged) {
        if (Observer && std::ranges::find(Tabs, &document) != Tabs.end())
            Observer(document, residentChanged);
    });
    CloseToken = Documents_.OnClosing([this](const DataDocument& document) { Dropped(document); });
}

DataDocumentSet::~DataDocumentSet()
{
    Documents_.Unobserve(ChangeToken);
    Documents_.Unobserve(CloseToken);
    // Whoever closed this view settled its changes; a document still changed
    // stays in the store, which refuses to be destroyed with it.
    const std::vector<DataDocument*> shown = Tabs;
    for (DataDocument* document : shown)
    {
        std::string error;
        (void)Documents_.Release(*document, DirtyDisposition::Refuse, error);
    }
}

DataDocument* DataDocumentSet::OpenOrFocus(std::string_view virtualPath, std::string& error)
{
    if (const std::optional<std::size_t> index = IndexOf(virtualPath))
    {
        SetActive(*index);
        return Tabs[*index];
    }
    const std::string subtype = Documents_.SubtypeOf(virtualPath);
    if (!subtype.empty() && !Accepts(subtype))
    {
        error = std::format("'{}' is a {} asset, which this editor does not open.", virtualPath, subtype);
        return nullptr;
    }
    DataDocument* document = Documents_.Open(virtualPath, error);
    return document != nullptr ? Show(*document) : nullptr;
}

DataDocument* DataDocumentSet::Create(std::string_view subtype, std::string_view relativePath, std::string& error)
{
    if (!Accepts(subtype))
    {
        error = std::format("'{}' is not a subtype this editor can create.", subtype);
        return nullptr;
    }
    DataDocument* document = Documents_.Create(subtype, Config.ContentRoot, relativePath, error);
    return document != nullptr ? Show(*document) : nullptr;
}

bool DataDocumentSet::Close(std::size_t index, DirtyDisposition disposition, std::string& error)
{
    if (index >= Tabs.size())
    {
        error = "That document is not open.";
        return false;
    }
    // The store tells every view, this one included, when the document goes.
    DataDocument& document = *Tabs[index];
    if (!Documents_.Release(document, disposition, error))
        return false;
    Dropped(document);
    return true;
}

void DataDocumentSet::Dropped(const DataDocument& document)
{
    const auto it = std::ranges::find(Tabs, &document);
    if (it == Tabs.end())
        return;
    const std::size_t index = static_cast<std::size_t>(it - Tabs.begin());
    Tabs.erase(it);
    if (ActiveTab > index || ActiveTab >= Tabs.size())
        ActiveTab = ActiveTab == 0 ? 0 : ActiveTab - 1;
    SelectField(nullptr, {});
}

void DataDocumentSet::SetActive(std::size_t index)
{
    if (index >= Tabs.size())
        return;
    if (index != ActiveTab)
        if (DataDocument* previous = Active())
            Documents_.CommitEdit(*previous);
    ActiveTab = index;
    SelectField(nullptr, {});
}

void DataDocumentSet::Reveal(std::string_view virtualPath)
{
    if (const std::optional<std::size_t> index = IndexOf(virtualPath))
        SetActive(*index);
}

DataDocument* DataDocumentSet::Active()
{
    return ActiveTab < Tabs.size() ? Tabs[ActiveTab] : nullptr;
}

DataDocument* DataDocumentSet::ActiveOf(std::string_view subtype)
{
    DataDocument* active = Active();
    return active != nullptr && active->Subtype() == subtype ? active : nullptr;
}

DataDocument* DataDocumentSet::Find(std::string_view virtualPath)
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Tabs[*index] : nullptr;
}

const DataDocument* DataDocumentSet::Find(std::string_view virtualPath) const
{
    const std::optional<std::size_t> index = IndexOf(virtualPath);
    return index ? Tabs[*index] : nullptr;
}

std::optional<std::size_t> DataDocumentSet::IndexOf(std::string_view virtualPath) const
{
    for (std::size_t index = 0; index < Tabs.size(); ++index)
        if (Tabs[index]->VirtualPath() == virtualPath)
            return index;
    return std::nullopt;
}

const DataSchema* DataDocumentSet::ActiveSchema()
{
    const DataDocument* active = Active();
    return active != nullptr ? Documents_.SchemaOf(*active) : nullptr;
}

std::vector<std::string> DataDocumentSet::CreatableSubtypes() const
{
    std::vector<std::string> subtypes;
    for (const DataAssetTypeRegistration& type : Documents_.Types().Entries())
        if (Accepts(type.Name) && Documents_.SchemaOf(type.Name) != nullptr)
            subtypes.push_back(type.Name);
    if (!Config.Subtypes.empty())
        std::ranges::sort(subtypes, {}, [&](const std::string& subtype) {
            return std::ranges::find(Config.Subtypes, subtype) - Config.Subtypes.begin();
        });
    return subtypes;
}

void DataDocumentSet::SelectField(const DataFieldSchema* field, std::string path)
{
    Selected = field;
    SelectedJsonPath = std::move(path);
}

std::vector<std::string> DataDocumentSet::DataAssetPaths(std::string_view subtype)
{
    return Documents_.DataAssetPaths(subtype);
}

void DataDocumentSet::OpenDataAsset(std::string_view path)
{
    std::string error;
    (void)OpenOrFocus(path, error);
}

void DataDocumentSet::SelectField(const DataFieldSchema& field, std::string_view path)
{
    SelectField(&field, std::string(path));
}

void DataDocumentSet::EditPreviewed(DataDocument& document)
{
    Documents_.Validate(document);
}

void DataDocumentSet::EditCommitted(DataDocument& document)
{
    Documents_.Changed(document);
}

bool DataDocumentSet::Accepts(std::string_view subtype) const
{
    return Config.Subtypes.empty() || std::ranges::find(Config.Subtypes, subtype) != Config.Subtypes.end();
}

DataDocument* DataDocumentSet::Show(DataDocument& document)
{
    Documents_.Hold(document);
    Tabs.push_back(&document);
    SetActive(Tabs.size() - 1);
    return &document;
}
