#pragma once

class DataAssetTypeRegistry;
class DataSchemaRegistry;

// The `selection.table` structured-data subtype; its compiled value is a
// SelectionTableDesc.
void RegisterSelectionTableData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterSelectionTableData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
