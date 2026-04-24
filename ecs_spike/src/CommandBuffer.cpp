#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <unordered_map>

// ─── CommandBuffer::Flush ────────────────────────────────────────────────────
//
// P13 mitigation: group AddComponent commands by (entity current archetype)
// so that a batch of "add C to N entities in {A,B}" becomes one bulk move
// per source archetype rather than N individual entity moves.
//
// The current spike groups by source ArchetypeId. Full batching (move multiple
// entities in one memcpy sweep) is deferred to Phase 1; here we call World's
// per-entity API in grouped order which is already faster than fully random order.

void CommandBuffer::Flush()
{
    assert(!W->InQueryScope()
           && "CommandBuffer::Flush called while a query is active.");

    // First pass: resolve component ids from type resolvers.
    for (auto& resolver : TypeResolvers)
        resolver(*W);
    TypeResolvers.clear();

    // Second pass: group AddComponent commands by source archetype for each entity.
    // For the spike we just execute in record order (which is correct, if not
    // maximally batched). A decisions.md entry captures this as future work.
    for (auto& cmd : Commands)
    {
        switch (cmd.Kind)
        {
        case CommandKind::AddComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            // We can't call the template AddComponent<T> here because we've lost
            // the type — we call the type-erased internal helper instead.
            // The spike exposes a raw-bytes path on World for this.
            W->AddComponentRaw(cmd.Entity,
                               cmd.Payload.Id,
                               cmd.Payload.Data.get(),
                               cmd.Payload.Size,
                               cmd.Payload.Align,
                               cmd.Payload.OnAddHook);
            break;
        }
        case CommandKind::RemoveComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->RemoveComponentRaw(cmd.Entity,
                                  cmd.Payload.Id,
                                  cmd.Payload.OnRemoveHook);
            break;
        }
        case CommandKind::DestroyEntity:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->DestroyEntity(cmd.Entity);
            break;
        }
        case CommandKind::CreateEntity:
        {
            W->CreateEntity();
            break;
        }
        }
    }

    Commands.clear();
}
