#include <net/ReplicationLayout.h>

#include <cassert>
#include <cmath>

namespace
{
    // Fold one value into a running FNV-1a hash. The table hash has to be
    // identical on two machines that never share a pointer, so everything mixed
    // in here is a value the schema states, never an address.
    void MixBytes(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    }

    void MixU64(std::uint64_t& hash, std::uint64_t value)
    {
        MixBytes(hash, &value, sizeof(value));
    }

    void MixText(std::uint64_t& hash, std::string_view text)
    {
        MixBytes(hash, text.data(), text.size());
    }

    bool IsWireScalar(FieldScalar scalar)
    {
        switch (scalar)
        {
        case FieldScalar::Bool:
        case FieldScalar::Int32:
        case FieldScalar::UInt32:
        case FieldScalar::Float:
        case FieldScalar::Double:
        case FieldScalar::Color3:
            return true;
        case FieldScalar::Unsupported:
            break;
        }
        return false;
    }

    bool QuantizationIsUsable(const FieldQuantization& q)
    {
        if (!q.IsQuantized())
            return true;
        // 32 bits is the ceiling because the encoder works in a 32-bit integer
        // domain; a range that is empty or not finite has no fixed-point form.
        return q.Bits <= 32
            && std::isfinite(q.Min)
            && std::isfinite(q.Max)
            && q.Max > q.Min;
    }
}

std::string_view ReplicationLayoutErrorToString(ReplicationLayoutError error)
{
    switch (error)
    {
    case ReplicationLayoutError::None:                return "none";
    case ReplicationLayoutError::UnsupportedField:    return "unsupported field type";
    case ReplicationLayoutError::NoReplicatedFields:  return "no replicated fields";
    case ReplicationLayoutError::InvalidQuantization: return "invalid quantization range";
    case ReplicationLayoutError::TooManyFields:       return "too many replicated fields";
    }
    return "unknown";
}

void ReplicationLayout::Fail(ReplicationLayoutError error, std::string detail)
{
    // First failure wins: it is the one that explains the rest.
    if (Error_ != ReplicationLayoutError::None)
        return;
    Error_ = error;
    ErrorDetail_ = std::move(detail);
}

bool ReplicationLayout::AddErased(ComponentTypeId type,
                                  std::string_view name,
                                  std::size_t size,
                                  const std::vector<RuntimeField>& fields)
{
    assert(!Sealed_ && "Cannot add components after ReplicationLayout::Seal");

    if (Find(type) != nullptr)
        return false;  // Idempotent, like the world schema's own Add.

    assert(Components_.size() < kMaxReplicatedComponents
           && "ReplicationLayout exceeds the one-byte component key budget");

    ReplicatedComponent component;
    component.Type = type;
    component.Name = name;
    component.Size = size;

    for (const RuntimeField& field : fields)
    {
        if (field.LocalOnly)
            continue;

        if (!IsWireScalar(field.Scalar))
        {
            Fail(ReplicationLayoutError::UnsupportedField,
                 std::string(name) + "." + field.Name);
            return false;
        }
        if (!QuantizationIsUsable(field.Quantization))
        {
            Fail(ReplicationLayoutError::InvalidQuantization,
                 std::string(name) + "." + field.Name);
            return false;
        }

        ReplicatedField out;
        out.Name = field.Name;
        out.Offset = field.Offset;
        out.Size = field.Size;
        // A Color3 leaf is three floats the flattener collapsed into one entry;
        // the wire treats it as the three floats it is.
        out.Count = field.Scalar == FieldScalar::Color3
                        ? std::uint8_t{ 3 }
                        : field.Count;
        out.Size = field.Scalar == FieldScalar::Color3 ? sizeof(float) : field.Size;
        out.Scalar = field.Scalar == FieldScalar::Color3 ? FieldScalar::Float
                                                        : field.Scalar;
        out.Quantization = field.Quantization;
        out.OwnerOnly = field.OwnerOnly;
        out.OwnerLocal = field.OwnerLocal;

        assert(out.Offset + out.Count * out.Size <= size
               && "a replicated field runs past the end of its component");
        component.Fields.push_back(std::move(out));
    }

    if (component.Fields.empty())
    {
        Fail(ReplicationLayoutError::NoReplicatedFields, std::string(name));
        return false;
    }
    if (component.Fields.size() > kMaxReplicatedFieldsPerComponent)
    {
        Fail(ReplicationLayoutError::TooManyFields, std::string(name));
        return false;
    }

    Components_.push_back(std::move(component));
    return true;
}

void ReplicationLayout::Seal()
{
    assert(!Sealed_ && "ReplicationLayout already sealed");
    Sealed_ = true;
}

const ReplicatedComponent* ReplicationLayout::Find(ComponentTypeId type) const
{
    for (const ReplicatedComponent& component : Components_)
    {
        if (component.Type == type)
            return &component;
    }
    return nullptr;
}

const ReplicatedComponent* ReplicationLayout::At(std::uint8_t index) const
{
    return index < Components_.size() ? &Components_[index] : nullptr;
}

std::optional<std::uint8_t> ReplicationLayout::IndexOf(ComponentTypeId type) const
{
    for (std::size_t i = 0; i < Components_.size(); ++i)
    {
        if (Components_[i].Type == type)
            return static_cast<std::uint8_t>(i);
    }
    return std::nullopt;
}

std::uint64_t ReplicationLayout::TableHash() const
{
    std::uint64_t hash = 14695981039346656037ull;
    MixU64(hash, Components_.size());
    for (const ReplicatedComponent& component : Components_)
    {
        MixU64(hash, component.Type.Value);
        MixU64(hash, component.Size);
        MixU64(hash, component.Fields.size());
        for (const ReplicatedField& field : component.Fields)
        {
            // The name goes in because a field list that agrees on every offset
            // but disagrees on which member is which is still a disagreement.
            MixText(hash, field.Name);
            MixU64(hash, field.Offset);
            MixU64(hash, field.Size);
            MixU64(hash, field.Count);
            MixU64(hash, static_cast<std::uint64_t>(field.Scalar));
            MixU64(hash, field.Quantization.Bits);
            MixBytes(hash, &field.Quantization.Min, sizeof(float));
            MixBytes(hash, &field.Quantization.Max, sizeof(float));
            MixU64(hash, field.OwnerOnly ? 1u : 0u);
            MixU64(hash, field.OwnerLocal ? 1u : 0u);
        }
    }
    return hash;
}
