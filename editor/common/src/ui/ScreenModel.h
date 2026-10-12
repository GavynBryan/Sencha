#pragma once

#include <ui/UiScreenDesc.h>
#include <ui/UiValue.h>

#include <string>
#include <utility>

// A screen's model and actions declared a name at a time: each declaration
// returns the id that name is addressed by, so the names and their ids are one
// list, and several presenters can each declare their part of one screen.
class ScreenModel
{
public:
    ScreenModel(std::string packagePath, std::string modelName)
    {
        Desc.PackagePath = std::move(packagePath);
        Desc.ModelName = std::move(modelName);
    }

    [[nodiscard]] UiModelPropertyId Property(std::string name, UiValue initial)
    {
        Desc.Properties.push_back(UiModelProperty{ std::move(name), std::move(initial) });
        return UiPropertyIdAt(Desc.Properties.size() - 1);
    }
    [[nodiscard]] UiModelRowsId Rows(std::string name)
    {
        Desc.RowLists.push_back(std::move(name));
        return UiRowsIdAt(Desc.RowLists.size() - 1);
    }
    [[nodiscard]] UiModelArrayId Array(std::string name)
    {
        Desc.Arrays.push_back(std::move(name));
        return UiArrayIdAt(Desc.Arrays.size() - 1);
    }
    [[nodiscard]] UiActionId Action(std::string name)
    {
        Desc.Actions.push_back(std::move(name));
        return UiActionIdAt(Desc.Actions.size() - 1);
    }
    void SetModal(bool modal) { Desc.Modal = modal; }

    [[nodiscard]] const UiScreenDesc& Description() const { return Desc; }

private:
    UiScreenDesc Desc;
};
