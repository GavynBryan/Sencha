#include <authored/VerbArguments.h>

#include <cmath>
#include <utility>

namespace
{
    // What an out-of-range read sees. One object rather than a per-call
    // temporary, so At can return a reference.
    const VerbValue& EmptyValue()
    {
        static const VerbValue empty;
        return empty;
    }
}

VerbValue VerbValue::Bool(bool value)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Bool;
    result.Value_ = value;
    return result;
}

VerbValue VerbValue::Int(std::int64_t value)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Int;
    result.Value_ = value;
    return result;
}

VerbValue VerbValue::Float(double value)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Float;
    result.Value_ = value;
    return result;
}

VerbValue VerbValue::String(std::string value)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::String;
    result.Value_ = std::move(value);
    return result;
}

VerbValue VerbValue::Enum(std::string choice)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Enum;
    result.Value_ = std::move(choice);
    return result;
}

VerbValue VerbValue::Vector(VerbVectorValue value)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Vector;
    result.Value_ = value;
    return result;
}

VerbValue VerbValue::Record(std::vector<VerbValue> members)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Record;
    result.Children_ = std::move(members);
    return result;
}

VerbValue VerbValue::Array(std::vector<VerbValue> elements)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Array;
    result.Children_ = std::move(elements);
    return result;
}

VerbValue VerbValue::Asset(AssetRef reference)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::AssetRef;
    result.Value_ = std::move(reference);
    return result;
}

VerbValue VerbValue::DataAsset(AssetRef reference)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::DataAssetRef;
    result.Value_ = std::move(reference);
    return result;
}

VerbValue VerbValue::Tag(GameplayTagId tag)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::GameplayTag;
    result.Value_ = tag;
    return result;
}

VerbValue VerbValue::Entity(EntityId entity)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::Entity;
    result.Value_ = entity;
    return result;
}

VerbValue VerbValue::PersistentEntity(PersistentEntityId identity)
{
    VerbValue result;
    result.Kind_ = VerbValueKind::PersistentEntity;
    result.Value_ = identity;
    return result;
}

bool VerbValue::TryGetBool(bool& out) const
{
    if (Kind_ != VerbValueKind::Bool)
        return false;
    out = std::get<bool>(Value_);
    return true;
}

bool VerbValue::TryGetInt(std::int64_t& out) const
{
    if (Kind_ != VerbValueKind::Int)
        return false;
    out = std::get<std::int64_t>(Value_);
    return true;
}

bool VerbValue::TryGetFloat(double& out) const
{
    if (Kind_ != VerbValueKind::Float)
        return false;
    out = std::get<double>(Value_);
    return true;
}

bool VerbValue::TryGetString(std::string_view& out) const
{
    if (Kind_ != VerbValueKind::String)
        return false;
    out = std::get<std::string>(Value_);
    return true;
}

bool VerbValue::TryGetEnum(std::string_view& out) const
{
    if (Kind_ != VerbValueKind::Enum)
        return false;
    out = std::get<std::string>(Value_);
    return true;
}

bool VerbValue::TryGetVector(VerbVectorValue& out) const
{
    if (Kind_ != VerbValueKind::Vector)
        return false;
    out = std::get<VerbVectorValue>(Value_);
    return true;
}

bool VerbValue::TryGetAsset(const AssetRef*& out) const
{
    if (Kind_ != VerbValueKind::AssetRef)
        return false;
    out = &std::get<AssetRef>(Value_);
    return true;
}

bool VerbValue::TryGetDataAsset(const AssetRef*& out) const
{
    if (Kind_ != VerbValueKind::DataAssetRef)
        return false;
    out = &std::get<AssetRef>(Value_);
    return true;
}

bool VerbValue::TryGetTag(GameplayTagId& out) const
{
    if (Kind_ != VerbValueKind::GameplayTag)
        return false;
    out = std::get<GameplayTagId>(Value_);
    return true;
}

bool VerbValue::TryGetEntity(EntityId& out) const
{
    if (Kind_ != VerbValueKind::Entity)
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

bool VerbValue::TryGetPersistentEntity(PersistentEntityId& out) const
{
    if (Kind_ != VerbValueKind::PersistentEntity)
        return false;
    out = std::get<PersistentEntityId>(Value_);
    return true;
}

bool VerbValueSatisfiesField(const VerbValue& value, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        return value.Kind() == VerbValueKind::Bool;
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
        return value.Kind() == VerbValueKind::String;
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
        VerbVectorValue vector;
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
        if (value.Kind() != VerbValueKind::Record
            || value.Children().size() != field.Children.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < field.Children.size(); ++index)
        {
            if (!VerbValueSatisfiesField(value.Children()[index], field.Children[index]))
                return false;
        }
        return true;
    }
    case DataFieldKind::Array:
    {
        if (value.Kind() != VerbValueKind::Array || field.Children.size() != 1)
            return false;
        for (const VerbValue& element : value.Children())
        {
            if (!VerbValueSatisfiesField(element, field.Children.front()))
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
            && VerbValueSatisfiesField(value, field.Children.front());
    case DataFieldKind::AssetRef:
        return value.Kind() == VerbValueKind::AssetRef;
    case DataFieldKind::DataAssetRef:
        return value.Kind() == VerbValueKind::DataAssetRef;
    case DataFieldKind::GameplayTag:
        return value.Kind() == VerbValueKind::GameplayTag;
    case DataFieldKind::Entity:
        // A constant holds the authored identity until the dispatcher resolves
        // it; a producer's value is already a handle.
        return value.Kind() == VerbValueKind::Entity
            || value.Kind() == VerbValueKind::PersistentEntity;
    }
    return false;
}

void VerbArguments::Resize(std::size_t slots)
{
    Slots.resize(slots);
}

void VerbArguments::Set(std::size_t slot, VerbValue value)
{
    if (slot >= Slots.size())
        return;
    Slots[slot] = std::move(value);
}

const VerbValue& VerbArguments::At(std::size_t slot) const
{
    return slot < Slots.size() ? Slots[slot] : EmptyValue();
}

bool VerbArguments::TryGetBool(std::size_t slot, bool& out) const
{
    return At(slot).TryGetBool(out);
}

bool VerbArguments::TryGetInt(std::size_t slot, std::int64_t& out) const
{
    return At(slot).TryGetInt(out);
}

bool VerbArguments::TryGetFloat(std::size_t slot, double& out) const
{
    return At(slot).TryGetFloat(out);
}

bool VerbArguments::TryGetString(std::size_t slot, std::string_view& out) const
{
    return At(slot).TryGetString(out);
}

bool VerbArguments::TryGetEnum(std::size_t slot, std::string_view& out) const
{
    return At(slot).TryGetEnum(out);
}

bool VerbArguments::TryGetVector(std::size_t slot, VerbVectorValue& out) const
{
    return At(slot).TryGetVector(out);
}

bool VerbArguments::TryGetAsset(std::size_t slot, const AssetRef*& out) const
{
    return At(slot).TryGetAsset(out);
}

bool VerbArguments::TryGetDataAsset(std::size_t slot, const AssetRef*& out) const
{
    return At(slot).TryGetDataAsset(out);
}

bool VerbArguments::TryGetTag(std::size_t slot, GameplayTagId& out) const
{
    return At(slot).TryGetTag(out);
}

bool VerbArguments::TryGetEntity(std::size_t slot, EntityId& out) const
{
    return At(slot).TryGetEntity(out);
}

bool VerbArguments::TryGetPersistentEntity(std::size_t slot, PersistentEntityId& out) const
{
    return At(slot).TryGetPersistentEntity(out);
}
