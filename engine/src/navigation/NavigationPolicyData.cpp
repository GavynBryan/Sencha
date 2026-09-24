#include <navigation/NavigationPolicyData.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <memory>
#include <span>

namespace
{
    DataSchema MakeNavigationPolicySchema()
    {
        DataFieldSchema cost = MakeDataField(DataFieldKind::Float, "cost", "Cost",
                                             "Multiplier on distance walked across the area.");
        cost.Numeric.Minimum = 0.01;
        DataFieldSchema areaCost = MakeDataField(DataFieldKind::Record, {}, "Area cost");
        areaCost.Children = { MakeDataField(DataFieldKind::GameplayTag, "area", "Area"),
                              std::move(cost) };

        DataFieldSchema multiplier = MakeDataField(DataFieldKind::Float, "multiplier", "Multiplier",
                                                   "Scales the link's base cost.");
        multiplier.Numeric.Minimum = 0.0;
        multiplier.Default = 1.0;
        multiplier.Required = false;
        DataFieldSchema add = MakeDataField(DataFieldKind::Float, "add", "Add",
                                            "Added after the multiplier.");
        add.Numeric.Minimum = 0.0;
        add.Default = 0.0;
        add.Required = false;
        DataFieldSchema traversalCost = MakeDataField(DataFieldKind::Record, {}, "Traversal cost");
        traversalCost.Children = { MakeDataField(DataFieldKind::GameplayTag, "kind", "Traversal kind"),
                                   std::move(multiplier), std::move(add) };

        const auto listOf = [](std::string key, std::string display, std::string summary,
                               DataFieldSchema element)
        {
            DataFieldSchema list = MakeDataField(DataFieldKind::Array, std::move(key),
                                                 std::move(display), std::move(summary));
            list.Required = false;
            list.Children.push_back(std::move(element));
            return list;
        };

        DataSchema schema;
        schema.TypeName = std::string(kNavigationPolicySubtype);
        schema.DisplayName = "Navigation Policy";
        schema.Description = "How an agent weighs ground areas and link traversals.";
        schema.Root = MakeDataField(DataFieldKind::Record, {}, "Navigation policy");
        schema.Root.Children = {
            listOf("area_costs", "Area costs", "Cost multipliers per ground area.",
                   std::move(areaCost)),
            listOf("forbidden_areas", "Forbidden areas", "Areas the agent never enters.",
                   MakeDataField(DataFieldKind::GameplayTag, {}, "Area")),
            listOf("traversal_costs", "Traversal costs", "Cost adjustments per traversal kind.",
                   std::move(traversalCost)),
        };
        return schema;
    }

    std::span<const JsonValue> ListAt(const JsonValue& data, std::string_view key)
    {
        const JsonValue* list = data.Find(key);
        return list != nullptr ? std::span<const JsonValue>(list->AsArray())
                               : std::span<const JsonValue>();
    }
}

const DataSchema& NavigationPolicySchema()
{
    static const DataSchema schema = MakeNavigationPolicySchema();
    return schema;
}

DataAssetCompileResult CompileNavigationPolicy(const JsonValue& data)
{
    DataAssetCompileResult result;
    std::vector<DataValidationError> errors;
    if (!ValidateDataAgainstSchema(data, NavigationPolicySchema(), errors))
    {
        result.Error = FormatDataValidationErrors(errors);
        return result;
    }
    auto policy = std::make_shared<NavigationPolicyData>();
    for (const JsonValue& entry : ListAt(data, "area_costs"))
        policy->AreaCosts.push_back({ entry.Find("area")->AsString(),
                                      static_cast<float>(entry.NumberOr("cost", 1.0)) });
    for (const JsonValue& area : ListAt(data, "forbidden_areas"))
        policy->ForbiddenAreas.push_back(area.AsString());
    for (const JsonValue& entry : ListAt(data, "traversal_costs"))
        policy->TraversalCosts.push_back({ entry.Find("kind")->AsString(),
                                           static_cast<float>(entry.NumberOr("multiplier", 1.0)),
                                           static_cast<float>(entry.NumberOr("add", 0.0)) });
    result.Value = std::move(policy);
    return result;
}

NavQueryPolicy BindNavigationPolicy(const NavigationPolicyData& data,
                                    const GameplayTagRegistry& tags,
                                    std::vector<std::string>* unresolved)
{
    NavQueryPolicy policy;
    const auto bind = [&](const std::string& name)
    {
        const GameplayTagId id = tags.FindTag(name);
        if (!id.IsValid() && unresolved != nullptr)
            unresolved->push_back(name);
        return id;
    };
    for (const NavigationPolicyData::AreaCost& cost : data.AreaCosts)
        if (const GameplayTagId id = bind(cost.Area); id.IsValid())
            policy.AreaCosts.push_back({ id, cost.Cost });
    for (const std::string& name : data.ForbiddenAreas)
        if (const GameplayTagId id = bind(name); id.IsValid())
            policy.ForbiddenAreas.push_back(id);
    for (const NavigationPolicyData::TraversalCost& cost : data.TraversalCosts)
        if (const GameplayTagId id = bind(cost.Kind); id.IsValid())
            policy.TraversalCosts.push_back({ id, cost.Multiplier, cost.Add });
    return policy;
}

void RegisterNavigationPolicyData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kNavigationPolicySubtype);
    type.CurrentVersion = 1;
    type.Compile = CompileNavigationPolicy;
    if (!types.Register(std::move(type)))
        return;
    if (!schemas.Register(NavigationPolicySchema()))
        (void)types.Unregister(kNavigationPolicySubtype);
}
