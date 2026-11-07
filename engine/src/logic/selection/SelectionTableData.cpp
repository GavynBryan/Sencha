#include <logic/selection/SelectionTableData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <logic/selection/SelectionTableDesc.h>

#include <memory>
#include <string>
#include <utility>

void RegisterSelectionTableData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kSelectionTableTypeName);
    type.CurrentVersion = 1;
    type.Compile = [](const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto desc = std::make_shared<SelectionTableDesc>();
        std::vector<std::string> errors;
        if (!CompileSelectionTableDesc(data, *desc, errors))
        {
            for (const std::string& error : errors)
                result.Error += result.Error.empty() ? error : "; " + error;
            return result;
        }
        result.Value = std::move(desc);
        return result;
    };
    if (!types.Register(std::move(type)))
        return;
    if (!schemas.Register(MakeSelectionTableSchema()))
        (void)types.Unregister(kSelectionTableTypeName);
}

void UnregisterSelectionTableData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kSelectionTableTypeName))
        (void)schemas.Unregister(kSelectionTableTypeName);
}
