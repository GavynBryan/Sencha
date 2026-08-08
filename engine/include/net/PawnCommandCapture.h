#pragma once

#include <app/GameContexts.h>
#include <net/ClientPrediction.h>
#include <net/NetTickEstimator.h>

//=============================================================================
// PawnCommandCaptureSystem
//
// Writes down what this tick asked of the client's own pawn, once, in the one
// place everything downstream reads it from.
//
// The alternative -- and what this replaces -- is each consumer sampling for
// itself: the simulation reading the live components, the wire re-reading them
// at send time on the frame clock, and a replay having nothing to read at all.
// Three samples of the same intent taken at three moments are three different
// intents, and the difference reaches the authority as a pawn that turned
// somewhere the player did not.
//
// Runs after the pawn's intent has been derived and before anything acts on it,
// so what is captured is exactly what the tick went on to simulate.
//=============================================================================
class PawnCommandCaptureSystem
{
public:
    PawnCommandCaptureSystem(ClientPrediction& prediction,
                             const NetTickEstimator& clock)
        : Prediction(&prediction), Clock(&clock)
    {
    }

    void FixedLogic(FixedLogicContext& ctx);

private:
    ClientPrediction* Prediction = nullptr;
    const NetTickEstimator* Clock = nullptr;
};
