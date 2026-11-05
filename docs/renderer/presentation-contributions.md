# Presentation contributions

`PresentationContributions<Key, Contribution>` (`presentation/PresentationContributions.h`)
is the one store for cosmetic state several writers feed at once. Mesh overrides, post
effects and outlines hold their contributions in it, so the owner rule exists once.

A contribution carries a `PresentationOwner` and an `Order`. `Set` adds or replaces by
owner and hands back what it replaced, so a caller can release what that held. `Resolve`
picks the winner: the highest `Order`, then the owner `PresentationOwnerPrecedes` puts
first, the same way on every machine and in any arrival order. It can take a filter, which
is how a post effect's audience is applied before the winner is chosen.

## Ending a contribution

Nothing survives by accident. A contribution ends when:

- its writer clears it, or everything it wrote (`ClearWriter`);
- its holder entity dies (`SweepDeadHolders`), if it named one;
- its key goes (`ForgetKey`), as when `MeshOverrides` is removed with its entity.

A contribution with no holder is the writer's to end.

## Presentation never writes simulation state

These stores are World resources local to this machine. Nothing in them is replicated,
saved, or a component, and no fixed-tick system reads them.
