#include <physics/2d/PlayerMotorSystem2D.h>

PlayerMotorSystem2D::PlayerMotorSystem2D(
    InputActionId actionMoveLeft,
    InputActionId actionMoveRight,
    InputActionId actionJump,
    DataBatch<CharacterMotor2D>& motors,
    DataBatchKey motorKey,
    const EventBuffer<InputActionEvent>& inputEvents)
    : ActionMoveLeft(actionMoveLeft)
    , ActionMoveRight(actionMoveRight)
    , ActionJump(actionJump)
    , Motors(motors)
    , MotorKey(motorKey)
    , InputEvents(inputEvents)
{}

void PlayerMotorSystem2D::Update(float /*dt*/)
{
    CharacterMotor2D* motor = Motors.TryGet(MotorKey);
    if (!motor) return;

    // Reset per-frame desired input before scanning events
    motor->DesiredMoveX   = 0.0f;
    motor->JumpRequested  = false;

    bool movingLeft  = false;
    bool movingRight = false;

    for (const InputActionEvent& ev : InputEvents.Items())
    {
        if (ev.Action == ActionMoveLeft)
        {
            if (ev.Phase == InputPhase::Performed || ev.Phase == InputPhase::Started)
                movingLeft = true;
        }
        else if (ev.Action == ActionMoveRight)
        {
            if (ev.Phase == InputPhase::Performed || ev.Phase == InputPhase::Started)
                movingRight = true;
        }
        else if (ev.Action == ActionJump)
        {
            if (ev.Phase == InputPhase::Started)
                motor->JumpRequested = true;
        }
    }

    if (movingRight && !movingLeft)  motor->DesiredMoveX =  1.0f;
    else if (movingLeft && !movingRight) motor->DesiredMoveX = -1.0f;
}
