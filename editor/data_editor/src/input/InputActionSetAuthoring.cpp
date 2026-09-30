#include "input/InputActionSetAuthoring.h"

#include "JsonObjectEdit.h"

#include "data/DataDocument.h"
#include "data/DataPath.h"
#include "properties/PropertyTree.h"

#include <core/metadata/DataSchema.h>

#include <cctype>
#include <format>

namespace
{
constexpr std::string_view kSubtype = "input.actions";

// The schema owns the designer wording for every enum, so the titles read it
// back rather than restating it.
const DataFieldSchema* ActionRecordSchema(const DataSchema& schema)
{
    const DataFieldSchema* actions = FindChild(schema.Root, "actions");
    return actions != nullptr && !actions->Children.empty() ? &actions->Children.front() : nullptr;
}

std::string EnumDisplayName(const DataFieldSchema* record, std::string_view key, std::string_view value)
{
    const DataFieldSchema* field = record != nullptr ? FindChild(*record, key) : nullptr;
    if (field != nullptr)
        for (const DataEnumChoice& choice : field->EnumChoices)
            if (choice.Value == value)
                return choice.DisplayName.empty() ? choice.Value : choice.DisplayName;
    return std::string(value);
}

// The action's value shape, defaulted the way the compiler defaults it.
std::string ActionTypeOf(const JsonValue& action)
{
    const std::string type = ReadMemberString(action, "type");
    return type.empty() ? std::string("digital") : type;
}

// "move - Plane", "jump - Button, while held", "pause - Button, presentation".
std::string ActionCardTitle(const JsonValue& action, const DataFieldSchema* record, std::size_t index)
{
    const std::string name = ReadMemberString(action, "name");
    std::string title = name.empty() ? std::format("Action {}", index + 1) : name;
    const std::string type = ActionTypeOf(action);
    title += " - " + EnumDisplayName(record, "type", type);
    // Only the modes that differ from the default earn room in the title: the
    // question a card answers at a glance is which button behaves unusually.
    if (const std::string fire = ReadMemberString(action, "fire"); type == "digital" && !fire.empty() && fire != "pressed")
    {
        std::string label = EnumDisplayName(record, "fire", fire);
        for (char& c : label)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        title += ", " + label;
    }
    if (ReadMemberString(action, "scope") == "presentation")
        title += ", presentation";
    return title;
}

// "$.data.actions[3].type" -> 3.
std::optional<std::size_t> ActionTypeKey(std::string_view key)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(key);
    if (!steps.has_value() || steps->size() != 3 || (*steps)[0].Key != "actions" || !(*steps)[1].Index.has_value()
        || (*steps)[2].Key != "type")
        return std::nullopt;
    return (*steps)[1].Index;
}

class InputActionSetAuthoring final : public IDataSubtypeAuthoring
{
public:
    [[nodiscard]] std::string_view Subtype() const override { return kSubtype; }

    void Refine(std::vector<PropertyNode>& nodes, const DataDocument& document, const DataSchema& schema) const override
    {
        const JsonValue* data = document.Data();
        const JsonValue* actions = data != nullptr ? FindMember(*data, "actions") : nullptr;
        if (actions == nullptr || !actions->IsArray())
            return;
        const DataFieldSchema* record = ActionRecordSchema(schema);
        for (std::size_t index = 0; index < actions->AsArray().size(); ++index)
        {
            const std::string path = std::format("$.data.actions[{}]", index);
            const JsonValue& action = actions->AsArray()[index];
            if (std::optional<PropertyPlace> card = FindPropertyNode(nodes, path))
                card->Node().Descriptor.Label = ActionCardTitle(action, record, index);
            // Only a button has a moment to fire on.
            if (ActionTypeOf(action) != "digital")
                if (std::optional<PropertyPlace> fire = FindPropertyNode(nodes, path + ".fire"))
                    fire->Siblings->erase(fire->Siblings->begin() + static_cast<std::ptrdiff_t>(fire->Index));
        }
    }

    [[nodiscard]] std::optional<bool> Write(std::string_view key, std::string_view value, JsonValue& data,
                                            const DataSchema& schema) const override
    {
        const std::optional<std::size_t> index = ActionTypeKey(key);
        if (!index.has_value())
            return std::nullopt;
        JsonValue* action = FindDataValue(data, std::format("$.data.actions[{}]", *index));
        const DataFieldSchema* record = ActionRecordSchema(schema);
        const DataFieldSchema* type = record != nullptr ? FindChild(*record, "type") : nullptr;
        if (action == nullptr || type == nullptr)
            return false;
        for (const DataEnumChoice& choice : type->EnumChoices)
        {
            if (value != choice.Value && value != (choice.DisplayName.empty() ? choice.Value : choice.DisplayName))
                continue;
            SetMember(*action, "type", JsonValue(choice.Value));
            // Turning a button into an axis takes the firing mode with it, rather
            // than leaving a member nothing shows and the compiler rejects.
            if (choice.Value != "digital")
                EraseMember(*action, "fire");
            return true;
        }
        return false;
    }
};
} // namespace

std::string_view InputActionSetSubtype()
{
    return kSubtype;
}

std::unique_ptr<IDataSubtypeAuthoring> CreateInputActionSetAuthoring()
{
    return std::make_unique<InputActionSetAuthoring>();
}
