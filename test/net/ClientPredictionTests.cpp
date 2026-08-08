#include <gtest/gtest.h>

#include <net/ClientPrediction.h>

#include <cstring>

//=============================================================================
// What a client does with its own pawn.
//
// Everything else it holds is a puppet, drawn where the authority last said.
// Its own pawn cannot be: the player is holding the key now. So it simulates
// immediately and the authority's word becomes an argument to settle rather
// than an instruction to obey.
//=============================================================================

namespace
{
    constexpr EntityId kPawn{ 5, 1 };
    constexpr EntityId kOther{ 6, 1 };

    ClientPrediction Predicting()
    {
        ClientPrediction prediction;
        prediction.SetPredicted(kPawn);
        return prediction;
    }

    // Stands in for the snapshot applier: writes an authoritative position into
    // the shadow the way a decode would.
    void Decode(ClientPrediction& prediction, const Vec3d& position)
    {
        LocalTransform value{};
        const std::span<std::byte> shadow = prediction.AuthoritativeBytes(ResolveComponentTypeId<LocalTransform>());
        std::memcpy(&value, shadow.data(), sizeof(value));
        value.Value.Position = position;
        std::memcpy(shadow.data(), &value, sizeof(value));
    }

    std::optional<ClientPrediction::Correction> Arrive(
        ClientPrediction& prediction, std::uint64_t tick, const Vec3d& position)
    {
        Decode(prediction, position);
        return prediction.Commit(tick);
    }
}

TEST(ClientPrediction, PredictsOnlyTheEntityItWasGiven)
{
    ClientPrediction prediction = Predicting();
    EXPECT_TRUE(prediction.Predicts(kPawn));
    EXPECT_FALSE(prediction.Predicts(kOther))
        << "another player's pawn is a puppet and must arrive as state";

    EXPECT_TRUE(prediction.Intercepts(kPawn, ResolveComponentTypeId<LocalTransform>()));
    EXPECT_FALSE(prediction.Intercepts(kOther, ResolveComponentTypeId<LocalTransform>()));
    EXPECT_FALSE(prediction.Intercepts(kPawn, ResolveComponentTypeId<WorldTransform>()))
        << "only what this predicts is withheld from the world";
}

TEST(ClientPrediction, AgreementIsNotACorrection)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 5.0f, 0.0f, 3.0f });

    EXPECT_FALSE(Arrive(prediction, 100, Vec3d{ 5.0f, 0.0f, 3.0f }).has_value());
    EXPECT_EQ(prediction.Corrections(), 0u);
    EXPECT_EQ(prediction.Observations(), 1u);
}

// Quantization puts a fraction of a centimetre between two machines that agree
// completely. Correcting that would jitter a pawn that is already right.
TEST(ClientPrediction, IgnoresDisagreementBeneathNoticing)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 0.0f, 0.0f, 0.0f });

    EXPECT_FALSE(Arrive(prediction, 100, Vec3d{ 0.001f, 0.0f, 0.0f }).has_value());
    EXPECT_EQ(prediction.Corrections(), 0u);
    EXPECT_GT(prediction.LastErrorMeters(), 0.0f)
        << "an error too small to correct is still worth reporting";
}

// The whole shape of the mechanism: a correction is an offset, because
// everything simulated since that tick was built on the same error. Assigning
// the authority's stale position instead would drag the player backwards by the
// round trip every time it happened.
TEST(ClientPrediction, CorrectsByOffsetRatherThanByTeleport)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 10.0f, 0.0f, 0.0f });

    const auto correction = Arrive(prediction, 100, Vec3d{ 10.5f, 0.0f, 0.0f });
    ASSERT_TRUE(correction.has_value());
    EXPECT_FLOAT_EQ(correction->Offset.X, 0.5f);
    EXPECT_FLOAT_EQ(correction->Distance, 0.5f);
    EXPECT_FALSE(correction->Snap);
    EXPECT_EQ(prediction.Corrections(), 1u);
}

TEST(ClientPrediction, SnapsWhenTheDisagreementIsTooLargeToBlendOut)
{
    ClientPrediction prediction = Predicting();
    prediction.SetSnapMeters(2.0f);
    prediction.Record(100, Vec3d{ 0.0f, 0.0f, 0.0f });

    const auto correction = Arrive(prediction, 100, Vec3d{ 0.0f, 0.0f, 50.0f });
    ASSERT_TRUE(correction.has_value());
    EXPECT_TRUE(correction->Snap);
    EXPECT_EQ(prediction.Snaps(), 1u);
}

//-----------------------------------------------------------------------------
// The history window
//-----------------------------------------------------------------------------

TEST(ClientPredictionHistory, SaysNothingAboutATickItNeverPredicted)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 1.0f, 0.0f, 0.0f });

    EXPECT_FALSE(Arrive(prediction, 101, Vec3d{ 99.0f, 0.0f, 0.0f }).has_value())
        << "an unpredicted tick has nothing to disagree with";
    EXPECT_EQ(prediction.Observations(), 0u);
}

// Slots are indexed by tick, so an old tick collides with a recent one. The
// stamp is what keeps a stale slot from answering for a tick it never held --
// without it, a tick exactly one window old would compare against a completely
// unrelated position and read as an enormous divergence.
TEST(ClientPredictionHistory, AnExpiredTickDoesNotAnswerFromItsSuccessorsSlot)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 1.0f, 0.0f, 0.0f });
    prediction.Record(100 + ClientPrediction::kHistory, Vec3d{ 900.0f, 0.0f, 0.0f });

    EXPECT_FALSE(Arrive(prediction, 100, Vec3d{ 1.0f, 0.0f, 0.0f }).has_value());
    EXPECT_EQ(prediction.Observations(), 0u);
}

TEST(ClientPredictionHistory, HoldsAWindowOfTicks)
{
    ClientPrediction prediction = Predicting();
    for (std::uint64_t tick = 200; tick < 200 + ClientPrediction::kHistory; ++tick)
        prediction.Record(tick, Vec3d{ static_cast<float>(tick), 0.0f, 0.0f });

    // The oldest tick still in the window answers for itself.
    EXPECT_FALSE(Arrive(prediction, 200, Vec3d{ 200.0f, 0.0f, 0.0f }).has_value());
    EXPECT_EQ(prediction.Observations(), 1u);

    const auto correction =
        Arrive(prediction, 250, Vec3d{ 250.0f + 0.5f, 0.0f, 0.0f });
    ASSERT_TRUE(correction.has_value());
    EXPECT_FLOAT_EQ(correction->Offset.X, 0.5f);
}

//-----------------------------------------------------------------------------
// The authority's own view
//
// The trap this shadow exists for: a delta carries only what changed. Applied
// to what this machine simulated, a field the authority had no reason to resend
// would be filled in from this machine's own guess -- and the divergence in
// exactly that field would read as perfect agreement.
//-----------------------------------------------------------------------------

TEST(ClientPredictionShadow, KeepsTheAuthoritysValueAcrossSnapshots)
{
    ClientPrediction prediction = Predicting();

    prediction.Record(100, Vec3d{ 0.0f, 0.0f, 0.0f });
    ASSERT_FALSE(Arrive(prediction, 100, Vec3d{ 0.0f, 0.0f, 0.0f }).has_value());
    EXPECT_TRUE(prediction.HasAuthoritativeState(ResolveComponentTypeId<LocalTransform>()));

    // The authority stands still, so the next snapshot carries no position at
    // all: the shadow keeps the last one. This machine, meanwhile, has drifted.
    prediction.Record(101, Vec3d{ 0.0f, 0.0f, 0.75f });
    const auto correction = prediction.Commit(101);

    ASSERT_TRUE(correction.has_value())
        << "an unsent field was filled in from this machine's own prediction, "
           "so a real divergence read as agreement";
    EXPECT_FLOAT_EQ(correction->Offset.Z, -0.75f);
}

TEST(ClientPredictionShadow, ResetForgetsBothTheHistoryAndTheAuthority)
{
    ClientPrediction prediction = Predicting();
    prediction.Record(100, Vec3d{ 1.0f, 0.0f, 0.0f });
    ASSERT_TRUE(Arrive(prediction, 100, Vec3d{ 9.0f, 0.0f, 0.0f }).has_value());

    prediction.Reset();
    EXPECT_FALSE(prediction.Predicted().IsValid());
    EXPECT_FALSE(prediction.HasAuthoritativeState(ResolveComponentTypeId<LocalTransform>()));
    EXPECT_EQ(prediction.Observations(), 0u);
    EXPECT_EQ(prediction.Corrections(), 0u);
    EXPECT_FLOAT_EQ(prediction.LastErrorMeters(), 0.0f);
}
