# Exposing gameplay to authored content

A game exposes three kinds of contract to authored content — level logic, UI
bindings, relays, and the LevelFlow graphs that come next:

- a **verb** is an operation content may request: `door.open`;
- a **query** is a question content may ask about the simulation now:
  `inventory.has_item`, `game.torch.lit`;
- an **event** is an announcement gameplay code makes when something happened:
  `torch.lit`.

Each is ordinary C++ with annotations. `sencha-component-codegen` reads the
annotations and writes the header's companion: the contract each declaration
states and the adapter that turns authored values into a C++ call. Declaring
the vocabulary, binding the implementation and publishing an event stay
explicit calls in the game's own code.

## Declaring

```cpp
#include <authored/AuthoredAnnotations.h>

struct SENCHA_COMPONENT("game.torch") SENCHA_SCHEMA("Torch") Torch
{
    SENCHA_FIELD("lit")
    SENCHA_QUERY("lit")
    SENCHA_LABEL("Lit")
    bool Lit = false;

    SENCHA_QUERY("hot")          // readable, never serialized
    bool Hot = false;

    float InternalTimer = 0.0f;  // neither
};

struct SENCHA_EVENT("torch.lit") SENCHA_EVENT_SOURCE(Torch) TorchLitEvent
{
    SENCHA_FIELD("instigator")
    EntityId Instigator{};
};

class TorchSystem
{
public:
    SENCHA_VERB("torch.ignite") SENCHA_LABEL("Ignite")
    VerbAdmission Ignite(const VerbInvocation& invocation,
                         SENCHA_TARGET("torch", Torch) EntityId torch,
                         SENCHA_ARG("brightness") SENCHA_RANGE(0, 10) std::int32_t brightness = 5,
                         SENCHA_ARG("instigator") std::optional<EntityId> instigator = std::nullopt);

    SENCHA_QUERY("torch.can_ignite")
    bool CanIgnite(SENCHA_TARGET("torch", Torch) EntityId torch) const;
};

#if !defined(SENCHA_CODEGEN)
#  include <src/TorchSystem.sencha.h>
#endif
```

A header of components goes in the game's `COMPONENT_HEADERS`. A header that
declares behaviour — a system's verbs, an event — goes in `API_HEADERS`, which
is generated the same way but is not held to the components-are-data check.

### What the annotations mean

`SENCHA_FIELD` puts a member in the reflected data schema of the record that
contains it. On a component, that schema feeds serialization, the inspector
and replication. On an event, it is the payload. `SENCHA_QUERY` on a component
member is independent of it: the member is readable by authored content as
`<component identity>.<name>` (`game.torch.lit` above), whether or not it is
serialized.

`SENCHA_TARGET("key", Component)` is an entity argument the author is
expected to pick from entities carrying `Component`. `SENCHA_EVENT_SOURCE` says
the same about the entity an event is published from. Both are **authoring
metadata**: they decide what an editor offers, not what the runtime accepts.
An implementation still checks the entity it was handed, and publishing an
event does not check its source — a development build warns once per event
when a source lacks the declared component, and nothing more. The named type
itself must be a component — declared by `SENCHA_COMPONENT` or
`SENCHA_DECLARE_COMPONENT_TYPE`. A type that exists but is not one fails the
build at the declaration rather than compiling to a constraint that
constrains nothing.

Identity is always written out. The generator infers only what the compiler
already knows:

- parameter, return and member types;
- whether a query is `const` (it must be);
- C++ default arguments, which become the schema's default;
- a leading `const VerbInvocation&` on a verb, which receives the request's
  provenance (`Id`, `Instigator`, `Tick`, `Parent`) and is not an argument;
- `std::optional<T>`, which is the only way to say an argument may be absent,
  and, as a query's return type, the way to answer "does not apply here".

### The value contract

One trait, `AuthoredValueTraits<T>` (`authored/AuthoredValueTraits.h`), decides
what may cross: `bool`, integers, `float` and `double`, `std::string`, enums
with an `EnumSchema`, `EntityId`, `GameplayTagId`, `AssetRef`, floating-point
`Vec<2..4>`, and `std::optional` of any of those.

- An authored integer is a signed 64-bit value. Signed types up to 64 bits and
  unsigned types up to 32 are supported; `std::uint64_t` and `std::size_t` are
  not. Decoding into a narrower type refuses a value that does not fit rather
  than wrapping it, and a narrower type declares its own range.
- An authored float is a `double`; decoding into `float` refuses what `float`
  cannot hold.
- An enum crosses as its `EnumSchema` name, never its underlying number.
- A default argument must be a constant a JSON document could spell — a bool,
  number, string or enum. An entity, tag, asset or vector parameter cannot have
  one, and an optional's only default is `std::nullopt`.

A declaration that breaks a rule is refused by the generator or by a
`static_assert` in the companion, naming the header, line, declaration and
parameter.

## Registering and binding

```cpp
void TorchGame::OnRegisterVocabulary(World& world)
{
    AuthoredVocabularyScope vocabulary(world, "torch");
    vocabulary.Declare<Torch>();           // its queryable members
    vocabulary.Declare<TorchLitEvent>();
    vocabulary.Declare<TorchSystem>();     // its verbs and queries
    (void)vocabulary.Commit();
}
```

The scope commits to the verb, query and event catalogs as one transaction:
every batch is validated against its catalog before any catalog changes, so a
provider whose query conflicts with another provider's publishes no verbs and
no events either. The engine reads the installation errors after the hook and
refuses to start on any; an editor reports them. Nothing is registered unless
a hook declares it.

```cpp
void TorchGame::OnStart(GameStartupContext&)
{
    Engine& engine = GetEngine();
    Torches.emplace(engine.World().Entities(), *engine.TryAuthoredEvents());
    TorchBindings = BindAuthoredApi(engine.TryVerbs(), engine.TryAuthoredQueries(), *Torches);
    TorchQueries = BindAuthoredApi<Torch>(*engine.TryAuthoredQueries(), engine.World().Entities());
}

void TorchGame::OnShutdown(GameShutdownContext&)
{
    TorchBindings.Reset();
    TorchQueries.Reset();
}
```

`BindAuthoredApi` puts one live object behind every verb and query its type
declares, looking each name up once, and returns the tokens. Resetting them
takes the object away. `Unbound()` lists anything declared that could not be
bound. A component's member queries are bound against a World; their readers
use the World's const access and answer `Unavailable` when the entity has no
such component.

## Handles, not slot numbers

A consumer that stores a reference to a query or an event stores a handle,
never a bare id: `Registry().Resolve(name)` returns `{Catalog, Slot, Contract}`.
Slot numbers are dense and local to one catalog — slot 1 in one World is an
unrelated entry in another — so every dispatcher that takes a handle checks
that it was resolved against its own catalog and that the contract has not
moved since. A handle that fails either check is stale and is refused, never
applied to whatever occupies the slot now. Verbs carry the same three facts in
a `CompiledVerbBinding`.

Vocabulary is not assumed to be immutable after startup. A module that is
reloaded can retire and revive names or change a contract, and handles are
what make that safe: anything resolved against the old contract stops
matching instead of reading the new layout as the old one.

## Queries

`AuthoredQueryDispatcher::Evaluate(handle, arguments, result)` is synchronous
and observational. The implementation receives a `const` target.

- **Arguments.** The caller supplies every argument the query declares, in
  order. A declared default is for whatever compiles the call to fill in — the
  graph compiler materializes it once — so evaluating stays a check and a call;
  an omitted argument is `InvalidArguments`. Arguments are checked against the
  declaration before the implementation runs.
- **Result.** The implementation's answer is checked against the declared
  result before it is handed on. `Value` always means a value the declaration
  allows. An answer of the wrong kind, an enumerator the schema does not list,
  or a number that is not finite is `InvalidResult`: a defect in the provider,
  never blamed on the caller, and never passed through.
- **Other outcomes.** `Unavailable` (the question does not apply here),
  `Unbound` (nothing answers, or what does was bound against an older
  contract) and `Stale` (the handle belongs to another catalog or an older
  contract). A condition never has to read `false` as "this entity has no
  torch".

An implementation that throws propagates the exception to the caller and
leaves the dispatcher usable.

## Events

```cpp
VerbAdmission TorchSystem::Ignite(const VerbInvocation& invocation, EntityId entity, ...)
{
    Torch* torch = World_.TryGet<Torch>(entity);
    if (torch == nullptr)
        return VerbAdmission::InvalidArguments;
    if (torch->Lit)
        return VerbAdmission::Accepted;  // nothing changed, nothing announced
    torch->Lit = true;
    Events_.Publish(entity, TorchLitEvent{ .Instigator = invocation.Instigator }, invocation.Id);
    return VerbAdmission::Accepted;
}
```

`Publish` only queues, and only an occurrence whose payload its declaration
allows: an unlisted enum choice or a non-finite number is refused at publish,
with one error per event. The engine drains the queue once per fixed tick, right
after fixed logic and before physics:

- occurrences are delivered first in first out, each to its subscribers in the
  order they subscribed, optionally filtered to one source entity. A
  subscription is made with a handle and hears only the contract it resolved:
  if the payload's declaration changes, it stops hearing the event until it
  subscribes again;
- anything published during the drain joins the tail and is delivered by the
  same drain, until the queue is empty. A chain — rotate A, A rotated, rotate
  B, B rotated, rotate C — completes in one tick, with no call nested inside
  another. No verb dispatch is in progress during a drain, so a subscriber
  invoking a verb is an ordinary request;
- an event published outside fixed logic, or during physics, is delivered at
  the next tick's drain;
- a chain completes in one tick only when the verbs it invokes act immediately.
  A verb that queues its work, as the arena's score does, applies it at the
  next tick's fixed logic, and its announcement follows then.

Each occurrence carries its cause — the verb request whose operation announced
it, and the occurrence being delivered at the time — and the root of its chain.
A subscriber that invokes a verb passes the cause as the request's parent.

**Change events are announced by the code that made the change, and only when
something changed.** A request to light a lit torch lights nothing and
announces nothing. There is no automatic change detection: a query answers
"what is true now", an event says "this happened", and neither is derived from
the other.

### Runaway chains

A drain has a budget of subscriber calls (`authored.events.drain_budget`, 4096
by default). A drain that spends it stops, keeps the rest queued in order for
the next tick, logs the recent deliveries, and marks the chains still queued as
suspect. A chain still running after `authored.events.quarantine_after`
consecutive exhausted drains (2 by default) is quarantined: its queued
occurrences are discarded, the report — root, tick, discarded count and trace
— is kept on `AuthoredEventDispatcher::LastQuarantine()`, and a debug build
stops there (`authored.events.trap_on_quarantine`). Other chains carry on.

This is a cutoff for sustained work, not cycle detection. A reaction that
announces the event it reacts to is the usual cause, but a finite chain long
enough to outlast the threshold is cut off too; raise the threshold, or the
budget, for content that legitimately does that much in one burst.

`authored.events.queue_capacity` bounds how many occurrences may wait. The
occurrence being delivered does not count against it, so a subscriber can
always queue its reaction while one waiting place is free, even when the queue
was full as the drain began. Publishing into a full queue is refused and
counted, never dropped silently.

## Reading the generated code

A companion is plain C++ and meant to be read. For each verb it has a
`Describe_<Method>` that builds the `VerbDefinition` the way a person would and
an `Invoke_<Method>` that decodes each argument slot and calls the method; for
each query a `Describe_` and an `Evaluate_`; for each event a `DescribeEvent`
and an `Encode`; and a table listing them. Registries hold only the
definitions; the adapters live behind the dispatchers' tokens, which is why
every token is reset in `OnShutdown`, before the module unloads.

The test that answers "who does what" is always a search in ordinary C++:

| Question | Where to look |
|---|---|
| What exposes this node? | The `SENCHA_*` annotation. |
| What runs? | The annotated method. |
| Who owns its dependencies? | The provider object's constructor. |
| Who registered it? | The game's `OnRegisterVocabulary`. |
| Who made it live? | The `BindAuthoredApi` call. |
| Who announced this event? | `Publish` with the event type. |
