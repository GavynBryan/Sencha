#include <net/PawnCommandCapture.h>

#include <controller/LookOrientation.h>
#include <ecs/World.h>
#include <input/InputActionState.h>
#include <movement/MovementIntent.h>

#include <algorithm>

void PawnCommandCaptureSystem::FixedLogic(FixedLogicContext& ctx)
{
    if (Prediction == nullptr || Clock == nullptr)
        return;

    const EntityId pawn = Prediction->Predicted();
    if (!pawn.IsValid() || !ctx.Entities.IsAlive(pawn))
        return;
    // Without a shared clock there is no name to file the tick under that the
    // authority would recognise, and a record it cannot place is a record it
    // cannot acknowledge.
    if (!Clock->HasEstimate())
        return;

    const MovementIntent* intent = ctx.Entities.TryGet<MovementIntent>(pawn);
    if (intent == nullptr)
        return;

    PawnCommandTick record;
    // Far enough ahead that the command lands before the authority reaches the
    // tick it asks for. This is the name both machines use for it.
    record.Tick = Clock->CommandTickAt(ctx.Time.TickIndex);
    record.Intent = *intent;

    if (const LookOrientation* look = ctx.Entities.TryGet<LookOrientation>(pawn))
    {
        record.Yaw = look->Yaw;
        record.Pitch = look->Pitch;
    }

    // The raw actions the authority re-derives its own intent from. What was
    // derived here travels beside them so a replay does not have to redo it,
    // but the authority never trusts the derivation -- only the actions.
    if (const InputActionState* actions =
            ctx.Entities.TryGetResource<InputActionState>();
        actions != nullptr && actions->HistoryCount() > 0)
    {
        const InputActionTickRecord source = actions->History(0);
        record.ActionCount = static_cast<std::uint8_t>(
            std::min<std::size_t>(source.Values.size(), kNetMaxCommandActions));
        for (std::uint8_t index = 0; index < record.ActionCount; ++index)
            record.Actions[index] = source.Values[index];
    }

    Prediction->Commands().Push(record);
}
