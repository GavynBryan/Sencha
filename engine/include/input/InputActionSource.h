#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <input/InputActionState.h>

#include <cstdint>
#include <type_traits>
#include <unordered_map>

class World;

//=============================================================================
// Input action sources
//
// Whose actions steer a given entity.
//
// With one player on one machine the question never comes up: there is one
// resolved InputActionState, it came from this machine's devices, and anything
// a player controls reads it. The moment a second player's actions exist in the
// same World -- arriving over a session, replayed from a recording, produced by
// a test -- "the resolved actions" stops being a single thing, and every
// consumer that read the resource directly is quietly steering every pawn at
// once.
//
// So an entity references a source, and a source is one InputActionState.
// Source zero is this machine's own, which stays the world resource the resolve
// pass fills; every other source is a slot in the table below. An entity with
// no reference reads source zero, which is why single-player content needs no
// reference at all and nothing about it changes.
//
// The input layer owns this because it owns action state. It knows nothing
// about where a non-local source's values came from: filling one is the job of
// whatever produced them.
//=============================================================================

using InputActionSourceId = std::uint32_t;

// This machine's own devices. Never a table slot -- it is the world resource.
inline constexpr InputActionSourceId kLocalInputActionSource = 0;

//-----------------------------------------------------------------------------
// InputActionSourceIds
//
// The one place a non-local source id comes from.
//
// Sources have several producers -- a session admitting a peer, a bot, a script
// driving an actor through a scene -- and they share one id space. Each
// producer numbering its own is how a bot ends up reading a peer's actions, and
// the collision reads as a possession defect rather than an allocation one.
//
// Minted and never reused. A reference outliving the thing it named then names
// nothing rather than whoever came next, which is the same reason a NetEntityId
// is minted from one.
//-----------------------------------------------------------------------------
class InputActionSourceIds
{
public:
    [[nodiscard]] InputActionSourceId Allocate() { return ++Last; }

private:
    // Source zero belongs to this machine's own devices and is never handed out.
    InputActionSourceId Last = kLocalInputActionSource;
};

//-----------------------------------------------------------------------------
// The reference an entity carries. Absent means source zero, so this exists
// only on entities some other source steers.
//-----------------------------------------------------------------------------
struct SENCHA_COMPONENT("sencha.input_action_source_ref") InputActionSourceRef
{
    InputActionSourceId Source = kLocalInputActionSource;
};

static_assert(std::is_trivially_copyable_v<InputActionSourceRef>,
              "InputActionSourceRef must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <input/InputActionSource.sencha.h>
#endif

//-----------------------------------------------------------------------------
// InputActionSourceTable
//
// The non-local sources, as a world resource. Ids are the caller's namespace;
// the table only requires that they are not zero, which belongs to the local
// one.
//-----------------------------------------------------------------------------
class InputActionSourceTable
{
public:
    // Opens the state for a source, sized to an action vocabulary. Calling it
    // again for a live source is how a caller keeps it sized as the vocabulary
    // changes; Configure clears the history when the count moves, because
    // records indexed by one vocabulary cannot be read with another.
    InputActionState& Open(InputActionSourceId source, std::size_t actionCount);

    [[nodiscard]] InputActionState* Find(InputActionSourceId source);
    [[nodiscard]] const InputActionState* Find(InputActionSourceId source) const;

    void Close(InputActionSourceId source);
    void Clear() { Sources.clear(); }

    [[nodiscard]] std::size_t Size() const { return Sources.size(); }

private:
    std::unordered_map<InputActionSourceId, InputActionState> Sources;
};

//-----------------------------------------------------------------------------
// InputActionSources
//
// The resolution rule, as a value a system builds once per pass. Systems steer
// several entities from several sources, and finding the resources for each one
// would be a lookup per entity for an answer that cannot change inside a pass.
//
// The views it returns are the ones the tick clock resolved, and stay valid
// until the next resolve pass.
//-----------------------------------------------------------------------------
class InputActionSources
{
public:
    explicit InputActionSources(const World& world);

    // The tick actions for one source. An id nothing has opened resolves to an
    // empty view rather than to the local player's actions: a pawn whose source
    // has gone silent does nothing, which is the truth, instead of mirroring
    // whoever is sitting at this keyboard.
    [[nodiscard]] InputActionView Tick(InputActionSourceId source) const;

    // The tick actions steering an entity, by the reference it carries.
    [[nodiscard]] InputActionView TickFor(EntityId entity) const;

private:
    const World* Entities = nullptr;
    const InputActionState* Local = nullptr;
    const InputActionSourceTable* Table = nullptr;
};
