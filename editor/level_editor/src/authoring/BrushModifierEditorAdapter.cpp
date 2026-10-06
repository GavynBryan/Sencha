#include "BrushModifierEditorAdapter.h"

#include "authoring/BrushModifierRows.h"
#include "brush/BrushEvaluation.h"
#include "brush/BrushModifier.h"
#include "commands/CommandStack.h"
#include "document/BrushComponents.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/SettingProperties.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace
{
    // "m2.offset" -> modifier 2, field "offset"; "m2" -> modifier 2, no field.
    struct RowKey
    {
        std::size_t Index = 0;
        std::string_view Field;
    };

    std::optional<RowKey> ParseRowKey(std::string_view key)
    {
        if (!key.starts_with('m'))
            return std::nullopt;
        key.remove_prefix(1);
        const std::size_t dot = key.find('.');
        const std::string_view digits = key.substr(0, dot);
        RowKey row;
        if (digits.empty() || std::from_chars(digits.data(), digits.data() + digits.size(), row.Index).ec != std::errc{})
            return std::nullopt;
        if (dot != std::string_view::npos)
            row.Field = key.substr(dot + 1);
        return row;
    }

    // What each registered kind is called and does, in kind order: the menu a
    // modifier is added from.
    std::vector<ModifierKindText> KindTexts()
    {
        std::vector<ModifierKindText> texts;
        for (const BrushModifierKindInfo& kind : BrushModifierKinds())
            texts.push_back(std::visit([](const auto& params) { return ModifierText(params); },
                                       kind.MakeDefault().Params));
        return texts;
    }

    std::vector<PropertyNode> FieldsOf(const std::string& prefix, const BrushModifier& modifier)
    {
        std::vector<PropertyNode> fields{ ToggleProperty(prefix + ".enabled", "Enabled", modifier.Enabled) };
        std::vector<PropertyNode> own =
            std::visit([&](const auto& params) { return ModifierFields(prefix, params); }, modifier.Params);
        fields.insert(fields.end(), std::make_move_iterator(own.begin()), std::make_move_iterator(own.end()));
        return fields;
    }

    // `value` written into one field of `modifier`; false when it is not a
    // value that field can hold.
    bool SetField(BrushModifier& modifier, std::string_view field, std::string_view value)
    {
        if (field == "enabled")
        {
            const std::optional<bool> on = ParseToggle(value);
            if (on.has_value())
                modifier.Enabled = *on;
            return on.has_value();
        }
        return std::visit([&](auto& params) { return SetModifierField(params, field, value); }, modifier.Params);
    }

    class BrushModifierEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        [[nodiscard]] ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<BrushComponent>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::span<const std::string_view> Actions() const override
        {
            static constexpr std::array<std::string_view, 3> kActions = { "brush.bake", "brush.export",
                                                                         "brush.normals" };
            return kActions;
        }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const BrushComponent* brush = context.Scene.TryGetBrush(context.Entity);
            const BrushModifierStack* stack = context.Scene.TryGetBrushModifiers(context.Entity);
            if (brush == nullptr || stack == nullptr)
                return {};

            std::optional<std::uint32_t> failed;
            bool overHardLimit = false;
            if (const BrushEvaluated* evaluated = context.Scene.TryGetBrushPieces(context.Entity);
                evaluated != nullptr && evaluated->Status != BrushEvaluationStatus::Ok)
            {
                failed = evaluated->FailedModifier;
                // Projected from the stack's factors, not from a second
                // evaluation under the cook policy.
                overHardLimit = BrushProjectedPieceCount(*stack) > BrushEvaluationPolicy::kHardPieceLimit;
            }

            PropertyNode modifiers;
            modifiers.Key = "stack";
            modifiers.Descriptor.Kind = PropertyKind::Group;
            modifiers.Descriptor.Label = "Modifiers";
            PropertyVerb add{ .Id = "add", .Label = "Add Modifier" };
            for (const ModifierKindText& kind : KindTexts())
            {
                add.Choices.emplace_back(kind.Label);
                add.ChoiceNotes.emplace_back(kind.Description);
            }
            modifiers.Descriptor.Verbs.push_back(std::move(add));
            for (std::size_t i = 0; i < stack->size(); ++i)
            {
                const BrushModifier& modifier = (*stack)[i];
                const std::string prefix = "m" + std::to_string(i);
                PropertyNode row;
                row.Key = prefix;
                row.Descriptor.Kind = PropertyKind::Group;
                row.Descriptor.Label = std::visit(
                    [](const auto& params) {
                        return std::string(ModifierText(params).Label) + "  " + ModifierSummary(params);
                    },
                    modifier.Params);
                if (i > 0)
                    row.Descriptor.Verbs.push_back({ .Id = "up", .Label = "Up" });
                if (i + 1 < stack->size())
                    row.Descriptor.Verbs.push_back({ .Id = "down", .Label = "Down" });
                row.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove", .Removes = true });
                row.Children = FieldsOf(prefix, modifier);
                if (failed == i)
                    row.Children.push_back(NoteProperty(prefix + ".failed", "Not shown",
                                                overHardLimit ? "Exceeds the piece limit: the level will not cook."
                                                              : "Exceeds the preview piece budget."));
                modifiers.Children.push_back(std::move(row));
            }
            return { std::move(modifiers) };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            const std::optional<RowKey> row = ParseRowKey(key);
            if (stored == nullptr || !row.has_value() || row->Index >= stored->size())
                return PropertyEditResult::Gone;
            BrushModifierStack edited = *stored;
            if (!SetField(edited[row->Index], row->Field, value))
                return PropertyEditResult::Refused;
            return Commit(context, *stored, std::move(edited));
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            const BrushModifierStack* stored = context.Scene.TryGetBrushModifiers(context.Entity);
            if (stored == nullptr)
                return PropertyEditResult::Gone;
            BrushModifierStack edited = *stored;
            if (key == "stack" && verb == "add")
            {
                const std::vector<ModifierKindText> kinds = KindTexts();
                const auto kind = std::ranges::find(kinds, choice, &ModifierKindText::Label);
                if (kind == kinds.end())
                    return PropertyEditResult::Refused;
                edited.push_back(BrushModifierKinds()[static_cast<std::size_t>(kind - kinds.begin())].MakeDefault());
                return Commit(context, *stored, std::move(edited));
            }
            const std::optional<RowKey> row = ParseRowKey(key);
            if (!row.has_value() || row->Index >= edited.size())
                return PropertyEditResult::Gone;
            const auto at = static_cast<std::ptrdiff_t>(row->Index);
            if (verb == "remove")
                edited.erase(edited.begin() + at);
            else if (verb == "up" && row->Index > 0)
                std::swap(edited[row->Index], edited[row->Index - 1]);
            else if (verb == "down" && row->Index + 1 < edited.size())
                std::swap(edited[row->Index], edited[row->Index + 1]);
            else
                return PropertyEditResult::Refused;
            return Commit(context, *stored, std::move(edited));
        }

    private:
        static PropertyEditResult Commit(EditorComponentInspectorContext& context, BrushModifierStack before,
                                         BrushModifierStack after)
        {
            context.Commands.Execute(MakeEditBrushModifiersCommand(
                context.Entity, std::move(before), std::move(after), context.Scene, context.Document));
            return PropertyEditResult::Applied;
        }
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeBrushModifierEditorAdapter()
{
    return std::make_unique<BrushModifierEditorAdapter>();
}
