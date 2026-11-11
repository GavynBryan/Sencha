# Claims

A claim is a set of places a group's members can hold: a role only so many may
take, a token only so many may spend, a signal that something is ready. The
group is a gameplay tag its members carry; the claim is another tag. Nothing
in a claim knows what it means. A squad's flanking role, a hive's two attack
tokens and an "in position" signal are three claims.

| Piece | Where | What it is |
| --- | --- | --- |
| `ClaimBoard` | `logic/Claims.h` | the World's claims: which holder holds a place in which group's claim |
| `claims.hold {group, claim, count = 1, share = 0}` | `logic/ClaimVocabulary.h` | a lease that holds a place while held |
| `claims.count {group, claim}` | | how many places are held |
| `claims.held {holder, group, claim}` | | whether an entity holds one |

- **Capacity.** A claim holds `count` places, or `share` of the group's
  members rounded down if that is more, so a role a quarter of a squad may
  take grows with the squad. Members are the live entities whose tags hold the
  group or a tag beneath it.
- **Holders.** A holder is a `LeaseOwner`. Taking a place a holder already
  holds succeeds and takes nothing more. An ability that holds a claim through
  `claims.hold` lets go when it ends, and a holder whose entity has gone holds
  nothing: its place is free on the next take.
- **Native use.** A game system that decides for its entities takes and
  releases places on the `ClaimBoard` directly, with the same capacity rule
  (`ClaimCapacity`, `CountGroupMembers`). `CollectGroupMembersWithin` finds the
  members near a point in identity order, for a squad that weighs its neighbours.
- **Authority.** Taking a place is refused off the authority; counts read
  whatever the World holds.
