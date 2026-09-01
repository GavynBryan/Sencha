#include <gtest/gtest.h>
#include <net/NetComponentSchemas.h>
#include <controller/ControllerComponentSchemas.h>

#include <controller/LookOrientation.h>
#include <math/MathSchemas.h>
#include <movement/JumpState.h>
#include <movement/MovementComponentSchemas.h>
#include <net/ReplicationLayout.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/WorldComponentSchemas.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    // Every field kind the layout has an opinion about, in one component.
    struct Sampled
    {
        float Plain = 0.0f;
        float Ranged = 0.0f;
        float Secret = 0.0f;
        float Derived = 0.0f;
        Vec3d Position;
    };

    struct Excluded
    {
        float OnlyLocal = 0.0f;
    };

    // A 64-bit member has no entry in the scalar table, so it is exactly the
    // shape the layout has to refuse rather than silently truncate.
    struct Unsupported
    {
        // Signed and eight bytes wide: the codec carries unsigned identities at
        // that width and nothing else, so this leaf has no encoder.
        std::int64_t Wide = 0;
    };

    struct Nested
    {
        Transform3f Pose;
    };

    // Transform3f has its own schema, so reaching its floats means recursing
    // rather than stopping at a packed group. That is a different code path
    // from a Vec3 member and needs its own coverage.
    struct DeeplyRanged
    {
        Transform3f Pose;
    };

    struct Hooked
    {
        int Value = 0;
    };

    // The owner keeps simulating this one between snapshots, which is a fact
    // its schema states and the layout carries.
    struct Resumed
    {
        float Value = 0.0f;
    };
}

template <>
struct TypeSchema<Sampled>
{
    static constexpr std::string_view Name = "test.Sampled";
    static auto Fields()
    {
        return std::tuple{
            MakeField("plain", &Sampled::Plain),
            MakeField("ranged", &Sampled::Ranged).Quantize(-10.0f, 10.0f, 12),
            MakeField("secret", &Sampled::Secret).OwnerOnly(),
            MakeField("derived", &Sampled::Derived).LocalOnly(),
            MakeField("position", &Sampled::Position).Quantize(-1024.0f, 1024.0f, 18),
        };
    }
};

template <>
struct TypeSchema<Excluded>
{
    static constexpr std::string_view Name = "test.Excluded";
    static auto Fields()
    {
        return std::tuple{ MakeField("only_local", &Excluded::OnlyLocal).LocalOnly() };
    }
};

template <>
struct TypeSchema<Unsupported>
{
    static constexpr std::string_view Name = "test.Unsupported";
    static auto Fields()
    {
        return std::tuple{ MakeField("wide", &Unsupported::Wide) };
    }
};

template <>
struct TypeSchema<Nested>
{
    static constexpr std::string_view Name = "test.Nested";
    static auto Fields()
    {
        // Tagged on the composite, not on the scalars underneath it.
        return std::tuple{ MakeField("pose", &Nested::Pose).LocalOnly() };
    }
};

template <>
struct TypeSchema<DeeplyRanged>
{
    static constexpr std::string_view Name = "test.DeeplyRanged";
    static auto Fields()
    {
        return std::tuple{
            MakeField("pose", &DeeplyRanged::Pose).Quantize(-64.0f, 64.0f, 14),
        };
    }
};

template <>
struct TypeSchema<Hooked>
{
    static constexpr std::string_view Name = "test.Hooked";
    static auto Fields()
    {
        return std::tuple{ MakeField("value", &Hooked::Value) };
    }
};

template <>
struct TypeSchema<Resumed>
{
    static constexpr std::string_view Name = "test.Resumed";
    static constexpr bool Replicated = true;
    static constexpr bool Predicted = true;
    static auto Fields()
    {
        return std::tuple{ MakeField("value", &Resumed::Value).OwnerOnly() };
    }
};

namespace
{
    const ReplicatedField* FindField(const ReplicatedComponent& component,
                                     std::string_view name)
    {
        for (const ReplicatedField& field : component.Fields)
        {
            if (field.Name == name)
                return &field;
        }
        return nullptr;
    }
}

TEST(ReplicationLayout, LocalOnlyFieldsNeverReachTheWire)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<Sampled>());

    const ReplicatedComponent* component = layout.Find(ResolveComponentTypeId<Sampled>());
    ASSERT_NE(component, nullptr);

    EXPECT_NE(FindField(*component, "plain"), nullptr);
    EXPECT_EQ(FindField(*component, "derived"), nullptr)
        << "a LocalOnly field must not appear in the wire layout";
}

TEST(ReplicationLayout, QuantizationAndOwnershipSurviveFlattening)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<Sampled>());
    const ReplicatedComponent* component = layout.Find(ResolveComponentTypeId<Sampled>());
    ASSERT_NE(component, nullptr);

    const ReplicatedField* plain = FindField(*component, "plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_FALSE(plain->Quantization.IsQuantized());
    EXPECT_FALSE(plain->OwnerOnly);

    const ReplicatedField* ranged = FindField(*component, "ranged");
    ASSERT_NE(ranged, nullptr);
    EXPECT_EQ(ranged->Quantization.Bits, 12);
    EXPECT_FLOAT_EQ(ranged->Quantization.Min, -10.0f);
    EXPECT_FLOAT_EQ(ranged->Quantization.Max, 10.0f);

    const ReplicatedField* secret = FindField(*component, "secret");
    ASSERT_NE(secret, nullptr);
    EXPECT_TRUE(secret->OwnerOnly);
}

// A range tagged on a Vec3 has to reach the floats inside it, or tagging a
// composite would silently do nothing.
TEST(ReplicationLayout, AnnotationsOnACompositeReachItsScalars)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<Sampled>());
    const ReplicatedComponent* component = layout.Find(ResolveComponentTypeId<Sampled>());
    ASSERT_NE(component, nullptr);

    const ReplicatedField* position = FindField(*component, "position");
    ASSERT_NE(position, nullptr);
    EXPECT_EQ(position->Count, 3) << "a Vec3 is one three-wide run";
    EXPECT_EQ(position->Quantization.Bits, 18);
    EXPECT_FLOAT_EQ(position->Quantization.Max, 1024.0f);
}

// A member with its own schema is reached by recursion, not by the packed-group
// shortcut above, so it exercises the path where the annotation has to be
// handed down rather than applied in place.
TEST(ReplicationLayout, AnnotationsSurviveRecursionIntoANestedSchema)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<DeeplyRanged>());
    const ReplicatedComponent* component =
        layout.Find(ResolveComponentTypeId<DeeplyRanged>());
    ASSERT_NE(component, nullptr);
    ASSERT_FALSE(component->Fields.empty());

    for (const ReplicatedField& field : component->Fields)
    {
        EXPECT_EQ(field.Quantization.Bits, 14) << field.Name;
        EXPECT_FLOAT_EQ(field.Quantization.Min, -64.0f) << field.Name;
        EXPECT_FLOAT_EQ(field.Quantization.Max, 64.0f) << field.Name;
    }
}

// The same, one level deeper and for exclusion: a subtree marked local must not
// leak any of its leaves.
TEST(ReplicationLayout, ExcludingACompositeExcludesEverythingUnderIt)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<Nested>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::NoReplicatedFields);
}

TEST(ReplicationLayout, AComponentWithNothingToSendIsRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<Excluded>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::NoReplicatedFields);
    EXPECT_NE(layout.ErrorDetail().find("Excluded"), std::string::npos);
}

// A leaf the codec cannot express must fail loudly at startup. Silently
// dropping it would replicate a component that looks complete and is not.
TEST(ReplicationLayout, AFieldTheCodecCannotExpressIsRefusedByName)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<Unsupported>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::UnsupportedField);
    EXPECT_NE(layout.ErrorDetail().find("wide"), std::string::npos)
        << "the failure must name the field, not just the component";
}

TEST(ReplicationLayout, WireKeysArePositionalAndRoundTrip)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<LocalTransform>());
    ASSERT_TRUE(layout.Add<LookOrientation>());
    layout.Seal();

    // A component's wire key is its position in registration order.
    ASSERT_NE(layout.At(0), nullptr);
    ASSERT_NE(layout.At(1), nullptr);
    EXPECT_EQ(layout.At(0)->Type, ResolveComponentTypeId<LocalTransform>());
    EXPECT_EQ(layout.At(1)->Type, ResolveComponentTypeId<LookOrientation>());
    EXPECT_EQ(layout.At(200), nullptr) << "an out-of-range key must not resolve";
}

TEST(ReplicationLayout, AddingTheSameComponentTwiceIsIdempotent)
{
    ReplicationLayout layout;
    EXPECT_TRUE(layout.Add<LookOrientation>());
    EXPECT_FALSE(layout.Add<LookOrientation>());
    EXPECT_EQ(layout.Size(), 1u);
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::None)
        << "a duplicate is a no-op, not a broken table";
}

// Which components a client resumes simulating for itself is a fact of the
// schema, carried by the same table as everything else about them, so nothing
// downstream has to keep its own list of them.
TEST(ReplicationLayout, PredictionIsAFactTheSchemaStates)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<Resumed>());
    ASSERT_TRUE(layout.Add<Sampled>());

    const ReplicatedComponent* resumed = layout.Find(ResolveComponentTypeId<Resumed>());
    ASSERT_NE(resumed, nullptr);
    EXPECT_TRUE(resumed->Predicted);

    const ReplicatedComponent* sampled = layout.Find(ResolveComponentTypeId<Sampled>());
    ASSERT_NE(sampled, nullptr);
    EXPECT_FALSE(sampled->Predicted)
        << "a schema that says nothing about prediction is not predicted";
}

//=============================================================================
// Table hash
//
// This is what the handshake compares, so it has to move for every difference
// that would make two builds read each other's snapshots differently, and stay
// put otherwise.
//=============================================================================

TEST(ReplicationLayoutHash, IdenticalTablesAgree)
{
    ReplicationLayout first;
    ASSERT_TRUE(first.Add<LocalTransform>());
    ASSERT_TRUE(first.Add<LookOrientation>());

    ReplicationLayout second;
    ASSERT_TRUE(second.Add<LocalTransform>());
    ASSERT_TRUE(second.Add<LookOrientation>());

    EXPECT_EQ(first.TableHash(), second.TableHash());
}

TEST(ReplicationLayoutHash, OrderIsPartOfTheContract)
{
    ReplicationLayout first;
    ASSERT_TRUE(first.Add<LocalTransform>());
    ASSERT_TRUE(first.Add<LookOrientation>());

    ReplicationLayout reordered;
    ASSERT_TRUE(reordered.Add<LookOrientation>());
    ASSERT_TRUE(reordered.Add<LocalTransform>());

    EXPECT_NE(first.TableHash(), reordered.TableHash())
        << "wire keys are positional, so a reorder must not hash the same";
}

TEST(ReplicationLayoutHash, MembershipChangesTheHash)
{
    ReplicationLayout smaller;
    ASSERT_TRUE(smaller.Add<LocalTransform>());

    ReplicationLayout larger;
    ASSERT_TRUE(larger.Add<LocalTransform>());
    ASSERT_TRUE(larger.Add<LookOrientation>());

    EXPECT_NE(smaller.TableHash(), larger.TableHash());
}

TEST(ReplicationLayoutHash, AnEmptyTableStillHashes)
{
    ReplicationLayout empty;
    ReplicationLayout other;
    EXPECT_EQ(empty.TableHash(), other.TableHash());
    EXPECT_NE(empty.TableHash(), 0u);
}

//=============================================================================
// The engine's own table
//=============================================================================

TEST(EngineReplicationLayout, CompilesWithoutError)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);

    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None)
        << ReplicationLayoutErrorToString(layout.Error()) << ": " << layout.ErrorDetail();
    EXPECT_GT(layout.Size(), 0u);
}

// Where a player is looking replicates; how far their neck bends does not.
TEST(EngineReplicationLayout, LookOrientationSendsAimAndNotItsLimits)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);

    const ReplicatedComponent* look =
        layout.Find(ResolveComponentTypeId<LookOrientation>());
    ASSERT_NE(look, nullptr);

    EXPECT_NE(FindField(*look, "yaw"), nullptr);
    EXPECT_NE(FindField(*look, "pitch"), nullptr);
    EXPECT_EQ(FindField(*look, "min_pitch"), nullptr);
    EXPECT_EQ(FindField(*look, "max_pitch"), nullptr);

    const ReplicatedField* pitch = FindField(*look, "pitch");
    ASSERT_NE(pitch, nullptr);
    EXPECT_TRUE(pitch->Quantization.IsQuantized());

    // Yaw accumulates without bound, so a fixed range would clamp a player who
    // kept turning. It is deliberately unquantized until the codec can wrap.
    const ReplicatedField* yaw = FindField(*look, "yaw");
    ASSERT_NE(yaw, nullptr);
    EXPECT_FALSE(yaw->Quantization.IsQuantized());
}

// The set a client resumes simulating for its own character: where it is, how
// fast, what it is standing on, which rules it moves under, and whether it may
// jump. Resuming from a subset of these puts the character in the right place
// still carrying the wrong everything else, so the membership is pinned here --
// this is the whole set, stated once, where dropping a flag is caught.
TEST(EngineReplicationLayout, PredictedComponentsAreTheOnesAReplayResumesFrom)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);

    std::vector<ComponentTypeId> predicted;
    for (const ReplicatedComponent& component : layout.Components())
    {
        if (component.Predicted)
            predicted.push_back(component.Type);
    }

    const std::vector<ComponentTypeId> expected{
        ResolveComponentTypeId<LocalTransform>(),
        ResolveComponentTypeId<KinematicState>(),
        ResolveComponentTypeId<SupportState>(),
        ResolveComponentTypeId<CharacterMovement>(),
        ResolveComponentTypeId<JumpState>(),
    };

    EXPECT_EQ(predicted.size(), expected.size());
    for (const ComponentTypeId type : expected)
    {
        EXPECT_NE(std::find(predicted.begin(), predicted.end(), type), predicted.end())
            << "a component a replay resumes from is missing from the predicted set";
    }
}

// Prediction resumes from what the authority said, which only arrives if the
// component travels. The registrar refuses the pairing at compile time; this
// checks the engine's own components honour it.
TEST(EngineReplicationLayout, EveryPredictedComponentAlsoReplicates)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);

    for (const ReplicatedComponent& component : layout.Components())
    {
        if (!component.Predicted)
            continue;
        EXPECT_FALSE(component.Fields.empty()) << component.Name;
    }
}

// Every replicated field has to address bytes that exist, or the codec would
// read past the component it was handed.
TEST(EngineReplicationLayout, EveryFieldLiesInsideItsComponent)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);

    for (const ReplicatedComponent& component : layout.Components())
    {
        for (const ReplicatedField& field : component.Fields)
        {
            EXPECT_LE(field.Offset + field.Count * field.Size, component.Size)
                << component.Name << "." << field.Name;
            EXPECT_GT(field.Count, 0) << component.Name << "." << field.Name;
            EXPECT_NE(field.Scalar, FieldScalar::Unsupported)
                << component.Name << "." << field.Name;
        }
    }
}

//=============================================================================
// Quantization belongs to floats
//
// A range describes a continuous value. The sizer reserves the declared bits
// for any field that carries one, and the writer spends them only on a float,
// so a range anywhere else makes the two disagree about the same field. What
// that costs is not a rounding error: an entity measured to fit a snapshot and
// then written wider overflows the writer, the whole snapshot is refused, and
// the peer it was for receives nothing -- every tick, for as long as the field
// is declared that way.
//=============================================================================

namespace
{
    struct RangedInteger
    {
        std::uint32_t Value = 0;
    };

    struct RangedDouble
    {
        double Value = 0.0;
    };

    struct RangedBool
    {
        bool Value = false;
    };

    struct RangedNarrow
    {
        std::uint16_t Value = 0;
    };
}

template <>
struct TypeSchema<RangedInteger>
{
    static constexpr std::string_view Name = "test.RangedInteger";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &RangedInteger::Value).Quantize(0.0f, 1.0f, 8),
        };
    }
};

template <>
struct TypeSchema<RangedDouble>
{
    static constexpr std::string_view Name = "test.RangedDouble";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &RangedDouble::Value).Quantize(0.0f, 1.0f, 8),
        };
    }
};

template <>
struct TypeSchema<RangedBool>
{
    static constexpr std::string_view Name = "test.RangedBool";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &RangedBool::Value).Quantize(0.0f, 1.0f, 8),
        };
    }
};

template <>
struct TypeSchema<RangedNarrow>
{
    static constexpr std::string_view Name = "test.RangedNarrow";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &RangedNarrow::Value).Quantize(0.0f, 1.0f, 8),
        };
    }
};

TEST(ReplicationLayoutQuantization, ARangeOnAnIntegerIsRefusedByName)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<RangedInteger>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::InvalidQuantization);
    EXPECT_NE(layout.ErrorDetail().find("value"), std::string::npos)
        << "the refusal has to name the field, or nobody can find it";
}

TEST(ReplicationLayoutQuantization, ARangeOnADoubleIsRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<RangedDouble>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::InvalidQuantization);
}

TEST(ReplicationLayoutQuantization, ARangeOnABoolIsRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<RangedBool>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::InvalidQuantization);
}

// The narrow integer case specifically: it is the one where the sizer's answer
// (eight bits) and the writer's (thirty-two) are furthest apart.
TEST(ReplicationLayoutQuantization, ARangeOnANarrowIntegerIsRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<RangedNarrow>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::InvalidQuantization);
}

// A colour is three floats wearing one entry, so a range on it is a range on
// floats and has to survive.
TEST(ReplicationLayoutQuantization, TheEngineTableQuantizesOnlyFloats)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);
    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None) << layout.ErrorDetail();

    for (const ReplicatedComponent& component : layout.Components())
    {
        for (const ReplicatedField& field : component.Fields)
        {
            if (!field.Quantization.IsQuantized())
                continue;
            EXPECT_EQ(field.Scalar, FieldScalar::Float)
                << component.Name << "." << field.Name;
        }
    }
}

//=============================================================================
// Field policy has to name a reachable audience
//=============================================================================

namespace
{
    struct SeenByNobody
    {
        float Value = 0.0f;
    };

    struct PrivateSubtree
    {
        Vec3d Inner;
    };

    // LocalOnly is a narrowing of any audience, not a disagreement with one:
    // the field stays off the wire either way, so the pair is redundant rather
    // than impossible and has to keep registering.
    struct LocalAndOwned
    {
        float Value = 0.0f;
        float Kept = 0.0f;
    };
}

template <>
struct TypeSchema<SeenByNobody>
{
    static constexpr std::string_view Name = "test.SeenByNobody";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &SeenByNobody::Value).OwnerOnly().OwnerLocal(),
        };
    }
};

template <>
struct TypeSchema<PrivateSubtree>
{
    static constexpr std::string_view Name = "test.PrivateSubtree";
    static auto Fields()
    {
        // Nobody writes both here. The composite says owner-only, and the
        // annotation reaches the three floats underneath it -- so a member that
        // said everyone-but-owner would be the one producing the pair.
        return std::tuple{
            MakeField("inner", &PrivateSubtree::Inner).OwnerOnly().OwnerLocal(),
        };
    }
};

template <>
struct TypeSchema<LocalAndOwned>
{
    static constexpr std::string_view Name = "test.LocalAndOwned";
    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &LocalAndOwned::Value).LocalOnly().OwnerOnly(),
            MakeField("kept", &LocalAndOwned::Kept),
        };
    }
};

TEST(ReplicationFieldPolicy, AFieldSentToNobodyIsRefusedByName)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<SeenByNobody>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::ContradictoryFieldPolicy);
    EXPECT_NE(layout.ErrorDetail().find("value"), std::string::npos);
}

// The case nobody writes on purpose: the pair arrives at the leaf by
// inheritance, so the refusal has to name the leaf rather than the member that
// carried the annotation down.
TEST(ReplicationFieldPolicy, AContradictionInheritedByAScalarIsStillRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.Add<PrivateSubtree>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::ContradictoryFieldPolicy);
    EXPECT_NE(layout.ErrorDetail().find("inner"), std::string::npos);
}

TEST(ReplicationFieldPolicy, LocalOnlyNarrowsAnAudienceRatherThanContradictingIt)
{
    ReplicationLayout layout;
    ASSERT_TRUE(layout.Add<LocalAndOwned>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::None) << layout.ErrorDetail();

    const ReplicatedComponent* component =
        layout.Find(ResolveComponentTypeId<LocalAndOwned>());
    ASSERT_NE(component, nullptr);
    ASSERT_EQ(component->Fields.size(), 1u) << "the local field reached the wire";
    EXPECT_EQ(component->Fields[0].Name, "kept");
}

// Nothing shipping may be declared this way, and the check above is what keeps
// it that way.
TEST(ReplicationFieldPolicy, NoEngineFieldIsSentToNobody)
{
    ReplicationLayout layout;
    ComponentRegistrar components(nullptr, nullptr, &layout);
    RegisterEngineComponents(components);
    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None) << layout.ErrorDetail();

    for (const ReplicatedComponent& component : layout.Components())
    {
        for (const ReplicatedField& field : component.Fields)
        {
            EXPECT_FALSE(field.OwnerOnly && field.OwnerLocal)
                << component.Name << "." << field.Name;
        }
    }
}
