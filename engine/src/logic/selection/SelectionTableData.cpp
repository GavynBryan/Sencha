#include <logic/selection/SelectionTableData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <logic/selection/SelectionTableDesc.h>

#include <string>
#include <utility>

void RegisterSelectionTableData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kSelectionTableTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompileCollectingErrors<SelectionTableDesc>(CompileSelectionTableDesc);
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
