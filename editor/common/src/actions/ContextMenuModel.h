#pragma once

#include "tools/RadialMenuModel.h"
#include "workspaces/WorkspaceFacets.h"

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContextStack;

//=============================================================================
// ContextMenuModel
//
// The wheel for what the pointer is on: as it opens, the workspace aims at
// the thing under the pointer (selecting it, or keeping the selection when
// nothing is there), and the wheel offers that selection's choices -- each a
// fan of its options, the one in effect marked -- and the actions it can take
// now, the first few as entries and the rest in a fan, with the full sheet
// last. The hub reads out what it is and its values, so holding the key and
// letting go in the middle only looks.
//
// Nothing from the selection is held across frames: a choice is found again
// by where it is and its key when it is made.
//=============================================================================
class ContextMenuModel final : public IRadialMenuModel
{
public:
    // An asset a fan offers: the value it sets, what to call it, its picture.
    struct AssetChoice
    {
        std::string Path;
        std::string Label;
        std::string Image = {};
    };

    struct Hooks
    {
        // Points the selection at what is under `pointer`, or leaves it.
        std::function<void(Vec2d pointer)> Aim;
        std::function<WorkspaceSelectionFacet()> Selection;
        // What the window can perform now, which the actions resolve through.
        std::function<const ActionContextStack*()> Actions;
        // The action that shows everything about the selection.
        std::string Details;
        // The assets of a kind a person has to hand -- the one in use and the
        // ones put by -- and the action that browses every one; an asset
        // property is offered only with both.
        std::function<std::vector<AssetChoice>(AssetType)> Recent = {};
        std::function<std::string(AssetType)> Browse = {};
    };

    ContextMenuModel(Hooks hooks, const ActionCatalog& catalog);

    [[nodiscard]] int Count() const override { return static_cast<int>(Entries.size()); }
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override { return -1; }
    [[nodiscard]] std::span<const MenuItem> Variants(int index) const override;
    [[nodiscard]] int ActiveVariant(int index) const override;
    void Select(int index, int variant) override;
    void Opening(Vec2d pointer) override;
    [[nodiscard]] std::vector<std::string> Summary() const override { return Lines; }

private:
    enum class EntryKind : std::uint8_t
    {
        Choice,
        Asset,
        Action,
        Actions,
    };
    struct Entry
    {
        EntryKind Kind = EntryKind::Action;
        std::string Label = {};
        // A choice's values, or the actions in a fan.
        std::vector<std::string> Options = {};
        std::vector<std::string> OptionLabels = {};
        std::vector<std::string> OptionImages = {};
        // An asset fan's last option, which browses every asset of its kind.
        std::string BrowseAction = {};
        int Current = -1;
        // Where a choice is: the section list it is in, the section, its key.
        bool InDetails = false;
        std::size_t Section = 0;
        std::string Key = {};
        // A single action's id.
        std::string Action = {};
    };

    void Build(const WorkspaceSelectionFacet& selection);
    void Choose(const Entry& entry, const std::string& value);
    void Invoke(const std::string& action) const;

    Hooks Callbacks;
    const ActionCatalog& Catalog;
    std::vector<Entry> Entries;
    std::vector<std::vector<MenuItem>> Fans;
    std::vector<std::string> Lines;
};
