#pragma once

#include <authored/VerbBinding.h>
#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <authored/VerbTrace.h>

#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

template<typename T>
concept IsVerbImplementation = requires(T& implementation, const VerbInvocation& invocation) {
    { implementation.Invoke(invocation) } -> std::same_as<VerbAdmission>;
};

class DataAssetCache;
class PersistentEntityIndex;
class VerbDispatcher;

//-----------------------------------------------------------------------------
// VerbBindingToken
//
// What an implementation's owner holds, and gives back before the
// implementation is destroyed.
//
// The dispatcher publishes a control block the tokens watch, so a token that
// outlives its dispatcher is inert rather than a dangling pointer. That is the
// ownership contract, chosen rather than inherited from declaration order: a
// host that composes the dispatcher and the implementations in the same scope
// is correct, and so is one whose token member happens to be declared first.
//
// A token also carries the generation its binding had, so an owner that unbinds
// late cannot remove the replacement someone else bound in the meantime.
//-----------------------------------------------------------------------------
class VerbBindingToken
{
public:
    VerbBindingToken() = default;
    ~VerbBindingToken() { Reset(); }

    VerbBindingToken(const VerbBindingToken&) = delete;
    VerbBindingToken& operator=(const VerbBindingToken&) = delete;

    VerbBindingToken(VerbBindingToken&& other) noexcept { MoveFrom(std::move(other)); }
    VerbBindingToken& operator=(VerbBindingToken&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    // Removes the binding this token minted, if it is still the one in place and
    // the dispatcher is still alive. Idempotent.
    void Reset();

    [[nodiscard]] bool IsValid() const { return Verb.IsValid() && !Link.expired(); }
    [[nodiscard]] VerbId BoundVerb() const { return Verb; }

private:
    friend class VerbDispatcher;

    struct DispatcherLink
    {
        VerbDispatcher* Owner = nullptr;
    };

    void MoveFrom(VerbBindingToken&& other) noexcept
    {
        Link = std::move(other.Link);
        Verb = other.Verb;
        Generation = other.Generation;
        other.Link.reset();
        other.Verb = {};
        other.Generation = {};
    }

    std::weak_ptr<DispatcherLink> Link;
    VerbId Verb;
    VerbBindingGeneration Generation;
};

//=============================================================================
// VerbDispatcher
//
// The one entry point from an authored producer to a registered operation.
//
// It holds implementations, not behaviour: there is no switch here, no
// subsystem list, and nothing a new verb adds except a row. Adding the
// hundredth operation grows this table and the domain code behind it, and
// changes nothing in this file.
//
// One dispatcher is associated with one catalog, composed explicitly by the
// runtime host and handed to producers. It is not a World resource and is not
// discovered: a metadata World installs a registry and no dispatcher, which is
// what stops an editor acquiring the ability to quit the application because a
// module declared the name.
//
// Owner-thread only in v1. A worker publishes through the existing async drain
// and never reaches this.
//=============================================================================
class VerbDispatcher
{
public:
    explicit VerbDispatcher(const VerbRegistry& registry);
    ~VerbDispatcher();

    VerbDispatcher(const VerbDispatcher&) = delete;
    VerbDispatcher& operator=(const VerbDispatcher&) = delete;
    VerbDispatcher(VerbDispatcher&&) = delete;
    VerbDispatcher& operator=(VerbDispatcher&&) = delete;

    [[nodiscard]] const VerbRegistry& Registry() const { return Verbs; }

    // Puts one implementation behind one verb, against the verb's current
    // contract revision. An invalid or unknown verb, or a call made from inside
    // a dispatch, returns an invalid token and binds nothing. Binding over an
    // existing implementation replaces it and moves the generation, so the
    // previous owner's token can no longer remove it. A contract that changes
    // or is revived afterwards makes the implementation unavailable until its
    // owner binds it again: rebinding is the statement that it was updated.
    template<IsVerbImplementation T>
    [[nodiscard]] VerbBindingToken Bind(VerbId verb, T& implementation)
    {
        return BindErased(verb, &implementation, [](void* self, const VerbInvocation& invocation) {
            return static_cast<T*>(self)->Invoke(invocation);
        });
    }

    [[nodiscard]] bool HasImplementation(VerbId verb) const;

    // Offers one request to the operation behind a compiled binding.
    //
    // `inputs` are the producer's values, in the order the binding declared its
    // input slots. The constants are already in place; this fills the dynamic
    // slots, checks them against what the binding recorded, and calls.
    [[nodiscard]] VerbInvocationResult Invoke(const CompiledVerbBinding& binding,
                                              std::span<const VerbValue> inputs,
                                              const VerbInvocationSource& source = {});

    // Where an entity constant resolves at each invocation. Borrowed and
    // outlived by its owner; null means a binding naming an entity is refused
    // with UnresolvedReference, never dispatched with an invalid handle.
    void SetEntityIndex(const PersistentEntityIndex* entities) { Entities = entities; }

    // Where a dynamic data-asset reference's subtype is read, for an input
    // whose field constrains one. Borrowed and outlived by its owner. Null
    // means such an input is refused as InvalidArguments rather than accepted
    // unchecked: the schema promised a subtype, and nothing here can keep it.
    void SetDataAssets(const DataAssetCache* dataAssets) { DataAssets = dataAssets; }

    // Stops admitting anything. The first half of shutdown: producers are
    // refused before implementations are removed, so nothing is accepted by an
    // operation that is about to go.
    void CloseAdmission() { Admitting = false; }
    [[nodiscard]] bool IsAdmitting() const { return Admitting; }

    // Borrowed, and outlived by the ring's owner. Null disables tracing without
    // disabling identity: an invocation still gets its id and still carries its
    // parent.
    void SetTrace(VerbTraceRing* trace) { Trace_ = trace; }
    [[nodiscard]] VerbTraceRing* Trace() const { return Trace_; }

    // Whether a call is in progress. Registration, unbinding, and destroying an
    // implementation are all forbidden while it is.
    [[nodiscard]] bool IsDispatching() const { return Dispatching; }

private:
    friend class VerbBindingToken;

    using InvokeFn = VerbAdmission (*)(void*, const VerbInvocation&);

    struct Implementation
    {
        void* Target = nullptr;
        InvokeFn Invoke = nullptr;
        VerbBindingGeneration Generation;
        // The contract this implementation was written against. A binding
        // compiled against a newer one is not offered to it: the argument
        // layout it would read is not the layout it was handed.
        VerbContractRevision Revision;
    };

    [[nodiscard]] VerbBindingToken BindErased(VerbId verb, void* target, InvokeFn invoke);
    void Unbind(VerbId verb, VerbBindingGeneration generation);

    [[nodiscard]] Implementation* Find(VerbId verb);
    [[nodiscard]] const Implementation* Find(VerbId verb) const;

    void RecordTrace(VerbTraceEvent event,
                     VerbAdmission status,
                     const VerbInvocation& invocation);

    const VerbRegistry& Verbs;
    std::shared_ptr<VerbBindingToken::DispatcherLink> Link;

    // Indexed by the verb's dense slot, grown as verbs acquire implementations.
    // A registry id is dense and one-based by contract, so this needs no map.
    std::vector<Implementation> Implementations;

    // Reused across calls so a warmed no-argument invocation allocates nothing.
    VerbArguments Scratch;

    VerbTraceRing* Trace_ = nullptr;
    const PersistentEntityIndex* Entities = nullptr;
    const DataAssetCache* DataAssets = nullptr;
    std::uint64_t NextInvocation = 0;
    std::uint32_t NextGeneration = 0;
    bool Admitting = true;
    bool Dispatching = false;
};
