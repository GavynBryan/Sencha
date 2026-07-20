# Backend residency contract

A registry-local binding is justified when it owns retained objects in a shared backend that are derived from one registry's entities, reconciled against entity lifetime, and released with registry residency. Stateless queries, transient commands, gameplay interpretation, and one-shot interactions do not receive bindings.

Frame participation controls iteration. Registry residency transitions control retained backend state.

Every binding must state and test these edges:

- **Enter domain participation:** materialize or restore backend objects before the new frame view exposes the registry to that domain.
- **Participating:** reconcile entity topology and synchronize state through the domain's normal passes.
- **Leave domain participation:** remove backend presence while preserving component-authoritative state. A dormant registry has no contacts, query hits, solver work, voices, or other backend effect in the domain it left.
- **Detaching:** perform an unconditional final visit while the registry and sibling resources remain alive. Normal teardown empties the binding before registry destruction; destructors remain defensive fallback.
- **Return:** restore deterministically from authoritative component state.

Lifecycle mutations are recorded at their mutation sites and processed once per rendered frame before frame-view construction. The processing batch is stable: mutations raised during residency processing accumulate for the next frame.

Cross-registry relationships that must survive routine streaming use game-held, generational participation leases. Independent leases compose by union and release independently. Forced teardown may override a lease; the relationship reports its own terminal result.

Concrete bindings remain separate resources, named for their backend object family. Shared lifecycle shape does not justify a common base class or aggregate manager. After multiple implementations exist, only proven duplicated mechanics may be extracted.
