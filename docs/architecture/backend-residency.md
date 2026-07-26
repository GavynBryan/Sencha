# Backend residency contract

This document records the **behavioral contract** established by the current runtime foundation. The per-registry placement used by that implementation is transitional. The authoritative destination is `docs/plans/unified-runtime-world.md`.

Frame participation controls iteration. Zone residency controls retained backend presence.

Every retained backend record family must state and test these edges:

- **Enter domain participation:** materialize or restore backend objects before the frame view exposes the zone to that domain.
- **Participating:** reconcile entity topology and synchronize state through the domain's normal passes.
- **Leave domain participation:** remove backend presence while preserving component-authoritative state. A dormant zone has no contacts, query hits, solver work, voices, or other backend effect in the domain it left.
- **Detaching:** perform an unconditional final visit before zone entities and zone-scoped resources are destroyed. Destructors remain defensive fallback rather than the primary lifecycle path.
- **Return:** restore deterministically from authoritative component state.

Lifecycle mutations are processed once per rendered frame before frame-view construction. The processing batch is stable and read-only. Work raised while residency handlers run is queued for a later owner-thread drain point, preserving the invariant that backend state is corrected before a new frame view observes the corresponding zone state.

Participation leases are engine-owned and explicitly caller-held. Independent leases compose by union and release independently. Forced teardown invalidates affected tokens before the zone is destroyed so relationship owners can report a terminal result.

## Transitional implementation boundary

The current foundation stores rigid-body and character-mover ownership in registry resources because runtime zones are still separate registries. Do not extend that placement to new backend capabilities.

The unified runtime replaces those resources with one backend scene per simulation. Each backend scene remains a composition root over separate record-family modules with their own storage, reconciliation, diagnostics, zone indices, and tests. Forces, impulses, raycasts, and other transient operations do not receive retained record families.
