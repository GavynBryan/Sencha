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

// Turns one subtype's authored `data` object into its compiled runtime value.
//
// Called only after the subtype's registered schema has accepted the document,
// so a compiler may rely on the shapes that schema guarantees. A subtype with
// no registered schema guarantees nothing, and its compiler sees whatever the
// file contained. Everything a compiler does not validate through its schema
// it must therefore check itself, including value kinds: these run inside the
// editor as well as the runtime, and a compiler that throws takes its host with
// it.
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
