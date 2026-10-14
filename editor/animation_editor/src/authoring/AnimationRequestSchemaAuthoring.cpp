#include "authoring/AnimationPredicateAuthoring.h"

#include "data/DataDocument.h"
#include "data/DataPath.h"

#include <anim/AnimRequestSchema.h>
#include <anim/AnimTypes.h>

#include <algorithm>
#include <format>

namespace
{
// An intent carries at most kAnimRequestParams parameters: the sheet counts
// them against that budget and stops offering more.
void Budget(std::vector<PropertyNode>& nodes, const JsonValue& data)
{
    for (PropertyNode& node : nodes)
    {
        Budget(node.Children, data);
        if (!node.Key.starts_with("$.data.intents[") || !node.Key.ends_with("].params"))
            continue;
        const JsonValue* params = FindDataValue(data, node.Key);
        const std::size_t count = params != nullptr && params->IsArray() ? params->AsArray().size() : 0;
        node.Descriptor.Label = std::format("Parameters, {} of {}", count, kAnimRequestParams);
        if (count >= kAnimRequestParams)
            std::erase_if(node.Descriptor.Verbs, [](const PropertyVerb& verb) { return verb.Id == "add"; });
    }
}

class AnimationRequestSchemaAuthoring final : public IDataSubtypeAuthoring
{
public:
    [[nodiscard]] std::string_view Subtype() const override { return kAnimRequestSchemaType; }

    void Refine(std::vector<PropertyNode>& nodes, const DataDocument& document, const DataSchema&) const override
    {
        if (const JsonValue* data = document.Data())
            Budget(nodes, *data);
    }
};
} // namespace

std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationRequestSchemaAuthoring()
{
    return std::make_unique<AnimationRequestSchemaAuthoring>();
}
