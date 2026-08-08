#include <input/InputActionSource.h>

#include <ecs/World.h>

#include <cassert>

InputActionState& InputActionSourceTable::Open(InputActionSourceId source,
                                               std::size_t actionCount)
{
    assert(source != kLocalInputActionSource
           && "source zero is the world's own action state, not a table slot");

    InputActionState& state = Sources[source];
    state.Configure(actionCount);
    return state;
}

InputActionState* InputActionSourceTable::Find(InputActionSourceId source)
{
    const auto it = Sources.find(source);
    return it == Sources.end() ? nullptr : &it->second;
}

const InputActionState* InputActionSourceTable::Find(InputActionSourceId source) const
{
    const auto it = Sources.find(source);
    return it == Sources.end() ? nullptr : &it->second;
}

void InputActionSourceTable::Close(InputActionSourceId source)
{
    Sources.erase(source);
}

InputActionSources::InputActionSources(const World& world)
    : Entities(&world)
    , Local(world.TryGetResource<InputActionState>())
    , Table(world.TryGetResource<InputActionSourceTable>())
{
}

InputActionView InputActionSources::Tick(InputActionSourceId source) const
{
    if (source == kLocalInputActionSource)
        return Local == nullptr ? InputActionView{} : Local->Tick();

    if (Table == nullptr)
        return InputActionView{};
    const InputActionState* state = Table->Find(source);
    return state == nullptr ? InputActionView{} : state->Tick();
}

InputActionView InputActionSources::TickFor(EntityId entity) const
{
    if (Entities == nullptr)
        return InputActionView{};

    // Probed per entity rather than read off the chunk, because most entities
    // carry no reference at all and the ones that do are counted in players.
    // Work here is proportional to what a pass already decided to steer.
    if (Entities->IsRegistered<InputActionSourceRef>())
    {
        if (const InputActionSourceRef* ref =
                Entities->TryGet<InputActionSourceRef>(entity))
        {
            return Tick(ref->Source);
        }
    }
    return Tick(kLocalInputActionSource);
}
