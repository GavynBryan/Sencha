#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <anim/AnimTypes.h>

#include <memory>
#include <string>
#include <vector>

class DataDocumentStore;

// The names a condition may read in a project: every fact its fact schemas
// declare or derive, and every request intent its request schemas declare,
// with the intent's parameters.
struct AnimationPredicateVocabulary
{
    struct Fact
    {
        std::string Name;
        AnimFactKind Kind = AnimFactKind::Bool;
    };
    struct Intent
    {
        std::string Name;
        std::vector<std::string> Params = {};
    };
    std::vector<Fact> Facts;
    std::vector<Intent> Intents;
};

[[nodiscard]] AnimationPredicateVocabulary GatherAnimationPredicateVocabulary(const DataDocumentStore& store);

// Selector and slot map documents read as conditions: each row titled as it
// reads ("Speed > 2.2"), added as a test of a named fact, request or time in
// behavior, widened with an "or", and a fact, intent or parameter chosen among
// those the project declares. A selector rule's stay starts as a copy of its
// enter.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSelectorAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSlotMapAuthoring();

void RegisterAnimationSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry);
