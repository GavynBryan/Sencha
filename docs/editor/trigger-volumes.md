# Trigger volume authoring

Trigger Volume is a configured choice of the ordinary Brush tool. Choose it
from the tool's variant menu or the `Trigger Volume` action in the command
palette. Drag a footprint in a viewport, adjust the pending brush using the
usual brush controls, and press Enter to keep it. Escape, focus loss or Undo
cancels the pending brush. Switching tools commits it, matching ordinary brush
placement. Creation is one undoable operation, including the volume role,
trigger settings, inline empty program, name and persistent identity.

The existing zone-bounds handles remain active when nothing is selected;
begin a creation drag away from a handle. Existing explicit vertex, edge and
face selection modes remain the way to edit geometry after placement.

Select an existing box brush and choose `Convert to Trigger Volume` to retain
its geometry, transform, name and identity while adding volume and trigger
semantics. A conversion batch is atomic and undoable. Space, shell, opening,
generated-wall and separately authored Collider ownership is refused with an
explanation. Edit or explicitly break a scene instance before converting one
of its projected members. The current sensor cooker accepts an exact oriented
box; simplify unsupported geometry rather than relying on an AABB approximation.

`Remove Empty Trigger and Make Solid` is the explicit reverse operation. It
refuses commands, conditions, eligibility filters, unknown program fields and
additional component ownership. Disabled unknown commands count as authored
data too. No conversion silently discards a program.

Duplicate uses the existing entity duplication workflow. Each copy receives
its own persistent identity and program storage. Unknown Verb and Query data
survives duplication and scene save/load even when its registration is absent;
active unresolved vocabulary still fails cook validation.

The brush is the sole geometric source. The authored entity does not carry a
second, independently editable Collider. Cooking derives the invisible sensor
through the [sensor cook](../physics/colliders.md), and a new play session binds
that cooked configuration. Live mutation of an already resident physics body
is outside that contract; see `docs/deferred.md`.

This creation/conversion milestone does not yet provide the orange lettered
presentation, command inspector or direction affordance. Those remain required
parts of the trigger authoring implementation; the complete bumper workflow
must not be inferred from creation alone.
