#include <ecs/CommandBuffer.h>
#include <ecs/World.h>

#include <cassert>
#include <cstring>
#include <vector>

std::optional<std::uint32_t> CommandBuffer::ClaimForRun(EntityId entity, std::uint32_t item)
{
    if (entity.Index >= RunClaims.size())
        RunClaims.resize(entity.Index + 1);
    RunClaim& claim = RunClaims[entity.Index];
    if (claim.Run == CurrentRun)
        return claim.Item;
    claim = RunClaim{ CurrentRun, item };
    return std::nullopt;
}

void CommandBuffer::Flush()
{
    assert(!W->InQueryScope()
           && "CommandBuffer::Flush called while a query is active.");

    // Entities created during this flush, in the order their CreateEntity commands
    // were recorded, so a command addressing a PendingEntity can name one. A
    // handle can only be obtained from an earlier CreateEntity call and commands
    // execute in record order, so the creation an ordinal names has always run by
    // the time something addresses it.
    std::vector<EntityId> created;
    const auto resolve = [&created](const Command& command)
    {
        if (command.PendingOrdinal == PendingEntity::InvalidOrdinal)
            return command.Entity;

        assert(command.PendingOrdinal < created.size()
               && "a command addressed a creation that has not run yet");
        return created[command.PendingOrdinal];
    };

    // The batch paths fire no lifecycle hooks, so only a hook-free component may
    // take them. That is a fact about the component rather than about the
    // recording, which is why it is asked of the World here and not captured
    // when the command was recorded: the unit that records an add need not have
    // the component's ComponentTraits in scope.
    const auto hasAddHook = [this](ComponentId id) {
        const ComponentMeta* meta = W->GetMeta(id);
        return meta != nullptr && meta->OnAddHook != nullptr;
    };
    const auto hasRemoveHook = [this](ComponentId id) {
        const ComponentMeta* meta = W->GetMeta(id);
        return meta != nullptr && meta->OnRemoveHook != nullptr;
    };

    const auto overwrite = [this](EntityId entity, ComponentId id, const void* payload, size_t size)
    {
        if (size > 0 && payload != nullptr)
            std::memcpy(W->GetComponentRaw(entity, id), payload, size);
    };

    for (size_t i = 0; i < Commands.size();)
    {
        Command& cmd = Commands[i];

        if (cmd.Kind == CommandKind::AddComponent && !hasAddHook(cmd.Payload.Id))
        {
            const ComponentId id = cmd.Payload.Id;
            const size_t size = cmd.Payload.Size;
            const size_t align = cmd.Payload.Align;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::AddComponent
                   && Commands[end].Payload.Id == id
                   && Commands[end].Payload.Size == size
                   && Commands[end].Payload.Align == align)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<ComponentBatchItem> items;
                items.reserve(end - i);
                ++CurrentRun;
                for (size_t j = i; j < end; ++j)
                {
                    const EntityId entity = resolve(Commands[j]);
                    if (!W->IsAlive(entity)) continue;
                    const void* payload = PayloadData(Commands[j].Payload);
                    if (W->HasComponent(entity, id))
                    {
                        overwrite(entity, id, payload, size);
                        continue;
                    }
                    const auto item = static_cast<std::uint32_t>(items.size());
                    if (const auto earlier = ClaimForRun(entity, item))
                        items[*earlier].Blob = payload;
                    else
                        items.push_back(ComponentBatchItem{ entity, payload });
                }
                if (!items.empty())
                    W->AddComponentsRawBatch(id, items.data(), items.size(), size, align);
                i = end;
                continue;
            }
        }

        if (cmd.Kind == CommandKind::RemoveComponent && !hasRemoveHook(cmd.Payload.Id))
        {
            const ComponentId id = cmd.Payload.Id;

            size_t end = i + 1;
            while (end < Commands.size()
                   && Commands[end].Kind == CommandKind::RemoveComponent
                   && Commands[end].Payload.Id == id)
            {
                ++end;
            }

            if (end - i > 1)
            {
                std::vector<EntityId> entities;
                entities.reserve(end - i);
                ++CurrentRun;
                for (size_t j = i; j < end; ++j)
                {
                    const EntityId entity = resolve(Commands[j]);
                    if (!W->IsAlive(entity) || !W->HasComponent(entity, id)) continue;
                    if (!ClaimForRun(entity, 0).has_value())
                        entities.push_back(entity);
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
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity)) break;
            if (W->HasComponent(entity, cmd.Payload.Id))
            {
                overwrite(entity, cmd.Payload.Id, PayloadData(cmd.Payload), cmd.Payload.Size);
                break;
            }
            W->AddComponentRaw(
                entity,
                cmd.Payload.Id,
                PayloadData(cmd.Payload),
                cmd.Payload.Size,
                cmd.Payload.Align);
            break;
        }
        case CommandKind::RemoveComponent:
        {
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity) || !W->HasComponent(entity, cmd.Payload.Id)) break;
            W->RemoveComponentRaw(entity, cmd.Payload.Id);
            break;
        }
        case CommandKind::DestroyEntity:
        {
            const EntityId entity = resolve(cmd);
            if (!W->IsAlive(entity)) break;
            W->DestroyEntity(entity);
            break;
        }
        case CommandKind::CreateEntity:
        {
            assert(cmd.PendingOrdinal == created.size()
                   && "creation ordinals must match their execution order");
            created.push_back(W->CreateEntity(cmd.Partition));
            break;
        }
        }

        ++i;
    }

    Commands.clear();
    PayloadArena.clear();
    EndRecording();
}
