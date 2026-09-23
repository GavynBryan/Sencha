#include <authored/AuthoredValue.h>

#include <cmath>
#include <utility>

namespace
{
    // What an out-of-range read sees. One object rather than a per-call
    // temporary, so At can return a reference.
    const AuthoredValue& EmptyValue()
    {
        static const AuthoredValue empty;
        return empty;
    }
}

AuthoredValue AuthoredValue::Bool(bool value)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Bool;
    result.Value_ = value;
    return result;
}

AuthoredValue AuthoredValue::Int(std::int64_t value)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Int;
    result.Value_ = value;
    return result;
}

AuthoredValue AuthoredValue::Float(double value)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Float;
    result.Value_ = value;
    return result;
}

AuthoredValue AuthoredValue::String(std::string value)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::String;
    result.Value_ = std::move(value);
    return result;
}

AuthoredValue AuthoredValue::Enum(std::string choice)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Enum;
    result.Value_ = std::move(choice);
    return result;
}

AuthoredValue AuthoredValue::Vector(AuthoredVectorValue value)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Vector;
    result.Value_ = value;
    return result;
}

AuthoredValue AuthoredValue::Record(std::vector<AuthoredValue> members)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Record;
    result.Children_ = std::move(members);
    return result;
}

AuthoredValue AuthoredValue::Array(std::vector<AuthoredValue> elements)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Array;
    result.Children_ = std::move(elements);
    return result;
}

AuthoredValue AuthoredValue::Asset(AssetRef reference)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::AssetRef;
    result.Value_ = std::move(reference);
    return result;
}

AuthoredValue AuthoredValue::DataAsset(AssetRef reference)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::DataAssetRef;
    result.Value_ = std::move(reference);
    return result;
}

AuthoredValue AuthoredValue::Tag(GameplayTagId tag)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::GameplayTag;
    result.Value_ = tag;
    return result;
}

AuthoredValue AuthoredValue::Entity(EntityId entity)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::Entity;
    result.Value_ = entity;
    return result;
}

AuthoredValue AuthoredValue::PersistentEntity(PersistentEntityId identity)
{
    AuthoredValue result;
    result.Kind_ = AuthoredValueKind::PersistentEntity;
    result.Value_ = identity;
    return result;
}

bool AuthoredValue::TryGetBool(bool& out) const
{
    if (Kind_ != AuthoredValueKind::Bool)
        return false;
    out = std::get<bool>(Value_);
    return true;
}

bool AuthoredValue::TryGetInt(std::int64_t& out) const
{
    if (Kind_ != AuthoredValueKind::Int)
        return false;
    out = std::get<std::int64_t>(Value_);
    return true;
}

bool AuthoredValue::TryGetFloat(double& out) const
{
    if (Kind_ != AuthoredValueKind::Float)
        return false;
    out = std::get<double>(Value_);
    return true;
}

bool AuthoredValue::TryGetString(std::string_view& out) const
{
    if (Kind_ != AuthoredValueKind::String)
        return false;
    out = std::get<std::string>(Value_);
    return true;
}

bool AuthoredValue::TryGetEnum(std::string_view& out) const
{
    if (Kind_ != AuthoredValueKind::Enum)
        return false;
    out = std::get<std::string>(Value_);
    return true;
}

bool AuthoredValue::TryGetVector(AuthoredVectorValue& out) const
{
    if (Kind_ != AuthoredValueKind::Vector)
        return false;
    out = std::get<AuthoredVectorValue>(Value_);
    return true;
}

bool AuthoredValue::TryGetAsset(const AssetRef*& out) const
{
    if (Kind_ != AuthoredValueKind::AssetRef)
        return false;
    out = &std::get<AssetRef>(Value_);
    return true;
}

bool AuthoredValue::TryGetDataAsset(const AssetRef*& out) const
{
    if (Kind_ != AuthoredValueKind::DataAssetRef)
        return false;
    out = &std::get<AssetRef>(Value_);
    return true;
}

bool AuthoredValue::TryGetTag(GameplayTagId& out) const
{
    if (Kind_ != AuthoredValueKind::GameplayTag)
        return false;
    out = std::get<GameplayTagId>(Value_);
    return true;
}

bool AuthoredValue::TryGetEntity(EntityId& out) const
{
    if (Kind_ != AuthoredValueKind::Entity)
        return false;
    out = std::get<EntityId>(Value_);
    return true;
}

namespace
{
    [[nodiscard]] bool WithinRange(double value, const DataFieldSchema& field)
    {
        if (field.Numeric.Minimum && value < *field.Numeric.Minimum)
            return false;
        if (field.Numeric.Maximum && value > *field.Numeric.Maximum)
            return false;
        return true;
    }
}

bool AuthoredValue::TryGetPersistentEntity(PersistentEntityId& out) const
{
    if (Kind_ != AuthoredValueKind::PersistentEntity)
        return false;
    out = std::get<PersistentEntityId>(Value_);
    return true;
}

bool AuthoredValueSatisfiesField(const AuthoredValue& value, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        return value.Kind() == AuthoredValueKind::Bool;
    case DataFieldKind::Int:
    {
        std::int64_t whole = 0;
        return value.TryGetInt(whole) && WithinRange(static_cast<double>(whole), field);
    }
    case DataFieldKind::Float:
    {
        double number = 0.0;
        return value.TryGetFloat(number) && std::isfinite(number) && WithinRange(number, field);
    }
    case DataFieldKind::String:
        return value.Kind() == AuthoredValueKind::String;
    case DataFieldKind::Enum:
    {
        std::string_view choice;
        if (!value.TryGetEnum(choice))
            return false;
        for (const DataEnumChoice& declared : field.EnumChoices)
        {
            if (declared.Value == choice)
                return true;
        }
        return false;
    }
    case DataFieldKind::Vector:
    {
        AuthoredVectorValue vector;
        if (!value.TryGetVector(vector) || vector.Length != field.VectorLength)
            return false;
        for (std::uint8_t index = 0; index < vector.Length; ++index)
        {
            if (!std::isfinite(vector.Components[index])
                || !WithinRange(vector.Components[index], field))
            {
                return false;
            }
        }
        return true;
    }
    case DataFieldKind::Record:
    {
        if (value.Kind() != AuthoredValueKind::Record
            || value.Children().size() != field.Children.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < field.Children.size(); ++index)
        {
            if (!AuthoredValueSatisfiesField(value.Children()[index], field.Children[index]))
                return false;
        }
        return true;
    }
    case DataFieldKind::Array:
    {
        if (value.Kind() != AuthoredValueKind::Array || field.Children.size() != 1)
            return false;
        for (const AuthoredValue& element : value.Children())
        {
            if (!AuthoredValueSatisfiesField(element, field.Children.front()))
                return false;
        }
        return true;
    }
    case DataFieldKind::Optional:
        // Absent is a legal value for an optional; anything else has to be the
        // element it wraps.
        if (value.IsNone())
            return true;
        return field.Children.size() == 1
            && AuthoredValueSatisfiesField(value, field.Children.front());
    case DataFieldKind::AssetRef:
    {
        const AssetRef* reference = nullptr;
        if (!value.TryGetAsset(reference))
            return false;
        return field.Reference.AssetTypeFilter == AssetType::Unknown
            || reference->Type == field.Reference.AssetTypeFilter;
    }
    case DataFieldKind::DataAssetRef:
    {
        const AssetRef* reference = nullptr;
        return value.TryGetDataAsset(reference) && reference->Type == AssetType::Data;
    }
    case DataFieldKind::GameplayTag:
        return value.Kind() == AuthoredValueKind::GameplayTag;
    case DataFieldKind::Entity:
        // A handle, and only a handle. A persistent identity is the authored
        // form of a constant, which the dispatcher resolves before anything is
        // validated; a producer supplying one dynamically has not done the
        // resolving it owes, and an operation must never be handed one.
        return value.Kind() == AuthoredValueKind::Entity;
    }
    return false;
}

void AuthoredArguments::Resize(std::size_t slots)
{
    Slots.resize(slots);
}

void AuthoredArguments::Set(std::size_t slot, AuthoredValue value)
{
    if (slot >= Slots.size())
        return;
    Slots[slot] = std::move(value);
}

const AuthoredValue& AuthoredArguments::At(std::size_t slot) const
{
    return slot < Slots.size() ? Slots[slot] : EmptyValue();
}

bool AuthoredArguments::TryGetBool(std::size_t slot, bool& out) const
{
    return At(slot).TryGetBool(out);
}

bool AuthoredArguments::TryGetInt(std::size_t slot, std::int64_t& out) const
{
    return At(slot).TryGetInt(out);
}

bool AuthoredArguments::TryGetFloat(std::size_t slot, double& out) const
{
    return At(slot).TryGetFloat(out);
}

bool AuthoredArguments::TryGetString(std::size_t slot, std::string_view& out) const
{
    return At(slot).TryGetString(out);
}

bool AuthoredArguments::TryGetEnum(std::size_t slot, std::string_view& out) const
{
    return At(slot).TryGetEnum(out);
}

bool AuthoredArguments::TryGetVector(std::size_t slot, AuthoredVectorValue& out) const
{
    return At(slot).TryGetVector(out);
}

bool AuthoredArguments::TryGetAsset(std::size_t slot, const AssetRef*& out) const
{
    return At(slot).TryGetAsset(out);
}

bool AuthoredArguments::TryGetDataAsset(std::size_t slot, const AssetRef*& out) const
{
    return At(slot).TryGetDataAsset(out);
}

bool AuthoredArguments::TryGetTag(std::size_t slot, GameplayTagId& out) const
{
    return At(slot).TryGetTag(out);
}

bool AuthoredArguments::TryGetEntity(std::size_t slot, EntityId& out) const
{
    return At(slot).TryGetEntity(out);
}

bool AuthoredArguments::TryGetPersistentEntity(std::size_t slot, PersistentEntityId& out) const
{
    return At(slot).TryGetPersistentEntity(out);
}
