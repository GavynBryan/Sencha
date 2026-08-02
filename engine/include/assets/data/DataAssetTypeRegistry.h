#pragma once

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct DataAssetCompileResult
{
    std::shared_ptr<const void> Value;
    std::vector<AssetRef> Dependencies;
    std::string Error;

    [[nodiscard]] bool IsValid() const
    {
        return Value != nullptr && Error.empty();
    }
};

using CompileDataAssetFn =
    std::function<DataAssetCompileResult(const JsonValue& data)>;
using DataAssetResidentQueryFn = std::function<bool(std::string_view typeName)>;

struct DataAssetTypeRegistration
{
    std::string Name;
    uint32_t CurrentVersion = 1;
    CompileDataAssetFn Compile;
};

class DataAssetTypeRegistry
{
public:
    [[nodiscard]] bool Register(DataAssetTypeRegistration registration);
    [[nodiscard]] bool Unregister(std::string_view name);

    [[nodiscard]] const DataAssetTypeRegistration* Find(std::string_view name) const;
    [[nodiscard]] std::span<const DataAssetTypeRegistration> Entries() const;

    void SetResidentQuery(DataAssetResidentQueryFn query)
    {
        HasResidentValues = std::move(query);
    }

private:
    std::vector<DataAssetTypeRegistration> Registrations;
    DataAssetResidentQueryFn HasResidentValues;
};
