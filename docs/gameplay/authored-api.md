# Exposing gameplay to authored content

A game exposes three kinds of contract to authored content — level logic, UI
bindings, relays, and the LevelFlow graphs that come next:

- a **verb** is an operation content may request: `door.open`;
- a **query** is a question content may ask about the simulation now:
  `inventory.count`, `game.torch.lit`;
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
is generated the same way but is not held to the components-are-data check. The
engine passes its own provider headers the same way
(`SENCHA_ENGINE_API_HEADERS` in `engine/CMakeLists.txt`).

A companion is regenerated when its header changes, or any header the parse of
it read: the generator writes a dependency file beside the companion
(`<Header>.sencha.d`) and the build reads it, so a default that names a constant
or an enumerator from another header follows that header. Under a generator
without dependency files (Visual Studio) only the header itself is tracked.

### Annotation reference

| Annotation | On | Meaning |
|---|---|---|
| `SENCHA_VERB(id)` | method | An operation content may request. Returns `VerbAdmission`, `VerbAnswer`, `VerbRefusal<E>` or `VerbResult<T, E>` (see "What a verb answers"). |
| `SENCHA_LEASE(id)` | method | The acquire half of a lease: its arguments and its contract. Returns as a verb does, but never a `VerbResult`. |
| `SENCHA_LEASE_RELEASE(id)` | method | The release half: takes only `const VerbInvocation&` and returns `VerbAdmission` or `VerbAnswer`. Every lease has one. |
| `SENCHA_LEASE_HOLDS(id)` | const method | Whether an owner's hold goes on: takes `const LeaseOwner&`, returns `bool`. A `SENCHA_COMPLETES` lease has one and no other lease may. |
| `SENCHA_COSMETIC` | verb, lease | `Scope = Cosmetic`, `Prediction = Presentation`. |
| `SENCHA_PREDICTION(p)` | verb, lease | `AuthorityOnly` (the default) or `Simulation`. A cosmetic verb names none. |
| `SENCHA_COMPLETES` | lease | `Completes = true`: the hold can finish while held. |
| `SENCHA_RESULT(key[, C])` | verb | The result a `VerbResult<T, E>` answers, by its key; `C` is the component an entity result is expected to carry. |
| `SENCHA_QUERY(id)` | const method | A question content may ask. |
| `SENCHA_QUERY(name)` | component member | Readable as `<component identity>.<name>`. |
| `SENCHA_EVENT(id)` | struct | A typed announcement; its `SENCHA_FIELD` members are the payload. |
| `SENCHA_EVENT_SOURCE(C)` | event | The component a source entity is expected to carry. |
| `SENCHA_RECORD` | struct | A result answered whole: its `SENCHA_FIELD` members are the record's. |
| `SENCHA_ARG(key)` | parameter | An argument, by its persisted key. |
| `SENCHA_TARGET(key[, C])` | parameter | An entity argument, optionally expected to carry `C`. On a verb or lease, a target that is not alive is refused before the method runs. |
| `SENCHA_RANGE(min, max)` | parameter | The numeric range the argument accepts. |
| `SENCHA_MIN(x)`, `SENCHA_MAX(x)` | parameter | One end of that range, or both; not beside `SENCHA_RANGE`. |
| `SENCHA_UNITS(text)` | parameter | `DataFieldSchema::Units`. |
| `SENCHA_ASSET(Kind)` | `AssetRef` parameter | The `AssetType` the argument names (`Reference.AssetTypeFilter`). |
| `SENCHA_DATA_ASSET(subtype)` | `AuthoredDataRef` parameter | A structured-data asset of that subtype, written as the literal or the constant naming it. |
| `SENCHA_LABEL`, `SENCHA_DESCRIPTION`, `SENCHA_CATEGORY` | any | Presentation only. On a parameter, the description is its `Summary`. |

Every parameter except a leading `const VerbInvocation&` needs `SENCHA_ARG` or
`SENCHA_TARGET`. The annotations expand to nothing outside the generator.

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
`Vec<2..4>`, and `std::optional` of any of those. An argument's type is
described and decoded (`IsAuthoredArgumentType`); a result's, a payload
member's or a record member's is described and encoded (`IsAuthoredResultType`).
Some types go one way only:

- **Read in place, for arguments.** `std::string_view` is the argument's own
  text, valid for the call, and `AuthoredDataRef` points at the data asset the
  argument names. A `const T&` parameter is bound to the argument's stored value
  where the trait can lend it (`Borrow`; `AssetRef` can) and decoded into a
  local otherwise (`const std::string&`). A verb that keeps any of them past the
  call copies what it keeps, as it copies anything it queues. With these a
  warmed invocation allocates nothing for a text or an asset argument; a
  `std::string` or a by-value `AssetRef` parameter costs a copy.
- **Answered whole, for results.** `std::vector<T>` answers an array of `T`,
  and a `SENCHA_RECORD` struct a record of its `SENCHA_FIELD` members, in
  declaration order (`equipment.sources` and the physics queries are their
  first consumers). A record's companion specializes `AuthoredValueTraits` for
  it, so it is used where its header's companion is included.

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
- An asset argument is written `{"asset": ...}` (or `{"data": ...}` for a data
  asset) and is a dependency of the asset that writes it: loaded with it, by the
  synchronous and the asynchronous load alike, and held for as long as it is. A
  definition that content declares — an ability, a cue presentation — is held
  while its content is published, so what its operations name is resident before
  any of them runs and is released when the content is. The document cannot know
  the kind the verb takes, so the dependency loads as whatever is at its path;
  the binding then compiles against the mounted assets, which requires the asset
  to be there and of the kind the argument declares
  (`DataReferenceConstraints::AssetTypeFilter`), and compiles the reference to the
  asset as it is now: its id, its current path, and its kind.

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
refuses to start on any; an editor reports them. Apart from the engine's own
vocabulary, which `InstallEngineVocabulary` declares into every runtime and
editor World (its verbs, and the `perception.*` queries in
`docs/plans/perception.md`), nothing is registered unless a hook declares it.

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

A provider whose vocabulary already holds registration scopes declares a
generated type into them with `DeclareAuthoredApi<T>(verbs, queries)`, which is
what `Declare<T>()` does for verbs and queries. The engine's own vocabulary
declares this way while its hand-written verbs share a provider with generated
ones: `InventoryOperations` reads its verbs and `inventory.count` by hand, and
declares `inventory.entry` with `SENCHA_QUERY`, bound by `BindAuthoredApi` on
the same object.

`BindAuthoredApi` puts one live object behind every verb and query its type
declares, looking each name up once, and returns the tokens. Resetting them
takes the object away. `Unbound()` lists anything declared that could not be
bound. A component's member queries are bound against a World; their readers
use the World's const access and answer `Unavailable` when the entity has no
such component.

## Catalogs

Each World holds three catalogs — `VerbRegistry`, `AuthoredQueryRegistry`,
`AuthoredEventRegistry` — all instances of `AuthoredCatalog`. They hold
metadata only; an editor installs them to offer and validate names without
being able to run anything.

- **Slots.** A name keeps its slot for the catalog's lifetime. A provider that
  stops declaring a name retires its slot rather than freeing it, and a name
  that comes back revives the same slot. A cached id therefore resolves to
  nothing rather than to whatever was declared next.
- **Revisions.** A slot's contract revision moves when its contract changes —
  arguments, result or payload, not presentation — and when a retired name is
  revived.
- **Generation.** Moves on every publish and retirement, so a compiled set can
  tell whether a name that failed to resolve might resolve now.
- **Registration.** A provider declares into a registration scope and commits.
  `CommitTogether` validates every scope's batch against its catalog before
  any catalog changes, so a conflict in one publishes nothing in any.
  `AuthoredVocabularyScope` does this across all three. Refused batches land in
  each catalog's `InstallationErrors`, which the host reads after the vocabulary
  hook; the runtime refuses to start on any, and editors report them.

The same template, outside the World, holds a host's candidate-evaluation
operations (`docs/spatial/candidates.md`); those catalogs are not part of the
authored vocabulary. A literal written in authored data compiles against its
field through `CompileAuthoredLiteral` (`authored/AuthoredLiteral.h`), shared by
verb bindings and the candidate `authored_query` measure. A response's sequence
(`docs/gameplay/responses.md`) compiles each command into an ordinary verb
binding whose inputs are its producer's ports. Consumers that rebuild
when a catalog, the tag vocabulary or an asset moves compare
`BindingDependencyStamp` values.

Bindings and subscriptions are owned by tokens. A token outliving its
dispatcher is inert, and a token cannot remove a replacement bound after it.
Binding or unbinding while a query is being answered, or a verb dispatched, is
refused.

## Verb kinds and classification

A verb definition carries three contract fields beside its arguments, and a
change to any of them moves its revision.

- **Kind.** An `Instant` verb runs once per request through `Invoke`. A
  `Lease` is held: `AcquireLease(binding, inputs, owner)` runs the same checks
  as `Invoke` and hands the implementation `Step = Acquire` with the holder's
  `LeaseOwner` (an entity and an opaque key); `ReleaseLease(verb, owner)` hands
  it `Step = Release` with the owner and no arguments, and the implementation
  ends whatever that owner holds through it. Each refuses the other kind
  (`KindMismatch`), an acquire with no owner is `InvalidArguments`, and a
  release from inside a dispatch is `Reentrant`. An effect hold whose owner's
  entity has gone ends in the next lifetime pass; an animation hold in that
  state is reported as orphaned, like any Held request its producer forgot.
  A lease whose definition `Completes` can finish while still held (a timed
  burst runs out, a tether's anchor goes); `LeaseHolds(verb, owner)` asks its
  provider with `Step = Probe`, answered `Accepted` while the hold goes on. A
  lease that does not complete is never probed, so a provider written before the
  probe existed never sees one; `Completes` is part of the contract.
- **Scope.** `Gameplay` or `Cosmetic`. A producer states its own scope in
  `VerbInvocationSource::Scope`, and a cosmetic producer that names a gameplay
  verb is refused (`ScopeMismatch`): presentation can never change simulation.
  A gameplay producer may ask for cosmetic operations.
- **Prediction.** `AuthorityOnly`, `Presentation` or `Simulation`: whether a
  predicting client may run the operation for its own pawn, never, wherever
  presentation runs, or as a journaled state change that replay repeats. A
  cosmetic verb cannot be `Simulation` and a gameplay verb cannot be
  `Presentation`; the catalog refuses either.

Animation events are producers with a scope: a cosmetic event bound to a
gameplay verb, or to a lease, is reported when the rig binds
(`anim.event.scope_mismatch`) and refused if it fires anyway.

The generated API declares all three. A lease is three methods on one
identity, because a release carries no arguments and a probe carries only the
owner:

```cpp
SENCHA_LEASE("movement.burst") SENCHA_PREDICTION(Simulation) SENCHA_COMPLETES
VerbAnswer Burst(const VerbInvocation& invocation, SENCHA_TARGET("target", CharacterMovement) EntityId target,
                 SENCHA_ARG("speed") SENCHA_MIN(0) float speed = 10.0f);
SENCHA_LEASE_RELEASE("movement.burst") VerbAnswer EndBurst(const VerbInvocation& invocation);
SENCHA_LEASE_HOLDS("movement.burst")   bool BurstHolds(const LeaseOwner& owner) const;
```

The generated adapter switches on `invocation.Step`: `Release` calls the release
method, `Probe` the holds method (a lease that does not complete is never
probed, and its adapter answers `Refused` to one), and `Acquire` decodes the
arguments and calls the annotated method. The generator refuses a lease without
a release, a completing lease without a holds method, a holds method on a lease
that does not complete, a release that takes arguments, and a result on a lease.

## What a verb answers

An implementation answers a `VerbAnswer`: a `VerbAdmission` and a 16-bit
`Detail`. An implementation that returns a plain `VerbAdmission` answers it with
no detail, and `Bind` takes either.

- **Refused with a reason.** A verb declares the reasons it may refuse with in
  `VerbDefinition::Refusals`, each a code and a name; `VerbRefusalsOf<E>()` lists
  every non-zero value of an enum with an `EnumSchema`, and `VerbAnswerOf(reason)`
  answers `Accepted` for its zero value and `Refused` with the code otherwise.
  The codes and names are part of the contract. The detail is only a number: a
  trace record outlives the module that declared the verb, so a reader resolves
  the name against the verb's catalog entry when it reads, and prints the number
  once that entry is retired.
- **An argument at fault.** `InvalidArguments` and `UnresolvedReference` carry
  one past the argument's slot (`VerbArgumentAnswer`), or 0 when no one argument
  is at fault. The dispatcher's own checks name the slot they reject.
- **Reading it.** `DescribeVerbAnswer(verbs, verb, answer)` reads `refused:
  container_full`, `invalid argument 'count'`, `unresolved reference 'target'`,
  or the admission's name. `VerbInvocationResult::Detail` and
  `VerbTraceRecord::Detail` carry it; a game's C++ compares the detail with its
  own enum. A response that runs a failed command logs it
  (`docs/gameplay/responses.md`, "Diagnostics"), and the Ability Workbench's
  lanes and the animation editor's admissions describe each op's answer.

**A result.** A verb that makes something can say what: `VerbDefinition::Result`
declares the field it answers with, and is part of the contract. A producer that
wants it passes `Invoke(binding, inputs, source, &result)`. The implementation
writes `*invocation.Result` when the pointer is not null and skips encoding when
it is; the dispatcher hands a verb that declares no result a null pointer
whatever the producer asked. The result is checked against the declaration
before it is handed on: a refusal hands on nothing, and a verb that accepts
without writing its result, or writes one of the wrong kind, is a provider
defect — the producer receives no value and the dispatcher logs it once per
verb. A verb that declares a result acts before it returns: one that queues its
work has nothing to answer yet, and cannot declare one. Nothing checks that
rule mechanically. A response's sequence names a command's result and reads it
in a later command (`docs/gameplay/responses.md`, "Results");
`inventory.give` and `inventory.take` answer the entry the units went into.

**From a generated method.** The return type says what the verb declares:

```cpp
VerbAdmission                      // no reasons, no result
VerbAnswer                         // a status and a detail the method sets itself
VerbRefusal<E>                     // E's reasons, from its EnumSchema; E's zero value refuses nothing
VerbResult<T, E = VerbNoReason>    // a value, or E's reason, or a status; needs SENCHA_RESULT
```

`VerbRefusal` and `VerbResult` (`authored/VerbResult.h`) fill `Refusals` from
`EnumSchema<E>`, and `VerbResult` fills `Result` from `T` under the key
`SENCHA_RESULT` names. The adapter encodes the value only when the call was
accepted and a producer asked for it. The adapter also answers the common cases
before the method runs: an argument that does not decode is `InvalidArguments`
naming its slot, and a `SENCHA_TARGET` entity that is not alive is
`UnresolvedReference` naming it (a present optional target is checked, an absent
one is not). For that check a provider whose verbs take a target answers
`const World& AuthoredWorld() const`; the companion refuses to compile one that
does not. Queries keep answering `Unavailable` for a target that does not apply.

The answer for the same situation is the same whichever verb gives it:

| Situation | Answer |
|---|---|
| A value of the wrong kind, out of range, an unknown enum choice or definition | `InvalidArguments`, naming the argument |
| An entity the request names is not in this World | `UnresolvedReference`, naming the argument |
| The feature is not installed in this World | `Unavailable`: nothing is bound, so the dispatcher answers |
| Anything else | `Refused`, with the verb's reason |
| A query whose arguments are well formed but do not apply | `Unavailable` |

The dispatcher checks kind, range and choice from the schema and resolves
entities named by persistent identity; everything else is the verb's.

## The engine's vocabulary

`InstallEngineVocabulary` (`app/EngineVocabulary.h`) declares, as provider
`engine`, into every runtime and editor World the verbs, leases, queries and
events listed in [the vocabulary reference](../reference/vocabulary.md): each
one's kind, scope and prediction, arguments with their defaults and ranges, a
verb's result and the reasons it may refuse with, and description.

The reference is rendered from the catalog by `RenderVocabularyReference`
(`authored/VocabularyReference.h`), one table per catalog in name order, so a
change to any contract shows as a reviewed diff of that one file.
`VocabularyReference.TheEnginesReferenceIsWhatItsCatalogDeclares` fails with the
difference when the file and the catalog disagree; run it with
`SENCHA_WRITE_VOCABULARY_REFERENCE=1` to rewrite the file. An argument reads
`key: type = default [minimum .. maximum] (units)`, and `key?` is one that may be
left out and has no default.

Both effect operations refuse a target without an attribute set, or without an
attribute the effect modifies. The physics queries are answered only in a host
whose game registered physics. `EngineVocabularyBindings` binds every operation
in the reference into a World's dispatchers; the runtime and the Ability Workbench's preview
both bind through it. The movement leases are described in
`docs/gameplay/movement.md`, "Held motion".

## From the console

Every declared verb and query can be run from the developer console:

```
verb  <name> [key=value ...]     verb inventory.give into=@me definition=items/key.sdata count=2
query <name> [key=value ...]     query counter.value name=quest.keys
```

`app/VocabularyCommands.h` builds a binding of constants from the line and
compiles it with `CompileVerbBinding` against the live catalog, so the schema
checks every value and fills every default, then invokes it through the
dispatcher with the local participant as instigator and prints the answer
`DescribeVerbAnswer` gives, and the result when the verb declares one. A value
is read as its argument's kind (`authored/AuthoredConstantText.h`): an entity is
`@me` (the entity this machine controls), `index:generation`, or a persistent
id; an asset or data path may leave out `asset://`. A key the verb does not take
is refused with the keys it does. A lease is refused, since a console line has
no owner to hold one. An `AuthorityOnly` verb is refused where this World is not
the authority: a client connected to a server runs it at the server's console.
Both commands are `Developer` commands.

`save.write [slot] [arrival]` and `save.restore [slot]` are aliases for `verb
save.write slot=... arrival=...` and `verb save.restore slot=...`. A developer
command that is not a verb or a query, such as `save.describe`, stays its own
command.

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
and an `Encode`; for each record an `AuthoredValueTraits` specialization; and a
table listing them. Each verb, lease and query also has
`static constexpr std::string_view Name_<Method>`, so C++ that must name it — a
test, a menu binding — reads the declaration instead of keeping a second
constant. Registries hold only the
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
