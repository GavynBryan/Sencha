#include "MaterialTabSet.h"

#include <algorithm>
#include <utility>

MaterialEditTab* MaterialTabSet::OpenOrFocus(std::string virtualPath, std::string filePath,
                                             std::string* error)
{
    for (std::size_t i = 0; i < List.size(); ++i)
    {
        if (List[i]->Session.VirtualPath() == virtualPath)
        {
            ActiveTab = i;
            return List[i].get();
        }
    }

    auto tab = std::make_unique<MaterialEditTab>();
    if (!tab->Session.Open(std::move(virtualPath), std::move(filePath), error))
        return nullptr;

    List.push_back(std::move(tab));
    ActiveTab = List.size() - 1;
    return List.back().get();
}

void MaterialTabSet::Close(std::size_t index)
{
    if (index >= List.size())
        return;
    List.erase(List.begin() + static_cast<std::ptrdiff_t>(index));
    if (ActiveTab >= List.size() && !List.empty())
        ActiveTab = List.size() - 1;
    if (List.empty())
        ActiveTab = 0;
}

MaterialEditTab* MaterialTabSet::Active()
{
    if (ActiveTab >= List.size())
        return nullptr;
    return List[ActiveTab].get();
}

void MaterialTabSet::SetActive(std::size_t index)
{
    if (index < List.size())
        ActiveTab = index;
}

MaterialEditTab* MaterialTabSet::Find(std::string_view virtualPath)
{
    for (const auto& tab : List)
        if (tab->Session.VirtualPath() == virtualPath)
            return tab.get();
    return nullptr;
}

bool MaterialTabSet::AnyDirty() const
{
    return std::any_of(List.begin(), List.end(),
                       [](const auto& tab) { return tab->Session.IsDirty(); });
}

int MaterialTabSet::SaveAll(std::string* error)
{
    int saved = 0;
    for (const auto& tab : List)
    {
        if (!tab->Session.IsDirty())
            continue;
        std::string tabError;
        if (tab->Session.Save(&tabError))
            ++saved;
        else if (error != nullptr && error->empty())
            *error = tabError;
    }
    return saved;
}
