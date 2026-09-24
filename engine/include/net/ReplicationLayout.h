#pragma once

#include <core/metadata/RuntimeSchema.h>
#include <net/ReplicationSchemas.h>
#include <net/ReplicationWireContext.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

// The owner's own machine keeps simulating this component between snapshots,
// so what arrives for it is the authority's view rather than the world's next
// value. Declared as `static constexpr bool Predicted = true;` on the schema,
// beside Replicated. Read here rather than passed in by the registrar, so a
// layout built directly still knows the fact.
//
// This routes bytes and does not schedule work. The value is restored to the
// authority's word before the unanswered ticks are re-run; whether it is then
// carried forward depends entirely on whether that re-run steps it, and what
// re-runs is character movement. Declaring it on something else is answered at
// startup rather than silently -- see prediction/PawnStateReplay.h.
template <typename T>
inline constexpr bool ComponentIsPredicted = []
{
    if constexpr (requires { TypeSchema<T>::Predicted; })
        return static_cast<bool>(TypeSchema<T>::Predicted);
    else
        return false;
}();

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

// A component's wire image from its bytes, and its bytes back from an image.
// FromWire writes into the receiver's own value, so what the image does not
// carry is left as this machine had it.
using ReplicationToWire = void (*)(const ReplicationWireContext& context, std::span<const std::byte> component,
                                   std::span<std::byte> wire);
using ReplicationFromWire = void (*)(const ReplicationWireContext& context, std::span<const std::byte> wire,
                                     std::span<std::byte> component);

//-----------------------------------------------------------------------------
// ReplicationCodec<T>
//
// Specialized by a component whose wire form is not its bytes: it holds
// process-local values -- an entity, a gameplay tag id -- that have to travel
// as what every machine agrees on. The specialization names a plain Wire
// struct with its own TypeSchema, which is all the rest of replication sees:
// change detection, field masks, encoding, baselines and the desync probe
// work on the image, so they stay the generic path.
//
//   template <> struct ReplicationCodec<Pointer>
//   {
//       using Wire = PointerWire;
//       static void ToWire(const ReplicationWireContext&, const Pointer&, PointerWire&);
//       static void FromWire(const ReplicationWireContext&, const PointerWire&, Pointer&);
//   };
//
// Declaring one makes the component replicated; its own schema does not also
// say Replicated. A translated component is never predicted.
//-----------------------------------------------------------------------------
template <typename T>
struct ReplicationCodec;

template <typename T>
concept ComponentHasReplicationCodec = requires { typename ReplicationCodec<T>::Wire; };

//-----------------------------------------------------------------------------
// A replicated component type and the fields of it that travel.
//-----------------------------------------------------------------------------
struct ReplicatedComponent
{
    ComponentTypeId Type;
    std::string_view Name;
    // Size of what travels: the component itself, or its wire image when a
    // codec translates it. An applier sizes the staging it decodes into by it.
    std::size_t Size = 0;
    // Size of the component as the World stores it.
    std::size_t LocalSize = 0;
    // Set for a component that travels as a wire image rather than its bytes.
    ReplicationToWire ToWire = nullptr;
    ReplicationFromWire FromWire = nullptr;
    // The image of the type's defaults. A receiver stages a translated
    // component from it and writes the component whole from the result, so a
    // run still at its default when the component is first seen is already
    // what every receiver will hold, and is not sent.
    std::vector<std::byte> WireDefault;
    // The owner's machine simulates this one for itself, so an applier holds
    // what arrives apart from the world's copy instead of overwriting it.
    //
    // Deliberately absent from TableHash: the hash exists so two builds that
    // read each other's snapshots know they agree on how, and this changes only
    // where a client lands the bytes afterwards -- never the encoding, the field
    // order, or a width. Folding it in would refuse a handshake between builds
    // that understand each other perfectly.
    bool Predicted = false;
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
    // OwnerOnly and OwnerLocal on the same field: one says the owner is the
    // only peer who may see it, the other says the owner is the only peer who
    // may not. The field reaches nobody, while still costing a mask bit in
    // every encode and a slot against the per-component field budget.
    //
    // Reachable without anyone writing both, because a composite's annotation
    // reaches the scalars underneath it: an OwnerOnly struct with an OwnerLocal
    // member produces this at the leaf. Refused rather than resolved -- there
    // is no reading of the pair that is safe to guess at, and picking the
    // member over the composite would send what the composite called private.
    ContradictoryFieldPolicy,
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
                         ComponentIsPredicted<T>,
                         RuntimeFieldsOf<T, SchemaPurpose::Replication>());
    }

    // Registers T as travelling through its ReplicationCodec: what is sent is
    // the codec's wire image, not T's bytes.
    template <ComponentHasReplicationCodec T>
    bool AddCodec()
    {
        using Wire = typename ReplicationCodec<T>::Wire;
        static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<Wire>,
                      "A replicated component and its wire image must be trivially copyable.");
        static_assert(HasTypeSchema<Wire>, "A wire image needs a TypeSchema: it says what of the image travels.");
        static_assert(!ComponentHasOnAdd<T> && !ComponentHasOnRemove<T>,
                      "A replicated component must not declare ComponentTraits lifecycle hooks: snapshot "
                      "apply overwrites bytes in place and cannot run them.");
        if (!AddErased(ResolveComponentTypeId<T>(), ResolveComponentName<T>(), sizeof(Wire), false,
                       RuntimeFieldsOf<Wire, SchemaPurpose::Replication>()))
            return false;
        ReplicatedComponent& added = Components_.back();
        added.LocalSize = sizeof(T);
        {
            const T local{};
            Wire image{};
            ReplicationCodec<T>::ToWire(ReplicationWireContext{}, local, image);
            added.WireDefault.resize(sizeof(Wire));
            std::memcpy(added.WireDefault.data(), &image, sizeof(Wire));
        }
        added.ToWire = [](const ReplicationWireContext& context, std::span<const std::byte> component,
                          std::span<std::byte> wire) {
            T local;
            Wire image;
            std::memcpy(&local, component.data(), sizeof(T));
            std::memcpy(&image, wire.data(), sizeof(Wire));
            ReplicationCodec<T>::ToWire(context, local, image);
            std::memcpy(wire.data(), &image, sizeof(Wire));
        };
        added.FromWire = [](const ReplicationWireContext& context, std::span<const std::byte> wire,
                            std::span<std::byte> component) {
            T local;
            Wire image;
            std::memcpy(&local, component.data(), sizeof(T));
            std::memcpy(&image, wire.data(), sizeof(Wire));
            ReplicationCodec<T>::FromWire(context, image, local);
            std::memcpy(component.data(), &local, sizeof(T));
        };
        return true;
    }

    void Seal();
    [[nodiscard]] bool IsSealed() const { return Sealed_; }

    [[nodiscard]] const ReplicatedComponent* Find(ComponentTypeId type) const;
    // By wire key. Null for a key this build does not define, which is what a
    // peer sending an out-of-range component index looks like.
    [[nodiscard]] const ReplicatedComponent* At(std::uint8_t index) const;

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
                   bool predicted,
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
