#include "movement/MovementProfileAuthoring.h"

#include "movement/MovementLayerSummary.h"
#include "movement/MovementPreviewProperties.h"
#include "movement/MovementResolvePreview.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
#include "data/DataPath.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <format>

namespace
{
constexpr std::string_view kSubtype = "movement.profile";

// "$.data.layers[i].when.tags.all[k]": an element of a rule's tag condition.
bool IsTagElement(std::string_view key)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(key);
    if (!steps.has_value() || steps->size() < 5 || !steps->back().Index.has_value())
        return false;
    const std::string& list = (*steps)[steps->size() - 2].Key;
    return (*steps)[steps->size() - 3].Key == "tags" && (list == "all" || list == "any" || list == "none");
}

void MarkTagElements(std::vector<PropertyNode>& nodes)
{
    for (PropertyNode& node : nodes)
    {
        if (IsTagElement(node.Key))
            node.Descriptor.Kind = PropertyKind::Asset;
        MarkTagElements(node.Children);
    }
}

class MovementProfileAuthoring final : public IDataSubtypeAuthoring
{
public:
    [[nodiscard]] std::string_view Subtype() const override { return kSubtype; }

    void Update(const DataDocument& document, DataDocumentSet& documents) override
    {
        Preview.Update(document, documents.Store().Types(), *documents.Store().PathsById());
        if (const DataSchema* schema = documents.Store().SchemaOf(document))
        {
            Labels = MovementCoefficientLabels(*schema);
            Resolve.Label(Labels);
        }
    }

    void Refine(std::vector<PropertyNode>& nodes, const DataDocument&, const DataSchema&) const override
    {
        MarkTagElements(nodes);
        const CompiledMovementProfile* compiled = Preview.Compiled();
        if (compiled == nullptr)
            return;
        const MovementResolveResult resolved = Preview.IsBound() ? Preview.Resolve() : MovementResolveResult{};
        for (std::size_t index = 0; index < compiled->Layers.size(); ++index)
        {
            std::optional<PropertyPlace> card = FindPropertyNode(nodes, std::format("$.data.layers[{}]", index));
            if (!card.has_value())
                continue;
            const MovementProfileLayer& layer = compiled->Layers[index];
            PropertyNode& node = card->Node();
            node.Descriptor.Label = MovementLayerName(layer);
            const std::string changes = DescribeMovementPatches(layer, Labels);
            node.Descriptor.Tooltip = DescribeMovementCondition(layer.When) + (changes.empty() ? "" : ": " + changes);
            // Whether the rule is live in the dialled situation, so the author
            // sees the ruleset react rather than infers it.
            if (index < resolved.Trace.size())
            {
                const MovementLayerTrace& trace = resolved.Trace[index];
                node.Children.insert(node.Children.begin(),
                                     NoteProperty(node.Key + "!live", "Now",
                                                  trace.Matched ? std::string("Active")
                                                  : trace.Failure.empty() ? std::string("Off")
                                                                          : "Off: " + trace.Failure));
            }
        }
    }

    [[nodiscard]] std::optional<std::vector<std::string>> Candidates(std::string_view key, const JsonValue&) const override
    {
        if (!IsTagElement(key))
            return std::nullopt;
        return Preview.KnownTags();
    }

    [[nodiscard]] std::vector<PropertySection> Sections(const DataDocument&) override
    {
        return { { .Heading = "Resolve", .Source = &Resolve }, { .Heading = "Response", .Source = &Response } };
    }

private:
    MovementResolvePreview Preview;
    MovementResolveProperties Resolve{ Preview };
    MovementResponseProperties Response{ Preview };
    std::vector<MovementCoefficientLabel> Labels;
};
} // namespace

std::string_view MovementProfileSubtype()
{
    return kSubtype;
}

std::unique_ptr<IDataSubtypeAuthoring> CreateMovementProfileAuthoring()
{
    return std::make_unique<MovementProfileAuthoring>();
}
