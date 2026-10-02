#pragma once

#include "data/DataSubtypeAuthoring.h"

#include <anim/AnimTypes.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class DataDocument;
class DataDocumentSet;
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

// What every animation document holding conditions shares on its sheet: each
// condition row titled as it reads ("Speed > 2.2"), added as a test of a named
// fact, request or time in behavior, widened with an "or", and a fact, intent
// or parameter chosen among those the project declares. A subtype's authoring
// composes it with what it adds of its own.
class AnimationConditionAuthoring
{
public:
    // `predicates`: the member keys that hold conditions, such as "enter".
    explicit AnimationConditionAuthoring(std::vector<std::string_view> predicates)
        : Predicates(std::move(predicates))
    {
    }

    // Reads the project's names again once the document has changed.
    void Update(const DataDocument& document, DataDocumentSet& documents);
    void Refine(std::vector<PropertyNode>& nodes, const JsonValue& data) const;
    [[nodiscard]] std::optional<bool> Perform(std::string_view key, std::string_view verb, std::string_view choice,
                                              JsonValue& data) const;
    [[nodiscard]] const AnimationPredicateVocabulary& Names() const { return Vocabulary; }

private:
    void RefineAll(std::vector<PropertyNode>& nodes, const JsonValue& data) const;
    void Refine(PropertyNode& node, const JsonValue& data) const;
    [[nodiscard]] std::optional<JsonValue> TestFor(std::string_view choice) const;
    [[nodiscard]] std::vector<std::string> ConditionChoices() const;

    std::vector<std::string_view> Predicates;
    AnimationPredicateVocabulary Vocabulary;
    std::string ReadFor;
    std::uint64_t ReadAt = 0;
};

// A text property made a choice among `names`, keeping the value it holds.
void ChooseAmong(PropertyNode& node, std::vector<std::string> names);
// The parameters the project declares for `intent`.
[[nodiscard]] std::vector<std::string> AnimationIntentParams(const AnimationPredicateVocabulary& names,
                                                             std::string_view intent);

// Selectors (enter and stay; a rule's stay starts as a copy of its enter),
// slot maps (when) and flows (branch and loop conditions, and control that
// only ever goes forward: AnimationFlowAuthoring.cpp); request schemas count
// each intent's parameters against their budget.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSelectorAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSlotMapAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationFlowAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationRequestSchemaAuthoring();

void RegisterAnimationSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry);
