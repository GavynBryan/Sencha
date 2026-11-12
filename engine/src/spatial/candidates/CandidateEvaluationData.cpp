#include <spatial/candidates/CandidateEvaluationData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <spatial/candidates/CandidateEvaluationDesc.h>
#include <core/metadata/DataSchema.h>

#include <string>
#include <utility>

void RegisterCandidateEvaluationData(DataAssetTypeRegistry& types,
                                     DataSchemaRegistry& schemas,
                                     const CandidateCatalogs& catalogs)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kCandidateEvaluationTypeName);
    type.CurrentVersion = 1;
    type.Compile = CompileCollectingErrors<CandidateEvaluationDesc>(
        [&catalogs](const JsonValue& data, CandidateEvaluationDesc& out, std::vector<std::string>& errors)
        { return CompileCandidateEvaluationDesc(data, catalogs, out, errors); });
    if (!types.Register(std::move(type)))
        return;

    if (!schemas.Register(MakeCandidateEvaluationSchema()))
        (void)types.Unregister(kCandidateEvaluationTypeName);
}

void UnregisterCandidateEvaluationData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kCandidateEvaluationTypeName))
        (void)schemas.Unregister(kCandidateEvaluationTypeName);
}
