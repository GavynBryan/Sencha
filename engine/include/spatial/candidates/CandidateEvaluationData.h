#pragma once

#include <string_view>

class CandidateCatalogs;
class DataAssetTypeRegistry;
class DataSchemaRegistry;

// The `candidates.evaluation` structured-data subtype. Its compiled value is a
// CandidateEvaluationDesc, checked against `catalogs` in the load stage, so a
// host registers it only once it owns the catalogs it validates against.
inline constexpr std::string_view kCandidateEvaluationTypeName = "candidates.evaluation";

void RegisterCandidateEvaluationData(DataAssetTypeRegistry& types,
                                     DataSchemaRegistry& schemas,
                                     const CandidateCatalogs& catalogs);
void UnregisterCandidateEvaluationData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
