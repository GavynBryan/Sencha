#pragma once

#include <core/metadata/RuntimeSchema.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

//=============================================================================
// ReplicationLayout
//
// Which components replicate, and which of their bytes travel. Compiled once at
// startup from the schemas the components already declare, then consumed by the
// wire codec without naming any component type.
//
// There is no second registry to keep in step with the first: a component
// replicates because its TypeSchema says `Replicated = true`, and the fields
// that travel are the fields that schema declares, minus what it marks local.
// Adding a replicated component is one line on the component and one line in
// its feature's registrar, and no netcode at all.
//=============================================================================

//-----------------------------------------------------------------------------
// One run of contiguous, same-typed scalars inside a component: a float, a
// Vec3's three floats, a bool. The codec addresses component bytes through
// these and never through a component type.
//-----------------------------------------------------------------------------
struct ReplicatedField
{
    // Dotted path, for diagnostics and desync reports. Never on the wire --
    // field identity is position in the owning component's field list.
    std::string Name;
    std::size_t Offset = 0;
    // Bytes of one scalar. The run spans [Offset, Offset + Count * Size).
    std::size_t Size = 0;
    std::uint8_t Count = 1;
    FieldScalar Scalar = FieldScalar::Unsupported;
    FieldQuantization Quantization{};
    // Sent only to the peer that owns the entity.
    bool OwnerOnly = false;
    // Sent to everyone except that peer, which computes it itself.
    bool OwnerLocal = false;
};

//-----------------------------------------------------------------------------
// A replicated component type and the fields of it that travel.
//-----------------------------------------------------------------------------
struct ReplicatedComponent
{
    ComponentTypeId Type;
    std::string_view Name;
    // Size of the whole component, so an applier can size the staging buffer it
    // decodes into before writing it back through the world schema.
    std::size_t Size = 0;
    std::vector<ReplicatedField> Fields;
};

// Why a component could not be registered for replication. Every one of these
// is a programming error caught at startup, never a wire condition.
enum class ReplicationLayoutError : std::uint8_t
{
    None = 0,
    // A leaf the wire codec cannot express: a handle, a string, a type the
    // scalar table does not name. Mark it LocalOnly, or give it a codec.
    UnsupportedField,
    // Every field was excluded, so the component would replicate nothing.
    NoReplicatedFields,
    // Quantization asked for a range that cannot round-trip.
    InvalidQuantization,
    // More replicated fields than one field mask can address.
    TooManyFields,
};

// A component's per-field presence mask is one machine word, so this is how
// many fields of one component the wire can distinguish. Comfortably above any
// real component; a type that needs more is really several components.
inline constexpr std::size_t kMaxReplicatedFieldsPerComponent = 64;

[[nodiscard]] std::string_view ReplicationLayoutErrorToString(ReplicationLayoutError error);

class ReplicationLayout
{
public:
    // Registers T as replicated. Ordering is the wire contract: a component's
    // position here is its one-byte wire key, so two builds must add the same
    // components in the same order. That is a same-build contract rather than a
    // persisted one -- the handshake compares TableHash and refuses a peer that
    // does not match -- so this is normally reached through ComponentRegistrar,
    // which derives it from the component's own schema.
    template <typename T>
    bool Add()
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "A replicated component must be trivially copyable.");
        static_assert(HasTypeSchema<T>,
                      "A replicated component needs a TypeSchema: the schema is "
                      "what says which of its bytes travel.");
        // The structural half of the snapshot-apply contract. Applying a
        // snapshot overwrites component bytes in place, which cannot fire
        // OnAdd for what arrives or OnRemove for what it replaces -- so a
        // component that retains an external handle would leak the old one and
        // hold an unretained new one. Made impossible here rather than written
        // down somewhere: state that must reach clients and whose component
        // owns handles travels in a spawn payload, which goes through the
        // typed import path and fires hooks exactly once.
        static_assert(!ComponentHasOnAdd<T> && !ComponentHasOnRemove<T>,
                      "A replicated component must not declare ComponentTraits "
                      "lifecycle hooks: snapshot apply overwrites bytes in place "
                      "and cannot run them. Replicate a handle-free component, or "
                      "carry this one in the spawn payload instead.");

        return AddErased(ResolveComponentTypeId<T>(),
                         ResolveComponentName<T>(),
                         sizeof(T),
                         RuntimeFieldsOf<T>());
    }

    void Seal();
    [[nodiscard]] bool IsSealed() const { return Sealed_; }

    [[nodiscard]] const ReplicatedComponent* Find(ComponentTypeId type) const;
    // By wire key. Null for a key this build does not define, which is what a
    // peer sending an out-of-range component index looks like.
    [[nodiscard]] const ReplicatedComponent* At(std::uint8_t index) const;
    [[nodiscard]] std::optional<std::uint8_t> IndexOf(ComponentTypeId type) const;

    [[nodiscard]] std::span<const ReplicatedComponent> Components() const
    {
        return { Components_.data(), Components_.size() };
    }
    [[nodiscard]] std::size_t Size() const { return Components_.size(); }

    // Folds the whole table -- order, identity, and every field's offset, width,
    // and quantization -- into one value. Two builds that agree on this agree on
    // how to read each other's snapshots; two that do not would silently
    // misread them, so the handshake compares it instead of assuming.
    [[nodiscard]] std::uint64_t TableHash() const;

    // Set when an Add failed, with the component that failed and why. Checked
    // once at startup rather than per call: a failure here means the build is
    // wrong, and the composition root reports it before a session can exist.
    [[nodiscard]] ReplicationLayoutError Error() const { return Error_; }
    [[nodiscard]] const std::string& ErrorDetail() const { return ErrorDetail_; }

private:
    bool AddErased(ComponentTypeId type,
                   std::string_view name,
                   std::size_t size,
                   const std::vector<RuntimeField>& fields);

    void Fail(ReplicationLayoutError error, std::string detail);

    std::vector<ReplicatedComponent> Components_;
    bool Sealed_ = false;
    ReplicationLayoutError Error_ = ReplicationLayoutError::None;
    std::string ErrorDetail_;
};

// A component's wire key is one byte, so the table cannot outgrow that. The
// world schema's own 256-component budget makes this the same ceiling.
inline constexpr std::size_t kMaxReplicatedComponents = 256;
