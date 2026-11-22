#include <authored/AuthoredValueText.h>

#include <core/assets/AssetRef.h>
#include <ecs/EntityText.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <format>

std::string AuthoredValueText(const AuthoredValue& value, const GameplayTagRegistry* tags)
{
    const auto list = [&](std::string_view open, std::string_view close) {
        std::string text(open);
        for (std::size_t i = 0; i < value.Children().size(); ++i)
            text += (i == 0 ? "" : ", ") + AuthoredValueText(value.Children()[i], tags);
        return text + std::string(close);
    };
    bool flag = false;
    std::int64_t whole = 0;
    double number = 0.0;
    std::string_view text;
    AuthoredVectorValue vector;
    const AssetRef* asset = nullptr;
    GameplayTagId tag;
    EntityId entity;
    switch (value.Kind())
    {
    case AuthoredValueKind::None: return "(none)";
    case AuthoredValueKind::Bool: return value.TryGetBool(flag) && flag ? "true" : "false";
    case AuthoredValueKind::Int: return value.TryGetInt(whole) ? std::format("{}", whole) : "?";
    case AuthoredValueKind::Float: return value.TryGetFloat(number) ? std::format("{}", number) : "?";
    case AuthoredValueKind::String: return value.TryGetString(text) ? std::format("\"{}\"", text) : "?";
    case AuthoredValueKind::Enum: return value.TryGetEnum(text) ? std::string(text) : "?";
    case AuthoredValueKind::Vector:
    {
        if (!value.TryGetVector(vector))
            return "?";
        std::string out = "(";
        for (std::size_t i = 0; i < vector.Length; ++i)
            out += std::format("{}{}", i == 0 ? "" : ", ", vector.Components[i]);
        return out + ")";
    }
    case AuthoredValueKind::Record: return list("{", "}");
    case AuthoredValueKind::Array: return list("[", "]");
    case AuthoredValueKind::AssetRef: return value.TryGetAsset(asset) ? asset->Path : "?";
    case AuthoredValueKind::DataAssetRef: return value.TryGetDataAsset(asset) ? asset->Path : "?";
    case AuthoredValueKind::GameplayTag:
        if (value.TryGetTag(tag) && tags != nullptr)
            return std::string(tags->GetName(tag));
        return "?";
    case AuthoredValueKind::Entity:
    {
        if (!value.TryGetEntity(entity))
            return "?";
        std::string out = "entity ";
        AppendEntityText(out, entity);
        return out;
    }
    case AuthoredValueKind::PersistentEntity: return "(persistent entity)";
    }
    return "?";
}
