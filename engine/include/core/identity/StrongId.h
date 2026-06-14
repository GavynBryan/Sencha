#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>

//=============================================================================
// StrongId<Tag, Underlying>
//
// One generator for stable identities — a *name*, distinct from Handle<Tag>
// (a transient slot reference). This is the engine's single id vocabulary:
// asset ids, type ids, serialized entity ids. The phantom Tag prevents mixing
// ids of different domains; Underlying is the storage width.
//
//   using AssetId = StrongId<struct AssetIdTag, uint64_t>;
//   using TypeId  = StrongId<struct TypeIdTag,  uint32_t>;
//
// Zero is the invalid id. Unlike handles, identities ARE persisted (their
// hashing and on-disk text/binary forms live here and in id-specific free
// functions), and are never resolved to memory.
//=============================================================================
template <typename Tag, typename Underlying = uint64_t>
struct StrongId
{
    Underlying Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    explicit operator bool() const { return Value != 0; }

    friend bool operator==(StrongId, StrongId) = default;
    friend auto operator<=>(StrongId, StrongId) = default;
};

template <typename Tag, typename Underlying>
struct std::hash<StrongId<Tag, Underlying>>
{
    std::size_t operator()(StrongId<Tag, Underlying> id) const noexcept
    {
        return std::hash<Underlying>{}(id.Value);
    }
};

// Binary serialization for the whole family — identities are persisted by
// their underlying value. Integrates with the Serialize.h vocabulary; the
// generic arithmetic/schema overloads there don't match StrongId, so these
// are unambiguous.
template <typename Tag, typename Underlying>
inline bool Serialize(BinaryWriter& writer, const StrongId<Tag, Underlying>& id)
{
    return writer.Write(id.Value);
}

template <typename Tag, typename Underlying>
inline bool Deserialize(BinaryReader& reader, StrongId<Tag, Underlying>& id)
{
    return reader.Read(id.Value);
}
