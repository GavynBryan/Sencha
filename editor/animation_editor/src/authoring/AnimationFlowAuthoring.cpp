#include "authoring/AnimationFlowEdits.h"
#include "authoring/AnimationPredicateAuthoring.h"

#include "data/DataDocument.h"
#include "data/DataPath.h"

#include <anim/AnimFlowData.h>
#include <core/metadata/DataSchema.h>

#include <algorithm>

namespace
{
// The section a key names or lies within, and the steps that follow it there.
struct SectionPlace
{
    std::size_t Section = 0;
    std::vector<DataPathStep> Rest;

    [[nodiscard]] bool IsMember(std::string_view key) const
    {
        return Rest.size() == 1 && !Rest.front().Index && Rest.front().Key == key;
    }
};

std::optional<SectionPlace> SectionOf(std::string_view key)
{
    std::optional<std::vector<DataPathStep>> steps = ParseDataPath(key);
    if (!steps || steps->size() < 2 || (*steps)[0].Key != "sections" || !(*steps)[1].Index)
        return std::nullopt;
    return SectionPlace{ .Section = *(*steps)[1].Index, .Rest = { steps->begin() + 2, steps->end() } };
}

std::vector<std::string> Tags(const JsonValue& data, std::size_t after = 0)
{
    std::vector<std::string> tags;
    const JsonValue* sections = data.Find("sections");
    if (sections == nullptr || !sections->IsArray())
        return tags;
    for (std::size_t s = after; s < sections->AsArray().size(); ++s)
        if (const JsonValue* tag = sections->AsArray()[s].Find("tag"); tag != nullptr && tag->IsString())
            tags.push_back(tag->AsString());
    return tags;
}

// The enum value a choice's label names, by the flow schema's loop field.
std::optional<std::string> LoopValue(std::string_view label, const DataSchema& schema)
{
    const auto sections = std::ranges::find(schema.Root.Children, "sections", &DataFieldSchema::Key);
    if (sections == schema.Root.Children.end() || sections->Children.size() != 1)
        return std::nullopt;
    const std::vector<DataFieldSchema>& members = sections->Children.front().Children;
    const auto loop = std::ranges::find(members, "loop", &DataFieldSchema::Key);
    if (loop == members.end())
        return std::nullopt;
    for (const DataEnumChoice& choice : loop->EnumChoices)
        if (choice.Value == label || choice.DisplayName == label)
            return choice.Value;
    return std::nullopt;
}

void RefineAll(std::vector<PropertyNode>& nodes, const JsonValue& data, const AnimationPredicateVocabulary& names)
{
    for (PropertyNode& node : nodes)
    {
        RefineAll(node.Children, data, names);
        if (node.Key == "$.data.cancel")
        {
            ChooseAmong(node, Tags(data));
            continue;
        }
        const std::optional<SectionPlace> place = SectionOf(node.Key);
        if (!place)
            continue;
        if (place->Rest.size() == 3 && place->Rest[0].Key == "branches" && place->Rest[1].Index
            && place->Rest[2].Key == "to")
            ChooseAmong(node, Tags(data, place->Section + 1));
        else if (place->IsMember("count_intent"))
        {
            std::vector<std::string> intents;
            for (const AnimationPredicateVocabulary::Intent& intent : names.Intents)
                intents.push_back(intent.Name);
            ChooseAmong(node, std::move(intents));
        }
        else if (place->IsMember("count_param"))
        {
            const JsonValue* section = FindDataValue(data, DataPathParent(node.Key).value_or(node.Key));
            const JsonValue* intent = section != nullptr ? section->Find("count_intent") : nullptr;
            ChooseAmong(node, AnimationIntentParams(names, intent != nullptr && intent->IsString()
                                                                ? std::string_view(intent->AsString())
                                                                : std::string_view{}));
        }
    }
}

class AnimationFlowAuthoring final : public IDataSubtypeAuthoring
{
public:
    [[nodiscard]] std::string_view Subtype() const override { return kAnimFlowType; }

    void Update(const DataDocument& document, DataDocumentSet& documents) override
    {
        Conditions.Update(document, documents);
    }

    void Refine(std::vector<PropertyNode>& nodes, const DataDocument& document, const DataSchema&) const override
    {
        const JsonValue* data = document.Data();
        if (data == nullptr)
            return;
        Conditions.Refine(nodes, *data);
        RefineAll(nodes, *data, Conditions.Names());
    }

    [[nodiscard]] DataSubtypeEdit Write(std::string_view key, std::string_view value, JsonValue& data,
                                        const DataSchema& schema) const override
    {
        const std::optional<SectionPlace> place = SectionOf(key);
        if (!place || !place->IsMember("loop"))
            return DataSubtypeEdit::NotMine;
        const std::optional<std::string> loop = LoopValue(value, schema);
        return DataSubtypeEditOf(loop && SetAnimFlowLoop(data, place->Section, *loop));
    }

    [[nodiscard]] DataSubtypeEdit Perform(std::string_view key, std::string_view verb, std::string_view choice,
                                          JsonValue& data) const override
    {
        if (const std::optional<SectionPlace> place = SectionOf(key); place && place->Rest.empty())
        {
            if (verb == "up")
                return DataSubtypeEditOf(place->Section > 0
                                         && MoveAnimFlowSection(data, place->Section, place->Section - 1));
            if (verb == "down")
                return DataSubtypeEditOf(MoveAnimFlowSection(data, place->Section, place->Section + 1));
            if (verb == "delete")
                return DataSubtypeEditOf(RemoveAnimFlowSection(data, place->Section));
        }
        return Conditions.Perform(key, verb, choice, data);
    }

private:
    AnimationConditionAuthoring Conditions{ { "when", "while" } };
};
} // namespace

std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationFlowAuthoring()
{
    return std::make_unique<AnimationFlowAuthoring>();
}
