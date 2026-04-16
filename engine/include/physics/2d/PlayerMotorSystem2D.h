#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <core/event/EventBuffer.h>
#include <core/system/ISystem.h>
#include <input/InputTypes.h>
#include <physics/2d/CharacterMotor2D.h>

//=============================================================================
// PlayerMotorSystem2D
//
// Reads input action events and drives a single CharacterMotor2D.
// Produces DesiredMoveX and sets JumpRequested each frame, ready for
// KinematicMoveSystem2D to consume.
//
// Runs at SystemPhase::PreUpdate (after input, before the move phase).
//
// Action IDs are resolved by name outside this system (at setup time via
// InputBindingService::GetActionName lookups or the action registry) and
// passed at construction. This keeps the hot path ID-based with no string
// comparison.
//
// v0: single player only. The motor is referenced by key into the caller's
// DataBatch<CharacterMotor2D>. For multi-player, instantiate one system
// per player or extend with InputUserId filtering.
//=============================================================================
class PlayerMotorSystem2D : public ISystem
{
public:
    // actionMoveLeft  : InputActionId for "move left"  (Held trigger)
    // actionMoveRight : InputActionId for "move right" (Held trigger)
    // actionJump      : InputActionId for "jump"       (Pressed trigger)
    // motors          : batch owning CharacterMotor2D components
    // motorKey        : key of the player's motor in the batch
    // inputEvents     : the action event buffer from SdlInputSystem
    PlayerMotorSystem2D(InputActionId actionMoveLeft,
                        InputActionId actionMoveRight,
                        InputActionId actionJump,
                        DataBatch<CharacterMotor2D>& motors,
                        DataBatchKey motorKey,
                        const EventBuffer<InputActionEvent>& inputEvents);

private:
    void Update(const FrameTime& time) override;

    InputActionId ActionMoveLeft;
    InputActionId ActionMoveRight;
    InputActionId ActionJump;

    DataBatch<CharacterMotor2D>&         Motors;
    DataBatchKey                          MotorKey;
    const EventBuffer<InputActionEvent>& InputEvents;
};
