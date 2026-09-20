#include <authored/VerbDispatcher.h>

#include <world/identity/PersistentEntityIndex.h>

#include <cassert>
#include <utility>

const char* VerbAdmissionName(VerbAdmission admission)
{
    switch (admission)
    {
    case VerbAdmission::Accepted: return "accepted";
    case VerbAdmission::UnresolvedBinding: return "unresolved binding";
    case VerbAdmission::StaleBinding: return "stale binding";
    case VerbAdmission::Unavailable: return "no implementation";
    case VerbAdmission::InvalidArguments: return "invalid arguments";
    case VerbAdmission::Refused: return "refused";
    case VerbAdmission::QueueFull: return "queue full";
    case VerbAdmission::Reentrant: return "reentrant dispatch";
    case VerbAdmission::UnresolvedReference: return "unresolved reference";
    }
    return "unknown";
}

void VerbBindingToken::Reset()
{
    const std::shared_ptr<DispatcherLink> link = Link.lock();
    if (link != nullptr && link->Owner != nullptr && Verb.IsValid())
        link->Owner->Unbind(Verb, Generation);

    Link.reset();
    Verb = {};
    Generation = {};
}

VerbDispatcher::VerbDispatcher(const VerbRegistry& registry)
    : Verbs(registry)
    , Link(std::make_shared<VerbBindingToken::DispatcherLink>())
{
    Link->Owner = this;
}

VerbDispatcher::~VerbDispatcher()
{
    // Tokens outliving this become inert rather than dangling. Whichever way a
    // host declared its members, unbinding late is safe and unbinding early is
    // correct.
    Link->Owner = nullptr;
}

VerbDispatcher::Implementation* VerbDispatcher::Find(VerbId verb)
{
    if (!verb.IsValid() || VerbRegistry::IndexOf(verb) >= Implementations.size())
        return nullptr;
    Implementation& entry = Implementations[VerbRegistry::IndexOf(verb)];
    return entry.Invoke != nullptr ? &entry : nullptr;
}

const VerbDispatcher::Implementation* VerbDispatcher::Find(VerbId verb) const
{
    if (!verb.IsValid() || VerbRegistry::IndexOf(verb) >= Implementations.size())
        return nullptr;
    const Implementation& entry = Implementations[VerbRegistry::IndexOf(verb)];
    return entry.Invoke != nullptr ? &entry : nullptr;
}

bool VerbDispatcher::HasImplementation(VerbId verb) const
{
    return Find(verb) != nullptr;
}

VerbBindingToken VerbDispatcher::BindErased(VerbId verb, void* target, InvokeFn invoke)
{
    // Replacing an implementation under a call that is running inside it would
    // leave the frame executing code the dispatcher no longer points at.
    assert(!Dispatching && "an implementation cannot be bound from inside a dispatch");
    if (Dispatching || target == nullptr || invoke == nullptr || !Verbs.IsLive(verb))
        return {};

    const std::size_t slot = VerbRegistry::IndexOf(verb);
    if (slot >= Implementations.size())
        Implementations.resize(slot + 1);

    Implementation& entry = Implementations[slot];
    entry.Target = target;
    entry.Invoke = invoke;
    entry.Generation = VerbBindingGeneration{ ++NextGeneration };
    entry.Revision = Verbs.Revision(verb);

    VerbBindingToken token;
    token.Link = Link;
    token.Verb = verb;
    token.Generation = entry.Generation;
    return token;
}

void VerbDispatcher::Unbind(VerbId verb, VerbBindingGeneration generation)
{
    assert(!Dispatching && "an implementation cannot be unbound from inside a dispatch");
    if (Dispatching)
        return;

    Implementation* entry = Find(verb);
    // A token from the previous implementation must not remove the one that
    // replaced it.
    if (entry == nullptr || entry->Generation != generation)
        return;

    *entry = Implementation{};
}

void VerbDispatcher::RecordTrace(VerbTraceEvent event,
                                 VerbAdmission status,
                                 const VerbInvocation& invocation)
{
    if (Trace_ == nullptr)
        return;
    Trace_->Record(VerbTraceRecord{
        .Event = event,
        .Status = status,
        .Verb = invocation.Verb,
        .Id = invocation.Id,
        .Parent = invocation.Parent,
        .Binding = invocation.Binding,
        .Producer = invocation.Producer,
    });
}

VerbInvocationResult VerbDispatcher::Invoke(const CompiledVerbBinding& binding,
                                            std::span<const VerbValue> inputs,
                                            const VerbInvocationSource& source)
{
    // Every attempt takes its own id, accepted or not, so a refusal in the
    // trace can never share a number with an execution that came later. An
    // operation that queues work has the id to put in the record it queues;
    // the accepted sequence simply has gaps where refusals were.
    VerbInvocation invocation;
    invocation.Verb = binding.Verb;
    invocation.Id = InvocationId{ ++NextInvocation };
    invocation.Parent = source.Parent;
    invocation.Binding = binding.Key;
    invocation.Producer = source.Producer;

    const auto reject = [&](VerbAdmission status) {
        RecordTrace(VerbTraceEvent::Rejected, status, invocation);
        return VerbInvocationResult{ .Status = status, .Id = InvocationId{} };
    };

    if (Dispatching)
        return reject(VerbAdmission::Reentrant);
    if (!Admitting)
        return reject(VerbAdmission::Unavailable);
    if (!binding.IsValid() || binding.Catalog != Verbs.Catalog())
        return reject(VerbAdmission::UnresolvedBinding);
    if (!Verbs.IsLive(binding.Verb) || Verbs.Revision(binding.Verb) != binding.Revision)
        return reject(VerbAdmission::StaleBinding);

    Implementation* entry = Find(binding.Verb);
    // An implementation bound against an older contract is not offered a newer
    // binding: the catalog moved, the content recompiled, and the code that
    // reads the arguments has not said it did.
    if (entry == nullptr || entry->Revision != binding.Revision)
        return reject(VerbAdmission::Unavailable);

    if (inputs.size() != binding.Inputs.size())
        return reject(VerbAdmission::InvalidArguments);

    Scratch.Resize(binding.Constants.Size());
    for (std::size_t slot = 0; slot < binding.Constants.Size(); ++slot)
    {
        const VerbValue& constant = binding.Constants.At(slot);
        PersistentEntityId identity;
        if (!constant.TryGetPersistentEntity(identity))
        {
            Scratch.Set(slot, constant);
            continue;
        }
        // The authored relationship, resolved now: whichever entity carries
        // the identity at this moment, or nothing. A target that is absent
        // refuses this request and does not wait for a later one.
        const EntityId entity =
            Entities != nullptr ? Entities->TryResolve(identity) : EntityId{};
        if (!entity.IsValid())
            return reject(VerbAdmission::UnresolvedReference);
        Scratch.Set(slot, VerbValue::Entity(entity));
    }

    for (std::size_t index = 0; index < binding.Inputs.size(); ++index)
    {
        for (const VerbInputDestination& destination : binding.Inputs[index].Destinations)
        {
            if (!VerbValueSatisfiesField(inputs[index], destination.Expected))
                return reject(VerbAdmission::InvalidArguments);
            Scratch.Set(destination.ArgumentSlot, inputs[index]);
        }
    }

    invocation.Arguments = &Scratch;

    // Scoped rather than a pair of assignments: an implementation that throws
    // would otherwise leave the dispatcher permanently refusing every later
    // request as reentrant.
    struct DispatchScope
    {
        explicit DispatchScope(bool& flag) : Flag(flag) { Flag = true; }
        ~DispatchScope() { Flag = false; }
        bool& Flag;
    };

    VerbAdmission status = VerbAdmission::Refused;
    {
        const DispatchScope scope(Dispatching);
        status = entry->Invoke(entry->Target, invocation);
    }

    if (status != VerbAdmission::Accepted)
        return reject(status);

    RecordTrace(VerbTraceEvent::Admitted, status, invocation);
    return VerbInvocationResult{ .Status = status, .Id = invocation.Id };
}
