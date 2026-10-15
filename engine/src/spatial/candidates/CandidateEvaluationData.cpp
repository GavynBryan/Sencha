#include <spatial/candidates/CandidateEvaluationData.h>

#include <assets/data/DataAssetTypeRegistry.h>
#include <spatial/candidates/CandidateEvaluationDesc.h>
#include <core/metadata/DataSchema.h>

#include <memory>
#include <string>
#include <utility>

void RegisterCandidateEvaluationData(DataAssetTypeRegistry& types,
                                     DataSchemaRegistry& schemas,
                                     const CandidateCatalogs& catalogs)
{
    DataAssetTypeRegistration type;
    type.Name = std::string(kCandidateEvaluationTypeName);
    type.CurrentVersion = 1;
    type.Compile = [&catalogs](const JsonValue& data)
    {
        DataAssetCompileResult result;
        auto desc = std::make_shared<CandidateEvaluationDesc>();
        std::vector<std::string> errors;
        if (!CompileCandidateEvaluationDesc(data, catalogs, *desc, errors))
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

    if (!schemas.Register(MakeCandidateEvaluationSchema()))
        (void)types.Unregister(kCandidateEvaluationTypeName);
}

void UnregisterCandidateEvaluationData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (types.Unregister(kCandidateEvaluationTypeName))
        (void)schemas.Unregister(kCandidateEvaluationTypeName);
}
