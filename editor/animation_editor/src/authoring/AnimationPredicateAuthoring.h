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

// What every animation document holding conditions shares on its sheet: rows
// titled as they read, tests of the project's declared names, and an "or" to
// widen a row. A subtype's authoring composes it.
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
    [[nodiscard]] DataSubtypeEdit Perform(std::string_view key, std::string_view verb, std::string_view choice,
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

// The animation subtypes' sheets: selectors, slot maps and flows read their
// conditions; request schemas count parameters against their budget.
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSelectorAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSlotMapAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationFlowAuthoring();
[[nodiscard]] std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationRequestSchemaAuthoring();

void RegisterAnimationSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry);
