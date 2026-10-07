#include <core/assets/AssetRefCodec.h>

#include <core/assets/AssetPath.h>
#include <core/json/JsonValue.h>
#include <core/serialization/Archive.h>

#include <algorithm>
#include <utility>

namespace
{
constexpr std::string_view kIdKey = "id";
constexpr std::string_view kPathKey = "path";

// Both parts as read, before either is trusted.
std::optional<AssetRef> Accept(std::string_view idText, bool hasId, std::string path)
{
    AssetRef ref;
    if (hasId)
    {
        const std::optional<AssetId> id = AssetIdFromString(idText);
        if (!id.has_value())
            return std::nullopt;
        ref.Id = *id;
    }
    if (!path.empty() && !IsValidAssetPath(path))
        return std::nullopt;
    ref.Path = std::move(path);
    if (!ref.Id.IsValid() && ref.Path.empty())
        return std::nullopt;
    return ref;
}
} // namespace

bool ReadAssetRef(IReadArchive& archive, std::string_view key, AssetRef& out)
{
    std::optional<AssetRef> read;
    if (archive.IsString(key))
    {
        std::string path;
        archive.Field(key, path);
        if (archive.Ok() && !path.empty())
            read = Accept({}, false, std::move(path));
    }
    else if (archive.IsObject(key))
    {
        archive.BeginObject(key);
        std::string idText;
        std::string path;
        const bool hasId = archive.HasField(kIdKey);
        if (hasId)
            archive.Field(kIdKey, idText);
        if (archive.HasField(kPathKey))
            archive.Field(kPathKey, path);
        archive.End();
        if (archive.Ok())
            read = Accept(idText, hasId, std::move(path));
    }
    if (!read.has_value())
    {
        archive.MarkInvalidField(key);
        return false;
    }
    out.Id = read->Id;
    out.Path = std::move(read->Path);
    return true;
}

void WriteAssetRef(IWriteArchive& archive, std::string_view key, const AssetRef& ref)
{
    if (!ref.Id.IsValid())
    {
        archive.Field(key, std::string_view(ref.Path));
        return;
    }
    archive.BeginObject(key);
    archive.Field(kIdKey, std::string_view(AssetIdToString(ref.Id)));
    if (!ref.Path.empty())
        archive.Field(kPathKey, std::string_view(ref.Path));
    archive.End();
}

std::optional<AssetRef> AssetRefFromJson(const JsonValue& value)
{
    if (value.IsString())
        return value.AsString().empty() ? std::nullopt : Accept({}, false, value.AsString());
    if (!value.IsObject() || std::ranges::any_of(value.AsObject(), [](const auto& member) {
            return member.first != kIdKey && member.first != kPathKey;
        }))
        return std::nullopt;
    const JsonValue* id = value.Find(kIdKey);
    const JsonValue* path = value.Find(kPathKey);
    if ((id != nullptr && !id->IsString()) || (path != nullptr && !path->IsString()))
        return std::nullopt;
    return Accept(id != nullptr ? std::string_view(id->AsString()) : std::string_view{}, id != nullptr,
                  path != nullptr ? path->AsString() : std::string{});
}

namespace
{
// An object found while walking unknown JSON is a reference only in the form a
// writer produces for one: naming its id.
std::optional<AssetRef> IdForm(const JsonValue& value)
{
    return value.IsObject() && value.Find(kIdKey) != nullptr ? AssetRefFromJson(value) : std::nullopt;
}

void Collect(const JsonValue& value, std::vector<AssetRef>& out)
{
    if (value.IsString())
    {
        if (IsValidAssetPath(value.AsString()))
            out.push_back(AssetRef{ .Path = value.AsString() });
    }
    else if (value.IsObject())
    {
        if (std::optional<AssetRef> ref = IdForm(value))
        {
            out.push_back(std::move(*ref));
            return;
        }
        for (const auto& [key, member] : value.AsObject())
            Collect(member, out);
    }
    else if (value.IsArray())
        for (const JsonValue& element : value.AsArray())
            Collect(element, out);
}
} // namespace

std::vector<AssetRef> CollectAssetRefs(const JsonValue& root)
{
    std::vector<AssetRef> refs;
    Collect(root, refs);
    return refs;
}

JsonValue ResolveAssetRefs(const JsonValue& root, const AssetPathsById& current)
{
    if (root.IsObject())
    {
        if (std::optional<AssetRef> ref = IdForm(root))
        {
            const auto found = ref->Id.IsValid() ? current.find(ref->Id) : current.end();
            return JsonValue(found != current.end() ? found->second : ref->Path);
        }
        JsonValue::Object members;
        members.reserve(root.Size());
        for (const auto& [key, member] : root.AsObject())
            members.emplace_back(key, ResolveAssetRefs(member, current));
        return JsonValue(std::move(members));
    }
    if (root.IsArray())
    {
        JsonValue::Array elements;
        elements.reserve(root.Size());
        for (const JsonValue& element : root.AsArray())
            elements.push_back(ResolveAssetRefs(element, current));
        return JsonValue(std::move(elements));
    }
    return root;
}

JsonValue AssetRefToJson(const AssetRef& ref)
{
    if (!ref.Id.IsValid())
        return JsonValue(ref.Path);
    JsonValue::Object object{ { std::string(kIdKey), JsonValue(AssetIdToString(ref.Id)) } };
    if (!ref.Path.empty())
        object.emplace_back(std::string(kPathKey), JsonValue(ref.Path));
    return JsonValue(std::move(object));
}
