#pragma once

class ConsoleService;
struct FixedLogicContext;

//=============================================================================
// MovementTuningSystem
//
// Applies the live movement.* feel cvars to controlled pawns' MovementProfile each
// fixed tick, so designers tune locomotion from the dev console without a recompile.
//=============================================================================
class MovementTuningSystem
{
public:
    explicit MovementTuningSystem(ConsoleService& console) : Console(&console) {}
    void Init();
    void FixedLogic(FixedLogicContext& ctx);

private:
    ConsoleService* Console;
};
