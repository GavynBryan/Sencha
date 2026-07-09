#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <cassert>
#include <utility>
#include <vector>

void CommandBuffer::ApplyCommands()
{
    for (size_t i = 0; i < Commands.size();)
    {
        Command& cmd = Commands[i];

        // Coalescing merges only a run of consecutive same-id adds that have no
        // native hook AND no runtime dispatch. An observed component (HasDispatch)
        // must be applied singly so its transition fires; the batch path fires
        // neither the native hook nor the dispatch.
        if (cmd.Kind == CommandKind::AddComponent && cmd.Payload.OnAddHook == nullptr
            && !cmd.Payload.HasDispatch)
        {
            const ComponentId id = cmd.Payload.Id;
            const size_t size = cmd.Payload.Size;
            const size_t align = cmd.Payload.Align;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::AddComponent
                   && Commands[end].Payload.Id == id
                   && Commands[end].Payload.Size == size
                   && Commands[end].Payload.Align == align
                   && Commands[end].Payload.OnAddHook == nullptr
                   && !Commands[end].Payload.HasDispatch)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<ComponentBatchItem> items;
                items.reserve(end - i);
                for (size_t j = i; j < end; ++j)
                {
                    if (!W->IsAlive(Commands[j].Entity)) continue;
                    items.push_back(ComponentBatchItem{
                        Commands[j].Entity,
                        PayloadData(Commands[j].Payload)
                    });
                }
                if (!items.empty())
                    W->AddComponentsRawBatch(id, items.data(), items.size(), size, align);
                i = end;
                continue;
            }
        }

        if (cmd.Kind == CommandKind::RemoveComponent && cmd.Payload.OnRemoveHook == nullptr
            && !cmd.Payload.HasDispatch)
        {
            const ComponentId id = cmd.Payload.Id;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::RemoveComponent
                   && Commands[end].Payload.Id == id
                   && Commands[end].Payload.OnRemoveHook == nullptr
                   && !Commands[end].Payload.HasDispatch)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<EntityId> entities;
                entities.reserve(end - i);
                for (size_t j = i; j < end; ++j)
                {
                    if (!W->IsAlive(Commands[j].Entity)) continue;
                    entities.push_back(Commands[j].Entity);
                }
                if (!entities.empty())
                    W->RemoveComponentsRawBatch(id, entities.data(), entities.size());
                i = end;
                continue;
            }
        }

        switch (cmd.Kind)
        {
        case CommandKind::AddComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->AddComponentRaw(
                cmd.Entity,
                cmd.Payload.Id,
                PayloadData(cmd.Payload),
                cmd.Payload.Size,
                cmd.Payload.Align,
                cmd.Payload.OnAddHook);
            break;
        }
        case CommandKind::RemoveComponent:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->RemoveComponentRaw(
                cmd.Entity,
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
        case CommandKind::SetField:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->ApplyFieldSet(cmd.Entity, cmd.Payload.Id, cmd.Payload.FieldOffset,
                             PayloadData(cmd.Payload), cmd.Payload.Size);
            break;
        }
        case CommandKind::DeltaField:
        {
            if (!W->IsAlive(cmd.Entity)) break;
            W->ApplyFieldDelta(cmd.Entity, cmd.Payload.Id, cmd.Payload.FieldOffset,
                               cmd.Payload.NumericKind, PayloadData(cmd.Payload),
                               cmd.Payload.Size);
            break;
        }
        }

        ++i;
    }
}

void CommandBuffer::Flush()
{
    assert(!W->InQueryScope()
           && "CommandBuffer::Flush called while a query is active.");

    // Wave 0 is this buffer's recorded commands. A synchronous observer's
    // deferred effects append to nextWave (the world's active reaction scope);
    // after wave 0 the flush drains nextWave, and so on to quiescence. This
    // realizes the breadth-first wave model: effects observed in wave N apply in
    // wave N+1, never interleaved into the wave being applied.
    CommandBuffer nextWave(*W);
    CommandBuffer* previous = W->SetActiveReactions(&nextWave);

    ApplyCommands();

    uint32_t waves = 0;
    while (!nextWave.IsEmpty())
    {
        CommandBuffer current(std::move(nextWave));
        nextWave = CommandBuffer(*W);
        W->SetActiveReactions(&nextWave);

        current.ApplyCommands();

        if (++waves > kMaxReactionWaves)
        {
            assert(false
                   && "Reaction wave cycle guard exceeded: an observer cascade "
                      "did not converge.");
            break;
        }
    }

    W->SetActiveReactions(previous);

    Commands.clear();
    PayloadArena.clear();
}
