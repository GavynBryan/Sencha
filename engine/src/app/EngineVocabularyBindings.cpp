#include <app/EngineVocabularyBindings.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <effects/EffectVerbs.h>
#include <movement/MovementVerbs.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <physics/PhysicsQueries.h>
#include <time/ActorTimeVerbs.h>

#include <utility>

EngineVocabularyBindings::EngineVocabularyBindings(World& entities, const FixedSimulationLoop& clock,
                                                   VerbDispatcher& verbs)
    : Animation(std::make_unique<AnimRequestOperations>(entities, clock))
    , Effects(std::make_unique<EffectOperations>(entities, clock))
    , Time(std::make_unique<ActorTimeOperations>(entities, clock))
    , Abilities(std::make_unique<AbilityOperations>(entities))
    , Movement(std::make_unique<MovementOperations>(entities))
{
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeAnimRequest>(catalog.Find(kAnimRequestVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimCancel>(catalog.Find(kAnimCancelVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimHold>(catalog.Find(kAnimHoldLease), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeEffectApply>(catalog.Find(kEffectApplyVerb), *Effects));
    Verbs.push_back(verbs.Bind<&InvokeEffectHold>(catalog.Find(kEffectHoldLease), *Effects));
    Verbs.push_back(verbs.Bind<&InvokeTimeScale>(catalog.Find(kTimeScaleLease), *Time));
    Verbs.push_back(verbs.Bind<&InvokeAbilityActivate>(catalog.Find(kAbilityActivateVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityCancel>(catalog.Find(kAbilityCancelVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityGrant>(catalog.Find(kAbilityGrantVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeAbilityRevoke>(catalog.Find(kAbilityRevokeVerb), *Abilities));
    Verbs.push_back(verbs.Bind<&InvokeMovementBurst>(catalog.Find(kMovementBurstLease), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementTether>(catalog.Find(kMovementTetherLease), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeMovementMode>(catalog.Find(kMovementModeLease), *Movement));
}

EngineVocabularyBindings::~EngineVocabularyBindings()
{
    Queries.clear();
    Verbs.clear();
}

void EngineVocabularyBindings::BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries)
{
    Physics = std::make_unique<PhysicsQueries>(physics);
    Queries.push_back(queries.Bind<&EvaluatePhysicsTrace>(queries.Registry().Find(kPhysicsTraceQuery),
                                                          std::as_const(*Physics)));
    Queries.push_back(queries.Bind<&EvaluatePhysicsOverlap>(queries.Registry().Find(kPhysicsOverlapQuery),
                                                            std::as_const(*Physics)));
}
