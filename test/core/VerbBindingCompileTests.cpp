// Turning an authored binding record into something one World can invoke.
//
// The compiler is where a name becomes an id, a reference becomes a resolved
// value, an unsupplied argument becomes its declared default, and everything
// else becomes a diagnostic that names the binding and the argument.

#include <authored/VerbBindingCompiler.h>
#include <core/identity/Id.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/identity/PersistentEntityIndex.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
[[nodiscard]] DataFieldSchema Field(std::string key, DataFieldKind kind)
{
    DataFieldSchema field;
    field.Key = std::move(key);
    field.Kind = kind;
    return field;
}

[[nodiscard]] DataFieldSchema Record(std::vector<DataFieldSchema> children)
{
    DataFieldSchema root;
    root.Kind = DataFieldKind::Record;
    root.Children = std::move(children);
    return root;
}

[[nodiscard]] VerbBindingArgument Literal(std::string key, JsonValue value)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = VerbArgumentSource::Literal;
    argument.Literal = std::move(value);
    return argument;
}

[[nodiscard]] VerbBindingArgument Reference(std::string key,
                                            VerbArgumentSource source,
                                            std::string text)
{
    VerbBindingArgument argument;
    argument.Key = std::move(key);
    argument.Source = source;
    argument.Text = std::move(text);
    return argument;
}

[[nodiscard]] VerbBindingDesc Binding(std::string key,
                                      std::string verb,
                                      std::vector<VerbBindingArgument> arguments,
                                      std::vector<std::string> inputs = {})
{
    VerbBindingDesc desc;
    desc.Key = std::move(key);
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = std::move(verb);
    desc.Inputs = std::move(inputs);
    desc.Arguments = std::move(arguments);
    return desc;
}

bool Declare(VerbRegistry& registry, std::string name, DataFieldSchema arguments)
{
    VerbDefinition definition;
    definition.Name = std::move(name);
    definition.Arguments = std::move(arguments);
    VerbRegistrationScope scope(registry, "test");
    (void)scope.Declare(std::move(definition));
    return scope.Commit();
}

class VerbBindingCompileTest : public ::testing::Test
{
protected:
    [[nodiscard]] VerbBindingEnvironment Environment()
    {
        return VerbBindingEnvironment{
            .Verbs = &Verbs,
            .Tags = &Tags,
            .Entities = &Entities,
        };
    }

    VerbRegistry Verbs;
    GameplayTagRegistry Tags;
    PersistentEntityIndex Entities;
    std::vector<std::string> Errors;
    CompiledVerbBinding Compiled;
};
}

TEST_F(VerbBindingCompileTest, EveryAdvertisedShapeCompiles)
{
    DataFieldSchema choice = Field("Mode", DataFieldKind::Enum);
    DataEnumChoice soft;
    soft.Value = "soft";
    DataEnumChoice hard;
    hard.Value = "hard";
    choice.EnumChoices = { std::move(soft), std::move(hard) };

    DataFieldSchema vector = Field("Where", DataFieldKind::Vector);
    vector.VectorLength = 3;

    DataFieldSchema list = Field("Counts", DataFieldKind::Array);
    list.Children.push_back(Field({}, DataFieldKind::Int));

    DataFieldSchema nested = Field("Options", DataFieldKind::Record);
    nested.Children.push_back(Field("Loud", DataFieldKind::Bool));
    nested.Children.push_back(Field("Gain", DataFieldKind::Float));

    DataFieldSchema maybe = Field("Note", DataFieldKind::Optional);
    maybe.Children.push_back(Field({}, DataFieldKind::String));
    maybe.Required = false;

    DataFieldSchema asset = Field("Look", DataFieldKind::AssetRef);
    asset.Reference.AssetTypeFilter = AssetType::Material;

    DataFieldSchema data = Field("Tuning", DataFieldKind::DataAssetRef);
    data.Reference.DataSubtype = "movement.profile";

    ASSERT_TRUE(Declare(Verbs, "test.everything",
                        Record({ Field("Flag", DataFieldKind::Bool),
                                 Field("Count", DataFieldKind::Int),
                                 Field("Scale", DataFieldKind::Float),
                                 Field("Label", DataFieldKind::String), std::move(choice),
                                 std::move(vector), std::move(list), std::move(nested),
                                 std::move(maybe), std::move(asset), std::move(data),
                                 Field("Kind", DataFieldKind::GameplayTag),
                                 Field("Anchor", DataFieldKind::Entity) })));

    const GameplayTagId tag = *Tags.RegisterTag("Score.Pickup");
    const PersistentEntityId identity{ 0xabull };
    const EntityId entity{ .Index = 7, .Generation = 3 };
    ASSERT_TRUE(Entities.Register(identity, entity));

    const VerbBindingDesc desc = Binding(
        "everything", "test.everything",
        { Literal("Flag", JsonValue(true)), Literal("Count", JsonValue(42.0)),
          Literal("Scale", JsonValue(0.5)), Literal("Label", JsonValue("hello")),
          Literal("Mode", JsonValue("hard")),
          Literal("Where", JsonValue(JsonValue::Array{ JsonValue(1.0), JsonValue(2.0),
                                                       JsonValue(3.0) })),
          Literal("Counts", JsonValue(JsonValue::Array{ JsonValue(1.0), JsonValue(2.0) })),
          Literal("Options", JsonValue(JsonValue::Object{ { "Loud", JsonValue(true) },
                                                          { "Gain", JsonValue(2.0) } })),
          Literal("Note", JsonValue("a note")),
          Reference("Look", VerbArgumentSource::Asset, "asset://materials/hit.smat"),
          Reference("Tuning", VerbArgumentSource::DataAsset, "asset://data/run.sdata"),
          Reference("Kind", VerbArgumentSource::Tag, "Score.Pickup"),
          Reference("Anchor", VerbArgumentSource::Entity,
                    PersistentEntityIdToString(identity)) });

    ASSERT_TRUE(CompileVerbBinding(desc, Environment(), Compiled, Errors))
        << (Errors.empty() ? std::string{} : Errors.front());

    const VerbArguments& arguments = Compiled.Constants;
    ASSERT_EQ(arguments.Size(), 13u);

    bool flag = false;
    std::int64_t count = 0;
    double scale = 0.0;
    std::string_view label;
    std::string_view mode;
    VerbVectorValue where;
    const AssetRef* look = nullptr;
    const AssetRef* tuning = nullptr;
    GameplayTagId kind;
    EntityId anchor;
    EXPECT_TRUE(arguments.TryGetBool(0, flag));
    EXPECT_TRUE(arguments.TryGetInt(1, count));
    EXPECT_TRUE(arguments.TryGetFloat(2, scale));
    EXPECT_TRUE(arguments.TryGetString(3, label));
    EXPECT_TRUE(arguments.TryGetEnum(4, mode));
    EXPECT_TRUE(arguments.TryGetVector(5, where));
    EXPECT_TRUE(arguments.TryGetAsset(9, look));
    EXPECT_TRUE(arguments.TryGetDataAsset(10, tuning));
    EXPECT_TRUE(arguments.TryGetTag(11, kind));
    EXPECT_TRUE(arguments.TryGetEntity(12, anchor));

    EXPECT_TRUE(flag);
    EXPECT_EQ(count, 42);
    EXPECT_DOUBLE_EQ(scale, 0.5);
    EXPECT_EQ(label, "hello");
    EXPECT_EQ(mode, "hard");
    EXPECT_EQ(where.Length, 3u);
    EXPECT_DOUBLE_EQ(where.Components[2], 3.0);
    EXPECT_EQ(arguments.At(6).Children().size(), 2u);
    EXPECT_EQ(arguments.At(7).Children().size(), 2u);
    EXPECT_EQ(arguments.At(8).Kind(), VerbValueKind::String);
    // The kind comes from the contract, never from the authored string.
    EXPECT_EQ(look->Type, AssetType::Material);
    EXPECT_EQ(look->Path, "asset://materials/hit.smat");
    EXPECT_EQ(tuning->Type, AssetType::Data);
    EXPECT_EQ(kind, tag);
    EXPECT_EQ(anchor, entity);
}

TEST_F(VerbBindingCompileTest, AnUnsuppliedArgumentTakesItsDeclaredDefault)
{
    DataFieldSchema withDefault = Field("Scale", DataFieldKind::Float);
    withDefault.Default = 2.5;

    DataFieldSchema optional = Field("Note", DataFieldKind::Optional);
    optional.Required = false;
    optional.Children.push_back(Field({}, DataFieldKind::String));

    DataFieldSchema absent = Field("Extra", DataFieldKind::String);
    absent.Required = false;

    ASSERT_TRUE(Declare(Verbs, "test.defaults",
                        Record({ std::move(withDefault), std::move(optional),
                                 std::move(absent) })));

    ASSERT_TRUE(CompileVerbBinding(Binding("defaults", "test.defaults", {}), Environment(),
                                   Compiled, Errors));

    double scale = 0.0;
    EXPECT_TRUE(Compiled.Constants.TryGetFloat(0, scale));
    EXPECT_DOUBLE_EQ(scale, 2.5);
    // Absent, not defaulted to an empty string: a value nobody wrote is not a
    // value, and the operation is what decides what to do about it.
    EXPECT_TRUE(Compiled.Constants.At(1).IsNone());
    EXPECT_TRUE(Compiled.Constants.At(2).IsNone());
}

TEST_F(VerbBindingCompileTest, AnExplicitNullIsNotAMissingField)
{
    DataFieldSchema optional = Field("Note", DataFieldKind::Optional);
    optional.Required = false;
    DataFieldSchema element = Field({}, DataFieldKind::String);
    element.Default = std::string("from the default");
    optional.Children.push_back(std::move(element));
    optional.Default = std::string("from the default");

    ASSERT_TRUE(Declare(Verbs, "test.null", Record({ std::move(optional) })));

    // Left out: the field's own default applies.
    ASSERT_TRUE(
        CompileVerbBinding(Binding("omitted", "test.null", {}), Environment(), Compiled, Errors));
    std::string_view note;
    EXPECT_TRUE(Compiled.Constants.TryGetString(0, note));
    EXPECT_EQ(note, "from the default");

    // Written as null: the author said "nothing here", which the default must
    // not overrule.
    ASSERT_TRUE(CompileVerbBinding(
        Binding("explicit", "test.null", { Literal("Note", JsonValue(nullptr)) }), Environment(),
        Compiled, Errors));
    EXPECT_TRUE(Compiled.Constants.At(0).IsNone());
}

TEST_F(VerbBindingCompileTest, ARequiredArgumentWithNoValueAndNoDefaultFails)
{
    ASSERT_TRUE(Declare(Verbs, "test.required", Record({ Field("Count", DataFieldKind::Int) })));

    EXPECT_FALSE(
        CompileVerbBinding(Binding("missing", "test.required", {}), Environment(), Compiled,
                           Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors.front().find("missing"), std::string::npos);
    EXPECT_NE(Errors.front().find("Count"), std::string::npos);
}

TEST_F(VerbBindingCompileTest, TypedValuesAreNeverCoercedOpportunistically)
{
    DataFieldSchema ranged = Field("Count", DataFieldKind::Int);
    ranged.Numeric.Minimum = 0.0;
    ranged.Numeric.Maximum = 10.0;
    ASSERT_TRUE(Declare(Verbs, "test.typed", Record({ std::move(ranged) })));

    // A string that looks like a number stays a string.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("text", "test.typed", { Literal("Count", JsonValue("3")) }), Environment(),
        Compiled, Errors));

    // A fractional part is not an integer.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("fraction", "test.typed", { Literal("Count", JsonValue(3.5)) }), Environment(),
        Compiled, Errors));

    // Outside the declared range.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("range", "test.typed", { Literal("Count", JsonValue(99.0)) }), Environment(),
        Compiled, Errors));

    Errors.clear();
    EXPECT_TRUE(CompileVerbBinding(
        Binding("ok", "test.typed", { Literal("Count", JsonValue(7.0)) }), Environment(),
        Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, AnIntegerTooLargeForADoubleToHoldExactlyIsRefused)
{
    ASSERT_TRUE(Declare(Verbs, "test.big", Record({ Field("Id", DataFieldKind::Int) })));

    // 2^53 + 2 is representable as a double but is not the number that was
    // written; accepting it would mean a value silently became another one.
    EXPECT_FALSE(CompileVerbBinding(
        Binding("huge", "test.big", { Literal("Id", JsonValue(9007199254740994.0)) }),
        Environment(), Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, ReferencesResolveAgainstTheBoundWorldOrFail)
{
    ASSERT_TRUE(Declare(Verbs, "test.refs",
                        Record({ Field("Kind", DataFieldKind::GameplayTag),
                                 Field("Anchor", DataFieldKind::Entity) })));
    (void)Tags.RegisterTag("Known.Tag");
    const PersistentEntityId identity{ 0x2211ull };
    ASSERT_TRUE(Entities.Register(identity, EntityId{ .Index = 1, .Generation = 1 }));

    const std::string identityText = PersistentEntityIdToString(identity);

    // A tag this World never declared is not resolved to something plausible.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("unknown_tag", "test.refs",
                { Reference("Kind", VerbArgumentSource::Tag, "Never.Declared"),
                  Reference("Anchor", VerbArgumentSource::Entity, identityText) }),
        Environment(), Compiled, Errors));

    // An entity nothing in this World carries.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("unknown_entity", "test.refs",
                { Reference("Kind", VerbArgumentSource::Tag, "Known.Tag"),
                  Reference("Anchor", VerbArgumentSource::Entity, "00000000000000ff") }),
        Environment(), Compiled, Errors));

    // A malformed identity, which is a typo rather than a missing entity.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("bad_identity", "test.refs",
                { Reference("Kind", VerbArgumentSource::Tag, "Known.Tag"),
                  Reference("Anchor", VerbArgumentSource::Entity, "not-an-id") }),
        Environment(), Compiled, Errors));

    // A World with no vocabulary to resolve against refuses rather than
    // compiling to an invalid id.
    Errors.clear();
    const VerbBindingEnvironment bare{ .Verbs = &Verbs, .Tags = nullptr, .Entities = nullptr };
    EXPECT_FALSE(CompileVerbBinding(
        Binding("bare", "test.refs",
                { Reference("Kind", VerbArgumentSource::Tag, "Known.Tag"),
                  Reference("Anchor", VerbArgumentSource::Entity, identityText) }),
        bare, Compiled, Errors));

    Errors.clear();
    EXPECT_TRUE(CompileVerbBinding(
        Binding("good", "test.refs",
                { Reference("Kind", VerbArgumentSource::Tag, "Known.Tag"),
                  Reference("Anchor", VerbArgumentSource::Entity, identityText) }),
        Environment(), Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, AReferenceMustMatchTheDeclaredArgumentKind)
{
    ASSERT_TRUE(Declare(Verbs, "test.kinds", Record({ Field("Label", DataFieldKind::String) })));
    EXPECT_FALSE(CompileVerbBinding(
        Binding("wrong", "test.kinds",
                { Reference("Label", VerbArgumentSource::Tag, "Some.Tag") }),
        Environment(), Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, AnArgumentTheContractDoesNotDeclareIsRefused)
{
    ASSERT_TRUE(Declare(Verbs, "test.narrow", Record({ Field("Count", DataFieldKind::Int) })));
    EXPECT_FALSE(CompileVerbBinding(
        Binding("extra", "test.narrow",
                { Literal("Count", JsonValue(1.0)), Literal("Nonsense", JsonValue(2.0)) }),
        Environment(), Compiled, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors.front().find("Nonsense"), std::string::npos);
}

TEST_F(VerbBindingCompileTest, InputsBecomeSlotIndicesInProducerOrder)
{
    ASSERT_TRUE(Declare(Verbs, "test.inputs",
                        Record({ Field("Amount", DataFieldKind::Int),
                                 Field("Target", DataFieldKind::Entity),
                                 Field("Label", DataFieldKind::String) })));

    // Declared target-then-amount, while the verb declares amount first: a
    // producer supplies values in the binding's order, not the schema's.
    ASSERT_TRUE(CompileVerbBinding(
        Binding("mapped", "test.inputs",
                { Literal("Label", JsonValue("fixed")),
                  Reference("Amount", VerbArgumentSource::Input, "amount"),
                  Reference("Target", VerbArgumentSource::Input, "target") },
                { "target", "amount" }),
        Environment(), Compiled, Errors))
        << (Errors.empty() ? std::string{} : Errors.front());

    ASSERT_EQ(Compiled.Inputs.size(), 2u);
    EXPECT_EQ(Compiled.Inputs[0].Name, "target");
    EXPECT_EQ(Compiled.Inputs[0].ArgumentSlot, 1u);
    EXPECT_EQ(Compiled.Inputs[1].Name, "amount");
    EXPECT_EQ(Compiled.Inputs[1].ArgumentSlot, 0u);
    // The constant is in place; the input slots are waiting.
    std::string_view label;
    EXPECT_TRUE(Compiled.Constants.TryGetString(2, label));
    EXPECT_TRUE(Compiled.Constants.At(0).IsNone());
    EXPECT_TRUE(Compiled.Constants.At(1).IsNone());
}

TEST_F(VerbBindingCompileTest, InputDeclarationsAndUsesMustAgree)
{
    ASSERT_TRUE(Declare(Verbs, "test.inputs", Record({ Field("Amount", DataFieldKind::Int) })));

    // Named but never declared: a producer would have nothing to supply.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("undeclared", "test.inputs",
                { Reference("Amount", VerbArgumentSource::Input, "amount") }),
        Environment(), Compiled, Errors));

    // Declared but never used: positional supply means an unread slot shifts
    // every value after it.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("unused", "test.inputs", { Literal("Amount", JsonValue(1.0)) }, { "amount" }),
        Environment(), Compiled, Errors));

    // Declared twice: there is no answer to which slot the producer meant.
    Errors.clear();
    EXPECT_FALSE(CompileVerbBinding(
        Binding("twice", "test.inputs",
                { Reference("Amount", VerbArgumentSource::Input, "amount") },
                { "amount", "amount" }),
        Environment(), Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, OneArgumentCannotBeFilledTwice)
{
    ASSERT_TRUE(Declare(Verbs, "test.once", Record({ Field("Target", DataFieldKind::Entity) })));
    EXPECT_FALSE(CompileVerbBinding(
        Binding("both", "test.once",
                { Reference("Target", VerbArgumentSource::Input, "target"),
                  Reference("Target", VerbArgumentSource::Entity, "00000000000000ab") },
                { "target" }),
        Environment(), Compiled, Errors));
}

TEST_F(VerbBindingCompileTest, AnUnknownVerbLeavesTheRecordAndRefusesTheBinding)
{
    const VerbBindingDesc desc = Binding("orphan", "never.declared", {});
    EXPECT_FALSE(CompileVerbBinding(desc, Environment(), Compiled, Errors));
    ASSERT_FALSE(Errors.empty());
    EXPECT_NE(Errors.front().find("never.declared"), std::string::npos);
    // The source record is untouched: an unresolved binding is content to
    // inspect, not content to lose.
    EXPECT_EQ(desc.VerbName, "never.declared");
    EXPECT_EQ(desc.Key, "orphan");
}

TEST_F(VerbBindingCompileTest, OneRecordResolvesIndependentlyInTwoWorlds)
{
    VerbRegistry other;
    // Declared in a different order, so the same name lands on a different slot.
    ASSERT_TRUE(Declare(other, "test.padding", Record({})));
    ASSERT_TRUE(Declare(other, "test.shared", Record({ Field("Count", DataFieldKind::Int) })));
    ASSERT_TRUE(Declare(Verbs, "test.shared", Record({ Field("Count", DataFieldKind::Int) })));

    const VerbBindingDesc desc =
        Binding("shared", "test.shared", { Literal("Count", JsonValue(3.0)) });

    CompiledVerbBinding here;
    CompiledVerbBinding there;
    GameplayTagRegistry otherTags;
    PersistentEntityIndex otherEntities;
    const VerbBindingEnvironment otherEnvironment{
        .Verbs = &other, .Tags = &otherTags, .Entities = &otherEntities
    };
    ASSERT_TRUE(CompileVerbBinding(desc, Environment(), here, Errors));
    ASSERT_TRUE(CompileVerbBinding(desc, otherEnvironment, there, Errors));

    EXPECT_NE(here.Verb, there.Verb);
    EXPECT_NE(here.Catalog, there.Catalog);
    EXPECT_EQ(here.Key, there.Key);
    EXPECT_TRUE(IsVerbBindingCurrent(here, Verbs));
    EXPECT_TRUE(IsVerbBindingCurrent(there, other));
    // Neither is usable against the other's catalog.
    EXPECT_FALSE(IsVerbBindingCurrent(here, other));
    EXPECT_FALSE(IsVerbBindingCurrent(there, Verbs));
}

TEST_F(VerbBindingCompileTest, AChangedContractMakesAnExistingBindingStale)
{
    ASSERT_TRUE(Declare(Verbs, "test.moving", Record({ Field("Count", DataFieldKind::Int) })));
    ASSERT_TRUE(CompileVerbBinding(
        Binding("moving", "test.moving", { Literal("Count", JsonValue(1.0)) }), Environment(),
        Compiled, Errors));
    EXPECT_TRUE(IsVerbBindingCurrent(Compiled, Verbs));

    ASSERT_TRUE(Declare(Verbs, "test.moving", Record({ Field("Amount", DataFieldKind::Int) })));
    EXPECT_FALSE(IsVerbBindingCurrent(Compiled, Verbs));

    // Retiring it has the same effect, for a different reason.
    ASSERT_TRUE(CompileVerbBinding(
        Binding("moving", "test.moving", { Literal("Amount", JsonValue(1.0)) }), Environment(),
        Compiled, Errors));
    EXPECT_TRUE(IsVerbBindingCurrent(Compiled, Verbs));
    Verbs.RetireProvider("test");
    EXPECT_FALSE(IsVerbBindingCurrent(Compiled, Verbs));
}
