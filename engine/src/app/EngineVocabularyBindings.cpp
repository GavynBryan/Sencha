#include <app/EngineVocabularyBindings.h>

#include <abilities/AbilityVerbs.h>
#include <anim/AnimRequestVerbs.h>
#include <audio/AudioVerbs.h>
#include <effects/EffectVerbs.h>
#include <logic/ResponseVerbs.h>
#include <movement/MovementVerbs.h>
#include <navigation/NavigationVerbs.h>
#include <perception/PerceptionVocabulary.h>
#include <physics/PhysicsAuthoredQueries.h>
#include <physics/PhysicsQueries.h>
#include <time/ActorTimeVerbs.h>

#include <utility>

EngineVocabularyBindings::EngineVocabularyBindings(World& entities, const FixedSimulationLoop& clock,
                                                   VerbDispatcher& verbs)
    : Entities(entities)
    , Animation(std::make_unique<AnimRequestOperations>(entities, clock))
    , Effects(std::make_unique<EffectOperations>(entities, clock))
    , Time(std::make_unique<ActorTimeOperations>(entities, clock))
    , Abilities(std::make_unique<AbilityOperations>(entities))
    , Movement(std::make_unique<MovementOperations>(entities))
    , Navigation(std::make_unique<NavigationOperations>(entities))
    , Audio(std::make_unique<AudioOperations>(entities))
    , Responses(std::make_unique<ResponseOperations>(entities, clock))
{
    const VerbRegistry& catalog = verbs.Registry();
    Verbs.push_back(verbs.Bind<&InvokeAnimRequest>(catalog.Find(kAnimRequestVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimCancel>(catalog.Find(kAnimCancelVerb), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimHold>(catalog.Find(kAnimHoldLease), *Animation));
    Verbs.push_back(verbs.Bind<&InvokeAnimPresent>(catalog.Find(kAnimPresentVerb), *Animation));
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
    Verbs.push_back(verbs.Bind<&InvokeMovementLaunch>(catalog.Find(kMovementLaunchVerb), *Movement));
    Verbs.push_back(verbs.Bind<&InvokeNavigationMoveTo>(catalog.Find(kNavigationMoveToVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeNavigationApproach>(catalog.Find(kNavigationApproachVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeNavigationStop>(catalog.Find(kNavigationStopVerb), *Navigation));
    Verbs.push_back(verbs.Bind<&InvokeAudioPlay>(catalog.Find(kAudioPlayVerb), *Audio));
    Verbs.push_back(verbs.Bind<&InvokeAudioLoop>(catalog.Find(kAudioLoopLease), *Audio));
    Verbs.push_back(verbs.Bind<&InvokeResponsesEnable>(catalog.Find(kResponsesEnableVerb), *Responses));
    Verbs.push_back(verbs.Bind<&InvokeResponsesDisable>(catalog.Find(kResponsesDisableVerb), *Responses));
    Verbs.push_back(verbs.Bind<&InvokeTriggerInteract>(catalog.Find(kTriggerInteractVerb), *Responses));
}

EngineVocabularyBindings::~EngineVocabularyBindings()
{
    Queries.clear();
    Verbs.clear();
}

void EngineVocabularyBindings::BindPerception(AuthoredQueryDispatcher& queries)
{
    for (AuthoredQueryBindingToken& token : BindPerceptionQueries(queries, Entities))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindNavigation(AuthoredQueryDispatcher& queries)
{
    for (AuthoredQueryBindingToken& token : BindNavigationQueries(queries, Entities))
        Queries.push_back(std::move(token));
}

void EngineVocabularyBindings::BindPhysics(const PhysicsWorld& physics, AuthoredQueryDispatcher& queries)
{
    Physics = std::make_unique<PhysicsQueries>(physics);
    Queries.push_back(queries.Bind<&EvaluatePhysicsTrace>(queries.Registry().Find(kPhysicsTraceQuery),
                                                          std::as_const(*Physics)));
    Queries.push_back(queries.Bind<&EvaluatePhysicsOverlap>(queries.Registry().Find(kPhysicsOverlapQuery),
                                                            std::as_const(*Physics)));
}
