#include "authoring/AnimationPredicateAuthoring.h"

#include "authoring/AnimationPredicateEdits.h"
#include "authoring/AnimationPredicateText.h"
#include "authoring/AnimationSelectorEdits.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
#include "data/DataDocumentStore.h"
#include "data/DataPath.h"

#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>

namespace
{
constexpr std::string_view kFactChoice = "Fact: ";
constexpr std::string_view kRequestChoice = "Request: ";
constexpr std::string_view kElapsedChoice = "Time in behavior";

template <typename T>
std::shared_ptr<const T> Compiled(const DataDocumentStore& store, std::string_view path, std::string_view type)
{
    const std::optional<JsonValue> root = store.CurrentRoot(path);
    const JsonValue* data = root ? root->Find("data") : nullptr;
    const DataAssetTypeRegistration* compiler = store.Types().Find(type);
    const DataSchema* schema = store.SchemaOf(type);
    std::vector<DataValidationError> errors;
    if (data == nullptr || compiler == nullptr || schema == nullptr || !ValidateDataAgainstSchema(*data, *schema, errors))
        return nullptr;
    const DataAssetCompileResult result = CompileDataAsset(*compiler, *data, *store.PathsById());
    return result.IsValid() ? std::static_pointer_cast<const T>(result.Value) : nullptr;
}

// Where a property sits among a predicate's parts, read from its key.
struct PredicatePlace
{
    enum class Part : std::uint8_t
    {
        None,
        Rows,   // the predicate itself: its rows
        Row,    // one row: a test, or an any-of group
        Member, // a field of a test
    };
    Part What = Part::None;
    std::string Member = {};
};

PredicatePlace PlaceOf(std::string_view key, std::span<const std::string_view> predicates)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(key);
    if (!steps || steps->empty())
        return {};
    const auto isPredicate = [&](const DataPathStep& step) {
        return !step.Index && std::ranges::find(predicates, step.Key) != predicates.end();
    };
    const std::size_t count = steps->size();
    const DataPathStep& last = steps->back();
    if (isPredicate(last))
        return { .What = PredicatePlace::Part::Rows };
    const auto within = std::ranges::find_if(*steps, isPredicate);
    if (within == steps->end())
        return {};
    if (last.Index && count >= 2 && (isPredicate((*steps)[count - 2]) || (*steps)[count - 2].Key == "any"))
        return { .What = PredicatePlace::Part::Row };
    if (!last.Index)
        return { .What = PredicatePlace::Part::Member, .Member = last.Key };
    return {};
}

// One subtype whose documents hold conditions and nothing else of their own.
class ConditionDocumentAuthoring final : public IDataSubtypeAuthoring
{
public:
    ConditionDocumentAuthoring(std::string_view subtype, std::vector<std::string_view> predicates, bool stayCopiesEnter)
        : Kind(subtype)
        , Conditions(std::move(predicates))
        , StayCopiesEnter(stayCopiesEnter)
    {
    }

    [[nodiscard]] std::string_view Subtype() const override { return Kind; }
    void Update(const DataDocument& document, DataDocumentSet& documents) override
    {
        Conditions.Update(document, documents);
    }
    void Refine(std::vector<PropertyNode>& nodes, const DataDocument& document, const DataSchema&) const override
    {
        if (const JsonValue* data = document.Data())
            Conditions.Refine(nodes, *data);
    }
    [[nodiscard]] DataSubtypeEdit Perform(std::string_view key, std::string_view verb, std::string_view choice,
                                          JsonValue& data) const override
    {
        if (StayCopiesEnter && verb == "add" && choice == "Stay")
        {
            JsonValue* rule = FindDataValue(data, key);
            return DataSubtypeEditOf(rule != nullptr && SetAnimRuleStay(*rule, true));
        }
        return Conditions.Perform(key, verb, choice, data);
    }

private:
    std::string Kind;
    AnimationConditionAuthoring Conditions;
    bool StayCopiesEnter = false;
};
} // namespace

void ChooseAmong(PropertyNode& node, std::vector<std::string> names)
{
    if (std::ranges::find(names, node.Value) == names.end())
        names.insert(names.begin(), node.Value);
    node.Descriptor.Kind = PropertyKind::Choice;
    node.Descriptor.Choices = std::move(names);
}

std::vector<std::string> AnimationIntentParams(const AnimationPredicateVocabulary& names, std::string_view intent)
{
    const auto found = std::ranges::find(names.Intents, intent, &AnimationPredicateVocabulary::Intent::Name);
    return found != names.Intents.end() ? found->Params : std::vector<std::string>{};
}

void AnimationConditionAuthoring::Update(const DataDocument& document, DataDocumentSet& documents)
{
    if (document.VirtualPath() == ReadFor && document.Revision() == ReadAt)
        return;
    ReadFor = document.VirtualPath();
    ReadAt = document.Revision();
    Vocabulary = GatherAnimationPredicateVocabulary(documents.Store());
}

void AnimationConditionAuthoring::Refine(std::vector<PropertyNode>& nodes, const JsonValue& data) const
{
    RefineAll(nodes, data);
}

DataSubtypeEdit AnimationConditionAuthoring::Perform(std::string_view key, std::string_view verb,
                                                     std::string_view choice, JsonValue& data) const
{
    if (verb == "condition")
    {
        JsonValue* rows = FindDataValue(data, key);
        std::optional<JsonValue> test = rows != nullptr && rows->IsArray() ? TestFor(choice) : std::nullopt;
        if (!test)
            return DataSubtypeEdit::Refused;
        AddAnimPredicateRow(rows->AsArray(), std::move(*test));
        return DataSubtypeEdit::Applied;
    }
    if (verb == "or")
    {
        const std::optional<std::size_t> row = DataPathLastIndex(key);
        const std::optional<std::string_view> parent = DataPathParent(key);
        JsonValue* rows = row && parent ? FindDataValue(data, *parent) : nullptr;
        if (rows == nullptr || !rows->IsArray() || *row >= rows->AsArray().size())
            return DataSubtypeEdit::Refused;
        const JsonValue& held = rows->AsArray()[*row];
        // The new alternative starts as a copy of the row's first test.
        const JsonValue* any = held.Find("any");
        JsonValue first = any != nullptr && any->IsArray() && !any->AsArray().empty() ? any->AsArray().front() : held;
        return DataSubtypeEditOf(AddAnimPredicateAlternative(rows->AsArray(), *row, std::move(first)));
    }
    return DataSubtypeEdit::NotMine;
}

std::optional<JsonValue> AnimationConditionAuthoring::TestFor(std::string_view choice) const
{
    if (choice == kElapsedChoice)
        return MakeAnimElapsedTest(1.0);
    if (choice.starts_with(kRequestChoice))
        return MakeAnimRequestTest(std::string(choice.substr(kRequestChoice.size())));
    if (!choice.starts_with(kFactChoice))
        return std::nullopt;
    const std::string_view name = choice.substr(kFactChoice.size());
    const auto fact = std::ranges::find(Vocabulary.Facts, name, &AnimationPredicateVocabulary::Fact::Name);
    return MakeAnimFactTest(std::string(name), fact != Vocabulary.Facts.end() ? fact->Kind : AnimFactKind::Bool);
}

std::vector<std::string> AnimationConditionAuthoring::ConditionChoices() const
{
    std::vector<std::string> choices;
    for (const AnimationPredicateVocabulary::Fact& fact : Vocabulary.Facts)
        choices.push_back(std::string(kFactChoice) + fact.Name);
    for (const AnimationPredicateVocabulary::Intent& intent : Vocabulary.Intents)
        choices.push_back(std::string(kRequestChoice) + intent.Name);
    choices.emplace_back(kElapsedChoice);
    return choices;
}

void AnimationConditionAuthoring::RefineAll(std::vector<PropertyNode>& nodes, const JsonValue& data) const
{
    for (PropertyNode& node : nodes)
    {
        Refine(node, data);
        RefineAll(node.Children, data);
    }
}

void AnimationConditionAuthoring::Refine(PropertyNode& node, const JsonValue& data) const
{
    const PredicatePlace place = PlaceOf(node.Key, Predicates);
    switch (place.What)
    {
    case PredicatePlace::Part::None: return;
    case PredicatePlace::Part::Rows:
        for (PropertyVerb& verb : node.Descriptor.Verbs)
            if (verb.Id == "add")
                verb = { .Id = "condition", .Label = "Add condition", .Choices = ConditionChoices() };
        if (const JsonValue* rows = FindDataValue(data, node.Key))
            node.Descriptor.Tooltip = DescribeAnimPredicate(rows);
        return;
    case PredicatePlace::Part::Row:
        if (const JsonValue* row = FindDataValue(data, node.Key))
        {
            const JsonValue rows(JsonValue::Array{ *row });
            node.Descriptor.Label = DescribeAnimPredicate(&rows);
        }
        if (const std::optional<std::string_view> rows = DataPathParent(node.Key);
            rows && DataPathLastIndex(node.Key) && !rows->ends_with(".any"))
            node.Descriptor.Verbs.insert(node.Descriptor.Verbs.begin(), { .Id = "or", .Label = "Or" });
        return;
    case PredicatePlace::Part::Member:
        if (place.Member == "fact")
        {
            std::vector<std::string> names;
            for (const AnimationPredicateVocabulary::Fact& fact : Vocabulary.Facts)
                names.push_back(fact.Name);
            ChooseAmong(node, std::move(names));
        }
        else if (place.Member == "request")
        {
            std::vector<std::string> names;
            for (const AnimationPredicateVocabulary::Intent& intent : Vocabulary.Intents)
                names.push_back(intent.Name);
            ChooseAmong(node, std::move(names));
        }
        else if (place.Member == "param")
        {
            const JsonValue* test = FindDataValue(data, DataPathParent(node.Key).value_or(node.Key));
            const JsonValue* request = test != nullptr ? test->Find("request") : nullptr;
            ChooseAmong(node, AnimationIntentParams(Vocabulary, request != nullptr && request->IsString()
                                                                   ? std::string_view(request->AsString())
                                                                   : std::string_view{}));
        }
        return;
    }
}

AnimationPredicateVocabulary GatherAnimationPredicateVocabulary(const DataDocumentStore& store)
{
    AnimationPredicateVocabulary vocabulary;
    for (const std::string& path : store.DataAssetPaths(kAnimFactSchemaType))
        if (const std::shared_ptr<const AnimFactSchema> schema =
                Compiled<AnimFactSchema>(store, path, kAnimFactSchemaType))
        {
            for (const AnimFactSlotDecl& slot : schema->Slots)
                vocabulary.Facts.push_back({ slot.Name, slot.Kind });
            for (const AnimDerivedFactDecl& derived : schema->Derived)
                vocabulary.Facts.push_back({ derived.Name, derived.ResultKind() });
        }
    for (const std::string& path : store.DataAssetPaths(kAnimRequestSchemaType))
        if (const std::shared_ptr<const AnimRequestSchema> schema =
                Compiled<AnimRequestSchema>(store, path, kAnimRequestSchemaType))
            for (const AnimRequestIntentDeclaration& intent : schema->Intents)
            {
                AnimationPredicateVocabulary::Intent named{ .Name = intent.Intent };
                for (std::size_t p = 0; p < intent.ParamCount; ++p)
                    named.Params.push_back(intent.Params[p].Name);
                vocabulary.Intents.push_back(std::move(named));
            }
    const auto byName = [](const auto& a, const auto& b) { return a.Name < b.Name; };
    const auto sameName = [](const auto& a, const auto& b) { return a.Name == b.Name; };
    std::ranges::sort(vocabulary.Facts, byName);
    vocabulary.Facts.erase(std::ranges::unique(vocabulary.Facts, sameName).begin(), vocabulary.Facts.end());
    std::ranges::sort(vocabulary.Intents, byName);
    vocabulary.Intents.erase(std::ranges::unique(vocabulary.Intents, sameName).begin(), vocabulary.Intents.end());
    return vocabulary;
}

std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSelectorAuthoring()
{
    return std::make_unique<ConditionDocumentAuthoring>(kAnimSelectorType,
                                                        std::vector<std::string_view>{ "enter", "stay" }, true);
}

std::unique_ptr<IDataSubtypeAuthoring> CreateAnimationSlotMapAuthoring()
{
    return std::make_unique<ConditionDocumentAuthoring>(kAnimSlotMapType, std::vector<std::string_view>{ "when" },
                                                        false);
}

void RegisterAnimationSubtypeAuthoring(DataSubtypeAuthoringRegistry& registry)
{
    (void)registry.Register(CreateAnimationSelectorAuthoring());
    (void)registry.Register(CreateAnimationSlotMapAuthoring());
    (void)registry.Register(CreateAnimationFlowAuthoring());
    (void)registry.Register(CreateAnimationRequestSchemaAuthoring());
}
