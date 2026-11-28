# World links

A world link joins two places that do not touch. It is a `WorldLink` entity in
the world scene. With no endpoints it is a demand edge: the linked zone streams
in as a one-hop neighbour and nothing else happens. With two endpoints it is a
*spatial link* between two *apertures*, planar rectangles authored in zones. What
goes in at the front of one comes out at the front of the other, carried by one
rigid mapping. Designers call this a portal; engine identifiers use the
mechanism names here, because "portal" already names a navmesh polygon edge.

This document is the reference for what exists. The execution plan for the work
still to come (traversal, views, bodies, light) is
`docs/plans/spatial-portals/`.

## Authored data

| Data | Where | Meaning |
|---|---|---|
| `LinkAperture { Vec2d HalfExtents }` | `zone/WorldConnectionComponents.h`, chunk `LAPT` | One end. The entity's transform is the frame. Default half extents 1 x 2 m. Placeable from the Entity tool. |
| `WorldLink::EndpointA`, `EndpointB` | same header, chunk `WLNK` | `PersistableRef`s to the two apertures. Both set: spatial. Neither: demand-only. One: invalid. |
| `WorldLink::ZoneA`, `ZoneB` | same | For a spatial link, the zones holding the apertures. They must agree. A same-zone link names one zone twice. |
| `WorldLink::Directions` | same | `DockDirectionAToB` lets a traveller enter A and leave B; `BToA` the reverse. |

An aperture holds nothing else: no partner, link id, matrix, zone, target, body
or state. Endpoint references are persistent entity ids, which are unique across
a world, so a reference needs no zone qualifier. A scene never holds the live
`EntityId` half of a reference. A `WLNK` chunk written before endpoints existed
reads as a demand-only link.

## Frame convention

- The origin is the aperture's world position.
- The front normal is the entity's forward (-Z local). Right is +X, up is +Y.
- The front has positive signed distance.
- A traveller enters through the front of one aperture and leaves through the
  front of the other. The mapping from A to B therefore includes a half turn
  about the aperture's up axis: entering A's front, your right becomes your left
  relative to B's axes, and up stays up.
- A valid aperture has a finite position and rotation, a rotation that
  normalizes, every scale axis within `ApertureTolerance::UnitScale` of one, and
  finite positive half extents.
- A link is *upright* when its mapping turns world up onto world up within
  `ApertureTolerance::Upright`. Only upright links can carry characters (their
  facing is yaw and pitch about world up). Any valid link can carry everything
  else.

`ApertureMapping(entry, exit)` returns a unit-scale `Transform3f`. Apply it to a
point with `TransformPoint`. Apply it to a direction, a linear velocity or an
angular velocity with `Rotation.RotateVector`; under a proper rotation an angular
velocity turns like any vector. Apply it to an orientation by composing
`Rotation * orientation`.

## Geometry kernel

`math/geometry/3d/ApertureFrame.h` holds pure functions over explicit values.
They touch no ECS, renderer, physics or cvar.

| Function | Answers |
|---|---|
| `CheckApertureFrame`, `MakeApertureFrame` | whether a transform and extents make a valid frame, and the frame |
| `ApertureSignedDistance`, `ClassifyApertureSide` | which side of the plane a point is on |
| `ApertureCoordinates`, `InsideApertureRectangle` | where a point falls in the rectangle, with an explicit edge tolerance |
| `SphereSupportInterval`, `CapsuleSupportInterval` | the nearest and farthest signed distance a shape reaches |
| `CrossAperture` | where a segment passes through the rectangle, and which way |
| `ApertureMapping`, `IsUprightMapping` | the rigid transform between two frames, and whether it keeps up |

The plane belongs to the front for crossings. A segment that ends on the plane
has not crossed, and one that starts on it and goes behind crosses at fraction
zero, so consecutive segments count each crossing exactly once.

Tolerances are named, one per purpose, in `ApertureTolerance`: `PlaneSide`,
`RectangleEdge`, `UnitScale`, `Upright`, `MatchingExtents`. The tolerances that
only character traversal uses (capsule fit, exit separation, clear hysteresis)
arrive with traversal.

`ContinueThroughLinks` (`zone/ResolvedLinks.h`) is the one-hop segment
continuation that traces, projectiles and particles share. Given a segment and
the resolved links, it returns the first aperture the segment enters from the
front, in a direction its link allows, and the remainder carried through the
mapping. The earliest crossing wins. A tie goes to the lower link id, then to
side A. The remainder is not tested again.

## Cook

`CollectWorldCookInput` (`editor/level_editor/src/document/WorldCook.cpp`) loads
every zone. `WorldDocument::CollectAuthoredApertures` reads each zone's apertures
with their world transforms. `ValidateAuthoredLinks`
(`zone/WorldLinkValidation.h`) judges every link. An error fails the cook and a
warning is logged. `CookWorldTopology` then writes each side's frame onto that
side's `LinkEndpoint::Aperture` in the cooked manifest. A demand-only link has no
frame. A spatial link puts its endpoint A in zone A and endpoint B in zone B, and
both in one zone when it is a same-zone link.

The live validation in Kyusu (`WorldDocument::RunValidation`) runs the same
rules against the zones that are open. A reference into a zone that is not open
is not judged.

### Validation

Link records are sorted by link id, then rule. They are followed by the
unlinked-aperture warnings, sorted by aperture id.

| Rule | Severity | Fires when |
|---|---|---|
| `partition.link.endpoint_one` | error | one endpoint is set |
| `partition.link.endpoint_not_aperture` | error | an endpoint names no aperture in the loaded zones, and its zone is loaded |
| `partition.link.endpoint_same` | error | both endpoints name one aperture |
| `partition.link.aperture_claimed_twice` | error | another link names the same aperture; reported on each link |
| `partition.link.aperture_invalid` | error | the aperture's frame or extents are invalid |
| `partition.link.extents_differ` | error | the two half extents differ by more than `MatchingExtents` |
| `partition.link.zone_mismatch` | error | `ZoneA` or `ZoneB` is not the zone holding the aperture |
| `partition.link.zones_overlap` | error | the two zones' bounds overlap in volume and no dock joins them; one Jolt world means overlapping content would collide |
| `partition.link.not_upright` | warning | the link does not keep up; characters cannot pass it |
| `partition.link.aperture_unlinked` | warning | no link names an aperture |

`partition.link.endpoint_invalid` (`WorldPartitionValidation`) also checks the
cooked manifest: either both endpoints carry a frame or neither does, and every
frame is orthonormal with positive extents.

The recess-depth check (an aperture needs free volume behind it as deep as a
character's radius) is not here. The cook has no character profiles and no
collision query; see `docs/deferred.md`, "Recess depth behind a link aperture".

### Cook identity

A link's frames live in the world manifest. No zone's cooked content depends on
a link, so editing a link restales no zone, and editing an aperture restales
only the zone that holds it. The manifest is written in id order, so input order
does not change its bytes.

## Resolution

`ResolvedLinks` (`zone/ResolvedLinks.h`) is a World resource.
`ResolvedLinksSystem` brings it up to date once a frame, in `PreSimulate`, after
zone residency and before any tick. The active span therefore does not change
during the frame's fixed ticks or its render extraction. Every host registers
the system. No worker writes the table.

Each authored link has one `LinkResolution`, recomputed in one function:

| State | Means |
|---|---|
| `DemandOnly` | no endpoints |
| `AwaitingA`, `AwaitingB`, `AwaitingBoth` | the named zone is not resident. This is normal and is not diagnosed. |
| `Active` | both apertures are resident and valid. The link appears in `Active()`. |
| `Invalid` | malformed, with a `LinkFault`, logged once when the link enters the state |

The faults are checked in this order: `OneEndpoint`, `SameAperture`,
`ClaimedTwice` (decided from authored data alone, so it does not depend on
residency), then per side `NotAnAperture` (the zone is resident but the
reference names no aperture there), `ZoneMismatch` and `InvalidFrame`, then
`ExtentsDiffer` and `Moved`. Endpoints are static. An aperture whose frame
changes after its link resolved invalidates the link with `Moved` until the
aperture is rebuilt, for example by its zone reloading.

A side resolves through `ResolvePersistableRef`. A stale cached `EntityId` is
dead, because entity ids are generational, so a reload resolves to the new
entity.

`Active()` and `States()` are sorted by link id. `Transitions()` lists each state
change from the last update once, with `Absent` for a link that appeared or went
away.

### When it rebuilds

An update rebuilds the whole table only when one of these may have changed:

- a `WorldLink` (a cached `Changed<WorldLink>` query);
- a `LinkAperture` or an aperture's `LocalTransform`;
- a tracked link or aperture entity's life;
- the residency of a zone a spatial link names.

Change queries compare one frame behind the last update. A write later in the
frame of an update is therefore still seen, and a change costs one redundant
rebuild on the following frame. A quiet frame costs one empty pass of the three
change queries, plus a liveness check and two residency lookups per spatial
link. In a world with no links, the queries match no archetype and no entity is
read. A rebuild is O(L log L) in authored links and allocates. There is no cap:
the table holds exactly the authored links.

`ZoneDemand` already treats a link as a one-hop edge, so a linked zone streams
in with neighbour participation (visible and physics by default). With
`HopCount` zero, a same-graph linked zone is not demanded and the link stays
`Awaiting`. Consumers must refuse to cross a link that is not `Active`.

## Debug

- `links` (console) prints every link's state and fault, `(not upright)` for an
  active link that is not, and the counters: active, awaiting, invalid,
  demand-only and total rebuilds.
- `world.links.debug.draw` (cvar, off) draws each active link's rectangles and
  front normals, coloured by whether the link is upright, and a marker behind
  aperture A together with where the mapping carries it. Link ids and states are
  read from `links`; the debug line channel draws no text.
- `ResolvedLinks::Stats()` holds the same counters for tools and tests.

## Reachability

`world.reachable` and `world.bypasses` (Kyusu level operations) walk a spatial
link the way they walk a dock. A walker reaches the mouth of the aperture it
enters, then lands one step in front of the other aperture. The walk works
within a zone and between two. The result lists links crossed under `links`.
Demand-only links, links whose apertures are missing, and links that are not
upright are listed under `unconsidered`.
