# Attachments

An attachment is an ordinary entity worn by a body: a rifle in a hand, a cap on
a head, a scope on the rifle. It is placed at a named socket of the body's rig,
or at an entity's origin, and drawn from exactly the pose the body is drawn
from. What a game shows, and when, is the game's policy; this is the mechanism
it uses (`attach/Attachment.h`).

Joints and sockets are never entities. Joints live in the skeleton, poses in
the `AnimPosePool`, and sockets in the bound rig (see "Sockets" in
[animation.md](animation.md)). Only what a caller attaches is an entity.

## Attaching

```cpp
AttachToSocket(world, body, "hand_r", rifle, { .Offset = grip, .Owner = owner });
EntityId crowbar = CreateMeshAttachment(world, { .Root = body, .Socket = "hand_r",
                                                 .Mesh = mesh, .Materials = materials });
```

- `AttachToSocket` names a socket of the root's rig by its plain string name,
  looked up once in the bound rig. A name the rig lacks is refused
  (`SocketUnknown`) and nothing changes; a root with no rig is `RootUnposable`.
- `AttachToOrigin` places an entity at a mount's origin, by intent: a socket is
  never silently replaced by an origin. Mounting on another attachment chains
  (a scope on a rifle); the chain is listed on the body it is worn by.
- `CreateMeshAttachment` makes the common case, an entity drawing one static
  mesh, and creates nothing when the attach is refused.
  `UpdateMeshAttachment` changes it in place: placement, mesh, materials,
  visibility, shadows. A mesh change goes through `World::ReplaceComponent`, so
  its asset leases stay balanced and the entity keeps its archetype.
- Any number of attachments share one socket.
- `Detach` makes an attachment an ordinary entity again, left where it is now,
  still carrying whatever is mounted on it (which it then heads the list of).
- Attaching an entity already worn by another body moves it, and everything
  mounted on it, onto the new body's list and into its partition.

An attached entity is moved into its body's storage partition, parented to its
mount, and given `PresentedWith { Root }`. It must not be replicated or carry
`WorldTransformHistory` of its own (`NotCosmetic`): it is each machine's own
presentation, derived from replicated state the machine already holds.

Every attachment names its `PresentationOwner` (see
[mesh-overrides.md](../renderer/mesh-overrides.md#contributions)), compared
whole: `FindAttachment` finds one by owner, and `DestroyAttachmentsOf` ends
everything a writer made.

## Placement

`PlaceAttachments` runs every frame in `FramePhase::ExtractRender`, before
presentation propagation. For each attachment at a joint it reads the root's
presented model pose (`AnimPosePool::Slot::PresentedModel`, the same result the
skinned palette is built from, at the same alpha: the presentation alpha for a
root carrying `WorldTransformHistory`, else its last tick whole) and writes the
attachment's `LocalTransform`: joint, then socket offset, then the
attachment's own offset. Propagation then composes the root's interpolated
pose, so the attachment and whatever is mounted on it follow with no lag.
No socket name is hashed per frame; the joint index is cached.

| The root is | The attachment is drawn |
|---|---|
| Posed | At the presented joint |
| Not posed yet, or posing another skeleton | At the joint's bind pose, as the mesh is |
| Rebound after its rig reloaded | At the socket of the same name in the new binding |
| Rebound to a rig without that socket | Where it last was, `Lost`, with `attachment_socket_lost` reported once; re-adding the socket binds it again |

A rebind is noticed from the root's `AnimPoseState::BindingGeneration`, one
integer compare per attachment.

## Lifetime

Nothing scans for dead attachments. The body's `AttachmentSet` is the head of a
list through `Attachment::Next`, with no capacity limit, and hooks keep it exact:

| What ends | What happens |
|---|---|
| `DestroyAttachment` | The attachment and everything mounted on it, at once |
| An attachment destroyed any other way | It leaves the list in its own `OnRemove`; what was mounted on it is queued |
| The body: death, relevance loss, `DestroyPartition` | Its `AttachmentSet`'s `OnRemove` queues the whole list |
| The body moves partition | A partition follower (`ecs/PartitionFollowers.h`) moves the list with it |
| A writer | `DestroyAttachmentsOf(writer)` |
| The owner's holder entity | `PlaceAttachments` ends it the frame the holder is gone |

A hook only queues, since hooks may not destroy entities; `PlaceAttachments`
destroys what is queued before it places anything, so nothing outlives its
body by a frame.

## From content

`presentation.attach` is a cosmetic lease: while it is held, its target shows a
static mesh with one material at a named socket (or at its origin, for an empty
socket name). A phase's presented cue sustains it, so a prop is shown for
exactly as long as the phase that plays it, on every machine that presents the
phase, a late joiner included:

```json
{ "key": "k_ocarina", "cue": "cue.ocarina", "sustain": [ { "lease": "presentation.attach", "arguments": {
    "target": { "port": "actor" }, "socket": { "const": "hand_l" },
    "mesh": { "asset": "asset://meshes/ocarina.smesh" }, "material": { "asset": "asset://materials/ocarina.smat" } } } ] }
```

A refused acquire (no such socket, a mesh not resident) holds nothing, and its
release is a no-op. Acquiring again for the same holder changes the prop in
place. The prop's owner is the lease's holder, so it ends with the holder's
entity as well as with its release.

## Rendering

A camera excluding an entity also excludes every entity `PresentedWith` it
(`CameraRenderData::Excludes`, the one rule static and skinned extraction
share), so a first-person player sees neither their body nor what it wears.
`PresentedWith` names the outermost body, so what a worn entity itself wears is
hidden with that body too. Shadows ignore camera exclusion, as they do for the
body.

## Where attachments exist

Only on a World that presents: the engine installs them
(`InstallAttachments`) where a window presents. Every operation refuses with
`NotPresenting` elsewhere, so a dedicated server holds none and pays nothing.
Games gate their own presentation systems on `SystemRegisterContext::Presents`.
